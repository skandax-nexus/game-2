#include "World/InhAtmosphereFX.h"
#include "World/InhArt.h"
#include "World/InhLayout.h"
#include "World/InhShapes.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"

// Helpers live in a named namespace: unity builds share one translation unit, where anonymous names could collide.
namespace InhAtmosphereParts
{
	constexpr int32 NumAmbientMotes = 70;
	constexpr int32 NumShaftMotes = 50;
	constexpr int32 MaxDrops = 600;
	constexpr int32 NumFogCards = 10;
	/** Motes keep this far inside the room's walls, floor and open top. */
	constexpr float RoomMargin = 15.f;
	/** How far the window's light reaches into the room before the motes in it thin out. */
	constexpr float ShaftLength = 700.f;
	/** Rain falls from here down to the street or the sea. */
	constexpr float RainTopZ = 650.f;
	constexpr float RainHalfWidth = 1300.f;
	/** Hidden instances shrink to this rather than zero, so their transforms stay invertible. */
	const FVector HiddenScale(0.0001f);

	FTransform HiddenAt(const FVector& Location)
	{
		return FTransform(FQuat::Identity, Location, HiddenScale);
	}

	/**
	 * The light's direction through the night, matching the keys in AInhRoomBuilder::SetLifeProgress (the dusk sun, the
	 * moon, moonset, the swing round to the sunrise, dawn).
	 */
	FRotator SunRotation(float Progress)
	{
		struct FKey { float At; float Pitch; float Yaw; };
		constexpr float NightPitch = -InhLayout::MoonElevation;
		constexpr float NightYaw = InhLayout::MoonYaw + 180.f;
		constexpr float DayYaw = InhLayout::SunriseYaw;
		static const FKey Keys[] = { { 0.f, -9.f, DayYaw }, { 0.2f, NightPitch, NightYaw }, { 0.8f, NightPitch, NightYaw },
			{ 0.87f, NightPitch, NightYaw }, { 0.88f, -1.f, DayYaw }, { 1.f, -5.f, DayYaw } };
		int32 i = 0;
		while (i < int32(UE_ARRAY_COUNT(Keys)) - 2 && Progress > Keys[i + 1].At)
		{
			++i;
		}
		const float T = FMath::SmoothStep(0.f, 1.f, (Progress - Keys[i].At) / FMath::Max(Keys[i + 1].At - Keys[i].At, UE_KINDA_SMALL_NUMBER));
		return FRotator(FMath::Lerp(Keys[i].Pitch, Keys[i + 1].Pitch, T), FMath::Lerp(Keys[i].Yaw, Keys[i + 1].Yaw, T), 0.f);
	}

	/** Where rain lands: the street in front of the houses, or the sea beyond the quay edge. */
	float GroundZ(float X)
	{
		return X < InhLayout::QuayEdgeX ? InhLayout::StreetZ : InhLayout::SeaLevel;
	}
}

AInhAtmosphereFX::AInhAtmosphereFX()
	: Rng(1907 + 7)
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void AInhAtmosphereFX::BeginPlay()
{
	Super::BeginPlay();
	if (!bBuilt)
	{
		BuildDust();
		BuildRain();
		BuildFog();
		bBuilt = true;
	}
	SetLifeProgress(Progress);
}

void AInhAtmosphereFX::SetLifeProgress(float InProgress)
{
	Progress = FMath::Clamp(InProgress, 0.f, 1.f);
	const float IntoNight = FMath::SmoothStep(0.f, 0.2f, Progress);
	const float IntoDawn = FMath::SmoothStep(0.8f, 1.f, Progress);
	// Dust shows where light catches it: some at dusk, little by night, the dawn shaft full of it.
	DustAmount = FMath::Lerp(FMath::Lerp(0.55f, 0.3f, IntoNight), 1.f, IntoDawn);
	// The light swings from the moon round to the sunrise while too faint to see, just as the Sunrise looks out of the
	// window: the shaft empties with the fading moon and fills again with the sun, so it never sweeps round on screen.
	const float Swinging = FMath::SmoothStep(0.83f, 0.87f, Progress) * (1.f - FMath::SmoothStep(0.88f, 0.9f, Progress));
	ShaftAmount = FMath::Lerp(FMath::Lerp(0.6f, 0.35f, IntoNight), 1.f, IntoDawn) * (1.f - Swinging);
	ShaftDirection = InhAtmosphereParts::SunRotation(Progress).Vector();
	UpdateFogLook();
}

