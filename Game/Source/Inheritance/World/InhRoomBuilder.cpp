#include "World/InhRoomBuilder.h"
#include "World/InhArt.h"
#include "World/InhAtmosphereFX.h"
#include "World/InhHarbourBuilder.h"
#include "World/InhLayout.h"
#include "World/InhShapes.h"
#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "EngineUtils.h"

// The room is a cutaway diorama: walls on two sides, open at the front (-X), the right (+Y) and the top, standing on a
// floor slab drawn like an architect's section (dark where it is cut), so the fish market below reads as the storey
// underneath. Pieces that share a look share one instanced component, which keeps the room to a couple of dozen draw
// calls on a phone. Every art lookup falls back to the engine's basic shapes with flat colours.

// A named namespace: unity builds put several .cpp files into one translation unit.
namespace InhRoomParts
{
	/** Flat looks: used when the art isn't installed, and for things that have no art (cork, ink, wax seals...). */
	namespace Palette
	{
		const FLinearColor Wood(0.10f, 0.055f, 0.03f);
		const FLinearColor LightWood(0.16f, 0.10f, 0.055f);
		const FLinearColor DarkWood(0.04f, 0.022f, 0.012f);
		const FLinearColor Plaster(0.20f, 0.23f, 0.24f);
		const FLinearColor PlasterShade(0.15f, 0.18f, 0.20f);
		/** The cut through the floor and walls, like the poché of a drawn section. */
		const FLinearColor Section(0.016f, 0.013f, 0.011f);
		const FLinearColor Cork(0.33f, 0.20f, 0.09f);
		const FLinearColor Rug(0.24f, 0.035f, 0.03f);
		const FLinearColor Brass(0.75f, 0.50f, 0.18f);
		const FLinearColor Paper(0.85f, 0.80f, 0.66f);
		const FLinearColor Iron(0.07f, 0.07f, 0.08f);
		const FLinearColor Blanket(0.10f, 0.18f, 0.26f);
		const FLinearColor Linen(0.70f, 0.68f, 0.62f);
		const FLinearColor Wax(0.78f, 0.72f, 0.60f);
		const FLinearColor Notice(0.55f, 0.06f, 0.05f);
		const FLinearColor SeaGlass(0.10f, 0.35f, 0.25f);
		const FLinearColor Seal(0.5f, 0.03f, 0.03f);
		const FLinearColor Glass(0.80f, 0.90f, 0.95f);

		const FLinearColor LampGlow(1.f, 0.70f, 0.42f);
		const FLinearColor Flame(1.f, 0.58f, 0.22f);
		const FLinearColor Screen(0.15f, 0.4f, 0.8f);

		const FLinearColor MarkClue(1.f, 0.85f, 0.45f);
		const FLinearColor MarkLedger(1.f, 0.72f, 0.25f);
		const FLinearColor MarkDanger(1.f, 0.25f, 0.18f);
		const FLinearColor MarkItem(0.9f, 0.8f, 0.6f);
		const FLinearColor MarkPhone(0.35f, 0.6f, 1.f);
		const FLinearColor MarkHidden(0.6f, 0.65f, 0.7f);
	}

	/** Art keys from Content/Data/Art/ArtManifest.json. */
	namespace ArtKey
	{
		const TCHAR* const FloorWood = TEXT("FloorWood");
		const TCHAR* const Plaster = TEXT("Plaster");
		const TCHAR* const PlasterDark = TEXT("PlasterDark");
		const TCHAR* const Wood = TEXT("Wood");
		const TCHAR* const WoodDark = TEXT("WoodDark");
		const TCHAR* const Linen = TEXT("Linen");
		const TCHAR* const Fabric = TEXT("Fabric");
		const TCHAR* const Paper = TEXT("Paper");
		const TCHAR* const Brass = TEXT("Brass");
		const TCHAR* const Iron = TEXT("Iron");
		const TCHAR* const Glass = TEXT("Glass");

		const TCHAR* const Table = TEXT("Table");
		const TCHAR* const Chair = TEXT("Chair");
		const TCHAR* const Bed = TEXT("Bed");
		const TCHAR* const Shelf = TEXT("Shelf");
		const TCHAR* const Lantern = TEXT("Lantern");
		const TCHAR* const Candle = TEXT("Candle");
		const TCHAR* const Books = TEXT("Books");
		const TCHAR* const Bottle = TEXT("Bottle");
		const TCHAR* const Vase = TEXT("Vase");
		const TCHAR* const Chest = TEXT("Chest");
	}

	/** Props on the table stand on this height (the phone, the table slots, the lamp). */
	constexpr float TableTopZ = 79.f;
	constexpr float LampCandela = 55.f;
	constexpr float LampShadeStrength = 3.f;
	constexpr float FlameStrength = 12.f;
	constexpr float ScreenStrength = 1.5f;

	namespace Slots
	{
		// Room: floor spans X -400..400, Y -300..300. Back wall at X=400 (with the window), left wall at Y=-300.
		const FVector Table[] = { {80, 50, 79}, {30, 20, 79}, {100, 15, 79}, {45, 70, 79}, {15, 55, 79}, {110, 70, 79} };
		// On the two shelf boards (tops at Z 150 and 205), clear of the books at the right-hand end.
		const FVector Shelf[] = { {-130, -285, 150}, {-70, -285, 150}, {-10, -285, 150}, {50, -285, 150},
			{-130, -285, 205}, {-70, -285, 205}, {-10, -285, 205}, {50, -285, 205} };
		// On the floor, clear of the bed, the table and the cupboard.
		const FVector Floor[] = { {-85, -245, 0}, {-120, 130, 0}, {170, -170, 0}, {250, 190, 0}, {-30, -150, 0}, {230, -40, 0} };

