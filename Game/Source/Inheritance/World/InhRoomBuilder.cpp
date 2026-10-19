#include "World/InhRoomBuilder.h"
#include "World/InhShapes.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

namespace Palette
{
	const FLinearColor Wood(0.10f, 0.055f, 0.03f);
	const FLinearColor LightWood(0.16f, 0.10f, 0.055f);
	const FLinearColor DarkWood(0.04f, 0.022f, 0.012f);
	const FLinearColor Plaster(0.20f, 0.23f, 0.24f);
	const FLinearColor PlasterShade(0.15f, 0.18f, 0.20f);
	const FLinearColor Cork(0.33f, 0.20f, 0.09f);
	const FLinearColor Rug(0.24f, 0.035f, 0.03f);
	const FLinearColor Brass(0.75f, 0.50f, 0.18f);
	const FLinearColor Paper(0.85f, 0.80f, 0.66f);
	const FLinearColor Iron(0.07f, 0.07f, 0.08f);
	const FLinearColor Blanket(0.10f, 0.18f, 0.26f);
	const FLinearColor Linen(0.70f, 0.68f, 0.62f);
	const FLinearColor Sea(0.008f, 0.025f, 0.04f);
	const FLinearColor Town(0.025f, 0.03f, 0.04f);
	const FLinearColor Notice(0.55f, 0.06f, 0.05f);
	const FLinearColor SeaGlass(0.10f, 0.35f, 0.25f);
	const FLinearColor Wax(0.5f, 0.03f, 0.03f);

	const FLinearColor MarkClue(1.f, 0.85f, 0.45f);
	const FLinearColor MarkLedger(1.f, 0.72f, 0.25f);
	const FLinearColor MarkDanger(1.f, 0.25f, 0.18f);
	const FLinearColor MarkItem(0.9f, 0.8f, 0.6f);
	const FLinearColor MarkPhone(0.35f, 0.6f, 1.f);
	const FLinearColor MarkHidden(0.6f, 0.65f, 0.7f);
}

namespace Slots
{
	// Room: floor spans X -400..400, Y -300..300. Back wall at X=400 (with the window), left wall at Y=-300.
	const FVector Table[] = { {80, 50, 79}, {30, 20, 79}, {100, 15, 79}, {45, 70, 79}, {15, 55, 79}, {110, 70, 79} };
	const FVector Shelf[] = { {-130, -285, 152}, {-70, -285, 152}, {-10, -285, 152}, {50, -285, 152},
		{-130, -285, 207}, {-70, -285, 207}, {-10, -285, 207}, {50, -285, 207} };
	const FVector Floor[] = { {-110, -245, 0}, {-120, 130, 0}, {170, -170, 0}, {250, 190, 0}, {-30, -150, 0}, {230, -40, 0} };

	template <int32 N>
	FVector Pick(const FVector (&List)[N], int32& Index)
	{
		const FVector Base = List[Index % N];
		const int32 Lap = Index / N;
		++Index;
		return Base + FVector(Lap * 12.f, Lap * 9.f, 0.f);
	}
}

