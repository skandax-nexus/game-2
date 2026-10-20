#include "World/InhCinematics.h"
#include "World/InhLayout.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"

// Every shot is placed from InhLayout so the camera stays in step with the room and the harbour.
// Rules the shots keep (checked against the room geometry):
//  - Solid: back wall X 400..410 (except the window), left wall Y -310..-300, floor Z 0, our building below the
//    room down to the street, neighbouring houses behind the facade line X = 410, the street Z -420, the sea -540.
//  - The window has a cross (mullion and transom) at its centre, so the camera only ever passes through the outer
//    upper pane, on a straight line, with ~26 cm to spare on every side.
//  - The room has no ceiling, so it can be entered from above; the front (-X) and right (+Y) sides are open.

namespace
{
	FInhShot MakeShot(const FVector& CameraFrom, const FVector& CameraTo, const FVector& LookFrom, const FVector& LookTo,
		float FocalFrom, float FocalTo, float Aperture, float Duration, EInhEase Ease, float Handheld)
	{
		FInhShot Shot;
		Shot.CameraFrom = CameraFrom;
		Shot.CameraTo = CameraTo;
		Shot.LookFrom = LookFrom;
		Shot.LookTo = LookTo;
		Shot.FocalFrom = FocalFrom;
		Shot.FocalTo = FocalTo;
		Shot.Aperture = Aperture;
		Shot.Duration = Duration;
		Shot.Ease = Ease;
		Shot.Handheld = Handheld;
		return Shot;
	}

	/** The middle of the window's outer upper pane, halfway through the wall: the only way in or out. */
	FVector WindowPane()
	{
		using namespace InhLayout;
		return FVector(WindowCenter.X + 5.f, (WindowCenter.Y + WindowMaxY) * 0.5f, (WindowCenter.Z + WindowMaxZ) * 0.5f);
	}

	/** The home orbit pose; DistanceScale as InhCinematics::OrbitDistanceScale() gives where the live orbit sits. */
	FVector HomeCamera(float DistanceScale = 1.f)
	{
		return InhCinematics::OrbitLocation(InhLayout::RoomCameraFocus, InhCinematics::HomeYaw, InhCinematics::HomePitch,
			InhCinematics::HomeArmLength * DistanceScale);
	}

	/** Behind the title screen, on a loop: the harbour at night, no captions. */
	void BuildTitleAttract(TArray<FInhShot>& Shots)
	{
		using namespace InhLayout;

		// A low glide over the water towards the lighthouse in the fog, the far shore's lights behind it.
		Shots.Add(MakeShot(
			FVector(QuayEdgeX + 250.f, -2500.f, SeaLevel + 110.f), FVector(QuayEdgeX + 550.f, -1800.f, SeaLevel + 130.f),
			LighthouseBase + FVector(0.f, 0.f, 560.f), LighthouseBase + FVector(0.f, 0.f, 620.f),
			26.f, 28.f, 4.f, 7.5f, EInhEase::Linear, 0.25f));

		// A slow lateral track along the quay street, past the lit windows of the neighbours.
		Shots.Add(MakeShot(
			FVector(FacadeX + 290.f, -1950.f, StreetZ + 180.f), FVector(FacadeX + 290.f, -1150.f, StreetZ + 188.f),
			FVector(FacadeX, -1600.f, StreetZ + 270.f), FVector(FacadeX, -800.f, StreetZ + 280.f),
			32.f, 32.f, 2.8f, 7.f, EInhEase::Linear, 0.3f));

		// High and wide over the street: the whole lighthouse over the bay, the far shore's waterfront and lower slopes
		// across the water near the top of frame (it is 130 m off, so a steeper tilt would lose it).
		Shots.Add(MakeShot(
			FVector(FacadeX + 240.f, -2100.f, 1350.f), FVector(FacadeX + 310.f, -1450.f, 1290.f),
			LighthouseBase + FVector(100.f, -650.f, 950.f), LighthouseBase + FVector(200.f, 0.f, 900.f),
			24.f, 24.f, 8.f, 7.5f, EInhEase::Linear, 0.08f));

		// From over the quay edge, a slow push up towards our lit window, settling on it before the loop cuts back.
		Shots.Add(MakeShot(
			FVector(QuayEdgeX + 450.f, -330.f, StreetZ + 290.f), FVector(QuayEdgeX + 90.f, -110.f, StreetZ + 430.f),
			WindowCenter + FVector(5.f, 10.f, -5.f), WindowCenter + FVector(5.f, 0.f, 0.f),
			35.f, 55.f, 2.f, 7.f, EInhEase::EaseOut, 0.2f));
	}