		template <int32 N>
		FVector Pick(const FVector (&List)[N], int32& Index)
		{
			const FVector Base = List[Index % N];
			const int32 Lap = Index / N;
			++Index;
			return Base + FVector(Lap * 12.f, Lap * 9.f, 0.f);
		}
	}

	/** One instanced component per mesh and look, made on first use: every piece with the same look is one draw call. */
	struct FInhRoomKits
	{
		FInhRoomKits(AActor* InOwner, USceneComponent* InParent)
			: Owner(InOwner), Parent(InParent)
		{
		}

		/** Size is in centimetres like InhShapes::AddPart. Key may be nullptr for a flat colour only. */
		void Add(UStaticMesh* Mesh, const TCHAR* Key, const FLinearColor& Fallback, const FVector& Center, const FVector& Size,
			const FRotator& Rotation = FRotator::ZeroRotator, bool bCollide = true)
		{
			const FName KeyName = Key ? FName(Key) : FName();
			// With art every piece of one key shares the material; without it each fallback colour needs its own set.
			const bool bArt = !KeyName.IsNone() && InhArt::Material(KeyName) != nullptr;
			const FString Id = FString::Printf(TEXT("%s|%s|%s|%d"), *GetNameSafe(Mesh), Key ? Key : TEXT(""),
				bArt ? TEXT("") : *Fallback.ToString(), bCollide ? 1 : 0);
			UInstancedStaticMeshComponent*& Set = Sets.FindOrAdd(Id);
			if (!Set)
			{
				Set = InhShapes::AddInstanced(Owner, Parent, Mesh, KeyName, Fallback, bCollide);
			}
			if (Set)
			{
				Set->AddInstance(FTransform(Rotation, Center, Size / 100.0));
			}
		}

		AActor* Owner = nullptr;
		USceneComponent* Parent = nullptr;
		TMap<FString, UInstancedStaticMeshComponent*> Sets;
	};

	enum class EFit : uint8
	{
		/** As large as fits inside the box. */
		Inside,
		/** Exactly as tall as the box (a table top props stand on), provided the footprint still fits. */
		Height,
	};

	/**
	 * An imported mesh standing in for a piece of furniture, its bottom centre at BottomCenter. Scans face any way, so it
	 * is turned to lay its long side along the box's. Returns nullptr when the art isn't installed or would be a poor
	 * stand-in (larger than its place, or a footprint under MinFootprint, where props would float off it): the caller
	 * then builds the primitive version. OutSize receives the fitted size in the box's axes.
	 */
	UStaticMeshComponent* AddFurniture(AActor* Owner, USceneComponent* Parent, const TCHAR* Key, const FVector& BottomCenter,
		const FVector& Box, const FVector2D& MinFootprint, EFit Fit, bool bCollide, FVector* OutSize = nullptr)
	{
		const FName KeyName(Key);
		UStaticMesh* Mesh = InhArt::Mesh(KeyName);
		if (!Mesh)
		{
			return nullptr;
		}
		const FVector Size = Mesh->GetBounds().BoxExtent * 2.0;
		if (Size.X < 0.1 || Size.Y < 0.1 || Size.Z < 0.1)
		{
			return nullptr;
		}
		const bool bTurn = (Box.X > Box.Y * 1.1 && Size.Y > Size.X * 1.1) || (Box.Y > Box.X * 1.1 && Size.X > Size.Y * 1.1);
		const double AlongX = bTurn ? Size.Y : Size.X;
		const double AlongY = bTurn ? Size.X : Size.Y;
		const double Scale = Fit == EFit::Height ? Box.Z / Size.Z : FMath::Min3(Box.X / AlongX, Box.Y / AlongY, Box.Z / Size.Z);
		const FVector Fitted(AlongX * Scale, AlongY * Scale, Size.Z * Scale);
		if (Fitted.X > Box.X * 1.02 || Fitted.Y > Box.Y * 1.02 || Fitted.X < MinFootprint.X || Fitted.Y < MinFootprint.Y)
		{
			return nullptr;
		}
		// AddArtMesh scales uniformly to fit; a box of exactly the scaled size, a hair wider, lets the height decide.
		const FVector FitBox(Size.X * Scale * 1.01, Size.Y * Scale * 1.01, Size.Z * Scale);
		UStaticMeshComponent* Part = InhShapes::AddArtMesh(Owner, Parent, KeyName, BottomCenter, FitBox,
			FRotator(0.f, bTurn ? 90.f : 0.f, 0.f), bCollide);
		if (Part && OutSize)
		{
			*OutSize = Fitted;
		}
		return Part;
	}

	/** An invisible box that catches taps, for a prop whose imported mesh may have been built without collision. */
	void AddHitBox(AActor* Owner, USceneComponent* Parent, const FVector& Center, const FVector& Size)
	{
		UBoxComponent* Box = NewObject<UBoxComponent>(Owner);
		Box->SetupAttachment(Parent);
		Box->SetMobility(EComponentMobility::Movable);
		Box->SetRelativeLocation(Center);
		Box->SetBoxExtent(Size * 0.5, false);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->SetGenerateOverlapEvents(false);
		Box->RegisterComponent();
		Owner->AddInstanceComponent(Box);
	}

	/** A prop part in an art surface (collides, so it can be tapped). */
	UStaticMeshComponent* PropSurface(AInhProp* Prop, UStaticMesh* Mesh, const FVector& Location, const FVector& Size, const TCHAR* Key,
		const FLinearColor& Fallback, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		return InhShapes::AddSurface(Prop, Prop->GetRootComponent(), Mesh, Location, Size, FName(Key), Fallback, Rotation, true);
	}