AInhRoomBuilder::AInhRoomBuilder()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(Root);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetAtmosphereSunLight(true);
	Sun->SetVolumetricScatteringIntensity(2.f);
	Sun->SetDynamicShadowCascades(3);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(Root);

	// Hollow Bay is a foggy port: thick height fog with volumetric light shafts on capable hardware.
	Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
	Fog->SetupAttachment(Root);
	Fog->SetFogDensity(0.035f);
	Fog->SetFogHeightFalloff(0.35f);
	Fog->SetStartDistance(350.f);
	Fog->SetFogInscatteringColor(FLinearColor(0.22f, 0.28f, 0.36f));
	Fog->SetVolumetricFog(true);
	Fog->SetVolumetricFogScatteringDistribution(0.7f);

	Post = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Post"));
	Post->SetupAttachment(Root);
	Post->bUnbound = true;
	FPostProcessSettings& S = Post->Settings;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = 0.9f;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.55f;
	S.bOverride_FilmGrainIntensity = true;
	S.FilmGrainIntensity = 0.12f;
	S.bOverride_SceneColorTint = true;
	S.SceneColorTint = FLinearColor(0.94f, 0.99f, 1.05f);
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(0.92f, 0.92f, 0.92f, 1.f);
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = 0.4f;

	Lamp = CreateDefaultSubobject<UPointLightComponent>(TEXT("Lamp"));
	Lamp->SetupAttachment(Root);
	Lamp->SetMobility(EComponentMobility::Movable);
	Lamp->SetRelativeLocation(FVector(100.f, 10.f, 112.f));
	Lamp->SetIntensityUnits(ELightUnits::Candelas);
	Lamp->SetIntensity(55.f);
	Lamp->SetAttenuationRadius(1000.f);
	Lamp->SetLightColor(FLinearColor(1.f, 0.68f, 0.42f));
	Lamp->SetSourceRadius(6.f);

	LighthouseBeam = CreateDefaultSubobject<USpotLightComponent>(TEXT("LighthouseBeam"));
	LighthouseBeam->SetupAttachment(Root);
	LighthouseBeam->SetMobility(EComponentMobility::Movable);
	LighthouseBeam->SetRelativeLocation(FVector(1800.f, 700.f, 690.f));
	LighthouseBeam->SetIntensityUnits(ELightUnits::Candelas);
	LighthouseBeam->SetIntensity(6000.f);
	LighthouseBeam->SetAttenuationRadius(6000.f);
	LighthouseBeam->SetOuterConeAngle(7.f);
	LighthouseBeam->SetInnerConeAngle(3.f);
	LighthouseBeam->SetLightColor(FLinearColor(1.f, 0.88f, 0.65f));
	LighthouseBeam->SetVolumetricScatteringIntensity(4.f);
	LighthouseBeam->SetCastShadows(false);
}

void AInhRoomBuilder::BeginPlay()
{
	Super::BeginPlay();
	if (!bShellBuilt)
	{
		BuildShell();
		BuildOutside();
		bShellBuilt = true;
	}
	SetLifeProgress(0.3f);
}

UStaticMeshComponent* AInhRoomBuilder::Shell(UStaticMesh* Mesh, const FVector& Location, const FVector& Size, const FLinearColor& Color,
	const FRotator& Rotation, bool bCollide)
{
	return InhShapes::AddPart(this, Root, Mesh, Location, Size, Color, Rotation, bCollide);
}

