#include "World/InhCameraPawn.h"
#include "World/InhLayout.h"
#include "CineCameraComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Scene.h"

namespace
{
	constexpr float MinYaw = -85.f;
	constexpr float MaxYaw = -5.f;
	constexpr float MinPitch = -70.f;
	constexpr float MaxPitch = -15.f;
	constexpr float MinArm = 450.f;
	constexpr float MaxArm = 1700.f;
	constexpr float LookAtArm = 800.f;

	/** Blends into and out of shots take this long, plus a little more per metre so long trips never rush. */
	constexpr float BlendSeconds = 1.5f;
	constexpr float BlendCmPerExtraSecond = 2500.f;
	constexpr float MaxBlendSeconds = 3.5f;
	/** Neighbouring roofs stay well below this; a return from outside crosses above it. */
	constexpr float RoofClearZ = 900.f;
	constexpr float OrbitHandheld = 0.1f;
	/** Circle of confusion for a Super 35 frame, used to derive the mobile depth of field. */
	constexpr float CircleOfConfusionMm = 0.025f;
	/**
	 * The camera keeps its horizontal angle, so on an upright phone the wide lenses would see almost 100 degrees top to
	 * bottom and stretch everything near the frame edges. Past the knee the lens is lengthened to ease the frame towards
	 * the limit instead. The 40 mm orbit stays under the knee on screens up to 9:21, so it (and the Opening's landing on
	 * it) is untouched.
	 */
	constexpr float PortraitKneeVerticalFov = 72.f;
	constexpr float PortraitMaxVerticalFov = 85.f;

	/** Critically damped spring (Game Programming Gems 4): smooth, never overshoots, frame-rate independent. */
	float SmoothDamp(float Current, float Target, float& Velocity, float SmoothTime, float DeltaSeconds)
	{
		const float Omega = 2.f / FMath::Max(SmoothTime, 0.0001f);
		const float X = Omega * DeltaSeconds;
		const float Decay = 1.f / (1.f + X + 0.48f * X * X + 0.235f * X * X * X);
		const float Change = Current - Target;
		const float Temp = (Velocity + Omega * Change) * DeltaSeconds;
		Velocity = (Velocity - Omega * Temp) * Decay;
		return Target + (Change + Temp) * Decay;
	}

	/** In the room or beside its open sides: a straight move between two such points never crosses a wall. */
	bool IsRoomSide(const FVector& Point)
	{
		return Point.X < InhLayout::RoomHalfExtent.X - 5.f && Point.Y > -InhLayout::RoomHalfExtent.Y + 5.f && Point.Z > 15.f;
	}

	/** True when To does not continue From's move (a new camera position, aim or lens). */
	bool IsCut(const FInhShot& From, const FInhShot& To)
	{
		const FVector EndAim = (From.LookTo - From.CameraTo).GetSafeNormal();
		const FVector StartAim = (To.LookFrom - To.CameraFrom).GetSafeNormal();
		return !From.CameraTo.Equals(To.CameraFrom, 2.f) || FVector::DotProduct(EndAim, StartAim) < 0.9998f
			|| !FMath::IsNearlyEqual(From.FocalTo, To.FocalFrom, 0.5f);
	}

	FVector CubicBezier(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, float T)
	{
		const float U = 1.f - T;
		return P0 * (U * U * U) + P1 * (3.f * U * U * T) + P2 * (3.f * U * T * T) + P3 * (T * T * T);
	}