void AInhAtmosphereFX::SetRain(float Amount01)
{
	RainTarget = FMath::Clamp(Amount01, 0.f, 1.f);
	UpdateFogLook();
}

void AInhAtmosphereFX::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBuilt)
	{
		return;
	}
	Time += DeltaSeconds;
	TickDust(DeltaSeconds);
	TickRain(DeltaSeconds);
	TickFog(DeltaSeconds);
}

// --- dust -----------------------------------------------------------------------------------------------------------

void AInhAtmosphereFX::BuildDust()
{
	using namespace InhAtmosphereParts;
	// Without the art, a packaged build may be unable to instance the flat fallback (InhShapes::CanInstance): then there
	// is no dust rather than specks of the default grid material, or a hundred components moved every frame.
	if (!InhShapes::CanInstanceEmissive())
	{
		return;
	}
	// Tiny cubes: at a centimetre they read as points of light and cost 12 triangles where a sphere costs hundreds.
	Dust = InhShapes::AddInstancedEmissive(this, Root, InhShapes::Cube(), FLinearColor(1.f, 0.93f, 0.82f), 1.6f);
	if (!Dust)
	{
		return;
	}
	const FVector Half = InhLayout::RoomHalfExtent;
	const float RoomTop = float(Half.Z) * 2.f;
	const int32 Total = NumAmbientMotes + NumShaftMotes;
	Motes.SetNum(Total);
	DustTransforms.SetNum(Total);
	for (int32 i = 0; i < Total; ++i)
	{
		FInhMote& Mote = Motes[i];
		Mote.bInShaft = i >= NumAmbientMotes;
		Mote.Base = Mote.bInShaft
			? FVector(FMath::Pow(Rng.FRand(), 1.6f), Rng.FRand(), Rng.FRand())
			: FVector(Rng.FRandRange(-float(Half.X) + RoomMargin, float(Half.X) - RoomMargin), Rng.FRandRange(-float(Half.Y) + RoomMargin, float(Half.Y) - RoomMargin),
				Rng.FRandRange(RoomMargin, RoomTop - RoomMargin));
		Mote.Phase = FVector(Rng.FRandRange(0.f, 2.f * UE_PI), Rng.FRandRange(0.f, 2.f * UE_PI), Rng.FRandRange(0.f, 2.f * UE_PI));
		Mote.Rate = FVector(Rng.FRandRange(0.07f, 0.2f), Rng.FRandRange(0.07f, 0.2f), Rng.FRandRange(0.05f, 0.15f));
		Mote.Size = Rng.FRandRange(0.5f, 1.1f);
		Mote.Threshold = Rng.FRand();
		// Spread over the whole room to start with, so the component's bounds already cover where motes go.
		const FVector Start = Mote.bInShaft ? InhLayout::RoomCameraFocus : Mote.Base;
		DustTransforms[i] = FTransform(FQuat::Identity, Start, FVector(Mote.Size / 100.f));
		Dust->AddInstance(DustTransforms[i]);
	}
}