void AInhRoomBuilder::BuildShell()
{
	using namespace Palette;
	UStaticMesh* Cube = InhShapes::Cube();
	UStaticMesh* Cyl = InhShapes::Cylinder();

	// Floor and plank seams.
	Shell(Cube, FVector(0, 0, -5), FVector(800, 600, 10), Wood);
	for (int32 i = 1; i < 8; ++i)
	{
		Shell(Cube, FVector(-400 + i * 100, 0, 0.3f), FVector(2, 600, 1), DarkWood, FRotator::ZeroRotator, false);
	}

	// Back wall with a window looking out over the harbour.
	Shell(Cube, FVector(405, -180, 150), FVector(10, 240, 300), Plaster);
	Shell(Cube, FVector(405, 200, 150), FVector(10, 200, 300), Plaster);
	Shell(Cube, FVector(405, 20, 55), FVector(10, 160, 110), Plaster);
	Shell(Cube, FVector(405, 20, 265), FVector(10, 160, 70), Plaster);
	Shell(Cube, FVector(392, 20, 108), FVector(30, 180, 6), DarkWood);
	Shell(Cube, FVector(400, 20, 232), FVector(14, 172, 6), DarkWood);
	Shell(Cube, FVector(400, -62, 170), FVector(14, 6, 124), DarkWood);
	Shell(Cube, FVector(400, 102, 170), FVector(14, 6, 124), DarkWood);
	Shell(Cube, FVector(402, 20, 170), FVector(3, 3, 120), DarkWood, FRotator::ZeroRotator, false);
	Shell(Cube, FVector(402, 20, 172), FVector(3, 160, 3), DarkWood, FRotator::ZeroRotator, false);

	// Left wall, skirting boards and shelves.
	Shell(Cube, FVector(0, -305, 150), FVector(800, 10, 300), PlasterShade);
	Shell(Cube, FVector(398, 0, 8), FVector(4, 600, 16), DarkWood, FRotator::ZeroRotator, false);
	Shell(Cube, FVector(0, -298, 8), FVector(800, 4, 16), DarkWood, FRotator::ZeroRotator, false);
	Shell(Cube, FVector(-40, -285, 148), FVector(230, 30, 4), Wood);
	Shell(Cube, FVector(-40, -285, 203), FVector(230, 30, 4), Wood);

	// Corkboard for clues.
	Shell(Cube, FVector(398, -180, 175), FVector(4, 190, 120), Cork);
	Shell(Cube, FVector(397, -180, 236), FVector(6, 196, 4), DarkWood, FRotator::ZeroRotator, false);
	Shell(Cube, FVector(397, -180, 114), FVector(6, 196, 4), DarkWood, FRotator::ZeroRotator, false);

	// Rug, table, chair.
	Shell(Cube, FVector(50, 40, 1), FVector(280, 190, 2), Rug, FRotator::ZeroRotator, false);
	Shell(Cube, FVector(60, 40, 76), FVector(170, 100, 6), LightWood);
	for (const FVector2D Leg : { FVector2D(-78, -44), FVector2D(78, -44), FVector2D(-78, 44), FVector2D(78, 44) })
	{
		Shell(Cube, FVector(60 + Leg.X, 40 + Leg.Y, 36.5f), FVector(6, 6, 73), Wood);
	}
	Shell(Cube, FVector(-60, 60, 45), FVector(45, 45, 5), Wood);
	Shell(Cube, FVector(-82, 60, 78), FVector(5, 45, 62), Wood);
	for (const FVector2D Leg : { FVector2D(-18, -18), FVector2D(18, -18), FVector2D(-18, 18), FVector2D(18, 18) })
	{
		Shell(Cube, FVector(-60 + Leg.X, 60 + Leg.Y, 21), FVector(4, 4, 42), Wood);
	}

	// Table lamp (the light itself is the Lamp component).
	Shell(Cyl, FVector(100, 10, 81), FVector(14, 14, 4), Brass);
	Shell(Cyl, FVector(100, 10, 98), FVector(3, 3, 32), Brass);
	Shell(Cyl, FVector(100, 10, 120), FVector(30, 30, 18), Paper);

	// Bed against the left wall.
	Shell(Cube, FVector(-240, -225, 20), FVector(220, 120, 40), DarkWood);
	Shell(Cube, FVector(-240, -225, 46), FVector(210, 112, 14), Linen);
	Shell(Cube, FVector(-205, -225, 55), FVector(140, 116, 6), Blanket);
	Shell(Cube, FVector(-325, -225, 58), FVector(35, 72, 10), Linen);
	Shell(Cube, FVector(-352, -225, 60), FVector(8, 120, 80), DarkWood);
}

void AInhRoomBuilder::BuildOutside()
{
	using namespace Palette;
	UStaticMesh* Cube = InhShapes::Cube();
	UStaticMesh* Cyl = InhShapes::Cylinder();

	Shell(Cube, FVector(3000, 0, -80), FVector(5000, 9000, 10), Sea, FRotator::ZeroRotator, false);

	// Rooftops across the harbour, deterministic so the city looks the same every launch.
	FRandomStream Rng(1907);
	for (int32 i = 0; i < 16; ++i)
	{
		const float H = Rng.FRandRange(220.f, 720.f);
		const FVector Loc(Rng.FRandRange(900.f, 2200.f), -1700.f + i * 230.f, H * 0.5f - 80.f);
		Shell(Cube, Loc, FVector(Rng.FRandRange(120.f, 220.f), 180.f, H), Town, FRotator(0, Rng.FRandRange(-8.f, 8.f), 0), false);
		Shell(InhShapes::Cone(), Loc + FVector(0, 0, H * 0.5f + 40.f), FVector(150, 190, 80), Town * 0.8f, FRotator::ZeroRotator, false);
	}

	// The lighthouse where the first Ledger page was found.
	Shell(Cyl, FVector(1800, 700, 260), FVector(90, 90, 680), Paper * 0.45f, FRotator::ZeroRotator, false);
	Shell(Cyl, FVector(1800, 700, 640), FVector(110, 110, 20), Iron, FRotator::ZeroRotator, false);
	Shell(InhShapes::Sphere(), FVector(1800, 700, 690), FVector(60, 60, 60), FLinearColor(1.f, 0.9f, 0.7f), FRotator::ZeroRotator, false);
	Shell(InhShapes::Cone(), FVector(1800, 700, 760), FVector(120, 120, 70), Iron, FRotator::ZeroRotator, false);
}