	/**
	 * Handheld micro-motion: a few incommensurate sines per axis, so it drifts like a breathing operator and never
	 * visibly repeats or jitters. Each axis sums to at most 1, so the offset stays under 0.6 degrees.
	 */
	FRotator HandheldOffset(double Seconds, float Amount)
	{
		if (Amount <= 0.f)
		{
			return FRotator::ZeroRotator;
		}
		const float P = float(0.55 * FMath::Sin(Seconds * 0.83 + 0.4) + 0.30 * FMath::Sin(Seconds * 1.91 + 2.1)
			+ 0.15 * FMath::Sin(Seconds * 4.27 + 4.0));
		const float Y = float(0.55 * FMath::Sin(Seconds * 0.67 + 1.7) + 0.30 * FMath::Sin(Seconds * 1.53 + 0.3)
			+ 0.15 * FMath::Sin(Seconds * 3.71 + 5.2));
		const float R = float(0.60 * FMath::Sin(Seconds * 0.51 + 3.3) + 0.40 * FMath::Sin(Seconds * 1.37 + 0.9));
		return FRotator(P * 0.5f * Amount, Y * 0.55f * Amount, R * 0.3f * Amount);
	}

	/**
	 * The focal length to put on the camera for FocalMm on the current screen. A plain function of the lens, applied to
	 * every view, so blends and zooms stay continuous; landscape screens get FocalMm back unchanged.
	 */
	float PortraitFocalLength(float FocalMm, float SensorWidthMm)
	{
		if (FocalMm <= 0.f || !GEngine || !GEngine->GameViewport)
		{
			return FocalMm;
		}
		FVector2D Size;
		GEngine->GameViewport->GetViewportSize(Size);
		if (Size.X <= 0.0 || Size.Y <= Size.X)
		{
			return FocalMm;
		}
		// Tangents of the half angles: the vertical one is the horizontal one stretched by the screen's aspect.
		const float Vertical = SensorWidthMm / (2.f * FocalMm) * float(Size.Y / Size.X);
		const float Knee = FMath::Tan(FMath::DegreesToRadians(PortraitKneeVerticalFov * 0.5f));
		if (Vertical <= Knee)
		{
			return FocalMm;
		}
		// A soft limit whose slope matches at the knee, so a zoom that crosses it keeps an even speed.
		const float Headroom = FMath::Tan(FMath::DegreesToRadians(PortraitMaxVerticalFov * 0.5f)) - Knee;
		const float Excess = Vertical - Knee;
		const float Limited = Knee + Excess / (1.f + Excess / Headroom);
		return FocalMm * Vertical / Limited;
	}

	float BlendDuration(float Distance)
	{
		return FMath::Clamp(BlendSeconds + Distance / BlendCmPerExtraSecond, BlendSeconds, MaxBlendSeconds);
	}

	FInhCameraView BlendViews(const FInhCameraView& A, const FInhCameraView& B, float Alpha)
	{
		FInhCameraView View;
		View.Location = FMath::Lerp(A.Location, B.Location, Alpha);
		// Pitch and yaw take the short way round with roll left at zero: a quaternion slerp between two level views
		// tilts the horizon on the way.
		View.Rotation = A.Rotation + (B.Rotation - A.Rotation).GetNormalized() * Alpha;
		View.FocalLength = FMath::Lerp(A.FocalLength, B.FocalLength, Alpha);
		View.Aperture = FMath::Lerp(A.Aperture, B.Aperture, Alpha);
		View.FocusDistance = FMath::Lerp(A.FocusDistance, B.FocusDistance, Alpha);
		View.Handheld = FMath::Lerp(A.Handheld, B.Handheld, Alpha);
		return View;
	}
}