	/** A prop drawn by an imported mesh, standing at the prop's origin; nullptr leaves the caller to build primitives. */
	UStaticMeshComponent* PropArt(AInhProp* Prop, const TCHAR* Key, const FVector& Box, const FVector2D& MinFootprint, FVector* OutSize = nullptr)
	{
		FVector Size = FVector::ZeroVector;
		UStaticMeshComponent* Part = AddFurniture(Prop, Prop->GetRootComponent(), Key, FVector::ZeroVector, Box, MinFootprint, EFit::Inside, true, &Size);
		if (Part)
		{
			AddHitBox(Prop, Prop->GetRootComponent(), FVector(0.0, 0.0, Size.Z * 0.5), Size);
			if (OutSize)
			{
				*OutSize = Size;
			}
		}
		return Part;
	}

	/** Floor, walls, window and the fixed fittings. */
	void BuildStructure(FInhRoomKits& Kits)
	{
		using namespace Palette;
		UStaticMesh* Cube = InhShapes::Cube();
		const FRotator Level = FRotator::ZeroRotator;

		// The floor: boards over a deep slab, which shows dark along the open front and right edges where the cut runs.
		Kits.Add(Cube, ArtKey::FloorWood, Wood, FVector(0.f, 0.f, -1.5f), FVector(800.f, 600.f, 3.f));
		Kits.Add(Cube, nullptr, Section, FVector(3.25f, 0.5f, -17.5f), FVector(812.5f, 625.f, 33.f), Level, false);
		if (!InhArt::Material(FName(ArtKey::FloorWood)))
		{
			// Flat boards need their seams drawn; the photographed floor has its own.
			for (int32 i = 1; i < 8; ++i)
			{
				Kits.Add(Cube, nullptr, DarkWood, FVector(-400.f + i * 100.f, 0.f, 0.3f), FVector(2.f, 600.f, 1.f), Level, false);
			}
		}

		// Back wall (X 400..410) around the window. Its open end runs on to Y 310 to meet the facade and cornice outside.
		Kits.Add(Cube, ArtKey::Plaster, Plaster, FVector(405.f, -180.f, 150.f), FVector(10.f, 240.f, 300.f), Level, false);
		Kits.Add(Cube, ArtKey::Plaster, Plaster, FVector(405.f, 205.f, 149.5f), FVector(10.f, 210.f, 301.f), Level, false);
		Kits.Add(Cube, ArtKey::Plaster, Plaster, FVector(405.f, 20.f, 55.f), FVector(10.f, 160.f, 110.f), Level, false);
		Kits.Add(Cube, ArtKey::Plaster, Plaster, FVector(405.f, 20.f, 265.f), FVector(10.f, 160.f, 70.f), Level, false);
		// Left wall (Y -310..-300), on to the back wall's outer face so the corner is closed.
		Kits.Add(Cube, ArtKey::PlasterDark, PlasterShade, FVector(5.f, -305.f, 150.f), FVector(810.f, 10.f, 300.f), Level, false);

		// Where the cut runs through the walls: dark caps along their tops and down their open ends.
		Kits.Add(Cube, nullptr, Section, FVector(405.f, 5.5f, 300.75f), FVector(11.f, 610.f, 1.5f), Level, false);
		Kits.Add(Cube, nullptr, Section, FVector(5.25f, -305.f, 300.75f), FVector(810.5f, 11.f, 1.5f), Level, false);
		Kits.Add(Cube, nullptr, Section, FVector(-401.5f, -305.f, 150.25f), FVector(3.f, 11.f, 302.5f), Level, false);
		Kits.Add(Cube, nullptr, Section, FVector(405.f, 311.5f, 150.25f), FVector(11.f, 3.f, 302.5f), Level, false);

		// Skirting boards.
		Kits.Add(Cube, ArtKey::WoodDark, DarkWood, FVector(398.f, 0.f, 8.f), FVector(4.f, 600.f, 16.f), Level, false);
		Kits.Add(Cube, ArtKey::WoodDark, DarkWood, FVector(0.f, -298.f, 8.f), FVector(800.f, 4.f, 16.f), Level, false);

		// The window frame and its cross. The cinematic camera flies through the outer upper pane (between the cross and
		// the frame, around Y 60, Z 200), so the cross stays at the centre and that pane stands open like a casement.
		Kits.Add(Cube, ArtKey::WoodDark, DarkWood, FVector(392.f, 20.f, 108.f), FVector(30.f, 180.f, 6.f), Level, false);
		Kits.Add(Cube, ArtKey::WoodDark, DarkWood, FVector(400.f, 20.f, 232.f), FVector(14.f, 172.f, 6.f), Level, false);
		Kits.Add(Cube, ArtKey::WoodDark, DarkWood, FVector(400.f, -62.f, 170.f), FVector(14.f, 6.f, 124.f), Level, false);
		Kits.Add(Cube, ArtKey::WoodDark, DarkWood, FVector(400.f, 102.f, 170.f), FVector(14.f, 6.f, 124.f), Level, false);
		Kits.Add(Cube, ArtKey::WoodDark, DarkWood, FVector(402.f, 20.f, 170.f), FVector(3.f, 3.f, 120.f), Level, false);
		Kits.Add(Cube, ArtKey::WoodDark, DarkWood, FVector(402.f, 20.f, 172.f), FVector(3.f, 160.f, 3.f), Level, false);
		if (InhArt::Material(FName(ArtKey::Glass)))
		{
			// Glass only when the glass material is installed: a flat stand-in would be an opaque sheet over the view.
			// The open pane stays empty: glass there would veil the whole frame as the camera neared it, then pop off
			// as it crossed. Centre and size of the glazed ones: the lower half, and the upper half left of the cross.
			// Glass never shadows, or the low sun couldn't pour in.
			const FVector Panes[][2] = {
				{ FVector(404.f, 20.f, 140.75f), FVector(0.6f, 158.f, 59.5f) },
				{ FVector(404.f, -20.25f, 201.25f), FVector(0.6f, 77.5f, 55.5f) },
			};
			for (const auto& Pane : Panes)
			{
				if (UStaticMeshComponent* Sheet = InhShapes::AddSurface(Kits.Owner, Kits.Parent, Cube, Pane[0], Pane[1],
					FName(ArtKey::Glass), Glass, Level, false))
				{
					Sheet->SetCastShadow(false);
				}
			}
		}

		// Corkboard for clues, framed.
		Kits.Add(Cube, nullptr, Cork, FVector(398.f, -180.f, 175.f), FVector(4.f, 190.f, 120.f), Level, false);
		Kits.Add(Cube, ArtKey::WoodDark, DarkWood, FVector(397.f, -180.f, 236.f), FVector(6.f, 196.f, 4.f), Level, false);
		Kits.Add(Cube, ArtKey::WoodDark, DarkWood, FVector(397.f, -180.f, 114.f), FVector(6.f, 196.f, 4.f), Level, false);
		Kits.Add(Cube, ArtKey::WoodDark, DarkWood, FVector(397.f, -277.f, 175.f), FVector(6.f, 4.f, 118.f), Level, false);
		Kits.Add(Cube, ArtKey::WoodDark, DarkWood, FVector(397.f, -83.f, 175.f), FVector(6.f, 4.f, 118.f), Level, false);

		// Two shelves on iron brackets along the left wall.
		for (const float Z : { 148.f, 203.f })
		{
			Kits.Add(Cube, ArtKey::Wood, Wood, FVector(-25.f, -285.f, Z), FVector(260.f, 30.f, 4.f));
			for (const float X : { -140.f, -25.f, 90.f })
			{
				Kits.Add(Cube, ArtKey::Iron, Iron, FVector(X, -296.f, Z - 9.f), FVector(2.f, 6.f, 14.f), Level, false);
				Kits.Add(Cube, ArtKey::Iron, Iron, FVector(X, -288.f, Z - 3.f), FVector(2.f, 22.f, 2.f), Level, false);
			}
		}

		// The rug under the table.
		Kits.Add(Cube, ArtKey::Fabric, Rug, FVector(50.f, 40.f, 1.f), FVector(280.f, 190.f, 2.f), Level, false);
	}