void AInhRoomBuilder::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;
	LighthouseBeam->SetRelativeRotation(FRotator(-4.f, FMath::Fmod(Time * 25.f, 360.f), 0.f));
	// The table lamp flickers a little, like an old oil lamp.
	const float Flicker = 1.f + 0.04f * FMath::Sin(Time * 13.f) + 0.03f * FMath::Sin(Time * 7.3f);
	Lamp->SetIntensity(55.f * Flicker);
}

void AInhRoomBuilder::SetLifeProgress(float P)
{
	// Keyframes: dusk -> deep night -> dawn (the moment the life ends).
	struct FSkyKey { float At; float Pitch; float Lux; FLinearColor Color; float Sky; };
	static const FSkyKey Keys[] = {
		{ 0.00f, -9.f, 3.5f, FLinearColor(1.f, 0.55f, 0.30f), 0.8f },
		{ 0.20f, -35.f, 0.6f, FLinearColor(0.55f, 0.65f, 1.f), 0.35f },
		{ 0.80f, -40.f, 0.5f, FLinearColor(0.50f, 0.60f, 1.f), 0.30f },
		{ 1.00f, -5.f, 5.0f, FLinearColor(1.f, 0.62f, 0.68f), 1.0f },
	};
	P = FMath::Clamp(P, 0.f, 1.f);
	int32 i = 0;
	while (i < int32(UE_ARRAY_COUNT(Keys)) - 2 && P > Keys[i + 1].At) ++i;
	const FSkyKey& A = Keys[i];
	const FSkyKey& B = Keys[i + 1];
	const float T = FMath::SmoothStep(0.f, 1.f, (P - A.At) / FMath::Max(B.At - A.At, KINDA_SMALL_NUMBER));

	Sun->SetWorldRotation(FRotator(FMath::Lerp(A.Pitch, B.Pitch, T), 195.f, 0.f));
	Sun->SetIntensity(FMath::Lerp(A.Lux, B.Lux, T));
	Sun->SetLightColor(FMath::Lerp(A.Color, B.Color, T));
	SkyLight->SetIntensity(FMath::Lerp(A.Sky, B.Sky, T));
}

void AInhRoomBuilder::ClearProps()
{
	for (AInhProp* Prop : Props)
	{
		if (IsValid(Prop))
		{
			Prop->Destroy();
		}
	}
	Props.Reset();
}

AInhProp* AInhRoomBuilder::SpawnProp(EInhPropKind Kind, const FString& RefId, const FVector& Location, const FLinearColor& MarkerColor)
{
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AInhProp* Prop = GetWorld()->SpawnActor<AInhProp>(AInhProp::StaticClass(), FTransform(Location), Params);
	Prop->Setup(Kind, RefId, MarkerColor);
	Props.Add(Prop);
	return Prop;
}

void AInhRoomBuilder::RevealHiddenItem(const FString& ItemId)
{
	RevealedItems.Add(ItemId);
	ShowLife(ShownLife);
}