void AInhAtmosphereFX::TickDust(float DeltaSeconds)
{
	using namespace InhAtmosphereParts;
	if (!Dust || Motes.Num() == 0)
	{
		return;
	}
	const FVector Half = InhLayout::RoomHalfExtent;
	const float RoomTop = float(Half.Z) * 2.f;
	const float WindowWidth = InhLayout::WindowMaxY - InhLayout::WindowMinY;
	const float WindowHeight = InhLayout::WindowMaxZ - InhLayout::WindowMinZ;
	for (int32 i = 0; i < Motes.Num(); ++i)
	{
		FInhMote& Mote = Motes[i];
		const float RX = float(Mote.Rate.X);
		const float RY = float(Mote.Rate.Y);
		const float RZ = float(Mote.Rate.Z);
		const float PX = float(Mote.Phase.X);
		const float PY = float(Mote.Phase.Y);
		const float PZ = float(Mote.Phase.Z);
		// Smooth drift: two incommensurate sines per axis, never a per-frame jitter.
		const FVector Wander(
			FMath::Sin(Time * RX + PX) + 0.4f * FMath::Sin(Time * RX * 2.3f + PY * 1.7f),
			FMath::Sin(Time * RY + PY) + 0.4f * FMath::Sin(Time * RY * 2.1f + PZ * 1.3f),
			FMath::Sin(Time * RZ + PZ) + 0.4f * FMath::Sin(Time * RZ * 2.7f + PX * 1.1f));
		FVector Location;
		float Weight = 0.f;
		if (Mote.bInShaft)
		{
			// A point in the window opening, carried into the room along the light; the shaft widens a little as it goes.
			const float Along = float(Mote.Base.X);
			const float Spread = 1.f + Along * 0.35f;
			const FVector InWindow(InhLayout::WindowCenter.X - 5.0,
				InhLayout::WindowCenter.Y + (Mote.Base.Y - 0.5) * WindowWidth * Spread,
				InhLayout::WindowCenter.Z + (Mote.Base.Z - 0.5) * WindowHeight * Spread);
			Location = InWindow + ShaftDirection * (Along * ShaftLength) + Wander * 8.f;
			// Thinner at the far end of the shaft.
			Weight = ShaftAmount * (1.f - FMath::SmoothStep(0.6f, 1.f, Along));
		}
		else
		{
			// Ambient motes settle very slowly and start again at the top.
			Mote.Base.Z -= 0.6f * FMath::Min(DeltaSeconds, 0.1f);
			if (Mote.Base.Z < RoomMargin)
			{
				Mote.Base.Z = RoomTop - RoomMargin;
			}
			Location = Mote.Base + Wander * 14.f;
			const float Z = float(Location.Z);
			Weight = FMath::Clamp((DustAmount - Mote.Threshold) / 0.1f, 0.f, 1.f)
				* FMath::SmoothStep(RoomMargin, RoomMargin + 25.f, Z) * (1.f - FMath::SmoothStep(RoomTop - RoomMargin - 25.f, RoomTop - RoomMargin, Z));
		}
		// Only inside the room: a mote carried out through a wall or the floor simply isn't there.
		const bool bInside = FMath::Abs(Location.X) < Half.X - 5.0 && FMath::Abs(Location.Y) < Half.Y - 5.0 && Location.Z > 2.0 && Location.Z < RoomTop - 2.0;
		// Each catches the light now and then as it turns.
		const float Glint = 0.75f + 0.25f * FMath::Sin(Time * (1.3f + RX * 6.f) + PY * 3.f);
		const float Scale = bInside ? Mote.Size * Weight * Glint / 100.f : 0.f;
		DustTransforms[i] = Scale > 0.0005f ? FTransform(FQuat::Identity, Location, FVector(Scale)) : HiddenAt(Location);
	}
	Dust->BatchUpdateInstancesTransforms(0, DustTransforms, false, true, false);
}

// --- rain -----------------------------------------------------------------------------------------------------------

void AInhAtmosphereFX::BuildRain()
{
	using namespace InhAtmosphereParts;
	// As with the dust: no rain at all where the fallback can't be instanced, rather than hundreds of single parts.
	if (!InhShapes::CanInstanceEmissive())
	{
		return;
	}
	// Thin streaks that catch the lamplight; a light breeze off the sea slants them a touch.
	Rain = InhShapes::AddInstancedEmissive(this, Root, InhShapes::Cube(), FLinearColor(0.62f, 0.68f, 0.78f), 0.8f);
	if (!Rain)
	{
		return;
	}
	RainDrift = FVector(-35.f, 55.f, 0.f);
	RainTurn = FRotationMatrix::MakeFromZ(FVector(RainDrift.X, RainDrift.Y, -950.0)).Rotator();
	Drops.SetNum(MaxDrops);
	RainTransforms.SetNum(MaxDrops);
	for (int32 i = 0; i < MaxDrops; ++i)
	{
		RespawnDrop(Drops[i], true);
		RainTransforms[i] = HiddenAt(Drops[i].Location);
		Rain->AddInstance(RainTransforms[i]);
	}
	// Hidden until it rains; the drops start spread through the whole fall, so the bounds already cover it.
	Rain->SetVisibility(false);
	bRainShown = false;
}