AInhCameraPawn::AInhCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	// The camera sets its own rotation; the controller must never turn the pawn.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Camera = CreateDefaultSubobject<UCineCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	// Fill any screen, portrait or landscape; the cinema overlay draws letterbox bars when they are wanted.
	Camera->bConstrainAspectRatio = false;
	// Super 35, the classic cinema frame.
	Camera->Filmback.SensorWidth = 24.89f;
	Camera->Filmback.SensorHeight = 18.67f;
	// A fast prime set: shots are authored between 24 and 85 mm.
	Camera->LensSettings.MinFocalLength = 18.f;
	Camera->LensSettings.MaxFocalLength = 105.f;
	Camera->LensSettings.MinFStop = 1.2f;
	Camera->LensSettings.MaxFStop = 22.f;
	Camera->LensSettings.MinimumFocusDistance = 250.f;
	Camera->LensSettings.DiaphragmBladeCount = 8;
	// We pull focus ourselves every frame (smoothed below), so the camera just takes the distance as given.
	Camera->FocusSettings.FocusMethod = ECameraFocusMethod::Manual;
	Camera->FocusSettings.bSmoothFocusChanges = false;
	Camera->FocusSettings.ManualFocusDistance = InhCinematics::HomeArmLength;
	Camera->CurrentFocalLength = InhCinematics::OrbitFocalLength;
	Camera->CurrentAperture = InhCinematics::OrbitAperture;

	Focus = TargetFocus = InhLayout::RoomCameraFocus;
	LookTarget = Focus;
}

// --- orbit --------------------------------------------------------------------------------------

void AInhCameraPawn::Orbit(const FVector2D& DeltaPixels)
{
	if (bPlayingShots)
	{
		return;
	}
	TargetYaw = FMath::Clamp(TargetYaw + float(DeltaPixels.X) * 0.18f, MinYaw, MaxYaw);
	TargetPitch = FMath::Clamp(TargetPitch - float(DeltaPixels.Y) * 0.15f, MinPitch, MaxPitch);
}

void AInhCameraPawn::Zoom(float Factor)
{
	if (bPlayingShots)
	{
		return;
	}
	TargetArmLength = FMath::Clamp(TargetArmLength * Factor, MinArm, MaxArm);
}

void AInhCameraPawn::LookAt(const FVector& WorldLocation)
{
	TargetFocus = FMath::Lerp(InhLayout::RoomCameraFocus, WorldLocation, 0.6f);
	TargetArmLength = FMath::Min(TargetArmLength, LookAtArm);
	LookTarget = WorldLocation;
	bHasLookTarget = true;
}

void AInhCameraPawn::ResetView()
{
	TargetFocus = InhLayout::RoomCameraFocus;
	TargetArmLength = InhCinematics::HomeArmLength;
	bHasLookTarget = false;
}

void AInhCameraPawn::ResetOrbitRig()
{
	Yaw = TargetYaw = InhCinematics::HomeYaw;
	Pitch = TargetPitch = InhCinematics::HomePitch;
	ArmLength = TargetArmLength = InhCinematics::HomeArmLength;
	Focus = TargetFocus = InhLayout::RoomCameraFocus;
	bHasLookTarget = false;
}

void AInhCameraPawn::TickOrbitRig(float DeltaSeconds)
{
	Yaw = FMath::FInterpTo(Yaw, TargetYaw, DeltaSeconds, 6.f);
	Pitch = FMath::FInterpTo(Pitch, TargetPitch, DeltaSeconds, 6.f);
	ArmLength = FMath::FInterpTo(ArmLength, TargetArmLength, DeltaSeconds, 5.f);
	Focus = FMath::VInterpTo(Focus, TargetFocus, DeltaSeconds, 3.f);
}

float AInhCameraPawn::OrbitDistanceScale() const
{
	// Shared with the Opening, which has to land exactly where the orbit sits on this screen.
	return InhCinematics::OrbitDistanceScale();
}

FInhCameraView AInhCameraPawn::OrbitView() const
{
	// A slow drift keeps the scene alive even when nobody is touching it.
	const float Sway = float(1.2 * FMath::Sin(Time * 0.21));
	const float Breath = float(0.5 * FMath::Sin(Time * 0.13 + 1.3));
	const float ViewYaw = Yaw + Sway;
	const float ViewPitch = Pitch + Breath;

	FInhCameraView View;
	View.Rotation = FRotator(ViewPitch, ViewYaw, 0.f);
	View.Location = InhCinematics::OrbitLocation(Focus, ViewYaw, ViewPitch, ArmLength * OrbitDistanceScale());
	View.FocalLength = InhCinematics::OrbitFocalLength;
	View.Aperture = InhCinematics::OrbitAperture;
	// Focus sits on the tapped prop when there is one (a rack focus as it changes), else on the room.
	const FVector FocusPoint = bHasLookTarget ? LookTarget : Focus;
	View.FocusDistance = FMath::Max(float(FVector::DotProduct(FocusPoint - View.Location, View.Rotation.Vector())), 20.f);
	View.Handheld = OrbitHandheld;
	return View;
}