	/** A new life begins: over the bay, down to the quay, in through our window, and into the room orbit. */
	void BuildOpening(TArray<FInhShot>& Shots)
	{
		using namespace InhLayout;
		const FVector Pane = WindowPane();

		// One straight descent split in two (EaseIn then EaseOut at the same average speed, so it flows through B).
		// The line runs from a hover facing our window, C, up over the lighthouse lantern with ~4 m to spare and on
		// beyond it, so the first frame has the lantern below the lit quay and the flight passes right over it.
		const FVector C(QuayEdgeX + 200.f, Pane.Y, Pane.Z);
		const FVector OverLantern = LighthouseLamp + FVector(0.f, 0.f, 510.f);
		const FVector A = C + (OverLantern - C) * 1.45f;
		const float FirstSeconds = 5.f;   // the controller shows the "HOLLOW BAY" card over this one
		const float SecondSeconds = 4.f;
		const FVector B = A + (C - A) * (FirstSeconds / (FirstSeconds + SecondSeconds));
		const FVector QuayLook(QuayEdgeX, 100.f, StreetZ + 170.f);
		const FVector FacadeLook(FacadeX + 40.f, 45.f, 60.f);
		// Inside the room, seen through the window from the quay side.
		const FVector RoomLook(0.f, 55.f, 130.f);
		// Once inside, look across to the bed and shelves: the crane that follows then never passes over its own
		// aim point, which keeps the swing round to the orbit gentle.
		const FVector CornerLook(-100.f, -180.f, 90.f);

		Shots.Add(MakeShot(A, B, QuayLook, FacadeLook, 24.f, 26.f, 8.f, FirstSeconds, EInhEase::EaseIn, 0.1f));
		Shots.Add(MakeShot(B, C, FacadeLook, RoomLook, 26.f, 30.f, 5.6f, SecondSeconds, EInhEase::EaseOut, 0.12f));

		// Straight in through the outer upper pane (constant Y and Z, so the crossing is exact), stopping inside.
		const FVector D(260.f, Pane.Y, Pane.Z);
		Shots.Add(MakeShot(C, D, RoomLook, CornerLook, 30.f, 28.f, 2.8f, 3.25f, EInhEase::EaseInOut, 0.15f));

		// Crane up over the open top of the room and swing round onto the home orbit pose, as far out as the orbit sits on
		// this screen (much farther on an upright phone), so the move lands on the orbit instead of settling short of it
		// and then dollying out again. The longer trip gets a little more time so it doesn't rush.
		const float OrbitScale = InhCinematics::OrbitDistanceScale();
		Shots.Add(MakeShot(D, HomeCamera(OrbitScale), CornerLook, RoomCameraFocus, 28.f, InhCinematics::OrbitFocalLength, 2.4f,
			3.75f * FMath::Sqrt(OrbitScale), EInhEase::EaseInOut, 0.12f));
	}

	/** The voice note waits on the table: a slow dolly round the phone on a long lens, almost no depth of field. */
	void BuildWillReveal(TArray<FInhShot>& Shots)
	{
		// The phone lies on the table at (20, 75, 79) (see AInhRoomBuilder::ShowLife). The path stays on the open
		// front side above the table edge, high enough that the sight line to the phone passes over the chair back
		// (top at Z 109, X -85..-80) by 8 cm and over an imported chair (at most 100 tall) all the way round: at f/1.8 a
		// blurred chair edge would wipe across the one subject of the shot.
		const FVector Phone(20.f, 75.f, 81.f);
		Shots.Add(MakeShot(
			FVector(-160.f, -60.f, 150.f), FVector(-115.f, 175.f, 127.f),
			Phone + FVector(5.f, -5.f, 1.f), Phone + FVector(0.f, 1.f, -1.f),
			45.f, 55.f, 1.8f, 6.f, EInhEase::EaseInOut, 0.2f));
	}