void AInhAtmosphereFX::RespawnDrop(FInhRaindrop& Drop, bool bAnyHeight)
{
	using namespace InhAtmosphereParts;
	// Thickest just outside the window and over the quay, thinning out over the water.
	const float MinX = InhLayout::FacadeX + 15.f;
	const float MaxX = InhLayout::QuayEdgeX + 1100.f;
	const float Across = Rng.FRandRange(-1.f, 1.f);
	const float X = MinX + FMath::Pow(Rng.FRand(), 1.6f) * (MaxX - MinX);
	const float Y = float(InhLayout::WindowCenter.Y) + FMath::Sign(Across) * FMath::Pow(FMath::Abs(Across), 1.4f) * RainHalfWidth;
	const float Ground = GroundZ(X);
	const float Z = bAnyHeight ? Rng.FRandRange(Ground, RainTopZ) : RainTopZ + Rng.FRandRange(0.f, 200.f);
	Drop.Location = FVector(X, Y, Z);
	Drop.Speed = Rng.FRandRange(850.f, 1050.f);
	Drop.Length = Rng.FRandRange(40.f, 70.f);
}

void AInhAtmosphereFX::TickRain(float DeltaSeconds)
{
	using namespace InhAtmosphereParts;
	if (!Rain || Drops.Num() == 0)
	{
		return;
	}
	RainLevel = FMath::FInterpConstantTo(RainLevel, RainTarget, DeltaSeconds, 0.35f);
	if (RainLevel <= 0.f)
	{
		if (bRainShown)
		{
			// Nothing falling: no draw call, no per-frame upload.
			Rain->SetVisibility(false);
			bRainShown = false;
		}
		return;
	}
	if (!bRainShown)
	{
		Rain->SetVisibility(true);
		bRainShown = true;
	}

	// The first drops in the list fall; the rest keep falling unseen, so more rain fades in mid-air, as it does.
	const int32 Active = FMath::RoundToInt(RainLevel * Drops.Num());
	const float Step = FMath::Min(DeltaSeconds, 0.1f);
	const float MinX = InhLayout::FacadeX + 5.f;
	for (int32 i = 0; i < Drops.Num(); ++i)
	{
		FInhRaindrop& Drop = Drops[i];
		Drop.Location += FVector(RainDrift.X * Step, RainDrift.Y * Step, -Drop.Speed * Step);
		if (Drop.Location.Z < GroundZ(float(Drop.Location.X)) || Drop.Location.X < MinX)
		{
			RespawnDrop(Drop, false);
		}
		RainTransforms[i] = i < Active
			? FTransform(RainTurn, Drop.Location, FVector(0.008f, 0.008f, Drop.Length / 100.f))
			: HiddenAt(Drop.Location);
	}
	Rain->BatchUpdateInstancesTransforms(0, RainTransforms, false, true, false);
}

// --- mist -----------------------------------------------------------------------------------------------------------

void AInhAtmosphereFX::BuildFog()
{
	using namespace InhAtmosphereParts;
	// Only with the art pipeline's soft, depth-faded fog card material; a flat opaque card would look like a wall.
	const FName FogKey(TEXT("FogCard"));
	UMaterialInterface* FogArt = InhArt::Material(FogKey);
	if (!FogArt)
	{
		return;
	}
	FogCards = InhShapes::AddInstanced(this, Root, InhShapes::Plane(), FogKey, FLinearColor(0.55f, 0.62f, 0.70f), false);
	if (!FogCards)
	{
		return;
	}
	FogCards->SetCastShadow(false);
	FogMaterial = UMaterialInstanceDynamic::Create(FogArt, FogCards);
	FogCards->SetMaterial(0, FogMaterial);

	// Out over the bay, drifting along the shore on the night breeze. The leaning sheets reach about 1.5 m back towards
	// the town, so they start well past the low glide over the water (which ends 5.5 m out from the quay edge).
	Cards.SetNum(NumFogCards);
	FogTransforms.SetNum(NumFogCards);
	for (int32 i = 0; i < NumFogCards; ++i)
	{
		FInhFogCard& Card = Cards[i];
		Card.Width = Rng.FRandRange(1400.f, 2000.f);
		Card.Height = Rng.FRandRange(380.f, 520.f);
		Card.Speed = Rng.FRandRange(12.f, 28.f);
		Card.Location = FVector(Rng.FRandRange(InhLayout::QuayEdgeX + 1400.f, float(InhLayout::LighthouseBase.X) + 1100.f),
			Rng.FRandRange(-3200.f, 3200.f), InhLayout::SeaLevel + Rng.FRandRange(120.f, 200.f));
		FogTransforms[i] = FTransform(FQuat::Identity, Card.Location, FVector(Card.Height / 100.f, Card.Width / 100.f, 1.f));
		FogCards->AddInstance(FogTransforms[i]);
	}
	UpdateFogLook();
}