// --- shots --------------------------------------------------------------------------------------

void AInhCameraPawn::PlayShots(const TArray<FInhShot>& InShots, TFunction<void()> OnFinished, bool bLoop)
{
	if (InShots.Num() == 0)
	{
		StopShots(true);
		if (OnFinished)
		{
			OnFinished();
		}
		return;
	}

	const bool bWasPlaying = bPlayingShots;
	Shots = InShots;
	for (FInhShot& Shot : Shots)
	{
		// A zero-length shot would never move the clock on.
		Shot.Duration = FMath::Max(Shot.Duration, 0.05f);
	}
	OnShotsFinished = MoveTemp(OnFinished);
	bLoopShots = bLoop;
	ShotIndex = 0;
	ShotTime = 0.f;
	bPlayingShots = true;
	bPendingFirstShotEvent = true;
	bBlendingBack = false;
	++PlaybackSerial;
	// Nobody sees the rig while shots play, so put it home now and every cinematic returns to the same framing.
	ResetOrbitRig();

	EntryBlendTime = 0.f;
	EntryBlendDuration = 0.f;
	if (!bWasPlaying && bHasView && IsRoomSide(LastView.Location) && IsRoomSide(Shots[0].CameraFrom))
	{
		// From the orbit to a shot that starts beside the room: glide into it instead of cutting.
		EntryFrom = LastView;
		const float Distance = float(FVector::Dist(LastView.Location, Shots[0].CameraFrom));
		EntryBlendDuration = FMath::Min(BlendDuration(Distance), Shots[0].Duration * 0.6f);
	}
	else
	{
		// A cut. Show the first frame now in case the view is read before our next tick.
		bSnapNext = true;
		ApplyView(ShotView(), 0.f);
	}
}

void AInhCameraPawn::StopShots(bool bBlendBack)
{
	const bool bWasPlaying = bPlayingShots;
	bPlayingShots = false;
	bPendingFirstShotEvent = false;
	OnShotsFinished = nullptr;
	++PlaybackSerial;

	if (!bBlendBack)
	{
		// A deliberate cut straight back to the orbit.
		bBlendingBack = false;
		bSnapNext = true;
		ApplyView(OrbitView(), 0.f);
	}
	else if (bWasPlaying && bHasView)
	{
		BeginBlendBack(LastView);
	}
}

void AInhCameraPawn::AdvanceShots(float DeltaSeconds)
{
	// Listeners may stop or restart playback from inside the broadcast; the serial tells us to bail out.
	const uint32 Serial = PlaybackSerial;
	if (bPendingFirstShotEvent)
	{
		bPendingFirstShotEvent = false;
		OnShotStarted.Broadcast(ShotIndex);
		if (Serial != PlaybackSerial)
		{
			return;
		}
	}

	ShotTime += DeltaSeconds;
	// Bounded, so even a hitch longer than the whole list can't spin here.
	for (int32 Step = 0; Step <= Shots.Num() && ShotTime >= Shots[ShotIndex].Duration; ++Step)
	{
		ShotTime -= Shots[ShotIndex].Duration;
		const int32 Previous = ShotIndex;
		if (ShotIndex + 1 < Shots.Num())
		{
			++ShotIndex;
		}
		else if (bLoopShots)
		{
			ShotIndex = 0;
		}
		else
		{
			FinishShots();
			return;
		}
		if (IsCut(Shots[Previous], Shots[ShotIndex]))
		{
			bSnapNext = true;
		}
		OnShotStarted.Broadcast(ShotIndex);
		if (Serial != PlaybackSerial)
		{
			return;
		}
	}
	ShotTime = FMath::Min(ShotTime, Shots[ShotIndex].Duration);
}