	/** The furniture: the imported piece where it is installed and a good fit, the primitive version otherwise. Returns the lamp light's height. */
	float BuildFurniture(FInhRoomKits& Kits)
	{
		using namespace Palette;
		AActor* Owner = Kits.Owner;
		USceneComponent* Parent = Kits.Parent;
		UStaticMesh* Cube = InhShapes::Cube();
		UStaticMesh* Cyl = InhShapes::Cylinder();
		UStaticMesh* Sphere = InhShapes::Sphere();
		const FRotator Level = FRotator::ZeroRotator;

		// The table: its top must sit at TableTopZ and reach the table slots, the phone, the lamp and the candle.
		if (!AddFurniture(Owner, Parent, ArtKey::Table, FVector(60.f, 40.f, 0.f), FVector(190.f, 115.f, TableTopZ),
			FVector2D(135.f, 92.f), EFit::Height, true))
		{
			Kits.Add(Cube, ArtKey::Wood, LightWood, FVector(60.f, 40.f, 76.f), FVector(170.f, 100.f, 6.f));
			for (const FVector2D Leg : { FVector2D(-78, -44), FVector2D(78, -44), FVector2D(-78, 44), FVector2D(78, 44) })
			{
				Kits.Add(Cube, ArtKey::Wood, Wood, FVector(60.f + Leg.X, 40.f + Leg.Y, 36.5f), FVector(6.f, 6.f, 73.f));
			}
		}

		// The chair, kept below the WillReveal dolly that passes behind it.
		if (!AddFurniture(Owner, Parent, ArtKey::Chair, FVector(-60.f, 60.f, 0.f), FVector(50.f, 50.f, 100.f),
			FVector2D(32.f, 32.f), EFit::Inside, true))
		{
			Kits.Add(Cube, ArtKey::Wood, Wood, FVector(-60.f, 60.f, 45.f), FVector(45.f, 45.f, 5.f));
			Kits.Add(Cube, ArtKey::Wood, Wood, FVector(-82.f, 60.f, 78.f), FVector(5.f, 45.f, 62.f));
			for (const FVector2D Leg : { FVector2D(-18, -18), FVector2D(18, -18), FVector2D(-18, 18), FVector2D(18, 18) })
			{
				Kits.Add(Cube, ArtKey::Wood, Wood, FVector(-60.f + Leg.X, 60.f + Leg.Y, 21.f), FVector(4.f, 4.f, 42.f));
			}
		}

		// The table lamp; its light is the Lamp component. Nothing that wraps the bulb may cast a shadow, or it would
		// black out the room.
		float LampZ = 114.f;
		FVector LanternSize = FVector::ZeroVector;
		if (UStaticMeshComponent* Lantern = AddFurniture(Owner, Parent, ArtKey::Lantern, FVector(100.f, 10.f, TableTopZ),
			FVector(28.f, 28.f, 44.f), FVector2D(10.f, 10.f), EFit::Inside, false, &LanternSize))
		{
			Lantern->SetCastShadow(false);
			LampZ = TableTopZ + float(LanternSize.Z) * 0.42f;
			InhShapes::AddEmissive(Owner, Parent, Sphere, FVector(100.f, 10.f, LampZ), FVector(3.f, 3.f, 5.f), Flame, FlameStrength);
		}
		else
		{
			Kits.Add(Cyl, ArtKey::Brass, Brass, FVector(100.f, 10.f, 81.f), FVector(14.f, 14.f, 4.f), Level, false);
			Kits.Add(Cyl, ArtKey::Brass, Brass, FVector(100.f, 10.f, 95.f), FVector(3.f, 3.f, 26.f), Level, false);
			// A glowing paper shade around the bulb.
			InhShapes::AddEmissive(Owner, Parent, Cyl, FVector(100.f, 10.f, 120.f), FVector(30.f, 30.f, 18.f), LampGlow, LampShadeStrength);
		}

		// A candle at the far end of the table.
		const FVector CandleBase(122.f, 38.f, TableTopZ);
		float WickZ = TableTopZ + 16.f;
		FVector CandleSize = FVector::ZeroVector;
		if (AddFurniture(Owner, Parent, ArtKey::Candle, CandleBase, FVector(10.f, 10.f, 24.f), FVector2D(3.f, 3.f), EFit::Inside, false, &CandleSize))
		{
			WickZ = TableTopZ + float(CandleSize.Z);
		}
		else
		{
			Kits.Add(Cyl, nullptr, Wax, CandleBase + FVector(0.f, 0.f, 8.f), FVector(4.5f, 4.5f, 16.f), Level, false);
		}
		InhShapes::AddEmissive(Owner, Parent, Sphere, FVector(CandleBase.X, CandleBase.Y, WickZ + 1.8f), FVector(1.4f, 1.4f, 3.2f), Flame, FlameStrength);

		// The bed against the left wall.
		if (!AddFurniture(Owner, Parent, ArtKey::Bed, FVector(-240.f, -225.f, 0.f), FVector(230.f, 130.f, 120.f),
			FVector2D(170.f, 80.f), EFit::Inside, true))
		{
			Kits.Add(Cube, ArtKey::WoodDark, DarkWood, FVector(-240.f, -225.f, 20.f), FVector(220.f, 120.f, 40.f));
			Kits.Add(Cube, ArtKey::Linen, Linen, FVector(-240.f, -225.f, 46.f), FVector(210.f, 112.f, 14.f));
			Kits.Add(Cube, ArtKey::Fabric, Blanket, FVector(-205.f, -225.f, 55.f), FVector(140.f, 116.f, 6.f));
			Kits.Add(Cube, ArtKey::Linen, Linen, FVector(-325.f, -225.f, 58.f), FVector(35.f, 72.f, 10.f));
			Kits.Add(Cube, ArtKey::WoodDark, DarkWood, FVector(-352.f, -225.f, 60.f), FVector(8.f, 120.f, 80.f));
		}

		// Books at the right-hand end of the lower shelf, clear of the shelf slots.
		const FVector BooksBase(86.f, -285.f, 150.f);
		if (!AddFurniture(Owner, Parent, ArtKey::Books, BooksBase, FVector(32.f, 24.f, 30.f), FVector2D(10.f, 8.f), EFit::Inside, false))
		{
			const FLinearColor Spines[] = { FLinearColor(0.30f, 0.05f, 0.04f), FLinearColor(0.05f, 0.10f, 0.16f), FLinearColor(0.20f, 0.14f, 0.06f) };
			for (int32 i = 0; i < 3; ++i)
			{
				const float Height = 26.f - i * 2.f;
				Kits.Add(Cube, nullptr, Spines[i], BooksBase + FVector(-6.f + i * 6.f, 0.f, Height * 0.5f), FVector(5.f, 18.f, Height), Level, false);
			}
		}

		// A low cupboard against the back wall, under the portraits.
		if (!AddFurniture(Owner, Parent, ArtKey::Shelf, FVector(368.f, 215.f, 0.f), FVector(48.f, 120.f, 125.f),
			FVector2D(28.f, 70.f), EFit::Inside, true))
		{
			Kits.Add(Cube, ArtKey::WoodDark, DarkWood, FVector(370.f, 215.f, 44.f), FVector(42.f, 114.f, 88.f));
			Kits.Add(Cube, ArtKey::Wood, LightWood, FVector(369.f, 215.f, 90.f), FVector(48.f, 120.f, 4.f));
		}
		return LampZ;
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
	// God rays around the low sun (and the moon), streaming in through the window when the camera looks out.
	Sun->bEnableLightShaftBloom = true;
	Sun->BloomScale = 0.15f;

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

	// The film grade: a filmic curve with a soft toe and shoulder, teal shadows and warm highlights, gentle bloom and
	// lens flares, a vignette, a trace of chromatic aberration at the frame edges, and fine grain.
	Post = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Post"));
	Post->SetupAttachment(Root);
	Post->bUnbound = true;
	FPostProcessSettings& S = Post->Settings;
	S.bOverride_FilmSlope = true;
	S.FilmSlope = 0.86f;
	S.bOverride_FilmToe = true;
	S.FilmToe = 0.58f;
	S.bOverride_FilmShoulder = true;
	S.FilmShoulder = 0.24f;
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(0.92f, 0.92f, 0.92f, 1.f);
	S.bOverride_ColorContrast = true;
	S.ColorContrast = FVector4(1.04f, 1.04f, 1.04f, 1.f);
	S.bOverride_ColorGainShadows = true;
	S.ColorGainShadows = FVector4(0.93f, 1.f, 1.07f, 1.f);
	S.bOverride_ColorGainHighlights = true;
	S.ColorGainHighlights = FVector4(1.06f, 1.01f, 0.93f, 1.f);
	S.bOverride_SceneColorTint = true;
	S.SceneColorTint = FLinearColor(0.94f, 0.99f, 1.05f);
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = 0.85f;
	S.bOverride_LensFlareIntensity = true;
	S.LensFlareIntensity = 0.35f;
	S.bOverride_LensFlareThreshold = true;
	S.LensFlareThreshold = 4.f;
	S.bOverride_LensFlareBokehSize = true;
	S.LensFlareBokehSize = 2.5f;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.55f;
	S.bOverride_SceneFringeIntensity = true;
	S.SceneFringeIntensity = 0.6f;
	S.bOverride_ChromaticAberrationStartOffset = true;
	S.ChromaticAberrationStartOffset = 0.45f;
	S.bOverride_FilmGrainIntensity = true;
	S.FilmGrainIntensity = 0.1f;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = 0.4f;
	// A floor under the auto-exposure (EV100: the project extends the luminance range). Without it a moonlit harbour is
	// lifted to a grey dusk and every lit window blows out into flares; the lamp-lit room sits above it, untouched.
	S.bOverride_AutoExposureMinBrightness = true;
	S.AutoExposureMinBrightness = -1.f;

	Lamp = CreateDefaultSubobject<UPointLightComponent>(TEXT("Lamp"));
	Lamp->SetupAttachment(Root);
	Lamp->SetMobility(EComponentMobility::Movable);
	Lamp->SetRelativeLocation(FVector(100.f, 10.f, 114.f));
	Lamp->SetIntensityUnits(ELightUnits::Candelas);
	Lamp->SetIntensity(InhRoomParts::LampCandela);
	Lamp->SetAttenuationRadius(1000.f);
	Lamp->SetLightColor(FLinearColor(1.f, 0.68f, 0.42f));
	Lamp->SetSourceRadius(6.f);
}

void AInhRoomBuilder::BeginPlay()
{
	Super::BeginPlay();
	FindWorldActors();
	if (!bShellBuilt)
	{
		BuildShell();
		SetupSky();
		bShellBuilt = true;
	}
	SkyBlend = -1.f;
	// The player controller may already have set the clock (it starts before the room); keep its value.
	SetLifeProgress(Progress);
}

void AInhRoomBuilder::BuildShell()
{
	InhRoomParts::FInhRoomKits Kits(this, Root);
	InhRoomParts::BuildStructure(Kits);
	const float LampZ = InhRoomParts::BuildFurniture(Kits);
	Lamp->SetRelativeLocation(FVector(100.f, 10.f, LampZ));
}

void AInhRoomBuilder::SetupSky()
{
	UTextureCube* Night = InhArt::Cubemap(FName(TEXT("Night")));
	UTextureCube* Dawn = InhArt::Cubemap(FName(TEXT("Dawn")));
	if (!Night || !Dawn)
	{
		// No photographed skies: keep capturing the procedural atmosphere.
		return;
	}
	NightSky = Night;
	DawnSky = Dawn;
	SkyLight->bRealTimeCapture = false;
	SkyLight->SourceType = SLS_SpecifiedCubemap;
	SkyLight->SetCubemap(NightSky);
	SkyLight->MarkRenderStateDirty();
}

void AInhRoomBuilder::FindWorldActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!Harbour)
	{
		for (TActorIterator<AInhHarbourBuilder> It(World); It; ++It)
		{
			Harbour = *It;
			break;
		}
	}
	if (!Air)
	{
		for (TActorIterator<AInhAtmosphereFX> It(World); It; ++It)
		{
			Air = *It;
			break;
		}
	}
}