void AInhAtmosphereFX::TickFog(float DeltaSeconds)
{
	if (!FogCards || Cards.Num() == 0)
	{
		return;
	}
	const float MinY = -3200.f;
	const float MaxY = 3200.f;
	// Facing the town, leaning back so they also read from above.
	static const FQuat Lean = FRotationMatrix::MakeFromZX(FVector(-0.82f, 0.f, 0.57f), FVector::UpVector).ToQuat();
	for (int32 i = 0; i < Cards.Num(); ++i)
	{
		FInhFogCard& Card = Cards[i];
		Card.Location.Y += Card.Speed * FMath::Min(DeltaSeconds, 0.1f);
		if (Card.Location.Y > MaxY)
		{
			Card.Location.Y = MinY;
		}
		// Each sheet swells in from nothing at one end of its run and thins away at the other, out of the way of the shots.
		const float Y = float(Card.Location.Y);
		const float Fade = FMath::SmoothStep(MinY, MinY + 900.f, Y) * (1.f - FMath::SmoothStep(MaxY - 900.f, MaxY, Y));
		const float Breath = 1.f + 0.06f * FMath::Sin(Time * 0.2f + i * 1.7f);
		FogTransforms[i] = FTransform(Lean, Card.Location, FVector(Card.Height / 100.f * FMath::Max(Fade, 0.0001f), Card.Width / 100.f * FMath::Max(Fade, 0.0001f) * Breath, 1.f));
	}
	FogCards->BatchUpdateInstancesTransforms(0, FogTransforms, false, true, false);
}

void AInhAtmosphereFX::UpdateFogLook()
{
	if (!FogMaterial)
	{
		return;
	}
	// The cards are unlit, so their colour is light they give off: it follows the height fog's in
	// AInhRoomBuilder::SetLifeProgress, or by night they would glow grey over the dark sea. The height fog also takes in
	// the sky's light, which outshines that colour once the sun is up (the dawn sun is some thirty times the dusk one),
	// so the cards brighten with the sunrise, as the sun's lux climbs: backlit mist glows, where at the dusk level the
	// sheets would lie over the sun's path on the sea like dark patches.
	static const FName ColorName(TEXT("Color"));
	const float Warmth = FMath::Max(1.f - FMath::SmoothStep(0.f, 0.2f, Progress), FMath::SmoothStep(0.8f, 1.f, Progress));
	const float Sunlit = FMath::Lerp(1.f, 3.5f, FMath::SmoothStep(0.92f, 1.f, Progress));
	FogMaterial->SetVectorParameterValue(ColorName, FMath::Lerp(FLinearColor(0.03f, 0.04f, 0.06f), FLinearColor(0.40f, 0.31f, 0.30f) * Sunlit, Warmth));
	// Thicker towards morning and in the rain; the manifest's 0.35 is the dusk look.
	static const FName OpacityName(TEXT("Opacity"));
	const float Opacity = 0.35f * (0.8f + 0.45f * FMath::SmoothStep(0.75f, 1.f, Progress) + 0.3f * RainTarget);
	FogMaterial->SetScalarParameterValue(OpacityName, FMath::Min(Opacity, 0.6f));
}