void AInhCameraPawn::FinishShots()
{
	// Hand over to the orbit from the exact final pose of the last shot.
	ShotTime = Shots[ShotIndex].Duration;
	const FInhCameraView From = ShotView();

	TFunction<void()> Done = MoveTemp(OnShotsFinished);
	OnShotsFinished = nullptr;
	bPlayingShots = false;
	++PlaybackSerial;
	BeginBlendBack(From);
	if (Done)
	{
		Done();
	}
}

FInhCameraView AInhCameraPawn::ShotView() const
{
	const FInhShot& Shot = Shots[ShotIndex];
	FVector Location;
	FVector Look;
	float Focal = 35.f;
	InhCinematics::Evaluate(Shot, ShotTime, Location, Look, Focal);

	FInhCameraView View;
	View.Location = Location;
	const FVector Aim = Look - Location;
	View.Rotation = Aim.IsNearlyZero() ? LastView.Rotation : Aim.Rotation();
	View.FocalLength = Focal;
	View.Aperture = Shot.Aperture;
	View.FocusDistance = FMath::Max(float(Aim.Size()), 20.f);
	View.Handheld = FMath::Clamp(Shot.Handheld, 0.f, 1.f);
	return View;
}

void AInhCameraPawn::BeginBlendBack(const FInhCameraView& From)
{
	BlendFrom = From;
	BlendBackTime = 0.f;
	bBlendOverRoofs = !IsRoomSide(From.Location);
	// The way home over the rooftops is longer than the straight distance, so it gets more time per metre.
	const float Distance = float(FVector::Dist(From.Location, OrbitView().Location));
	BlendBackDuration = BlendDuration(bBlendOverRoofs ? Distance * 1.6f : Distance);
	bBlendingBack = true;
}

FInhCameraView AInhCameraPawn::BlendBackView(float Alpha) const
{
	// The orbit keeps updating underneath, so the blend lands on wherever it is by then.
	const FInhCameraView Target = OrbitView();
	FInhCameraView View = BlendViews(BlendFrom, Target, Alpha);
	if (bBlendOverRoofs)
	{
		// Rise, cross high over the buildings, then settle onto the orbit: never through a wall or a roof.
		const float Height = FMath::Max(RoofClearZ + 600.f, FMath::Max(float(BlendFrom.Location.Z), float(Target.Location.Z)) + 300.f);
		const FVector Up(BlendFrom.Location.X, BlendFrom.Location.Y, Height);
		const FVector Over(Target.Location.X, Target.Location.Y, Height);
		View.Location = CubicBezier(BlendFrom.Location, Up, Over, Target.Location, Alpha);
	}
	return View;
}

// --- frame --------------------------------------------------------------------------------------

void AInhCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Springs and blends take a clamped step so a hitch can't fling the camera; shots keep real time to stay in sync
	// with the world (the Ferryman's boat, the dawn).
	const float Step = FMath::Min(DeltaSeconds, 0.1f);
	Time += Step;
	TickOrbitRig(Step);

	if (bPlayingShots)
	{
		AdvanceShots(DeltaSeconds);
	}

	FInhCameraView View;
	if (bPlayingShots)
	{
		View = ShotView();
		if (EntryBlendTime < EntryBlendDuration)
		{
			EntryBlendTime = FMath::Min(EntryBlendTime + Step, EntryBlendDuration);
			View = BlendViews(EntryFrom, View, InhCinematics::Ease(EInhEase::EaseInOut, EntryBlendTime / EntryBlendDuration));
		}
	}
	else if (bBlendingBack)
	{
		BlendBackTime = FMath::Min(BlendBackTime + Step, BlendBackDuration);
		const float Alpha = BlendBackDuration > 0.f ? BlendBackTime / BlendBackDuration : 1.f;
		View = BlendBackView(InhCinematics::Ease(EInhEase::EaseInOut, Alpha));
		bBlendingBack = BlendBackTime < BlendBackDuration;
	}
	else
	{
		View = OrbitView();
	}
	ApplyView(View, Step);
}