void AInhRoomBuilder::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;
	// The table lamp flickers a little, like an old oil lamp, and pales against the sunrise as the harbour's lamps do,
	// so the dawn shaft through the window outshines it.
	const float Flicker = 1.f + 0.04f * FMath::Sin(Time * 13.f) + 0.03f * FMath::Sin(Time * 7.3f);
	const float DawnFade = 1.f - 0.7f * FMath::SmoothStep(0.9f, 1.f, Progress);
	Lamp->SetIntensity(InhRoomParts::LampCandela * Flicker * DawnFade);
}

void AInhRoomBuilder::SetLifeProgress(float P)
{
	// Keyframes: dusk -> deep night -> dawn (the moment the life ends). AInhAtmosphereFX's dust shaft copies the sun pitch.
	// The lamps and lit windows are physically bright (the table lamp alone is 55 cd), so the rising sun must be far
	// brighter than the moon to outshine them through the low-sun haze and pour a visible shaft in at the window.
	// One light plays both moon and sun: the high moon fades where it hangs, the light swings to the horizon only while
	// too faint to see (the Sunrise cinematic is still in the lamp-lit room then), and the sun climbs from there. A
	// straight blend from moon to sun would sink the light through the whole sunrise, like a sunset.
	struct FSkyKey { float At; float Pitch; float Lux; FLinearColor Color; float Sky; };
	static const FSkyKey Keys[] = {
		{ 0.00f, -9.f, 3.5f, FLinearColor(1.f, 0.55f, 0.30f), 0.8f },
		{ 0.20f, -35.f, 0.6f, FLinearColor(0.55f, 0.65f, 1.f), 0.35f },
		{ 0.80f, -40.f, 0.5f, FLinearColor(0.50f, 0.60f, 1.f), 0.30f },
		{ 0.87f, -40.f, 0.03f, FLinearColor(0.50f, 0.60f, 1.f), 0.25f },
		{ 0.88f, -1.f, 0.03f, FLinearColor(1.f, 0.55f, 0.45f), 0.25f },
		{ 1.00f, -5.f, 120.f, FLinearColor(1.f, 0.62f, 0.68f), 1.0f },
	};
	P = FMath::Clamp(P, 0.f, 1.f);
	Progress = P;
	int32 i = 0;
	while (i < int32(UE_ARRAY_COUNT(Keys)) - 2 && P > Keys[i + 1].At) ++i;
	const FSkyKey& A = Keys[i];
	const FSkyKey& B = Keys[i + 1];
	const float T = FMath::SmoothStep(0.f, 1.f, (P - A.At) / FMath::Max(B.At - A.At, KINDA_SMALL_NUMBER));

	Sun->SetWorldRotation(FRotator(FMath::Lerp(A.Pitch, B.Pitch, T), InhLayout::SunriseYaw, 0.f));
	// Light spans many stops, so it blends in log space: the dawn brightens as the sun climbs, not all at once.
	Sun->SetIntensity(A.Lux * FMath::Pow(B.Lux / A.Lux, T));
	Sun->SetLightColor(FMath::Lerp(A.Color, B.Color, T));
	SkyLight->SetIntensity(FMath::Lerp(A.Sky, B.Sky, T));

	// The grade and the fog follow the light: cool and heavy at night, warm and open at dusk and dawn.
	const float IntoNight = FMath::SmoothStep(0.f, 0.2f, P);
	const float IntoDawn = FMath::SmoothStep(0.8f, 1.f, P);
	const float Warmth = FMath::Max(1.f - IntoNight, IntoDawn);
	FPostProcessSettings& S = Post->Settings;
	S.SceneColorTint = FMath::Lerp(FLinearColor(0.92f, 0.98f, 1.06f), FLinearColor(1.03f, 0.99f, 0.95f), Warmth);
	S.VignetteIntensity = FMath::Lerp(0.6f, 0.45f, Warmth);
	// The fog's colour is light it gives off: by night it must stay near the moonlit town's level, or the far shore and
	// the horizon wash out to a grey glow.
	Fog->SetFogInscatteringColor(FMath::Lerp(FLinearColor(0.03f, 0.04f, 0.06f), FLinearColor(0.40f, 0.31f, 0.30f), Warmth));

	if (NightSky && DawnSky)
	{
		// Re-filtering the sky light isn't free, so it follows the clock in 5% steps.
		const float Blend = FMath::RoundToFloat(Warmth * 20.f) / 20.f;
		if (Blend != SkyBlend)
		{
			SkyBlend = Blend;
			SkyLight->SetCubemapBlend(NightSky, DawnSky, Blend);
		}
	}

	if (!Harbour || !Air)
	{
		FindWorldActors();
	}
	if (Harbour)
	{
		Harbour->SetLifeProgress(P);
	}
	if (Air)
	{
		Air->SetLifeProgress(P);
	}
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
	using namespace InhRoomParts;
	using namespace InhRoomParts::Palette;
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

	// The phone with your predecessor's voice note, its screen glowing. The WillReveal cinematic circles this spot.
	AInhProp* Phone = SpawnProp(EInhPropKind::Phone, FString(), FVector(20, 75, TableTopZ), MarkPhone);
	Phone->AddPart(Cube, FVector(0, 0, 0.8f), FVector(8, 15, 1.6f), Iron, FRotator(0, 20, 0));
	InhShapes::AddEmissive(Phone, Phone->GetRootComponent(), Cube, FVector(0, 0, 1.7f), FVector(7, 13, 0.2f), Palette::Screen, ScreenStrength, FRotator(0, 20, 0));

	// A spyglass on the sill: look out at the city and the season.
	AInhProp* Window = SpawnProp(EInhPropKind::Window, FString(), FVector(390, 40, 114), MarkItem);
	PropSurface(Window, Cyl, FVector(0, 0, 0), FVector(6, 6, 34), ArtKey::Brass, Brass, FRotator(0, 0, 90));

	int32 TableSlot = 0;
	int32 ShelfSlot = 0;
	int32 FloorSlot = 0;
	for (const FInhItem& Item : Life.Estate.Items)
	{
		SpawnItem(Item, TableSlot, ShelfSlot, FloorSlot);
	}

	// Demands from creditors pinned to the left wall, right of the shelves (two rows of five).
	for (int32 i = 0; i < Life.Estate.Debts.Num(); ++i)
	{
		const int32 Lap = i / 10;
		const FVector Loc(130 + (i % 5) * 58 + Lap * 14, -297, 165 + ((i / 5) % 2) * 70 + Lap * 6);
		AInhProp* Demand = SpawnProp(EInhPropKind::Debt, Life.Estate.Debts[i].Id, Loc, MarkDanger);
		Demand->AddPart(Cube, FVector(0, 0, 0), FVector(42, 1.5f, 54), Notice, FRotator(0, 0, FMath::RandRange(-4, 4)));
		PropSurface(Demand, Sphere, FVector(0, 2, 22), FVector(3, 3, 3), ArtKey::Brass, Brass);
	}

	// Portraits of rivals, crossed out.
	for (int32 i = 0; i < Life.Estate.Feuds.Num(); ++i)
	{
		const FInhFeud& Feud = Life.Estate.Feuds[i];
		const FLinearColor Cross = Feud.IsBlood() ? FLinearColor(0.35f, 0.f, 0.f) : FLinearColor(0.7f, 0.08f, 0.05f);
		AInhProp* Portrait = SpawnProp(EInhPropKind::Feud, Feud.Id, FVector(397, 140 + (i % 3) * 55, 160 + (i / 3) * 65), MarkDanger);
		PropSurface(Portrait, Cube, FVector(0, 0, 0), FVector(3, 42, 52), ArtKey::WoodDark, DarkWood);
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
		PropSurface(Card, Cube, FVector(0, 0, 0), FVector(1, 34, 26), ArtKey::Paper, Paper, FRotator(0, 0, FMath::RandRange(-6, 6)));
		Card->AddPart(Sphere, FVector(-1, 0, 10), FVector(3, 3, 3), FLinearColor(0.7f, 0.1f, 0.1f));
		++ClueIndex;
	}
}