void AInhRoomBuilder::ShowLife(const FInhLife& Life)
{
	using namespace Palette;
	if (Life.Id != ShownLife.Id)
	{
		RevealedItems.Reset();
	}
	ShownLife = Life;
	ClearProps();
	if (!Life.IsValid())
	{
		return;
	}

	UStaticMesh* Cube = InhShapes::Cube();
	UStaticMesh* Cyl = InhShapes::Cylinder();
	UStaticMesh* Sphere = InhShapes::Sphere();

	// The phone with your predecessor's voice note.
	AInhProp* Phone = SpawnProp(EInhPropKind::Phone, FString(), FVector(20, 75, 79), MarkPhone);
	Phone->AddPart(Cube, FVector(0, 0, 0.8f), FVector(8, 15, 1.6f), Iron, FRotator(0, 20, 0));
	Phone->AddPart(Cube, FVector(0, 0, 1.7f), FVector(7, 13, 0.2f), FLinearColor(0.15f, 0.4f, 0.8f), FRotator(0, 20, 0));

	// A spyglass on the sill: look out at the city and the season.
	AInhProp* Window = SpawnProp(EInhPropKind::Window, FString(), FVector(390, 40, 114), MarkItem);
	Window->AddPart(Cyl, FVector(0, 0, 4), FVector(6, 6, 34), Brass, FRotator(0, 0, 90));

	int32 TableSlot = 0;
	int32 ShelfSlot = 0;
	int32 FloorSlot = 0;
	for (const FInhItem& Item : Life.Estate.Items)
	{
		SpawnItem(Item, TableSlot, ShelfSlot, FloorSlot);
	}

	// Demands from creditors pinned to the wall.
	for (int32 i = 0; i < Life.Estate.Debts.Num(); ++i)
	{
		AInhProp* Demand = SpawnProp(EInhPropKind::Debt, Life.Estate.Debts[i].Id, FVector(150 - i * 70, -297, 165), MarkDanger);
		Demand->AddPart(Cube, FVector(0, 0, 0), FVector(42, 1.5f, 54), Notice, FRotator(0, 0, FMath::RandRange(-4, 4)));
		Demand->AddPart(Sphere, FVector(0, 2, 22), FVector(3, 3, 3), Brass);
	}

	// Portraits of rivals, crossed out.
	for (int32 i = 0; i < Life.Estate.Feuds.Num(); ++i)
	{
		const FInhFeud& Feud = Life.Estate.Feuds[i];
		const FLinearColor Cross = Feud.IsBlood() ? FLinearColor(0.35f, 0.f, 0.f) : FLinearColor(0.7f, 0.08f, 0.05f);
		AInhProp* Portrait = SpawnProp(EInhPropKind::Feud, Feud.Id, FVector(397, 140 + (i % 3) * 55, 160 + (i / 3) * 65), MarkDanger);
		Portrait->AddPart(Cube, FVector(0, 0, 0), FVector(3, 42, 52), DarkWood);
		Portrait->AddPart(Cube, FVector(-1.6f, 0, 0), FVector(1, 34, 44), FLinearColor(0.32f, 0.27f, 0.22f));
		Portrait->AddPart(Cube, FVector(-2.4f, 0, 0), FVector(0.5f, 4, 52), Cross, FRotator(0, 0, 38));
		Portrait->AddPart(Cube, FVector(-2.4f, 0, 0), FVector(0.5f, 4, 52), Cross, FRotator(0, 0, -38));
	}

	// Open clues pinned to the corkboard.
	int32 ClueIndex = 0;
	for (const FInhClue& Clue : Life.Estate.Clues)
	{
		if (Clue.bResolved)
		{
			continue;
		}
		const FVector Loc(395, -250 + (ClueIndex % 4) * 46, 150 + (ClueIndex / 4 % 2) * 50 + (ClueIndex % 2) * 10);
		AInhProp* Card = SpawnProp(EInhPropKind::Clue, Clue.Id, Loc, MarkClue);
		Card->AddPart(Cube, FVector(0, 0, 0), FVector(1, 34, 26), Paper, FRotator(0, 0, FMath::RandRange(-6, 6)));
		Card->AddPart(Sphere, FVector(-1, 0, 10), FVector(3, 3, 3), FLinearColor(0.7f, 0.1f, 0.1f));
		++ClueIndex;
	}
}