	/** Dawn ends the life: to the window, out over the quay, the Ferryman passes Mara at the steps, then the horizon. */
	void BuildSunrise(TArray<FInhShot>& Shots)
	{
		using namespace InhLayout;
		const FVector Pane = WindowPane();
		const float ToWindowSeconds = 3.5f;
		const float LeanOutSeconds = 3.f;
		const float FindBoatSeconds = 1.75f;
		const float FollowSeconds = InhCinematics::SunriseFerrymanPassTime - (ToWindowSeconds + LeanOutSeconds + FindBoatSeconds);
		const float HorizonSeconds = 3.5f;

		// Start a little way from the home orbit towards the window (the pawn blends in from the live orbit view), and
		// glide to WindowViewpoint. From there the steps are hidden below the sill, so the look goes out across the
		// bay towards the dawn, through the middle of the lower outer pane rather than the cross bars.
		const FVector InRoom = FMath::Lerp(HomeCamera(), WindowViewpoint, 0.3f);
		const FVector OutOverBay(3200.f, 780.f, SeaLevel + 180.f);
		Shots.Add(MakeShot(InRoom, WindowViewpoint, RoomCameraFocus, OutOverBay,
			InhCinematics::OrbitFocalLength, 32.f, 2.8f, ToWindowSeconds, EInhEase::EaseInOut, 0.12f));

		// Lean out through the outer upper pane on the line from WindowViewpoint through its centre, tilting down to
		// the water steps where Mara stands.
		const FVector Outside = WindowViewpoint + (Pane - WindowViewpoint) * 2.f;
		Shots.Add(MakeShot(WindowViewpoint, Outside, OutOverBay, WaterSteps,
			32.f, 28.f, 4.f, LeanOutSeconds, EInhEase::EaseInOut, 0.15f));

		// Drift down over the street and pan along the Ferryman's line to where the boat is by then.
		const float FoundAt = (ToWindowSeconds + LeanOutSeconds + FindBoatSeconds) / InhCinematics::SunriseFerrymanPassTime;
		const FVector BoatFound = FMath::Lerp(FerrymanStart, FerrymanPassing, FoundAt);
		const FVector OverStreet(FacadeX + 310.f, 30.f, StreetZ + 320.f);
		Shots.Add(MakeShot(Outside, OverStreet, WaterSteps, BoatFound,
			28.f, 28.f, 4.f, FindBoatSeconds, EInhEase::EaseInOut, 0.18f));

		// Follow the boat as it passes the steps and Mara at the waterline.
		const FVector Watching(FacadeX + 350.f, 0.f, StreetZ + 275.f);
		Shots.Add(MakeShot(OverStreet, Watching, BoatFound, FerrymanPassing,
			28.f, 32.f, 4.f, FollowSeconds, EInhEase::EaseInOut, 0.18f));

		// Tilt up to the horizon where the sun rises (SunriseYaw points the light inwards, so it rises opposite),
		// with the boat heading out to sea on the right of frame.
		const FVector Rise(FacadeX + 390.f, -20.f, StreetZ + 320.f);
		const FVector Horizon = Rise + FRotator(5.f, SunriseYaw - 180.f + 6.f, 0.f).Vector() * 3600.f;
		Shots.Add(MakeShot(Watching, Rise, FerrymanPassing, Horizon,
			32.f, 30.f, 5.6f, HorizonSeconds, EInhEase::EaseInOut, 0.12f));
	}
}

TArray<FInhShot> InhCinematics::Build(EInhCinematic Which)
{
	TArray<FInhShot> Shots;
	switch (Which)
	{
	case EInhCinematic::TitleAttract: BuildTitleAttract(Shots); break;
	case EInhCinematic::Opening: BuildOpening(Shots); break;
	case EInhCinematic::WillReveal: BuildWillReveal(Shots); break;
	case EInhCinematic::Sunrise: BuildSunrise(Shots); break;
	}
	return Shots;
}

float InhCinematics::TotalDuration(const TArray<FInhShot>& Shots)
{
	float Total = 0.f;
	for (const FInhShot& Shot : Shots)
	{
		Total += FMath::Max(Shot.Duration, 0.f);
	}
	return Total;
}

float InhCinematics::Ease(EInhEase Kind, float Alpha)
{
	const float T = FMath::Clamp(Alpha, 0.f, 1.f);
	// Quintic smootherstep: zero speed and zero acceleration at both ends, so moves that meet at a pause never jolt.
	auto Smoother = [](float X) { return X * X * X * (X * (X * 6.f - 15.f) + 10.f); };
	switch (Kind)
	{
	case EInhEase::EaseInOut: return Smoother(T);
	// The two halves of the same curve, so an EaseIn shot hands over to an EaseOut shot without a bump.
	case EInhEase::EaseIn: return 2.f * Smoother(T * 0.5f);
	case EInhEase::EaseOut: return 2.f * Smoother(0.5f + T * 0.5f) - 1.f;
	default: return T;
	}
}

void InhCinematics::Evaluate(const FInhShot& Shot, float Time, FVector& OutCamera, FVector& OutLook, float& OutFocal)
{
	const float Alpha = Ease(Shot.Ease, Shot.Duration > 0.f ? Time / Shot.Duration : 1.f);
	OutCamera = FMath::Lerp(Shot.CameraFrom, Shot.CameraTo, Alpha);
	OutLook = FMath::Lerp(Shot.LookFrom, Shot.LookTo, Alpha);
	OutFocal = FMath::Lerp(Shot.FocalFrom, Shot.FocalTo, Alpha);
}

FVector InhCinematics::OrbitLocation(const FVector& Focus, float Yaw, float Pitch, float ArmLength)
{
	return Focus - FRotator(Pitch, Yaw, 0.f).Vector() * ArmLength;
}

float InhCinematics::OrbitDistanceScale()
{
	// Phones held upright see less width, so pull back to keep the whole room in frame.
	if (GEngine && GEngine->GameViewport)
	{
		FVector2D Size;
		GEngine->GameViewport->GetViewportSize(Size);
		if (Size.X > 0.0 && Size.Y > Size.X)
		{
			return FMath::Max(FMath::Clamp(float(Size.Y / Size.X), 1.f, 2.2f) * 0.85f, 1.f);
		}
	}
	return 1.f;
}