void AInhCameraPawn::ApplyView(const FInhCameraView& View, float DeltaSeconds)
{
	if (bSnapNext || !bHasView)
	{
		// A cut: the lens state jumps with the picture instead of visibly catching up.
		FocusDistance = View.FocusDistance;
		FocusVelocity = 0.f;
		Aperture = View.Aperture;
		ApertureVelocity = 0.f;
		Handheld = View.Handheld;
		bSnapNext = false;
	}
	else
	{
		// Shot focus is already smooth, so it is tracked tightly; the orbit racks focus more slowly, like a focus puller.
		const float FocusSmoothing = bPlayingShots ? 0.05f : 0.35f;
		FocusDistance = SmoothDamp(FocusDistance, View.FocusDistance, FocusVelocity, FocusSmoothing, DeltaSeconds);
		Aperture = SmoothDamp(Aperture, View.Aperture, ApertureVelocity, 0.6f, DeltaSeconds);
		Handheld = FMath::FInterpTo(Handheld, View.Handheld, DeltaSeconds, 1.5f);
	}
	LastView = View;
	bHasView = true;

	Camera->SetWorldLocationAndRotation(View.Location, View.Rotation + HandheldOffset(Time, Handheld));
	Camera->CurrentFocalLength = PortraitFocalLength(View.FocalLength, Camera->Filmback.SensorWidth);
	Camera->CurrentAperture = Aperture;
	Camera->FocusSettings.ManualFocusDistance = FMath::Max(FocusDistance, 20.f);
	UpdateMobileDepthOfField();
}

void AInhCameraPawn::UpdateMobileDepthOfField()
{
	// Phones render a cheaper gaussian depth of field that ignores the lens, so derive it from the same optics:
	// sharp from the focus plane to the far depth-of-field limit, then a soft falloff behind. No near blur on
	// phones (cheaper, and it smears foreground edges).
	const float FocalMm = Camera->CurrentFocalLength;
	const float HyperfocalCm = FocalMm * FocalMm / (FMath::Max(Aperture, 0.5f) * CircleOfConfusionMm) * 0.1f;
	const float FarLimit = FocusDistance < HyperfocalCm * 0.99f
		? HyperfocalCm * FocusDistance / (HyperfocalCm - FocusDistance)
		: FocusDistance + 1000000.f;
	const float SharpBehind = FMath::Min(FarLimit - FocusDistance, 1000000.f);

	FPostProcessSettings& Post = Camera->PostProcessSettings;
	Post.bOverride_DepthOfFieldScale = true;
	Post.DepthOfFieldScale = 1.f;
	Post.bOverride_DepthOfFieldFocalRegion = true;
	Post.DepthOfFieldFocalRegion = SharpBehind;
	Post.bOverride_DepthOfFieldFarTransitionRegion = true;
	Post.DepthOfFieldFarTransitionRegion = FMath::Clamp(SharpBehind * 2.f, 100.f, 1000000.f);
	Post.bOverride_DepthOfFieldNearTransitionRegion = true;
	Post.DepthOfFieldNearTransitionRegion = 0.f;
	Post.bOverride_DepthOfFieldNearBlurSize = true;
	Post.DepthOfFieldNearBlurSize = 0.f;
	Post.bOverride_DepthOfFieldFarBlurSize = true;
	Post.DepthOfFieldFarBlurSize = 3.f;
}