void AInhRoomBuilder::SpawnItem(const FInhItem& Item, int32& TableSlot, int32& ShelfSlot, int32& FloorSlot)
{
	using namespace Palette;
	UStaticMesh* Cube = InhShapes::Cube();
	UStaticMesh* Cyl = InhShapes::Cylinder();
	UStaticMesh* Sphere = InhShapes::Sphere();

	if (Item.bHidden && !RevealedItems.Contains(Item.Id))
	{
		AInhProp* Board = SpawnProp(EInhPropKind::HiddenItem, Item.Id, Slots::Pick(Slots::Floor, FloorSlot), MarkHidden);
		Board->AddPart(Cube, FVector(0, 0, 0.8f), FVector(70, 18, 1.6f), LightWood, FRotator(0, 8, 0));
		return;
	}

	const uint32 Hash = GetTypeHash(Item.Id);
	if (Item.IsLedgerPage())
	{
		AInhProp* Page = SpawnProp(EInhPropKind::Item, Item.Id, Slots::Pick(Slots::Table, TableSlot), MarkLedger);
		Page->AddPart(Cube, FVector(0, 0, 0.4f), FVector(22, 30, 0.6f), Paper * 1.1f, FRotator(0, float(Hash % 40) - 20.f, 0));
		Page->AddPart(Cube, FVector(0, 0, 0.8f), FVector(16, 1, 0.2f), FLinearColor(0.2f, 0.1f, 0.05f), FRotator(0, float(Hash % 40) - 20.f, 0));
	}
	else if (Item.Kind == TEXT("letter"))
	{
		AInhProp* Letter = SpawnProp(EInhPropKind::Item, Item.Id, Slots::Pick(Slots::Table, TableSlot), MarkItem);
		Letter->AddPart(Cube, FVector(0, 0, 0.5f), FVector(16, 24, 0.8f), Paper * 1.15f, FRotator(0, -15, 0));
		Letter->AddPart(Cyl, FVector(0, 0, 1.1f), FVector(4, 4, 0.6f), Wax);
	}
	else if (Item.Kind == TEXT("box"))
	{
		AInhProp* Box = SpawnProp(EInhPropKind::Item, Item.Id, Slots::Pick(Slots::Floor, FloorSlot), MarkItem);
		Box->AddPart(Cube, FVector(0, 0, 12), FVector(42, 30, 24), Iron);
		Box->AddPart(Cube, FVector(0, 0, 18), FVector(43, 31, 3), Brass * 0.6f);
		for (int32 k = 0; k < 5; ++k)
		{
			const FLinearColor KeyColor = k < Item.Streak ? Brass : Iron * 2.f;
			Box->AddPart(Sphere, FVector(-15.5f, -12 + k * 6, 10), FVector(2.5f, 2.5f, 2.5f), KeyColor);
		}
	}
	else if (Item.Kind == TEXT("property"))
	{
		AInhProp* Deed = SpawnProp(EInhPropKind::Item, Item.Id, Slots::Pick(Slots::Shelf, ShelfSlot), MarkItem);
		Deed->AddPart(Cyl, FVector(0, 0, 3), FVector(6, 6, 26), Paper, FRotator(0, 0, 90));
		Deed->AddPart(Cyl, FVector(0, 0, 3), FVector(6.5f, 6.5f, 3), FLinearColor(0.5f, 0.05f, 0.05f), FRotator(0, 0, 90));
	}
	else
	{
		AInhProp* Trinket = SpawnProp(EInhPropKind::Item, Item.Id, Slots::Pick(Slots::Shelf, ShelfSlot), MarkItem);
		switch (Hash % 3)
		{
		case 0:
			Trinket->AddPart(Sphere, FVector(0, 0, 6), FVector(12, 12, 12), Brass);
			break;
		case 1:
			Trinket->AddPart(Cyl, FVector(0, 0, 10), FVector(14, 14, 20), SeaGlass);
			Trinket->AddPart(Cyl, FVector(0, 0, 21), FVector(10, 10, 3), Wood);
			break;
		default:
			Trinket->AddPart(Cube, FVector(0, 0, 5), FVector(18, 12, 10), Wood);
			Trinket->AddPart(Sphere, FVector(0, 0, 11), FVector(4, 4, 4), Brass);
			break;
		}
	}
}