void AInhRoomBuilder::SpawnItem(const FInhItem& Item, int32& TableSlot, int32& ShelfSlot, int32& FloorSlot)
{
	using namespace InhRoomParts;
	using namespace InhRoomParts::Palette;
	UStaticMesh* Cube = InhShapes::Cube();
	UStaticMesh* Cyl = InhShapes::Cylinder();
	UStaticMesh* Sphere = InhShapes::Sphere();

	if (Item.bHidden && !RevealedItems.Contains(Item.Id))
	{
		// A loose board in the same wood as the floor, a little proud and out of line.
		AInhProp* Board = SpawnProp(EInhPropKind::HiddenItem, Item.Id, Slots::Pick(Slots::Floor, FloorSlot), MarkHidden);
		PropSurface(Board, Cube, FVector(0, 0, 0.8f), FVector(70, 18, 1.6f), ArtKey::FloorWood, LightWood, FRotator(0, 8, 0));
		return;
	}

	const uint32 Hash = GetTypeHash(Item.Id);
	if (Item.IsLedgerPage())
	{
		AInhProp* Page = SpawnProp(EInhPropKind::Item, Item.Id, Slots::Pick(Slots::Table, TableSlot), MarkLedger);
		PropSurface(Page, Cube, FVector(0, 0, 0.4f), FVector(22, 30, 0.6f), ArtKey::Paper, Paper * 1.1f, FRotator(0, float(Hash % 40) - 20.f, 0));
		Page->AddPart(Cube, FVector(0, 0, 0.8f), FVector(16, 1, 0.2f), FLinearColor(0.2f, 0.1f, 0.05f), FRotator(0, float(Hash % 40) - 20.f, 0));
	}
	else if (Item.Kind == TEXT("letter"))
	{
		AInhProp* Letter = SpawnProp(EInhPropKind::Item, Item.Id, Slots::Pick(Slots::Table, TableSlot), MarkItem);
		PropSurface(Letter, Cube, FVector(0, 0, 0.5f), FVector(16, 24, 0.8f), ArtKey::Paper, Paper * 1.15f, FRotator(0, -15, 0));
		Letter->AddPart(Cyl, FVector(0, 0, 1.1f), FVector(4, 4, 0.6f), Palette::Seal);
	}
	else if (Item.Kind == TEXT("box"))
	{
		// The box of five keyholes: one turns brass for every heir who kept it.
		AInhProp* Box = SpawnProp(EInhPropKind::Item, Item.Id, Slots::Pick(Slots::Floor, FloorSlot), MarkItem);
		FVector ChestSize = FVector::ZeroVector;
		if (PropArt(Box, ArtKey::Chest, FVector(44, 32, 28), FVector2D(26, 18), &ChestSize))
		{
			for (int32 k = 0; k < 5; ++k)
			{
				const FLinearColor KeyColor = k < Item.Streak ? Brass : Iron * 2.f;
				Box->AddPart(Sphere, FVector(-ChestSize.X * 0.5 - 0.6, (k - 2) * ChestSize.Y * 0.14, ChestSize.Z * 0.5), FVector(2.5f, 2.5f, 2.5f), KeyColor);
			}
		}
		else
		{
			PropSurface(Box, Cube, FVector(0, 0, 12), FVector(42, 30, 24), ArtKey::Iron, Iron);
			PropSurface(Box, Cube, FVector(0, 0, 18), FVector(43, 31, 3), ArtKey::Brass, Brass * 0.6f);
			for (int32 k = 0; k < 5; ++k)
			{
				const FLinearColor KeyColor = k < Item.Streak ? Brass : Iron * 2.f;
				Box->AddPart(Sphere, FVector(-15.5f, -12 + k * 6, 10), FVector(2.5f, 2.5f, 2.5f), KeyColor);
			}
		}
	}
	else if (Item.Kind == TEXT("property"))
	{
		AInhProp* Deed = SpawnProp(EInhPropKind::Item, Item.Id, Slots::Pick(Slots::Shelf, ShelfSlot), MarkItem);
		PropSurface(Deed, Cyl, FVector(0, 0, 3), FVector(6, 6, 26), ArtKey::Paper, Paper, FRotator(0, 0, 90));
		Deed->AddPart(Cyl, FVector(0, 0, 3), FVector(6.5f, 6.5f, 3), FLinearColor(0.5f, 0.05f, 0.05f), FRotator(0, 0, 90));
	}
	else
	{
		AInhProp* Trinket = SpawnProp(EInhPropKind::Item, Item.Id, Slots::Pick(Slots::Shelf, ShelfSlot), MarkItem);
		switch (Hash % 3)
		{
		case 0:
			if (!PropArt(Trinket, ArtKey::Vase, FVector(14, 14, 22), FVector2D(6, 6)))
			{
				PropSurface(Trinket, Sphere, FVector(0, 0, 6), FVector(12, 12, 12), ArtKey::Brass, Brass);
			}
			break;
		case 1:
			if (!PropArt(Trinket, ArtKey::Bottle, FVector(12, 12, 26), FVector2D(5, 5)))
			{
				Trinket->AddPart(Cyl, FVector(0, 0, 10), FVector(14, 14, 20), SeaGlass);
				PropSurface(Trinket, Cyl, FVector(0, 0, 21), FVector(10, 10, 3), ArtKey::Wood, Wood);
			}
			break;
		default:
			PropSurface(Trinket, Cube, FVector(0, 0, 5), FVector(18, 12, 10), ArtKey::Wood, Wood);
			PropSurface(Trinket, Sphere, FVector(0, 0, 11), FVector(4, 4, 4), ArtKey::Brass, Brass);
			break;
		}
	}
}
