#include "World/InhHarbourBuilder.h"
#include "World/InhArt.h"
#include "World/InhLayout.h"
#include "World/InhShapes.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "Inheritance.h"

// Hollow Bay outside the window. Everything is placed from InhLayout and drawn from one FRandomStream with a fixed
// seed, so the town is the same on every launch. Repeated pieces share instanced components (one draw call per shape
// and material), which keeps the whole exterior to a few dozen draw calls on a phone. Nothing here may enter the room
// (X -400..400, Y -300..300, Z 0..300): the town stays beyond our facade line, the walls of our building and the
// neighbours' side walls.

// Helpers live in a named namespace: unity builds share one translation unit, where anonymous names could collide.
namespace InhHarbourParts
{
	constexpr float Sqrt2 = 1.41421356f;
	constexpr int32 TownSeed = 1907;
	/** Floor to floor in the town's houses. */
	constexpr float Storey = 280.f;
	/** Nothing in the town rises above this: the camera's way home from outside crosses the rooftops higher up. */
	constexpr float RoofCapZ = 780.f;
	/** The Ferryman's boat reaches FerrymanPassing at this share of PlayFerryman's duration. */
	constexpr float FerryPassingFraction = 0.5f;
	constexpr int32 FerryLutSteps = 24;
	constexpr float StreetLightCandela = 14.f;
	constexpr float FerryLightCandela = 12.f;
	constexpr float BeamCandela = 6000.f;
	/** Hidden instances shrink to this rather than zero, so their transforms stay invertible. */
	const FVector HiddenScale(0.0001f);

	const FLinearColor White(1.f, 1.f, 1.f);
	const FLinearColor WindowGlow(1.f, 0.62f, 0.32f);
	const FLinearColor FarWindowGlow(1.f, 0.70f, 0.40f);
	const FLinearColor LampGlow(1.f, 0.72f, 0.42f);
	const FLinearColor LensGlow(1.f, 0.90f, 0.72f);
	const FLinearColor StarGlow(0.80f, 0.86f, 1.f);
	const FLinearColor MoonGlow(0.86f, 0.90f, 1.f);
	const FLinearColor LanternGlow(1.f, 0.64f, 0.30f);
	const FLinearColor WoodFallback(0.16f, 0.10f, 0.055f);
	const FLinearColor WoodDarkFallback(0.04f, 0.022f, 0.012f);
	const FLinearColor LinenFallback(0.70f, 0.68f, 0.62f);
	const FLinearColor IronFallback(0.07f, 0.07f, 0.08f);
	// Sized for the grade's night exposure floor: a few stops over the moonlit town, so lit windows stay warm amber
	// instead of clipping to white (lamps are point sources and may burn hotter).
	constexpr float WindowStrength = 1.5f;
	constexpr float FarWindowStrength = 2.f;
	constexpr float LampStrength = 8.f;
	constexpr float LensStrength = 40.f;
	constexpr float BrightStarStrength = 5.f;
	constexpr float FaintStarStrength = 2.5f;
	constexpr float MoonStrength = 6.f;

	enum class EShape : uint8 { Cube, Cylinder, Sphere, Cone, Plane, Count };
	enum class EKit : uint8 { Plaster, PlasterDark, PlasterWarm, PlasterCool, Brick, Stone, Cobble, Roof, WoodDark, Wood, Rock, Fabric, Iron, Linen, Algae, Pane, Count };

	struct FKitLook
	{
		/** Art material key (ArtManifest.json), or nullptr for a flat colour only. */
		const TCHAR* Key;
		FLinearColor Fallback;
		/** Multiplies the installed material's Tint: a couple of painted variants of one art material. White keeps it as installed. */
		FLinearColor Tint;
		bool bCastShadow;
	};

	const FKitLook KitLooks[] = {
		{ TEXT("Plaster"),     FLinearColor(0.20f, 0.23f, 0.24f), White, true },
		{ TEXT("PlasterDark"), FLinearColor(0.15f, 0.18f, 0.20f), White, true },
		{ TEXT("Plaster"),     FLinearColor(0.30f, 0.25f, 0.18f), FLinearColor(1.08f, 0.93f, 0.74f), true },
		{ TEXT("Plaster"),     FLinearColor(0.15f, 0.20f, 0.25f), FLinearColor(0.80f, 0.92f, 1.06f), true },
		{ TEXT("Brick"),       FLinearColor(0.25f, 0.09f, 0.06f), White, true },
		{ TEXT("Stone"),       FLinearColor(0.18f, 0.18f, 0.17f), White, true },
		{ TEXT("Cobble"),      FLinearColor(0.12f, 0.12f, 0.12f), White, false },
		{ TEXT("Roof"),        FLinearColor(0.06f, 0.065f, 0.075f), White, true },
		{ TEXT("WoodDark"),    WoodDarkFallback, White, true },
		{ TEXT("Wood"),        WoodFallback, White, true },
		{ TEXT("Rock"),        FLinearColor(0.14f, 0.13f, 0.12f), White, true },
		{ TEXT("Fabric"),      FLinearColor(0.24f, 0.035f, 0.03f), White, true },
		{ TEXT("Iron"),        IronFallback, White, true },
		{ TEXT("Linen"),       LinenFallback, White, true },
		// Weed and wet stone at the tide line, and unlit window glass: flat colours, no art needed.
		{ nullptr,             FLinearColor(0.02f, 0.03f, 0.022f), White, false },
		{ nullptr,             FLinearColor(0.012f, 0.014f, 0.018f), White, false },
	};
	static_assert(UE_ARRAY_COUNT(KitLooks) == int32(EKit::Count), "One look per kit");

	/** Lazily creates one instanced component per shape and look, so each pair costs one draw call however many copies. */
	class FInhKits
	{
	public:
		FInhKits(AActor* InOwner, USceneComponent* InParent)
			: Owner(InOwner), Parent(InParent)
		{
			Meshes[int32(EShape::Cube)] = InhShapes::Cube();
			Meshes[int32(EShape::Cylinder)] = InhShapes::Cylinder();
			Meshes[int32(EShape::Sphere)] = InhShapes::Sphere();
			Meshes[int32(EShape::Cone)] = InhShapes::Cone();
			Meshes[int32(EShape::Plane)] = InhShapes::Plane();
			// The looks with no art key are always flat colours on the engine's basic shape material, which a packaged
			// build may be unable to instance (InhShapes::CanInstance). They are few (window glass, the tide line), so
			// they become single parts then. Everything else is drawn by the art build's materials, which instance.
			bFlatSingles = !InhShapes::CanInstance(NAME_None);
		}

		UInstancedStaticMeshComponent* Get(EShape Shape, EKit Kit)
		{
			UInstancedStaticMeshComponent*& Set = Sets[int32(Shape)][int32(Kit)];
			if (!Set)
			{
				const FKitLook& Look = KitLooks[int32(Kit)];
				const FName Key = Look.Key ? FName(Look.Key) : FName();
				Set = InhShapes::AddInstanced(Owner, Parent, Meshes[int32(Shape)], Key, Look.Fallback, false);
				if (Set)
				{
					Set->SetCastShadow(Look.bCastShadow);
					if (!Look.Tint.Equals(White))
					{
						if (UMaterialInterface* Art = InhArt::Material(Key))
						{
							static const FName TintName(TEXT("Tint"));
							UMaterialInstanceDynamic* Tinted = UMaterialInstanceDynamic::Create(Art, Set);
							// On top of the installed Tint, not instead of it: a flat-fallback MI keeps its whole colour there.
							Tinted->SetVectorParameterValue(TintName, Tinted->K2_GetVectorParameterValue(TintName) * Look.Tint);
							Set->SetMaterial(0, Tinted);
						}
					}
					++NumSets;
				}
			}
			return Set;
		}

		/** Size is in centimetres like InhShapes::AddPart (a plane only uses X and Y). */
		void Add(EShape Shape, EKit Kit, const FVector& Center, const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator)
		{
			const FVector Scale = Shape == EShape::Plane ? FVector(Size.X / 100.0, Size.Y / 100.0, 1.0) : Size / 100.0;
			if (bFlatSingles && !KitLooks[int32(Kit)].Key)
			{
				AddSingle(Shape, Kit, Center, Scale, Rotation);
			}
			else if (UInstancedStaticMeshComponent* Set = Get(Shape, Kit))
			{
				Set->AddInstance(FTransform(Rotation, Center, Scale));
				++NumInstances;
			}
		}

		UStaticMesh* Mesh(EShape Shape) const { return Meshes[int32(Shape)]; }

		int32 NumSets = 0;
		int32 NumInstances = 0;
		int32 NumSingles = 0;

	private:
		/** One piece as a component of its own, with exactly the transform and look its instance would have had. */
		void AddSingle(EShape Shape, EKit Kit, const FVector& Center, const FVector& Scale, const FRotator& Rotation)
		{
			const FKitLook& Look = KitLooks[int32(Kit)];
			// AddSurface takes centimetres; a plane gets the instance's unit Z scale too.
			UStaticMeshComponent* Part = InhShapes::AddSurface(Owner, Parent, Meshes[int32(Shape)], Center, Scale * 100.0, FName(),
				Look.Fallback, Rotation, false);
			if (!Part)
			{
				return;
			}
			Part->SetCastShadow(Look.bCastShadow);
			// Every piece of one look shares the first one's material, so the renderer can still batch them.
			UMaterialInterface*& Shared = SingleMaterials[int32(Shape)][int32(Kit)];
			if (Shared)
			{
				Part->SetMaterial(0, Shared);
			}
			else
			{
				Shared = Part->GetMaterial(0);
			}
			++NumSingles;
		}

		AActor* Owner = nullptr;
		USceneComponent* Parent = nullptr;
		UStaticMesh* Meshes[int32(EShape::Count)] = {};
		UInstancedStaticMeshComponent* Sets[int32(EShape::Count)][int32(EKit::Count)] = {};
		UMaterialInterface* SingleMaterials[int32(EShape::Count)][int32(EKit::Count)] = {};
		bool bFlatSingles = false;
	};

	/** A plane (which faces +Z) turned to face Normal with its local X running up the wall: size it (height, width). */
	FRotator Facing(const FVector& Normal)
	{
		return FRotationMatrix::MakeFromZX(Normal, FVector::UpVector).Rotator();
	}

	FTransform Hidden(const FTransform& Shown)
	{
		return FTransform(Shown.GetRotation(), Shown.GetLocation(), HiddenScale);
	}

	/**
	 * Maps an art mesh into a box: uniformly scaled to fit FitSize with its bottom centre at the origin. Scans face any
	 * way, so when the box is clearly longer one way the mesh's longer side is turned to match.
	 */
	FTransform FitArt(const UStaticMesh* Mesh, const FVector& FitSize)
	{
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		FVector Size = Bounds.BoxExtent * 2.0;
		const bool bBoxLongX = FitSize.X > FitSize.Y * 1.25;
		const bool bBoxLongY = FitSize.Y > FitSize.X * 1.25;
		const bool bTurn = (bBoxLongX && Size.Y > Size.X) || (bBoxLongY && Size.X > Size.Y);
		if (bTurn)
		{
			Swap(Size.X, Size.Y);
		}
		double Scale = -1.0;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Size[Axis] > UE_KINDA_SMALL_NUMBER && FitSize[Axis] > UE_KINDA_SMALL_NUMBER)
			{
				const double AxisScale = FitSize[Axis] / Size[Axis];
				Scale = Scale < 0.0 ? AxisScale : FMath::Min(Scale, AxisScale);
			}
		}
		if (Scale <= 0.0)
		{
			Scale = 1.0;
		}
		const FRotator Turn(0.f, bTurn ? 90.f : 0.f, 0.f);
		const FVector LocalBottom(Bounds.Origin.X, Bounds.Origin.Y, Bounds.Origin.Z - Bounds.BoxExtent.Z);
		return FTransform(Turn, -Turn.RotateVector(LocalBottom * Scale), FVector(Scale));
	}

	/** An instanced component for an art mesh that keeps the mesh's own materials. */
	UInstancedStaticMeshComponent* NewArtInstances(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh)
	{
		UInstancedStaticMeshComponent* Set = NewObject<UInstancedStaticMeshComponent>(Owner);
		Set->SetStaticMesh(Mesh);
		Set->SetupAttachment(Parent);
		Set->SetMobility(EComponentMobility::Movable);
		Set->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Set->RegisterComponent();
		Owner->AddInstanceComponent(Set);
		return Set;
	}

	/** One imported prop mesh, shared by every copy of it; empty when the art pipeline hasn't run. */
	struct FInhArtSet
	{
		UStaticMesh* Mesh = nullptr;
		UInstancedStaticMeshComponent* Set = nullptr;

		explicit operator bool() const { return Set != nullptr; }

		void Add(const FVector& BottomCenter, const FVector& FitSize, float Yaw) const
		{
			Set->AddInstance(FitArt(Mesh, FitSize) * FTransform(FRotator(0.f, Yaw, 0.f), BottomCenter));
		}
	};

	/** A wall to put windows and doors on. */
	struct FInhFacade
	{
		/** The wall's surface at ground level, in the middle of its width. */
		FVector Origin = FVector::ZeroVector;
		FVector Normal = FVector::ForwardVector;
		FVector Along = FVector::RightVector;
		/** A cube turned by this yaw has its local X along Normal, so its size reads (thickness, width, height). */
		float Yaw = 0.f;
	};

	FInhFacade MakeFacade(const FVector& Origin, float Yaw)
	{
		FInhFacade Face;
		Face.Origin = Origin;
		Face.Normal = FRotator(0.f, Yaw, 0.f).Vector();
		Face.Along = FRotator(0.f, Yaw + 90.f, 0.f).Vector();
		Face.Yaw = Yaw;
		return Face;
	}

	/** When a window's lamp burns. */
	enum class EHours : uint8 { Household, LateShop, AllNight };

	struct FInhHouse
	{
		/** Centre of the footprint, on the ground. */
		FVector Base = FVector::ZeroVector;
		/** The front faces this way. */
		float Yaw = 0.f;
		/** Along the front. */
		float Width = 400.f;
		float Depth = 600.f;
		/** Ground to eaves. */
		float Height = 560.f;
		/** The ridge runs front to back, so the gable faces the street. */
		bool bGableFront = true;
		EKit Finish = EKit::Plaster;
		int32 Chimneys = 1;
		/** Full detail (frames, sills, shutters, doors); the far shore only gets lit windows. */
		bool bNear = true;
		bool bBackWindows = false;
		/** Share of windows that can light up at all. */
		float LitShare = 0.82f;
		EHours Hours = EHours::Household;
		float MaxTopZ = RoofCapZ;
	};

	struct FInhHill
	{
		FVector Center = FVector::ZeroVector;
		FVector Radii = FVector(1000.f);
	};

	/** Moving sets, created on first use. */
	enum class EMoving : uint8 { Hull, Deck, Wood, Spar, Sail, Lamp, ArtRowboat, GullBody, GullWing, Count };

	EKit StreetFinish(FRandomStream& Rng)
	{
		const float Roll = Rng.FRand();
		return Roll < 0.25f ? EKit::Plaster : Roll < 0.40f ? EKit::PlasterDark : Roll < 0.58f ? EKit::PlasterWarm
			: Roll < 0.72f ? EKit::PlasterCool : Roll < 0.90f ? EKit::Brick : EKit::Stone;
	}

	EKit FarFinish(FRandomStream& Rng)
	{
		const float Roll = Rng.FRand();
		return Roll < 0.30f ? EKit::Plaster : Roll < 0.55f ? EKit::PlasterDark : Roll < 0.70f ? EKit::PlasterWarm
			: Roll < 0.85f ? EKit::PlasterCool : Roll < 0.95f ? EKit::Brick : EKit::Stone;
	}

/** Builds the harbour once, in BeginPlay. A friend of AInhHarbourBuilder so it can fill the runtime state directly. */
struct FInhHarbourBuild
{
	explicit FInhHarbourBuild(AInhHarbourBuilder& InHarbour)
		: H(InHarbour)
		, Kits(&InHarbour, InHarbour.Root)
		, Rng(TownSeed)
	{
		for (int32& Index : MovingIndex)
		{
			Index = INDEX_NONE;
		}
	}

	AInhHarbourBuilder& H;
	FInhKits Kits;
	FRandomStream Rng;
	UInstancedStaticMeshComponent* LampGlows = nullptr;
	FInhArtSet CrateArt;
	FInhArtSet BarrelArt;
	FInhArtSet RopeArt;
	FInhArtSet BenchArt;
	FInhArtSet LanternArt;
	UStaticMesh* RowboatMesh = nullptr;
	TArray<FInhHill> Hills;
	int32 MovingIndex[int32(EMoving::Count)];
	float StepsMinY = 0.f;
	float StepsMaxY = 0.f;
	// Our building: the room's left wall is 10 cm thick, and the market below matches it on the open side. Members
	// rather than namespace globals, so they never depend on InhLayout's static initialisation order.
	const float OurMinY = -float(InhLayout::RoomHalfExtent.Y) - 10.f;
	const float OurMaxY = float(InhLayout::RoomHalfExtent.Y) + 10.f;
	const float RoomTopZ = float(InhLayout::RoomHalfExtent.Z) * 2.f;

	UStaticMesh* Cube() const { return Kits.Mesh(EShape::Cube); }
	UStaticMesh* Cylinder() const { return Kits.Mesh(EShape::Cylinder); }
	UStaticMesh* Sphere() const { return Kits.Mesh(EShape::Sphere); }
	UStaticMesh* Cone() const { return Kits.Mesh(EShape::Cone); }
	UStaticMesh* Plane() const { return Kits.Mesh(EShape::Plane); }

	void Run()
	{
		H.bEmissiveArt = InhArt::Material(FName(TEXT("Emissive"))) != nullptr;

		// The glowing sets every part of the town adds to: lit windows near and far, and the lamps.
		H.WindowLights = InhShapes::AddInstancedEmissive(&H, H.Root, Plane(), WindowGlow, WindowStrength);
		H.FarWindowLights = InhShapes::AddInstancedEmissive(&H, H.Root, Plane(), FarWindowGlow, FarWindowStrength);
		LampGlows = InhShapes::AddInstancedEmissive(&H, H.Root, Cube(), LampGlow, LampStrength);
		H.LampGlowMaterial = LampGlows ? Cast<UMaterialInstanceDynamic>(LampGlows->GetMaterial(0)) : nullptr;

		CrateArt = ArtSet(TEXT("Crate"));
		BarrelArt = ArtSet(TEXT("Barrel"));
		RopeArt = ArtSet(TEXT("Rope"));
		BenchArt = ArtSet(TEXT("Bench"));
		LanternArt = ArtSet(TEXT("Lantern"));
		RowboatMesh = InhArt::Mesh(FName(TEXT("Rowboat")));

		BuildGround();
		BuildHills();
		BuildOurBuilding();
		BuildStreetRow(-1.f);
		BuildStreetRow(1.f);
		BuildBackRow();
		BuildQuay();
		BuildSteps();
		BuildPier();
		BuildQuayProps();
		BuildSea();
		BuildLighthouse();
		BuildFarShore();
		BuildSky();
		BuildBoats();
		BuildGulls();
		BuildFerryRoute();
		BuildFerryman();
		BuildMara();

		UE_LOG(LogInheritance, Log, TEXT("Harbour built: %d static instances in %d sets, %d single parts, %d lit windows, %d far lights, %d boats, %d gulls."),
			Kits.NumInstances, Kits.NumSets, Kits.NumSingles, H.Windows.Num(), H.FarWindows.Num(), H.Boats.Num(), H.Gulls.Num());
	}

	FInhArtSet ArtSet(const TCHAR* Key)
	{
		FInhArtSet Result;
		Result.Mesh = InhArt::Mesh(FName(Key));
		if (Result.Mesh)
		{
			Result.Set = NewArtInstances(&H, H.Root, Result.Mesh);
		}
		return Result;
	}

	// --- helpers ---------------------------------------------------------------------------------------------------

	float HillTop(float X, float Y) const
	{
		float Best = -1.e6f;
		for (const FInhHill& Hill : Hills)
		{
			const float DX = (X - float(Hill.Center.X)) / float(Hill.Radii.X);
			const float DY = (Y - float(Hill.Center.Y)) / float(Hill.Radii.Y);
			const float D = DX * DX + DY * DY;
			if (D < 1.f)
			{
				Best = FMath::Max(Best, float(Hill.Center.Z) + float(Hill.Radii.Z) * FMath::Sqrt(1.f - D));
			}
		}
		return Best;
	}

	void AddGlow(const FVector& Center, const FVector& Size)
	{
		if (LampGlows)
		{
			LampGlows->AddInstance(FTransform(FRotator::ZeroRotator, Center, Size / 100.0));
		}
	}

	void AddLitWindow(UInstancedStaticMeshComponent* Set, TArray<FInhWindowLight>& List, const FTransform& Transform, EHours Hours)
	{
		if (!Set)
		{
			return;
		}
		FInhWindowLight& Window = List.AddDefaulted_GetRef();
		Window.Shown = Transform;
		switch (Hours)
		{
		case EHours::Household:
			// Some lamps come on as dusk deepens, they go out one by one through the night, and a few early risers
			// light up again before dawn.
			Window.OnFrom = Rng.FRand() < 0.4f ? Rng.FRandRange(0.f, 0.12f) : 0.f;
			Window.OffAt = 0.05f + 0.85f * FMath::Pow(Rng.FRand(), 1.5f);
			Window.OnAgainAt = Rng.FRand() < 0.22f ? Rng.FRandRange(0.86f, 0.98f) : 2.f;
			break;
		case EHours::LateShop:
			Window.OffAt = Rng.FRandRange(0.3f, 0.5f);
			Window.OnAgainAt = Rng.FRandRange(0.88f, 0.96f);
			break;
		case EHours::AllNight:
			Window.OffAt = 2.f;
			break;
		}
		Set->AddInstance(Transform);
	}

	void AddPointLight(const FVector& Location)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(&H);
		Light->SetupAttachment(H.Root);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetRelativeLocation(Location);
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetIntensity(StreetLightCandela);
		Light->SetAttenuationRadius(850.f);
		Light->SetLightColor(FLinearColor(1.f, 0.66f, 0.38f));
		Light->SetSourceRadius(4.f);
		// Pools of lamplight on the cobbles; only the lighthouse beam may cost a shadow.
		Light->SetCastShadows(false);
		Light->RegisterComponent();
		H.AddInstanceComponent(Light);
		H.StreetLights.Add(Light);
	}

	/** A lantern standing on Bottom: the imported lantern when installed (with a glow inside), else iron and glass. */
	void AddLantern(const FVector& Bottom, float Width, float Height)
	{
		const FVector Up = FVector::UpVector;
		if (LanternArt)
		{
			LanternArt.Add(Bottom, FVector(Width, Width, Height), 0.f);
			AddGlow(Bottom + Up * (Height * 0.42f), FVector(Width * 0.32f, Width * 0.32f, Height * 0.3f));
			return;
		}
		Kits.Add(EShape::Cube, EKit::Iron, Bottom + Up * 2.f, FVector(Width * 0.8f, Width * 0.8f, 4.f));
		AddGlow(Bottom + Up * (4.f + Height * 0.3f), FVector(Width * 0.55f, Width * 0.55f, Height * 0.56f));
		Kits.Add(EShape::Cone, EKit::Iron, Bottom + Up * (4.f + Height * 0.6f + Height * 0.14f), FVector(Width, Width, Height * 0.28f));
		Kits.Add(EShape::Cylinder, EKit::Iron, Bottom + Up * (Height + 6.f), FVector(4.f, 4.f, 10.f));
	}

	void AddStreetLamp(const FVector& Foot, float PostHeight, bool bLight)
	{
		const FVector Up = FVector::UpVector;
		Kits.Add(EShape::Cylinder, EKit::Iron, Foot + Up * 15.f, FVector(26.f, 26.f, 30.f));
		Kits.Add(EShape::Cylinder, EKit::Iron, Foot + Up * (PostHeight * 0.5f + 10.f), FVector(12.f, 12.f, PostHeight - 20.f));
		Kits.Add(EShape::Cylinder, EKit::Iron, Foot + Up * (PostHeight - 3.f), FVector(22.f, 22.f, 6.f));
		AddLantern(Foot + Up * PostHeight, 30.f, 46.f);
		if (bLight)
		{
			AddPointLight(Foot + Up * (PostHeight + 22.f));
		}
	}

	/** A gable roof pitched at 45 degrees over walls whose eaves line passes through Eaves along RidgeDir. */
	void AddRoof(EKit Walls, const FVector& Eaves, const FVector& RidgeDir, float HalfSpan, float RidgeLength, bool bNear)
	{
		const FVector Up = FVector::UpVector;
		const FVector Across = FVector::CrossProduct(Up, RidgeDir).GetSafeNormal();
		const FVector Diagonal = (Across + Up).GetSafeNormal();
		const float Thick = bNear ? 14.f : 18.f;
		const float Overhang = bNear ? 30.f : 24.f;
		const float GableOverhang = bNear ? 22.f : 16.f;
		const FRotator Diamond = FRotationMatrix::MakeFromXZ(RidgeDir, Diagonal).Rotator();

		// The gable ends: a square prism turned 45 degrees about the ridge. Its lower half hides inside the walls.
		Kits.Add(EShape::Cube, Walls, Eaves, FVector(RidgeLength, HalfSpan * Sqrt2, HalfSpan * Sqrt2), Diamond);
		for (const float Side : { -1.f, 1.f })
		{
			const FVector Normal = (Across * Side + Up).GetSafeNormal();
			const FVector Down = (Across * Side - Up).GetSafeNormal();
			const FVector Middle = Eaves + (Across * Side + Up) * (HalfSpan * 0.5f);
			Kits.Add(EShape::Cube, EKit::Roof, Middle + Normal * (Thick * 0.5f) + Down * (Overhang * 0.5f),
				FVector(RidgeLength + GableOverhang * 2.f, HalfSpan * Sqrt2 + Overhang, Thick),
				FRotationMatrix::MakeFromXZ(RidgeDir, Normal).Rotator());
		}
		// Ridge tiles over the joint.
		Kits.Add(EShape::Cube, EKit::Roof, Eaves + Up * (HalfSpan + 12.f), FVector(RidgeLength + GableOverhang * 2.f + 4.f, 22.f, 22.f), Diamond);
	}

	void AddDoor(const FInhFacade& Face, float U)
	{
		const FVector Up = FVector::UpVector;
		const FVector Foot = Face.Origin + Face.Along * U;
		const FRotator Turn(0.f, Face.Yaw, 0.f);
		Kits.Add(EShape::Cube, EKit::Stone, Foot + Face.Normal * 16.f + Up * 6.f, FVector(32.f, 130.f, 12.f), Turn);
		Kits.Add(EShape::Cube, EKit::WoodDark, Foot + Face.Normal * 1.f + Up * 117.f, FVector(8.f, 100.f, 210.f), Turn);
		Kits.Add(EShape::Cube, EKit::Stone, Foot + Face.Normal * 3.f + Up * 231.f, FVector(10.f, 124.f, 16.f), Turn);
	}

	/**
	 * A window centred U along the facade and Z above its foot. Near windows get a frame, sill, glazing bars, dark glass
	 * and sometimes shutters; far ones are only their light. bPlain skips the shutters.
	 */
	void AddWindow(const FInhFacade& Face, float U, float Z, float W, float Ht, bool bNear, EHours Hours, float LitChance, bool bPlain)
	{
		const FVector Up = FVector::UpVector;
		const FVector C = Face.Origin + Face.Along * U + Up * Z;
		const FVector N = Face.Normal;
		const FRotator Turn(0.f, Face.Yaw, 0.f);
		const FRotator PaneTurn = Facing(N);
		const FVector PaneScale(Ht / 100.f, W / 100.f, 1.f);
		if (!bNear)
		{
			if (Rng.FRand() < LitChance)
			{
				AddLitWindow(H.FarWindowLights, H.FarWindows, FTransform(PaneTurn, C + N * 4.f, PaneScale), Hours);
			}
			return;
		}

		const float Style = bPlain ? 1.f : Rng.FRand();
		Kits.Add(EShape::Cube, EKit::WoodDark, C - N * 0.5f, FVector(7.f, W + 14.f, Ht + 14.f), Turn);
		Kits.Add(EShape::Cube, EKit::Stone, C + N * 6.f - Up * (Ht * 0.5f + 10.f), FVector(16.f, W + 22.f, 7.f), Turn);
		if (Style < 0.12f)
		{
			// Shutters closed for the night.
			Kits.Add(EShape::Cube, EKit::WoodDark, C + N * 6.f, FVector(4.f, W + 4.f, Ht + 4.f), Turn);
			return;
		}
		// Dark glass, and the lit pane just in front of it, shown or hidden as the night goes on.
		Kits.Add(EShape::Plane, EKit::Pane, C + N * 4.5f, FVector(Ht, W, 1.f), PaneTurn);
		if (Rng.FRand() < LitChance)
		{
			AddLitWindow(H.WindowLights, H.Windows, FTransform(PaneTurn, C + N * 5.5f, PaneScale), Hours);
		}
		Kits.Add(EShape::Cube, EKit::WoodDark, C + N * 7.5f, FVector(3.f, 3.5f, Ht), Turn);
		Kits.Add(EShape::Cube, EKit::WoodDark, C + N * 7.5f + Up * (Ht * 0.12f), FVector(3.f, W, 3.5f), Turn);
		if (Style < 0.45f)
		{
			for (const float Side : { -1.f, 1.f })
			{
				Kits.Add(EShape::Cube, EKit::WoodDark, C + N * 2.5f + Face.Along * (Side * (W * 0.75f + 9.f)), FVector(4.f, W * 0.5f, Ht), Turn);
			}
		}
	}

	void AddFacadeWindows(const FInhHouse& House, const FInhFacade& Face, bool bFront, float HalfSpan)
	{
		const int32 Storeys = FMath::Max(1, FMath::FloorToInt((House.Height + 30.f) / Storey));
		const int32 Columns = FMath::Clamp(FMath::FloorToInt(House.Width / (House.bNear ? 150.f : 170.f)), 1, 4);
		const float Spacing = House.Width / Columns;
		const float WinW = FMath::Min(Spacing * 0.5f, House.bNear ? 92.f : 80.f);
		const int32 Door = bFront && House.bNear ? Rng.RandRange(0, Columns - 1) : INDEX_NONE;
		const bool bShop = bFront && House.bNear && Columns >= 2 && Rng.FRand() < 0.3f;
		for (int32 S = 0; S < Storeys; ++S)
		{
			for (int32 C = 0; C < Columns; ++C)
			{
				const float U = -House.Width * 0.5f + Spacing * (C + 0.5f);
				if (S == 0 && C == Door)
				{
					AddDoor(Face, U);
					continue;
				}
				// Backs and the far shore are plainer: fewer windows.
				if ((!bFront && Rng.FRand() < 0.5f) || (!House.bNear && Rng.FRand() < 0.25f))
				{
					continue;
				}
				if (S == 0 && bShop)
				{
					AddWindow(Face, U, 140.f, Spacing * 0.78f, 150.f, true, EHours::LateShop, 0.9f, true);
					continue;
				}
				const float Ht = S == 0 ? 130.f : (House.bNear ? Rng.FRandRange(125.f, 150.f) : 110.f);
				const float Z = S * Storey + (S == 0 ? 140.f : 150.f);
				if (Z + Ht * 0.5f > House.Height - 20.f)
				{
					continue;
				}
				AddWindow(Face, U, Z, WinW, Ht, House.bNear, House.Hours, House.LitShare, false);
			}
		}
		// An attic window up in a street-facing gable.
		if (bFront && House.bGableFront && HalfSpan > 150.f)
		{
			AddWindow(Face, 0.f, House.Height + HalfSpan * 0.36f, FMath::Min(64.f, HalfSpan * 0.4f), 80.f, House.bNear, House.Hours,
				House.LitShare * 0.7f, true);
		}
	}

	void AddHouse(const FInhHouse& House)
	{
		const FVector Up = FVector::UpVector;
		const FRotator Turn(0.f, House.Yaw, 0.f);
		const FVector Fwd = Turn.Vector();
		const FVector Right = FRotator(0.f, House.Yaw + 90.f, 0.f).Vector();
		Kits.Add(EShape::Cube, House.Finish, House.Base + Up * (House.Height * 0.5f), FVector(House.Depth, House.Width, House.Height), Turn);

		const FVector Eaves = House.Base + Up * House.Height;
		const FVector RidgeDir = House.bGableFront ? Fwd : Right;
		const FVector Across = House.bGableFront ? Right : Fwd;
		const float HalfSpan = (House.bGableFront ? House.Width : House.Depth) * 0.5f;
		const float RidgeLength = House.bGableFront ? House.Depth : House.Width;
		AddRoof(House.Finish, Eaves, RidgeDir, HalfSpan, RidgeLength, House.bNear);

		const float EavesZ = float(Eaves.Z);
		const float RidgeZ = EavesZ + HalfSpan;
		const FRotator ChimneyTurn = RidgeDir.Rotation();
		for (int32 i = 0; i < House.Chimneys; ++i)
		{
			const float AlongRidge = (i == 0 ? 1.f : -1.f) * RidgeLength * Rng.FRandRange(0.18f, 0.36f);
			const float AcrossRidge = Rng.FRand() < 0.5f ? 0.f : HalfSpan * Rng.FRandRange(-0.4f, 0.4f);
			const float Top = FMath::Min(RidgeZ + Rng.FRandRange(45.f, 110.f), House.MaxTopZ);
			// It has to clear the roof where it stands, or it is left out.
			if (Top < RidgeZ - FMath::Abs(AcrossRidge) + 45.f)
			{
				continue;
			}
			const FVector Foot = Eaves + RidgeDir * AlongRidge + Across * AcrossRidge;
			Kits.Add(EShape::Cube, EKit::Brick, FVector(Foot.X, Foot.Y, (EavesZ + Top) * 0.5f), FVector(52.f, 66.f, Top - EavesZ), ChimneyTurn);
			if (House.bNear)
			{
				Kits.Add(EShape::Cube, EKit::Stone, FVector(Foot.X, Foot.Y, Top + 4.f), FVector(62.f, 76.f, 8.f), ChimneyTurn);
				const int32 Pots = Rng.RandRange(1, 3);
				for (int32 Pot = 0; Pot < Pots; ++Pot)
				{
					const FVector At = Foot + Across * ((Pot - (Pots - 1) * 0.5f) * 20.f);
					Kits.Add(EShape::Cylinder, EKit::Iron, FVector(At.X, At.Y, Top + 23.f), FVector(15.f, 15.f, 30.f));
				}
			}
		}

		AddFacadeWindows(House, MakeFacade(House.Base + Fwd * (House.Depth * 0.5f), House.Yaw), true, HalfSpan);
		if (House.bBackWindows)
		{
			AddFacadeWindows(House, MakeFacade(House.Base - Fwd * (House.Depth * 0.5f), House.Yaw + 180.f), false, HalfSpan);
		}
	}

	/** Fits a house's storeys and roof under MaxTopZ, narrowing it if even one storey won't fit. False if nothing fits. */
	bool FitHouse(FInhHouse& House, float GroundZ, int32 MaxStoreys)
	{
		// Up to the top of the ridge tiles.
		const float Room = House.MaxTopZ - GroundZ - 30.f;
		auto Span = [&House]() { return (House.bGableFront ? House.Width : House.Depth) * 0.5f; };
		if (Room - Span() < 240.f)
		{
			House.bGableFront = House.Width <= House.Depth;
		}
		if (Room - Span() < 240.f)
		{
			const float MaxSpan = Room - 240.f;
			if (MaxSpan < 150.f)
			{
				return false;
			}
			(House.bGableFront ? House.Width : House.Depth) = MaxSpan * 2.f;
		}
		const int32 Storeys = FMath::Clamp(FMath::FloorToInt((Room - Span() - 30.f) / Storey), 1, MaxStoreys);
		House.Height = FMath::Min(Storeys * Storey + Rng.FRandRange(20.f, 60.f), Room - Span());
		return true;
	}

	// --- the town ---------------------------------------------------------------------------------------------------

	void BuildGround()
	{
		using namespace InhLayout;
		// Cobbles under the whole town, out to the quay edge; the quay wall drops from the street to below the waterline.
		const float GroundMinX = -6000.f;
		const float GroundMaxX = QuayEdgeX - 20.f;
		Kits.Add(EShape::Cube, EKit::Cobble, FVector((GroundMinX + GroundMaxX) * 0.5f, 0.f, StreetZ - 15.f), FVector(GroundMaxX - GroundMinX, 12000.f, 30.f));
		const float WallBottom = SeaLevel - 180.f;
		Kits.Add(EShape::Cube, EKit::Stone, FVector(QuayEdgeX - 150.f, 0.f, (WallBottom + StreetZ - 1.f) * 0.5f), FVector(300.f, 12000.f, StreetZ - 1.f - WallBottom));
	}

	void BuildHills()
	{
		using namespace InhLayout;
		const float Sea = SeaLevel;
		const float Shore = FarShoreX;
		// The far shore: hills 30 to 80 m high whose foot meets the water near FarShoreX, low where the harbour mouth
		// opens to the dawn (north of the lighthouse) and high behind. Seen from our window the mouth is the gap about
		// 12 to 22 degrees right of straight out, where the sun comes up.
		Hills.Add({ FVector(Shore + 8575.f, -14750.f, Sea - 3360.f), FVector(9100.f, 7700.f, 9100.f) });
		Hills.Add({ FVector(Shore + 7350.f, -5650.f, Sea - 2660.f), FVector(8050.f, 7000.f, 7000.f) });
		Hills.Add({ FVector(Shore + 6300.f, 475.f, Sea - 1610.f), FVector(6650.f, 4025.f, 4725.f) });
		Hills.Add({ FVector(Shore + 7700.f, 15000.f, Sea - 3010.f), FVector(8750.f, 5950.f, 8050.f) });
		Hills.Add({ FVector(Shore + 11900.f, 22350.f, Sea - 3710.f), FVector(10500.f, 8750.f, 9800.f) });
		Hills.Add({ FVector(Shore + 18550.f, -9150.f, Sea - 6510.f), FVector(12250.f, 12250.f, 14700.f) });
		Hills.Add({ FVector(Shore + 16800.f, -22800.f, Sea - 4200.f), FVector(11200.f, 9100.f, 9100.f) });
		// Rocky headlands past both ends of the quay, closing the bay.
		Hills.Add({ FVector(QuayEdgeX - 700.f, StreetMinY - 1300.f, Sea - 560.f), FVector(2800.f, 1100.f, 1500.f) });
		Hills.Add({ FVector(QuayEdgeX - 700.f, StreetMaxY + 1300.f, Sea - 560.f), FVector(2800.f, 1100.f, 1500.f) });
		Hills.Add({ FVector(3300.f, StreetMinY - 2700.f, Sea - 760.f), FVector(2200.f, 1400.f, 1500.f) });
		Hills.Add({ FVector(3300.f, StreetMaxY + 2700.f, Sea - 760.f), FVector(2200.f, 1400.f, 1500.f) });
		// The hill our side of town is built against, seen over the room's left wall.
		Hills.Add({ FVector(-2600.f, StreetMinY - 1600.f, Sea - 1060.f), FVector(4000.f, 3000.f, 2400.f) });

		for (const FInhHill& Hill : Hills)
		{
			Kits.Add(EShape::Sphere, EKit::Rock, Hill.Center, Hill.Radii * 2.0);
		}
		// Far and huge: their shadows would cost more than they show.
		if (UInstancedStaticMeshComponent* HillSet = Kits.Get(EShape::Sphere, EKit::Rock))
		{
			HillSet->SetCastShadow(false);
		}
	}

	void BuildOurBuilding()
	{
		using namespace InhLayout;
		const FVector Up = FVector::UpVector;
		const float Back = -float(RoomHalfExtent.X);
		const float Front = FacadeX;
		const float Width = OurMaxY - OurMinY;
		// The room's floor slab is 10 cm thick; the fish market fills the storey below it, down to the street.
		const float MarketTop = -10.f;
		Kits.Add(EShape::Cube, EKit::Plaster, FVector((Back + Front) * 0.5f, 0.f, (StreetZ + MarketTop) * 0.5f), FVector(Front - Back, Width, MarketTop - StreetZ));

		// Plinth, a string course at the room's floor, and the cornice where the room's walls stop.
		Kits.Add(EShape::Cube, EKit::Stone, FVector(Front + 3.f, 0.f, StreetZ + 25.f), FVector(6.f, Width, 50.f));
		Kits.Add(EShape::Cube, EKit::Stone, FVector((Back + Front) * 0.5f, OurMaxY + 3.f, StreetZ + 25.f), FVector(Front - Back, 6.f, 50.f));
		Kits.Add(EShape::Cube, EKit::Stone, FVector(Front + 5.f, 0.f, MarketTop + 1.f), FVector(10.f, Width + 4.f, 14.f));
		Kits.Add(EShape::Cube, EKit::Stone, FVector(Front + 9.f, 0.f, RoomTopZ - 5.f), FVector(18.f, Width + 4.f, 14.f));
		Kits.Add(EShape::Cylinder, EKit::Iron, FVector(Front + 9.f, OurMinY + 14.f, (StreetZ + RoomTopZ) * 0.5f), FVector(10.f, 10.f, RoomTopZ - StreetZ));

		// Our window from outside: a stone sill and lintel, and its shutters folded back against the wall.
		const float WinMidY = (WindowMinY + WindowMaxY) * 0.5f;
		const float WinW = WindowMaxY - WindowMinY;
		const float WinH = WindowMaxZ - WindowMinZ;
		Kits.Add(EShape::Cube, EKit::Stone, FVector(Front + 8.f, WinMidY, WindowMinZ - 6.f), FVector(16.f, WinW + 16.f, 8.f));
		Kits.Add(EShape::Cube, EKit::Stone, FVector(Front + 4.f, WinMidY, WindowMaxZ + 8.f), FVector(8.f, WinW + 24.f, 14.f));
		Kits.Add(EShape::Cube, EKit::WoodDark, FVector(Front + 2.f, WindowMinY - 41.f, WindowMinZ + WinH * 0.5f), FVector(4.f, 70.f, WinH - 4.f));
		Kits.Add(EShape::Cube, EKit::WoodDark, FVector(Front + 2.f, WindowMaxY + 41.f, WindowMinZ + WinH * 0.5f), FVector(4.f, 70.f, WinH - 4.f));

		// The market's double doors under a stone lintel, its sign above.
		const float DoorMidY = float(WindowCenter.Y) - 60.f;
		const float DoorW = 250.f;
		const float DoorH = 245.f;
		Kits.Add(EShape::Cube, EKit::WoodDark, FVector(Front + 2.f, DoorMidY, StreetZ + DoorH * 0.5f), FVector(4.f, DoorW, DoorH));
		Kits.Add(EShape::Cube, EKit::Iron, FVector(Front + 4.5f, DoorMidY, StreetZ + DoorH * 0.5f), FVector(2.f, 3.f, DoorH));
		for (const float Side : { -1.f, 1.f })
		{
			Kits.Add(EShape::Cube, EKit::Stone, FVector(Front + 4.f, DoorMidY + Side * (DoorW * 0.5f + 7.f), StreetZ + (DoorH + 10.f) * 0.5f), FVector(8.f, 14.f, DoorH + 10.f));
		}
		Kits.Add(EShape::Cube, EKit::Stone, FVector(Front + 4.f, DoorMidY, StreetZ + DoorH + 11.f), FVector(8.f, DoorW + 28.f, 22.f));
		Kits.Add(EShape::Cube, EKit::WoodDark, FVector(Front + 2.f, DoorMidY, StreetZ + DoorH + 70.f), FVector(4.f, DoorW + 20.f, 58.f));
		Kits.Add(EShape::Cube, EKit::Wood, FVector(Front + 3.f, DoorMidY, StreetZ + DoorH + 70.f), FVector(6.f, DoorW + 10.f, 50.f));

		// The fishmonger works late behind the shop windows.
		const FInhFacade Street = MakeFacade(FVector(Front, 0.f, StreetZ), 0.f);
		AddWindow(Street, OurMinY + 70.f, 150.f, 70.f, 150.f, true, EHours::LateShop, 1.f, true);
		AddWindow(Street, OurMaxY - 110.f, 150.f, 110.f, 150.f, true, EHours::LateShop, 1.f, true);
		// An awning over the right-hand window.
		Kits.Add(EShape::Cube, EKit::Fabric, FVector(Front + 60.f, OurMaxY - 110.f, StreetZ + 245.f), FVector(120.f, 150.f, 4.f), FRotator(-16.f, 0.f, 0.f));

		// A lamp on a bracket between the doors and the left-hand window.
		const FVector Lamp(Front + 55.f, DoorMidY - DoorW * 0.5f - 24.f, StreetZ + 210.f);
		Kits.Add(EShape::Cube, EKit::Iron, FVector(Front + 28.f, Lamp.Y, Lamp.Z + 58.f), FVector(56.f, 4.f, 4.f));
		Kits.Add(EShape::Cube, EKit::Iron, Lamp + Up * 50.f, FVector(3.f, 3.f, 16.f));
		AddLantern(Lamp, 24.f, 38.f);
		AddPointLight(Lamp + Up * 20.f);

		// The sides seen from the room's orbit: a back window, and one on the alley.
		const FInhFacade BackWall = MakeFacade(FVector(Back, 0.f, StreetZ), 180.f);
		AddWindow(BackWall, -150.f, 160.f, 80.f, 130.f, true, EHours::Household, 0.5f, false);
		AddWindow(BackWall, 150.f, 160.f, 80.f, 130.f, true, EHours::Household, 0.5f, false);
		const FInhFacade Alley = MakeFacade(FVector((Back + Front) * 0.5f, OurMaxY, StreetZ), 90.f);
		AddWindow(Alley, 200.f, 170.f, 80.f, 130.f, true, EHours::Household, 0.8f, false);
		AddWindow(Alley, -150.f, 170.f, 80.f, 130.f, true, EHours::Household, 0.5f, false);
	}

	/** A terrace of houses along the street on one side of ours (-1 towards StreetMinY, +1 towards StreetMaxY). */
	void BuildStreetRow(float Side)
	{
		using namespace InhLayout;
		const float Limit = Side < 0.f ? StreetMinY : StreetMaxY;
		// A slit beside the room's left wall keeps the neighbour's eaves out of the room; a narrow alley on the open side.
		float Edge = Side < 0.f ? OurMinY - 32.f : OurMaxY + 120.f;
		int32 Index = 0;
		while (Side * (Limit - Edge) > 300.f)
		{
			FInhHouse House;
			House.Width = FMath::Min(Rng.FRandRange(360.f, 620.f), Side * (Limit - Edge));
			House.Depth = Rng.FRandRange(560.f, 760.f);
			House.bGableFront = Rng.FRand() < 0.6f;
			House.Finish = StreetFinish(Rng);
			House.Chimneys = Rng.FRand() < 0.35f ? 2 : 1;
			House.bBackWindows = true;
			int32 MaxStoreys = Rng.RandRange(2, 3);
			if (Side > 0.f)
			{
				// The orbit camera looks into the room across these roofs, so they stay under a line rising away from
				// our building (checked against every orbit pose the camera allows).
				House.MaxTopZ = FMath::Min(RoofCapZ, StreetZ + 400.f + 0.32f * (Edge - OurMaxY));
			}
			else if (Index == 0)
			{
				// The tall neighbour that frames the room from behind its left wall.
				House.Width = FMath::Min(520.f, Side * (Limit - Edge));
				House.bGableFront = true;
				House.Finish = EKit::Brick;
				MaxStoreys = 3;
			}
			if (!FitHouse(House, StreetZ, MaxStoreys))
			{
				Edge += Side * 200.f;
				continue;
			}
			const float FrontX = FacadeX + Rng.FRandRange(-4.f, 4.f);
			House.Base = FVector(FrontX - House.Depth * 0.5f, Edge + Side * House.Width * 0.5f, StreetZ);
			AddHouse(House);
			Edge += Side * (House.Width + (Rng.FRand() < 0.2f ? Rng.FRandRange(80.f, 140.f) : 3.f));
			++Index;
		}
	}

	/** A back street behind the south row: more roofs and chimneys climbing behind the room's left wall. */
	void BuildBackRow()
	{
		using namespace InhLayout;
		// The orbit camera never goes this far towards StreetMinY, so these are free to be tall.
		const float LaneX = -float(RoomHalfExtent.X) - 150.f;
		float Edge = OurMinY - 140.f;
		while (Edge - StreetMinY > 300.f)
		{
			FInhHouse House;
			House.Yaw = 180.f;
			House.Width = FMath::Min(Rng.FRandRange(380.f, 600.f), Edge - StreetMinY);
			House.Depth = Rng.FRandRange(520.f, 700.f);
			House.bGableFront = Rng.FRand() < 0.5f;
			House.Finish = StreetFinish(Rng);
			House.Chimneys = Rng.FRand() < 0.5f ? 2 : 1;
			House.bBackWindows = true;
			if (FitHouse(House, StreetZ, Rng.RandRange(2, 3)))
			{
				House.Base = FVector(LaneX - House.Depth * 0.5f - Rng.FRandRange(0.f, 40.f), Edge - House.Width * 0.5f, StreetZ);
				AddHouse(House);
			}
			Edge -= House.Width + (Rng.FRand() < 0.25f ? Rng.FRandRange(90.f, 160.f) : 3.f);
		}
	}

	void BuildQuay()
	{
		using namespace InhLayout;
		// Coping stones along the quay edge, a little proud of the cobbles, broken for the water steps and the pier.
		const float PierHalfWidth = 110.f;
		const FVector2D Gaps[] = {
			FVector2D(float(WaterSteps.Y) - 90.f, float(WaterSteps.Y) + 300.f),
			FVector2D(float(PierStart.Y) - PierHalfWidth - 5.f, float(PierStart.Y) + PierHalfWidth + 5.f),
		};
		float Y = StreetMinY;
		while (Y < StreetMaxY - 20.f)
		{
			for (const FVector2D& Gap : Gaps)
			{
				if (Y >= Gap.X - 1.f && Y < Gap.Y)
				{
					Y = float(Gap.Y) + 1.5f;
				}
			}
			float Y1 = FMath::Min(Y + Rng.FRandRange(80.f, 140.f), StreetMaxY);
			for (const FVector2D& Gap : Gaps)
			{
				if (Y < Gap.X && Y1 > Gap.X)
				{
					Y1 = float(Gap.X) - 1.5f;
				}
			}
			if (Y1 - Y > 20.f)
			{
				Kits.Add(EShape::Cube, EKit::Stone, FVector(QuayEdgeX - 2.5f, (Y + Y1) * 0.5f, StreetZ + Rng.FRandRange(-0.6f, 0.6f)),
					FVector(35.f, Y1 - Y - 1.5f, 16.f), FRotator(0.f, Rng.FRandRange(-0.4f, 0.4f), 0.f));
			}
			Y = Y1 + 1.5f;
		}

		// Weed and wet stone at the tide line.
		Kits.Add(EShape::Cube, EKit::Algae, FVector(QuayEdgeX + 1.5f, 0.f, SeaLevel - 15.5f), FVector(3.f, 12000.f, 59.f));

		// Iron bollards for the boats' lines.
		for (float BollardY = StreetMinY + 250.f; BollardY < StreetMaxY - 100.f; BollardY += 450.f)
		{
			const bool bClear = FMath::Abs(BollardY - float(WaterSteps.Y)) > 260.f && FMath::Abs(BollardY - float(PierStart.Y)) > 180.f;
			if (bClear)
			{
				Kits.Add(EShape::Cylinder, EKit::Iron, FVector(QuayEdgeX - 40.f, BollardY, StreetZ + 23.f), FVector(26.f, 26.f, 46.f));
				Kits.Add(EShape::Cylinder, EKit::Iron, FVector(QuayEdgeX - 40.f, BollardY, StreetZ + 48.f), FVector(32.f, 32.f, 8.f));
			}
		}

		// A ladder down the quay wall where the rowboats tie up.
		const float LadderY = float(WaterSteps.Y) - 1150.f;
		const float LadderBottom = SeaLevel - 60.f;
		const float LadderTop = StreetZ + 60.f;
		for (const float Side : { -1.f, 1.f })
		{
			Kits.Add(EShape::Cube, EKit::Iron, FVector(QuayEdgeX + 3.f, LadderY + Side * 22.f, (LadderBottom + LadderTop) * 0.5f), FVector(4.f, 4.f, LadderTop - LadderBottom));
		}
		for (float Z = LadderBottom + 20.f; Z < StreetZ; Z += 30.f)
		{
			Kits.Add(EShape::Cube, EKit::Iron, FVector(QuayEdgeX + 3.f, LadderY, Z), FVector(4.f, 44.f, 3.f));
		}

		// A pavement along the house fronts, a step up from the cobbles.
		Kits.Add(EShape::Cube, EKit::Stone, FVector(FacadeX + 56.f, 0.f, StreetZ - 4.f), FVector(108.f, StreetMaxY - StreetMinY, 24.f));
	}

	/** Stone steps down to the water right below our window, a landing at the bottom where Mara stands. */
	void BuildSteps()
	{
		using namespace InhLayout;
		const float WallBottom = SeaLevel - 180.f;
		const float LandingTop = float(MaraPoint.Z);
		const float OuterX = QuayEdgeX + 80.f;
		const float MidX = (QuayEdgeX + OuterX) * 0.5f;
		const float DepthX = OuterX - QuayEdgeX;
		const float LandingMinY = float(WaterSteps.Y) - 85.f;
		const float LandingMaxY = float(WaterSteps.Y) + 85.f;
		Kits.Add(EShape::Cube, EKit::Stone, FVector(MidX, float(WaterSteps.Y), (LandingTop + WallBottom) * 0.5f), FVector(DepthX, LandingMaxY - LandingMinY, LandingTop - WallBottom));

		// The flight climbs along the quay wall to street level.
		const float Tread = 30.f;
		const int32 NumSteps = FMath::Max(1, FMath::RoundToInt((StreetZ - LandingTop) / 16.f));
		const float Rise = (StreetZ - LandingTop) / NumSteps;
		for (int32 i = 1; i <= NumSteps; ++i)
		{
			const float Top = LandingTop + Rise * i;
			const float Y0 = LandingMaxY + Tread * (i - 1);
			Kits.Add(EShape::Cube, EKit::Stone, FVector(MidX, Y0 + Tread * 0.5f, (Top + WallBottom) * 0.5f), FVector(DepthX, Tread, Top - WallBottom));
		}
		StepsMinY = LandingMinY;
		StepsMaxY = LandingMaxY + Tread * NumSteps;
		Kits.Add(EShape::Cube, EKit::Algae, FVector(OuterX + 1.5f, (StepsMinY + StepsMaxY) * 0.5f, (SeaLevel - 45.f + LandingTop - 1.f) * 0.5f),
			FVector(3.f, StepsMaxY - StepsMinY, LandingTop - 1.f - (SeaLevel - 45.f)));

		// An iron handrail up the outer edge of the flight.
		const float RailX = OuterX - 6.f;
		const float RailHeight = 90.f;
		for (const int32 Step : { 1, (NumSteps + 1) / 2, NumSteps })
		{
			const float Foot = LandingTop + Rise * Step;
			Kits.Add(EShape::Cylinder, EKit::Iron, FVector(RailX, LandingMaxY + Tread * (Step - 0.5f), Foot + RailHeight * 0.5f), FVector(5.f, 5.f, RailHeight));
		}
		const FVector RailA(RailX, LandingMaxY + Tread * 0.5f, LandingTop + Rise + RailHeight);
		const FVector RailB(RailX, LandingMaxY + Tread * (NumSteps - 0.5f), LandingTop + Rise * NumSteps + RailHeight);
		Kits.Add(EShape::Cube, EKit::Iron, (RailA + RailB) * 0.5f, FVector(float(FVector::Dist(RailA, RailB)) + 5.f, 5.f, 5.f),
			FRotationMatrix::MakeFromX(RailB - RailA).Rotator());
	}

	void BuildPier()
	{
		using namespace InhLayout;
		const float DeckTop = float(PierStart.Z);
		const float HalfWidth = 110.f;
		const float Y = float(PierStart.Y);
		const float X0 = float(PierStart.X) + 2.f;
		const float X1 = float(PierEnd.X);

		// Weathered planks, each a little out of true.
		for (float X = X0; X + 24.f <= X1 + 0.1f; X += 26.f)
		{
			Kits.Add(EShape::Cube, EKit::Wood, FVector(X + 12.f, Y + Rng.FRandRange(-2.f, 2.f), DeckTop - 3.5f + Rng.FRandRange(-0.5f, 0.3f)),
				FVector(24.f, HalfWidth * 2.f + Rng.FRandRange(-5.f, 3.f), 7.f), FRotator(0.f, Rng.FRandRange(-0.6f, 0.6f), 0.f));
		}
		for (const float Offset : { -90.f, 0.f, 90.f })
		{
			Kits.Add(EShape::Cube, EKit::WoodDark, FVector((X0 + X1) * 0.5f, Y + Offset, DeckTop - 16.f), FVector(X1 - X0, 14.f, 18.f));
		}

		// Piles in pairs with a beam across each, the end pair standing tall for mooring lines.
		const float PileBottom = SeaLevel - 180.f;
		TArray<float> PileXs;
		for (float X = X0 + 58.f; X < X1 - 60.f; X += 180.f)
		{
			PileXs.Add(X);
		}
		PileXs.Add(X1 - 10.f);
		for (int32 i = 0; i < PileXs.Num(); ++i)
		{
			const bool bEnd = i == PileXs.Num() - 1;
			const float Top = bEnd ? DeckTop + 90.f : DeckTop + 2.f;
			for (const float Side : { -1.f, 1.f })
			{
				Kits.Add(EShape::Cylinder, EKit::WoodDark, FVector(PileXs[i], Y + Side * (HalfWidth - 8.f), (PileBottom + Top) * 0.5f), FVector(30.f, 30.f, Top - PileBottom));
			}
			Kits.Add(EShape::Cube, EKit::WoodDark, FVector(PileXs[i], Y, DeckTop - 32.f), FVector(16.f, HalfWidth * 2.f + 10.f, 16.f));
		}
		// Diagonal bracing between the piles on both sides.
		for (int32 i = 0; i + 1 < PileXs.Num(); ++i)
		{
			for (const float Side : { -1.f, 1.f })
			{
				const FVector A(PileXs[i], Y + Side * (HalfWidth + 8.f), DeckTop - 45.f);
				const FVector B(PileXs[i + 1], Y + Side * (HalfWidth + 8.f), SeaLevel - 30.f);
				Kits.Add(EShape::Cube, EKit::WoodDark, (A + B) * 0.5f, FVector(float(FVector::Dist(A, B)), 8.f, 14.f), FRotationMatrix::MakeFromX(B - A).Rotator());
			}
		}

		// A lamp at the pier end, and gear left out on the boards.
		AddStreetLamp(FVector(X1 - 30.f, Y - HalfWidth + 14.f, DeckTop), 230.f, false);
		AddRope(FVector(X1 - 70.f, Y + 60.f, DeckTop), 56.f);
		AddCrate(FVector(X1 - 160.f, Y - 55.f, DeckTop), FVector(70.f, 50.f, 40.f), 14.f);
	}

	void AddCrate(const FVector& Bottom, const FVector& Size, float Yaw)
	{
		if (CrateArt)
		{
			CrateArt.Add(Bottom, Size, Yaw);
			return;
		}
		Kits.Add(EShape::Cube, EKit::Wood, Bottom + FVector(0.f, 0.f, Size.Z * 0.5f), Size, FRotator(0.f, Yaw, 0.f));
	}

	void AddBarrel(const FVector& Bottom, float Diameter, float Height)
	{
		// Drawn either way, so the rest of the town comes out the same with or without the art installed.
		const float Yaw = Rng.FRandRange(0.f, 360.f);
		if (BarrelArt)
		{
			BarrelArt.Add(Bottom, FVector(Diameter, Diameter, Height), Yaw);
			return;
		}
		Kits.Add(EShape::Cylinder, EKit::Wood, Bottom + FVector(0.f, 0.f, Height * 0.5f), FVector(Diameter, Diameter, Height));
		for (const float At : { 0.2f, 0.8f })
		{
			Kits.Add(EShape::Cylinder, EKit::Iron, Bottom + FVector(0.f, 0.f, Height * At), FVector(Diameter + 2.f, Diameter + 2.f, 4.f));
		}
	}

	void AddRope(const FVector& Bottom, float Diameter)
	{
		const float Yaw = Rng.FRandRange(0.f, 360.f);
		if (RopeArt)
		{
			RopeArt.Add(Bottom, FVector(Diameter, Diameter, Diameter * 0.4f), Yaw);
			return;
		}
		Kits.Add(EShape::Cylinder, EKit::Linen, Bottom + FVector(0.f, 0.f, 6.f), FVector(Diameter, Diameter, 12.f));
		Kits.Add(EShape::Cylinder, EKit::Linen, Bottom + FVector(0.f, 0.f, 15.f), FVector(Diameter * 0.7f, Diameter * 0.7f, 8.f));
	}

	/** A bench along the quay, facing the water (+X). */
	void AddBench(const FVector& Bottom)
	{
		if (BenchArt)
		{
			BenchArt.Add(Bottom, FVector(60.f, 170.f, 90.f), 0.f);
			return;
		}
		Kits.Add(EShape::Cube, EKit::Wood, Bottom + FVector(0.f, 0.f, 44.f), FVector(45.f, 170.f, 6.f));
		Kits.Add(EShape::Cube, EKit::Wood, Bottom + FVector(-20.f, 0.f, 72.f), FVector(6.f, 170.f, 34.f), FRotator(-8.f, 0.f, 0.f));
		for (const float LegX : { -18.f, 18.f })
		{
			for (const float LegY : { -75.f, 75.f })
			{
				Kits.Add(EShape::Cube, EKit::WoodDark, Bottom + FVector(LegX, LegY, 22.f), FVector(6.f, 6.f, 44.f));
			}
		}
	}

	void BuildQuayProps()
	{
		using namespace InhLayout;
		const float Ground = StreetZ;
		const FVector CrateSize(70.f, 50.f, 40.f);
		const float StepsY = float(WaterSteps.Y);

		// Fish crates stacked on the quay below our window, waiting for the morning boats.
		const FVector Stack(QuayEdgeX - 75.f, StepsY - 600.f, Ground);
		AddCrate(Stack, CrateSize, 3.f);
		AddCrate(Stack + FVector(0.f, 53.f, 0.f), CrateSize, -2.f);
		AddCrate(Stack + FVector(-2.f, 26.f, 40.f), CrateSize, 6.f);
		AddCrate(Stack + FVector(4.f, -62.f, 0.f), CrateSize, 12.f);
		// Fish boxes by the market, and more crates near the pier.
		const FVector Boxes(FacadeX + 45.f, OurMaxY - 45.f, Ground + 8.f);
		AddCrate(Boxes, FVector(60.f, 42.f, 22.f), 0.f);
		AddCrate(Boxes + FVector(0.f, 0.f, 22.f), FVector(60.f, 42.f, 22.f), 4.f);
		const FVector PierStack(QuayEdgeX - 90.f, float(PierStart.Y) + 330.f, Ground);
		AddCrate(PierStack, CrateSize, 20.f);
		AddCrate(PierStack + FVector(0.f, 0.f, 40.f), CrateSize, 26.f);
		AddCrate(PierStack + FVector(-8.f, 60.f, 0.f), CrateSize, 8.f);

		// Barrels on the quay and against a house front.
		const FVector Barrels(QuayEdgeX - 60.f, StepsY - 1520.f, Ground);
		AddBarrel(Barrels, 58.f, 88.f);
		AddBarrel(Barrels + FVector(-8.f, 62.f, 0.f), 58.f, 88.f);
		AddBarrel(Barrels + FVector(-50.f, 28.f, 0.f), 58.f, 88.f);
		AddBarrel(FVector(FacadeX + 45.f, StepsY - 520.f, Ground + 8.f), 58.f, 88.f);
		AddBarrel(FVector(FacadeX + 48.f, StepsY - 585.f, Ground + 8.f), 58.f, 88.f);

		// Coils of rope by the bollards, benches facing the water.
		AddRope(FVector(QuayEdgeX - 100.f, StepsY - 830.f, Ground), 60.f);
		AddRope(FVector(QuayEdgeX - 80.f, float(PierStart.Y) + 680.f, Ground), 60.f);
		AddRope(FVector(QuayEdgeX - 105.f, StreetMinY + 700.f, Ground), 60.f);
		AddBench(FVector(QuayEdgeX - 80.f, StepsY - 1100.f, Ground));
		AddBench(FVector(QuayEdgeX - 80.f, StreetMaxY - 900.f, Ground));

		// Lamps in a row just in from the quay edge (clear of the bollards), and kept away from the window: the dawn
		// camera hangs over the street there. Two of them, and the market's lamp, really light the cobbles.
		const float LampX = QuayEdgeX - 110.f;
		AddStreetLamp(FVector(LampX, StreetMinY + 400.f, Ground), 320.f, false);
		AddStreetLamp(FVector(LampX, StepsY - 1300.f, Ground), 320.f, true);
		AddStreetLamp(FVector(LampX, StepsY - 450.f, Ground), 320.f, false);
		AddStreetLamp(FVector(LampX, float(PierStart.Y) + 280.f, Ground), 320.f, true);
		AddStreetLamp(FVector(LampX, StreetMaxY - 1200.f, Ground), 320.f, false);
		AddStreetLamp(FVector(LampX, StreetMaxY - 450.f, Ground), 320.f, false);
	}

	void BuildSea()
	{
		using namespace InhLayout;
		const FLinearColor DeepSea(0.010f, 0.032f, 0.038f);
		const FName WaterKey(TEXT("Water"));
		// One finely divided sheet over the bay (AddSurface swaps in the art pipeline's wave grid), and plain sheets
		// round it out to the horizon, edge to edge so translucent water never doubles up.
		const float NearMinX = QuayEdgeX - 50.f;
		const float NearMaxX = QuayEdgeX + 9950.f;
		const float NearHalfY = 6000.f;
		H.Sea = InhShapes::AddSurface(&H, H.Root, Plane(), FVector((NearMinX + NearMaxX) * 0.5f, 0.f, SeaLevel), FVector(NearMaxX - NearMinX, NearHalfY * 2.f, 100.f),
			WaterKey, DeepSea, FRotator::ZeroRotator, false);
		UInstancedStaticMeshComponent* Outer = InhShapes::AddInstanced(&H, H.Root, Plane(), WaterKey, DeepSea, false);
		const float FarX = NearMinX + 80000.f;
		const float FarHalfY = 40000.f;
		if (Outer)
		{
			Outer->AddInstance(FTransform(FRotator::ZeroRotator, FVector((NearMaxX + FarX) * 0.5f, 0.f, SeaLevel), FVector((FarX - NearMaxX) / 100.f, FarHalfY / 50.f, 1.f)));
			for (const float Side : { -1.f, 1.f })
			{
				Outer->AddInstance(FTransform(FRotator::ZeroRotator, FVector((NearMinX + NearMaxX) * 0.5f, Side * (NearHalfY + FarHalfY) * 0.5f, SeaLevel),
					FVector((NearMaxX - NearMinX) / 100.f, (FarHalfY - NearHalfY) / 100.f, 1.f)));
			}
		}
		H.OuterSea = Outer;
		for (UStaticMeshComponent* Water : { H.Sea.Get(), H.OuterSea.Get() })
		{
			if (Water)
			{
				Water->SetCastShadow(false);
			}
		}
		// A dynamic copy of the water so the sea's colour can follow the night.
		if (UMaterialInterface* Water = InhArt::Material(WaterKey))
		{
			H.SeaMaterial = UMaterialInstanceDynamic::Create(Water, &H);
			if (H.Sea)
			{
				H.Sea->SetMaterial(0, H.SeaMaterial);
			}
			if (H.OuterSea)
			{
				H.OuterSea->SetMaterial(0, H.SeaMaterial);
			}
		}
	}

	void BuildLighthouse()
	{
		using namespace InhLayout;
		const FVector Up = FVector::UpVector;
		const FVector Base = LighthouseBase;
		const float BX = float(Base.X);
		const float BY = float(Base.Y);
		const float PlinthTop = SeaLevel + 70.f;
		const float LampZ = float(LighthouseLamp.Z);
		const float GalleryZ = LampZ - 120.f;

		// Armour stones heaped round a round stone plinth.
		for (int32 i = 0; i < 12; ++i)
		{
			const float Angle = FMath::DegreesToRadians(i * 30.f + Rng.FRandRange(-10.f, 10.f));
			const float Radius = Rng.FRandRange(300.f, 400.f);
			Kits.Add(EShape::Cube, EKit::Rock, FVector(BX + FMath::Cos(Angle) * Radius, BY + FMath::Sin(Angle) * Radius, SeaLevel + Rng.FRandRange(-40.f, 30.f)),
				FVector(Rng.FRandRange(140.f, 230.f), Rng.FRandRange(120.f, 200.f), Rng.FRandRange(100.f, 170.f)),
				FRotator(Rng.FRandRange(-25.f, 25.f), Rng.FRandRange(0.f, 360.f), Rng.FRandRange(-25.f, 25.f)));
		}
		Kits.Add(EShape::Cylinder, EKit::Stone, FVector(BX, BY, (PlinthTop + SeaLevel - 150.f) * 0.5f), FVector(620.f, 620.f, PlinthTop - (SeaLevel - 150.f)));

		// A white tower in three tapering drums with stone bands at the joints, up to the gallery.
		const float DrumHeight = (GalleryZ - PlinthTop) / 3.f;
		for (int32 i = 0; i < 3; ++i)
		{
			const float Z0 = PlinthTop + DrumHeight * i;
			const float Diameter = 330.f - 30.f * i;
			Kits.Add(EShape::Cylinder, EKit::Plaster, FVector(BX, BY, Z0 + DrumHeight * 0.5f), FVector(Diameter, Diameter, DrumHeight));
			Kits.Add(EShape::Cylinder, EKit::Stone, FVector(BX, BY, Z0 + DrumHeight - 9.f), FVector(Diameter + 16.f, Diameter + 16.f, 18.f));
		}
		// Its door and stair windows look back at the town, one lit by the keeper's stair lamp.
		const FVector TownSide(-1.f, 0.f, 0.f);
		Kits.Add(EShape::Cube, EKit::WoodDark, FVector(BX - 166.f, BY, PlinthTop + 80.f), FVector(8.f, 80.f, 160.f));
		Kits.Add(EShape::Cube, EKit::Stone, FVector(BX - 167.f, BY, PlinthTop + 168.f), FVector(8.f, 100.f, 16.f));
		Kits.Add(EShape::Plane, EKit::Pane, FVector(BX - 152.f, BY, PlinthTop + DrumHeight * 1.5f), FVector(70.f, 40.f, 1.f), Facing(TownSide));
		AddLitWindow(H.WindowLights, H.Windows, FTransform(Facing(TownSide), FVector(BX - 137.f, BY, PlinthTop + DrumHeight * 2.5f), FVector(0.7f, 0.4f, 1.f)), EHours::AllNight);

		// The gallery with its railing.
		Kits.Add(EShape::Cylinder, EKit::Iron, FVector(BX, BY, GalleryZ + 6.f), FVector(380.f, 380.f, 12.f));
		const float RailRadius = 182.f;
		const float RailHeight = 95.f;
		for (int32 i = 0; i < 12; ++i)
		{
			const float Angle = i * 30.f;
			const FVector Post = FVector(BX, BY, GalleryZ + 12.f + RailHeight * 0.5f) + FRotator(0.f, Angle, 0.f).Vector() * RailRadius;
			Kits.Add(EShape::Cylinder, EKit::Iron, Post, FVector(4.f, 4.f, RailHeight));
			const float Chord = 2.f * RailRadius * FMath::Sin(FMath::DegreesToRadians(15.f));
			const FVector RailMid = FVector(BX, BY, GalleryZ + 12.f + RailHeight) + FRotator(0.f, Angle + 15.f, 0.f).Vector() * (RailRadius * FMath::Cos(FMath::DegreesToRadians(15.f)));
			Kits.Add(EShape::Cube, EKit::Iron, RailMid, FVector(Chord, 4.f, 4.f), FRotator(0.f, Angle + 105.f, 0.f));
		}

		// The lantern room: an iron base, the glowing lens framed by mullions, a cone roof and its vent.
		const float LensHeight = 90.f;
		Kits.Add(EShape::Cylinder, EKit::Iron, FVector(BX, BY, (GalleryZ + 12.f + LampZ - LensHeight * 0.5f) * 0.5f), FVector(190.f, 190.f, LampZ - LensHeight * 0.5f - GalleryZ - 12.f));
		H.Lens = InhShapes::AddEmissive(&H, H.Root, Cylinder(), FVector(BX, BY, LampZ), FVector(140.f, 140.f, LensHeight), LensGlow, LensStrength);
		H.LensMaterial = H.Lens ? Cast<UMaterialInstanceDynamic>(H.Lens->GetMaterial(0)) : nullptr;
		for (int32 i = 0; i < 6; ++i)
		{
			Kits.Add(EShape::Cylinder, EKit::Iron, FVector(BX, BY, LampZ) + FRotator(0.f, i * 60.f + 30.f, 0.f).Vector() * 86.f, FVector(6.f, 6.f, LensHeight + 2.f));
		}
		const float RoofZ = LampZ + LensHeight * 0.5f;
		Kits.Add(EShape::Cone, EKit::Iron, FVector(BX, BY, RoofZ + 30.f), FVector(210.f, 210.f, 60.f));
		Kits.Add(EShape::Cylinder, EKit::Iron, FVector(BX, BY, RoofZ + 66.f), FVector(26.f, 26.f, 16.f));
		Kits.Add(EShape::Cone, EKit::Iron, FVector(BX, BY, RoofZ + 85.f), FVector(20.f, 20.f, 22.f));

		// A fake shaft of light along the beam for phones without volumetric fog, when the fog card art is installed.
		const FName FogKey(TEXT("FogCard"));
		if (UMaterialInterface* Fog = InhArt::Material(FogKey))
		{
			if (UInstancedStaticMeshComponent* Shaft = InhShapes::AddInstanced(&H, H.BeamPivot, Plane(), FogKey, White, false))
			{
				UMaterialInstanceDynamic* ShaftMaterial = UMaterialInstanceDynamic::Create(Fog, Shaft);
				ShaftMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.88f, 0.66f));
				ShaftMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.12f);
				Shaft->SetMaterial(0, ShaftMaterial);
				Shaft->SetCastShadow(false);
				// Crossed cards both ways up (cards are one-sided), tilted down with the beam.
				const float Length = 1800.f;
				const FRotator Tilt(-2.5f, 0.f, 0.f);
				for (const float Roll : { 0.f, 180.f, 90.f, -90.f })
				{
					Shaft->AddInstance(FTransform(FRotator(Tilt.Pitch, 0.f, Roll), Tilt.Vector() * (Length * 0.5f + 60.f), FVector(Length / 100.f, 2.2f, 1.f)));
				}
			}
		}

		// The breakwater runs out to the south-east, away from the harbour mouth: a stone walkway on heaped rock.
		const float WalkYaw = -50.7f;
		const FVector Dir = FRotator(0.f, WalkYaw, 0.f).Vector();
		const FVector Side = FRotator(0.f, WalkYaw + 90.f, 0.f).Vector();
		const float WalkLength = 1500.f;
		for (float D = 330.f; D < WalkLength; D += 155.f)
		{
			Kits.Add(EShape::Cube, EKit::Stone, Base + Dir * (D + 75.f) + Up * (PlinthTop - SeaLevel - 50.f + Rng.FRandRange(-3.f, 3.f)),
				FVector(150.f, 260.f, 100.f), FRotator(0.f, WalkYaw + Rng.FRandRange(-1.5f, 1.5f), 0.f));
		}
		for (float D = 300.f; D < WalkLength + 120.f; D += 85.f)
		{
			for (const float S : { -1.f, 1.f })
			{
				Kits.Add(EShape::Cube, EKit::Rock, Base + Dir * (D + Rng.FRandRange(-20.f, 20.f)) + Side * (S * Rng.FRandRange(140.f, 210.f)) + Up * Rng.FRandRange(-60.f, 20.f),
					FVector(Rng.FRandRange(110.f, 200.f), Rng.FRandRange(100.f, 170.f), Rng.FRandRange(90.f, 150.f)),
					FRotator(Rng.FRandRange(-30.f, 30.f), Rng.FRandRange(0.f, 360.f), Rng.FRandRange(-30.f, 30.f)));
			}
		}

		// The keeper's hut on the walkway, its window lit all night.
		FInhHouse Hut;
		Hut.Base = Base + Dir * 520.f + Up * (PlinthTop - SeaLevel);
		Hut.Yaw = WalkYaw - 90.f;
		Hut.Width = 300.f;
		Hut.Depth = 200.f;
		Hut.Height = 230.f;
		Hut.bGableFront = false;
		Hut.Finish = EKit::Plaster;
		Hut.Chimneys = 1;
		Hut.LitShare = 1.f;
		Hut.Hours = EHours::AllNight;
		AddHouse(Hut);
	}

	void BuildFarShore()
	{
		using namespace InhLayout;
		const float QuayTop = SeaLevel + 50.f;
		// The harbour mouth, north of the lighthouse, where the Ferryman leaves and the dawn comes up: no houses there.
		// It is the stretch of far shore our window sees between the hills (see BuildHills).
		const float MouthMinY = 2200.f;
		const float MouthMaxY = 9800.f;
		const FVector2D Fronts[] = { FVector2D(FarShoreMinY + 4900.f, MouthMinY - 500.f), FVector2D(MouthMaxY + 500.f, FarShoreMaxY - 2100.f) };

		// A stone quay along the far waterfront, a row of houses on it and lamps along its edge. It runs deep enough to
		// carry the houses where the hills only rise further back, so none stand over the water.
		for (const FVector2D& Front : Fronts)
		{
			const float Y0 = float(Front.X);
			const float Y1 = float(Front.Y);
			Kits.Add(EShape::Cube, EKit::Stone, FVector(FarShoreX + 490.f, (Y0 + Y1) * 0.5f, SeaLevel - 10.f), FVector(1100.f, Y1 - Y0, 120.f));
			for (float LampY = Y0 + 150.f; LampY < Y1 - 100.f; LampY += 840.f)
			{
				Kits.Add(EShape::Cylinder, EKit::Iron, FVector(FarShoreX - 20.f, LampY, QuayTop + 130.f), FVector(8.f, 8.f, 260.f));
				AddGlow(FVector(FarShoreX - 20.f, LampY, QuayTop + 275.f), FVector(18.f, 18.f, 26.f));
			}
			float Y = Y0 + 80.f;
			while (Y < Y1 - 300.f)
			{
				FInhHouse House;
				House.bNear = false;
				House.Yaw = 180.f + Rng.FRandRange(-3.f, 3.f);
				House.Width = FMath::Min(Rng.FRandRange(300.f, 520.f), Y1 - Y);
				House.Depth = Rng.FRandRange(400.f, 600.f);
				House.Height = Storey * Rng.RandRange(2, 3) + Rng.FRandRange(10.f, 50.f);
				House.bGableFront = Rng.FRand() < 0.55f;
				House.Finish = FarFinish(Rng);
				House.Chimneys = Rng.FRand() < 0.4f ? 1 : 0;
				House.LitShare = 0.9f;
				House.MaxTopZ = 1.e6f;
				const float FrontX = FarShoreX + 140.f + Rng.FRandRange(0.f, 40.f);
				const float CX = FrontX + House.Depth * 0.5f;
				const float CY = Y + House.Width * 0.5f;
				House.Base = FVector(CX, CY, FMath::Max(QuayTop, LowestGround(CX, CY, House)) - 10.f);
				AddHouse(House);
				Y += House.Width + (Rng.FRand() < 0.2f ? Rng.FRandRange(60.f, 160.f) : 4.f);
			}
		}

		// The rest of the town climbs the hills behind, facing down the slopes towards the bay.
		TArray<FVector> Taken;
		// Thicker by the water and thinning up the slopes, capped to bound the instances and triangles it adds.
		for (int32 Try = 0; Try < 2000 && Taken.Num() < 240; ++Try)
		{
			const float X = FarShoreX + 1300.f + FMath::Pow(Rng.FRand(), 1.4f) * 9100.f;
			const float Y = Rng.FRandRange(FarShoreMinY + 1000.f, FarShoreMaxY - 1000.f);
			const float Size = Rng.FRandRange(0.f, 1.f);
			if (Y > MouthMinY - 900.f && Y < MouthMaxY + 900.f)
			{
				continue;
			}
			const float Z = HillTop(X, Y);
			if (Z < SeaLevel + 70.f || Z > SeaLevel + 5200.f)
			{
				continue;
			}
			bool bFree = true;
			for (const FVector& Other : Taken)
			{
				if (FVector::Dist2D(Other, FVector(X, Y, 0.f)) < 420.f)
				{
					bFree = false;
					break;
				}
			}
			if (!bFree)
			{
				continue;
			}
			Taken.Add(FVector(X, Y, 0.f));

			FInhHouse House;
			House.bNear = false;
			const float SlopeX = HillTop(X + 50.f, Y) - HillTop(X - 50.f, Y);
			const float SlopeY = HillTop(X, Y + 50.f) - HillTop(X, Y - 50.f);
			float Yaw = 180.f;
			if (FMath::Abs(SlopeX) < 1.e4f && FMath::Abs(SlopeY) < 1.e4f)
			{
				const float Downhill = FMath::RadiansToDegrees(FMath::Atan2(-SlopeY, -SlopeX));
				Yaw = 180.f + FMath::Clamp(FMath::FindDeltaAngleDegrees(180.f, Downhill), -35.f, 35.f);
			}
			House.Yaw = Yaw + Rng.FRandRange(-6.f, 6.f);
			House.Width = FMath::Lerp(260.f, 440.f, Size);
			House.Depth = Rng.FRandRange(320.f, 480.f);
			House.Height = Storey * Rng.RandRange(1, 2) + Rng.FRandRange(20.f, 60.f);
			House.bGableFront = Rng.FRand() < 0.5f;
			House.Finish = FarFinish(Rng);
			House.Chimneys = Rng.FRand() < 0.3f ? 1 : 0;
			House.LitShare = 0.85f;
			House.MaxTopZ = 1.e6f;
			// Sunk into its slope from the lowest corner, but never so deep that a corner off the hill drags it down.
			House.Base = FVector(X, Y, FMath::Max(LowestGround(X, Y, House), Z - 250.f) - 40.f);
			AddHouse(House);
		}

		// The church on the hill: a stone tower and dark spire against the sky, its clock face lit.
		const float ChurchX = FarShoreX + 5250.f;
		const float ChurchY = FarShoreMinY + 12550.f;
		const float ChurchZ = FMath::Max(HillTop(ChurchX, ChurchY), SeaLevel + 100.f) - 60.f;
		const float TowerHeight = 1240.f;
		Kits.Add(EShape::Cube, EKit::Stone, FVector(ChurchX, ChurchY, ChurchZ + TowerHeight * 0.5f), FVector(320.f, 320.f, TowerHeight));
		Kits.Add(EShape::Cone, EKit::Iron, FVector(ChurchX, ChurchY, ChurchZ + TowerHeight + 380.f), FVector(340.f, 340.f, 760.f));
		const FVector Bay(-1.f, 0.f, 0.f);
		Kits.Add(EShape::Plane, EKit::Pane, FVector(ChurchX - 161.f, ChurchY, ChurchZ + TowerHeight - 110.f), FVector(130.f, 70.f, 1.f), Facing(Bay));
		AddLitWindow(H.FarWindowLights, H.FarWindows, FTransform(Facing(Bay), FVector(ChurchX - 162.f, ChurchY, ChurchZ + TowerHeight - 330.f), FVector(0.9f, 0.9f, 1.f)), EHours::AllNight);
		FInhHouse Nave;
		Nave.bNear = false;
		Nave.Yaw = 180.f;
		Nave.Width = 560.f;
		Nave.Depth = 1100.f;
		Nave.Height = 650.f;
		Nave.bGableFront = true;
		Nave.Finish = EKit::Stone;
		Nave.Chimneys = 0;
		Nave.LitShare = 0.f;
		Nave.MaxTopZ = 1.e6f;
		Nave.Base = FVector(ChurchX + 160.f + Nave.Depth * 0.5f, ChurchY, ChurchZ);
		AddHouse(Nave);
	}

	/** The lowest hill height under a house's footprint corners (the house sinks into its slope from there). */
	float LowestGround(float X, float Y, const FInhHouse& House) const
	{
		const FVector Fwd = FRotator(0.f, House.Yaw, 0.f).Vector() * (House.Depth * 0.5f);
		const FVector Right = FRotator(0.f, House.Yaw + 90.f, 0.f).Vector() * (House.Width * 0.5f);
		float Lowest = 1.e6f;
		for (const float F : { -1.f, 1.f })
		{
			for (const float R : { -1.f, 1.f })
			{
				const FVector Corner = FVector(X, Y, 0.f) + Fwd * F + Right * R;
				Lowest = FMath::Min(Lowest, HillTop(float(Corner.X), float(Corner.Y)));
			}
		}
		return Lowest;
	}

	void BuildSky()
	{
		using namespace InhLayout;
		const FVector SkyCenter(QuayEdgeX, 0.f, 0.f);
		// Beyond the far hills (they reach about 470 m out), so the lowest stars set behind them instead of hanging in front.
		const float SkyRadius = 80000.f;
		// Stars and the moon need the emissive art: the lit fallback faces away from the moonlight and would hang dark
		// squares and a grey disc in the sky. The stars still draw from the stream, so the rest of the town is unchanged.
		if (H.bEmissiveArt)
		{
			H.BrightStars = InhShapes::AddInstancedEmissive(&H, H.Root, Plane(), StarGlow, BrightStarStrength);
			H.FaintStars = InhShapes::AddInstancedEmissive(&H, H.Root, Plane(), StarGlow, FaintStarStrength);
			H.BrightStarsMaterial = H.BrightStars ? Cast<UMaterialInstanceDynamic>(H.BrightStars->GetMaterial(0)) : nullptr;
			H.FaintStarsMaterial = H.FaintStars ? Cast<UMaterialInstanceDynamic>(H.FaintStars->GetMaterial(0)) : nullptr;
		}
		// Over the bay and the town, where the shots look; more of them low down where the sky is deepest.
		const float LowSin = FMath::Sin(FMath::DegreesToRadians(5.f));
		const float HighSin = FMath::Sin(FMath::DegreesToRadians(78.f));
		for (int32 i = 0; i < 300; ++i)
		{
			const bool bBright = i < 70;
			UInstancedStaticMeshComponent* Set = bBright ? H.BrightStars.Get() : H.FaintStars.Get();
			const float Yaw = Rng.FRandRange(-115.f, 155.f);
			const float Elevation = FMath::RadiansToDegrees(FMath::Asin(FMath::Lerp(LowSin, HighSin, FMath::Pow(Rng.FRand(), 1.2f))));
			// About a tenth of a degree, a few pixels across at the attract's 26 mm: points of light rather than visible squares.
			const float Size = SkyRadius * (bBright ? Rng.FRandRange(0.0015f, 0.0025f) : Rng.FRandRange(0.00085f, 0.00135f));
			const FVector Dir = FRotator(Elevation, Yaw, 0.f).Vector();
			if (Set)
			{
				Set->AddInstance(FTransform(FRotationMatrix::MakeFromZ(-Dir).Rotator(), SkyCenter + Dir * SkyRadius, FVector(Size / 100.f, Size / 100.f, 1.f)));
			}
		}
		// The moon hangs left of the lighthouse as seen from the water.
		if (H.bEmissiveArt)
		{
			const FVector MoonDir = FRotator(InhLayout::MoonElevation, InhLayout::MoonYaw, 0.f).Vector();
			// Just inside the stars, and about 1.8 degrees across.
			const float MoonDistance = SkyRadius - 2000.f;
			const float MoonSize = MoonDistance * 0.032f;
			H.Moon = InhShapes::AddEmissive(&H, H.Root, Cylinder(), SkyCenter + MoonDir * MoonDistance, FVector(MoonSize, MoonSize, 10.f), MoonGlow, MoonStrength,
				FRotationMatrix::MakeFromZ(-MoonDir).Rotator());
			H.MoonMaterial = H.Moon ? Cast<UMaterialInstanceDynamic>(H.Moon->GetMaterial(0)) : nullptr;
		}
	}

	// --- moving things ----------------------------------------------------------------------------------------------

	int32 Moving(EMoving Kind)
	{
		int32& Index = MovingIndex[int32(Kind)];
		if (Index != INDEX_NONE)
		{
			return Index;
		}
		UInstancedStaticMeshComponent* Set = nullptr;
		switch (Kind)
		{
		case EMoving::Hull: Set = InhShapes::AddInstanced(&H, H.Root, Sphere(), FName(TEXT("WoodDark")), WoodDarkFallback, false); break;
		case EMoving::Deck: Set = InhShapes::AddInstanced(&H, H.Root, Sphere(), FName(TEXT("Wood")), WoodFallback, false); break;
		case EMoving::Wood: Set = InhShapes::AddInstanced(&H, H.Root, Cube(), FName(TEXT("Wood")), WoodFallback, false); break;
		case EMoving::Spar: Set = InhShapes::AddInstanced(&H, H.Root, Cylinder(), FName(TEXT("WoodDark")), WoodDarkFallback, false); break;
		case EMoving::Sail: Set = InhShapes::AddInstanced(&H, H.Root, Cube(), FName(TEXT("Linen")), LinenFallback, false); break;
		case EMoving::Lamp: Set = InhShapes::AddInstancedEmissive(&H, H.Root, Cube(), FLinearColor(1.f, 0.78f, 0.5f), 14.f); break;
		case EMoving::ArtRowboat: Set = RowboatMesh ? NewArtInstances(&H, H.Root, RowboatMesh) : nullptr; break;
		// Gulls are flat colours only. Where a packaged build can't instance those (InhShapes::CanInstance) they stay away
		// rather than circle as grid-material shapes; an empty set is allowed for everywhere below.
		case EMoving::GullBody: Set = InhShapes::CanInstance(NAME_None) ? InhShapes::AddInstanced(&H, H.Root, Sphere(), NAME_None, FLinearColor(0.80f, 0.81f, 0.83f), false) : nullptr; break;
		case EMoving::GullWing: Set = InhShapes::CanInstance(NAME_None) ? InhShapes::AddInstanced(&H, H.Root, Cube(), NAME_None, FLinearColor(0.58f, 0.61f, 0.65f), false) : nullptr; break;
		default: break;
		}
		if (Set && (Kind == EMoving::GullBody || Kind == EMoving::GullWing || Kind == EMoving::Lamp))
		{
			Set->SetCastShadow(false);
		}
		H.MovingSets.Add(Set);
		H.MovingTransforms.AddDefaulted();
		Index = H.MovingSets.Num() - 1;
		return Index;
	}

	/** An instance in a moving set at its starting transform; Tick rewrites it from then on. */
	int32 AddMovingInstance(int32 Set, const FTransform& World)
	{
		UInstancedStaticMeshComponent* Component = H.MovingSets[Set];
		if (!Component)
		{
			return INDEX_NONE;
		}
		Component->AddInstance(World);
		return H.MovingTransforms[Set].Add(World);
	}

	void AddBoatPart(EMoving Kind, const FTransform& Local, const FTransform& Boat)
	{
		const int32 Set = Moving(Kind);
		const int32 Instance = AddMovingInstance(Set, Local * Boat);
		if (Instance != INDEX_NONE)
		{
			FInhMovingPart& Part = H.BoatParts.AddDefaulted_GetRef();
			Part.Set = Set;
			Part.Instance = Instance;
			Part.Local = Local;
		}
	}

	void AddBoatPart(EMoving Kind, const FVector& Center, const FVector& Size, const FTransform& Boat, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		AddBoatPart(Kind, FTransform(Rotation, Center, Size / 100.0), Boat);
	}

	/** A boat moored at Mooring (its waterline centre), bow along Yaw. Hulls are half-sunk ellipsoids with a deck inset. */
	void AddBoat(const FVector& Mooring, float Yaw, bool bFishing, bool bLit)
	{
		const int32 BoatIndex = H.Boats.AddDefaulted();
		FInhBoat& Boat = H.Boats[BoatIndex];
		Boat.Mooring = Mooring;
		Boat.Yaw = Yaw;
		Boat.Phase = Rng.FRandRange(0.f, 2.f * UE_PI);
		Boat.Liveliness = bFishing ? 0.7f : 1.15f;
		Boat.FirstPart = H.BoatParts.Num();
		const FTransform At(FRotator(0.f, Yaw, 0.f), Mooring);

		if (bFishing)
		{
			AddBoatPart(EMoving::Hull, FVector(0.f, 0.f, 15.f), FVector(620.f, 210.f, 160.f), At);
			AddBoatPart(EMoving::Deck, FVector(0.f, 0.f, 84.f), FVector(540.f, 172.f, 34.f), At);
			AddBoatPart(EMoving::Wood, FVector(-110.f, 0.f, 140.f), FVector(150.f, 120.f, 90.f), At);
			AddBoatPart(EMoving::Wood, FVector(-110.f, 0.f, 189.f), FVector(166.f, 134.f, 8.f), At);
			AddBoatPart(EMoving::Spar, FVector(90.f, 0.f, 330.f), FVector(14.f, 14.f, 480.f), At);
			AddBoatPart(EMoving::Spar, FVector(-30.f, 0.f, 200.f), FVector(10.f, 10.f, 250.f), At, FRotator(90.f, 0.f, 0.f));
			AddBoatPart(EMoving::Sail, FVector(-30.f, 0.f, 214.f), FVector(230.f, 16.f, 20.f), At);
			if (bLit)
			{
				// A riding light at the masthead and a lamp in the wheelhouse.
				AddBoatPart(EMoving::Lamp, FVector(90.f, 0.f, 576.f), FVector(10.f, 10.f, 12.f), At);
				AddBoatPart(EMoving::Lamp, FVector(-34.f, 0.f, 152.f), FVector(2.f, 34.f, 20.f), At);
			}
		}
		else if (RowboatMesh)
		{
			// The imported rowboat, its keel a little under the water.
			AddBoatPart(EMoving::ArtRowboat, FitArt(RowboatMesh, FVector(400.f, 140.f, 110.f)) * FTransform(FVector(0.f, 0.f, -22.f)), At);
		}
		else
		{
			AddBoatPart(EMoving::Hull, FVector(0.f, 0.f, 8.f), FVector(400.f, 130.f, 95.f), At);
			AddBoatPart(EMoving::Deck, FVector(0.f, 0.f, 46.f), FVector(340.f, 104.f, 28.f), At);
			AddBoatPart(EMoving::Wood, FVector(-60.f, 0.f, 61.f), FVector(14.f, 96.f, 4.f), At);
			AddBoatPart(EMoving::Wood, FVector(70.f, 0.f, 61.f), FVector(14.f, 92.f, 4.f), At);
			AddBoatPart(EMoving::Spar, FVector(10.f, -22.f, 64.5f), FVector(4.f, 4.f, 250.f), At, FRotator(90.f, 4.f, 0.f));
			AddBoatPart(EMoving::Spar, FVector(10.f, 22.f, 64.5f), FVector(4.f, 4.f, 250.f), At, FRotator(90.f, -3.f, 0.f));
		}
		H.Boats[BoatIndex].NumParts = H.BoatParts.Num() - H.Boats[BoatIndex].FirstPart;
	}

	void BuildBoats()
	{
		using namespace InhLayout;
		const float Sea = SeaLevel;
		// Kept clear of the Ferryman's route, the steps, and the camera's low passes over the water.
		AddBoat(FVector(PierEnd.X - 300.f, PierStart.Y + 230.f, Sea), 0.f, true, true);            // alongside the pier
		AddBoat(FVector(QuayEdgeX + 130.f, PierStart.Y + 1000.f, Sea), 92.f, true, false);         // against the quay, north
		AddBoat(FVector(QuayEdgeX + 1150.f, WaterSteps.Y - 750.f, Sea), 35.f, true, true);          // riding at anchor
		AddBoat(FVector(QuayEdgeX + 75.f, PierStart.Y + 410.f, Sea), 88.f, false, false);
		AddBoat(FVector(QuayEdgeX + 75.f, StreetMaxY - 650.f, Sea), 91.f, false, false);
		AddBoat(FVector(QuayEdgeX + 75.f, StreetMinY + 450.f, Sea), 90.f, false, false);
		AddBoat(FVector(QuayEdgeX + 140.f, StreetMinY + 900.f, Sea), 80.f, false, false);
	}

	void BuildGulls()
	{
		using namespace InhLayout;
		struct FFlock { FVector Center; float Radius; int32 Count; float Turn; };
		// Round the lighthouse, over the anchorage, and over the pier: away from the cameras' paths. The lighthouse ring
		// circles the tower itself, clear of its drums, below the gallery and well under the Opening's flight over the lantern.
		const FFlock Flocks[] = {
			{ LighthouseBase + FVector(0.f, 0.f, 800.f), 380.f, 3, 1.f },
			{ FVector(QuayEdgeX + 900.f, WaterSteps.Y - 750.f, StreetZ + 800.f), 420.f, 2, -1.f },
			{ PierStart + FVector(400.f, 680.f, 730.f), 320.f, 2, 1.f },
		};
		const int32 BodySet = Moving(EMoving::GullBody);
		const int32 WingSet = Moving(EMoving::GullWing);
		H.GullBodySet = BodySet;
		H.GullWingSet = WingSet;
		for (const FFlock& Flock : Flocks)
		{
			for (int32 i = 0; i < Flock.Count; ++i)
			{
				FInhGull& Gull = H.Gulls.AddDefaulted_GetRef();
				Gull.Center = Flock.Center + FVector(0.f, 0.f, Rng.FRandRange(-60.f, 60.f));
				Gull.Radius = Flock.Radius * Rng.FRandRange(0.8f, 1.15f);
				Gull.AngularSpeed = Flock.Turn * Rng.FRandRange(380.f, 470.f) / Gull.Radius;
				Gull.Phase = i * 2.1f + Rng.FRandRange(0.f, 0.8f);
				const FTransform Start(Gull.Center + FVector(Gull.Radius, 0.f, 0.f));
				Gull.Body = AddMovingInstance(BodySet, Start);
				Gull.Wings = AddMovingInstance(WingSet, Start);
				AddMovingInstance(WingSet, Start);
			}
		}
	}

	// --- the dawn ---------------------------------------------------------------------------------------------------

	void BuildFerryRoute()
	{
		using namespace InhLayout;
		// A steady line in from the south (the point on it keeps the boat where the Sunrise camera expects it), past
		// the steps on a gentle curve, round the pier end with room to spare and out past the lighthouse to the end.
		// Checked numerically: about 1.8 m clear of the pier, 5 m of the lighthouse, no turn tighter than about 2.8 m.
		H.FerryPoints = {
			FerrymanStart,
			FMath::Lerp(FerrymanStart, FerrymanPassing, 0.7),
			FerrymanPassing,
			PierEnd + FVector(360.f, -120.f, 0.f),
			PierEnd + FVector(420.f, 900.f, 0.f),
			FerrymanEnd,
		};
		for (FVector& Point : H.FerryPoints)
		{
			Point.Z = SeaLevel;
		}
		const int32 Num = H.FerryPoints.Num();
		H.FerryDirections.SetNum(Num);
		for (int32 i = 0; i < Num; ++i)
		{
			const FVector& Prev = H.FerryPoints[FMath::Max(i - 1, 0)];
			const FVector& Next = H.FerryPoints[FMath::Min(i + 1, Num - 1)];
			H.FerryDirections[i] = (Next - Prev).GetSafeNormal2D();
		}
		// Past the steps heading north-north-east, already easing round towards the pier end.
		H.FerryDirections[2] = FRotator(0.f, 66.f, 0.f).Vector();

		H.FerryLut.SetNumZeroed((Num - 1) * (FerryLutSteps + 1));
		H.FerrySegmentEnds.SetNumZeroed(Num - 1);
		float Total = 0.f;
		for (int32 Segment = 0; Segment < Num - 1; ++Segment)
		{
			float Length = 0.f;
			FVector Last = H.FerryPoints[Segment];
			for (int32 k = 1; k <= FerryLutSteps; ++k)
			{
				FVector Point;
				FVector Derivative;
				H.FerryHermite(Segment, float(k) / FerryLutSteps, Point, Derivative);
				Length += float(FVector::Dist(Point, Last));
				Last = Point;
				H.FerryLut[Segment * (FerryLutSteps + 1) + k] = Length;
			}
			Total += Length;
			H.FerrySegmentEnds[Segment] = Total;
		}
		H.FerryLegToPassing = FMath::Max(H.FerrySegmentEnds[1], 1.f);
		H.FerryLegAfter = FMath::Max(Total - H.FerryLegToPassing, 1.f);
	}

	void BuildFerryman()
	{
		USceneComponent* Boat = H.FerryPivot;
		const FName WoodDarkKey(TEXT("WoodDark"));
		const FLinearColor Robe(0.016f, 0.015f, 0.018f);
		// A long, low, dark boat with a raised stem post.
		InhShapes::AddSurface(&H, Boat, Sphere(), FVector(0.f, 0.f, 6.f), FVector(470.f, 118.f, 80.f), WoodDarkKey, WoodDarkFallback, FRotator::ZeroRotator, false);
		InhShapes::AddSurface(&H, Boat, Sphere(), FVector(0.f, 0.f, 38.f), FVector(410.f, 96.f, 22.f), FName(TEXT("Wood")), WoodFallback, FRotator::ZeroRotator, false);
		InhShapes::AddSurface(&H, Boat, Cube(), FVector(226.f, 0.f, 62.f), FVector(10.f, 8.f, 56.f), WoodDarkKey, WoodDarkFallback, FRotator(-18.f, 0.f, 0.f), false);

		// The Ferryman stands at the stern, hooded, his face lost in shadow, working a long pole.
		InhShapes::AddPart(&H, Boat, Cone(), FVector(-150.f, 0.f, 124.f), FVector(70.f, 64.f, 150.f), Robe, FRotator::ZeroRotator, false);
		InhShapes::AddPart(&H, Boat, Sphere(), FVector(-150.f, 0.f, 170.f), FVector(54.f, 62.f, 28.f), Robe, FRotator::ZeroRotator, false);
		InhShapes::AddPart(&H, Boat, Sphere(), FVector(-148.f, 0.f, 197.f), FVector(38.f, 36.f, 46.f), Robe * 0.8f, FRotator::ZeroRotator, false);
		InhShapes::AddPart(&H, Boat, Sphere(), FVector(-133.f, 0.f, 193.f), FVector(14.f, 22.f, 26.f), FLinearColor(0.002f, 0.002f, 0.003f), FRotator::ZeroRotator, false);
		const FVector Shoulder(-150.f, 18.f, 172.f);
		const FVector Grip(-128.f, 22.f, 150.f);
		const FVector Sleeve = Grip - Shoulder;
		InhShapes::AddPart(&H, Boat, Cylinder(), (Shoulder + Grip) * 0.5f, FVector(12.f, 12.f, float(Sleeve.Size()) + 8.f), Robe,
			FRotationMatrix::MakeFromZ(Sleeve).Rotator(), false);
		H.FerryPole = InhShapes::AddPart(&H, Boat, Cylinder(), Grip, FVector(6.f, 6.f, 400.f), WoodDarkFallback, FRotator::ZeroRotator, false);

		// A lantern hung from a post at the bow, the only warm light on the water.
		InhShapes::AddSurface(&H, Boat, Cylinder(), FVector(200.f, 0.f, 98.f), FVector(6.f, 6.f, 104.f), WoodDarkKey, WoodDarkFallback, FRotator::ZeroRotator, false);
		InhShapes::AddSurface(&H, Boat, Cube(), FVector(212.f, 0.f, 148.f), FVector(30.f, 4.f, 4.f), WoodDarkKey, WoodDarkFallback, FRotator::ZeroRotator, false);
		const FVector LanternBottom(224.f, 0.f, 112.f);
		if (!InhShapes::AddArtMesh(&H, Boat, FName(TEXT("Lantern")), LanternBottom, FVector(22.f, 22.f, 34.f), FRotator::ZeroRotator, false))
		{
			InhShapes::AddSurface(&H, Boat, Cube(), LanternBottom + FVector(0.f, 0.f, 1.5f), FVector(16.f, 16.f, 3.f), FName(TEXT("Iron")), IronFallback, FRotator::ZeroRotator, false);
			InhShapes::AddSurface(&H, Boat, Cone(), LanternBottom + FVector(0.f, 0.f, 29.f), FVector(18.f, 18.f, 12.f), FName(TEXT("Iron")), IronFallback, FRotator::ZeroRotator, false);
		}
		InhShapes::AddEmissive(&H, Boat, Cube(), LanternBottom + FVector(0.f, 0.f, 14.f), FVector(10.f, 10.f, 16.f), LanternGlow, 20.f);
		H.FerryLight->SetRelativeLocation(LanternBottom + FVector(0.f, 0.f, 14.f));

		H.SetFerryVisible(false);
	}

	void BuildMara()
	{
		USceneComponent* Mara = H.MaraPivot;
		// Pale and slender, in a long dress that catches the dawn light. Lit like her face, not emissive: the art's emissive
		// master is unlit, and would leave the dress a flat grey cut-out once the sun is up.
		const FLinearColor Dress(0.62f, 0.67f, 0.72f);
		const FLinearColor Skin(0.68f, 0.64f, 0.60f);
		const FLinearColor Hair(0.035f, 0.028f, 0.024f);
		InhShapes::AddPart(&H, Mara, Cone(), FVector(0.f, 0.f, 75.f), FVector(50.f, 46.f, 150.f), Dress, FRotator::ZeroRotator, false);
		InhShapes::AddPart(&H, Mara, Cylinder(), FVector(0.f, 0.f, 114.f), FVector(21.f, 19.f, 50.f), Dress, FRotator::ZeroRotator, false);
		InhShapes::AddPart(&H, Mara, Sphere(), FVector(0.f, 0.f, 138.f), FVector(18.f, 30.f, 12.f), Dress, FRotator::ZeroRotator, false);
		for (const float Side : { -1.f, 1.f })
		{
			InhShapes::AddPart(&H, Mara, Cylinder(), FVector(0.f, Side * 13.5f, 112.f), FVector(6.f, 6.f, 52.f), Dress, FRotator(0.f, 0.f, Side * 5.f), false);
		}
		InhShapes::AddPart(&H, Mara, Cylinder(), FVector(0.f, 0.f, 146.f), FVector(7.f, 7.f, 12.f), Skin, FRotator::ZeroRotator, false);
		InhShapes::AddPart(&H, Mara, Sphere(), FVector(1.5f, 0.f, 158.f), FVector(18.f, 16.f, 22.f), Skin, FRotator::ZeroRotator, false);
		// Long dark hair down her back.
		InhShapes::AddPart(&H, Mara, Sphere(), FVector(-2.5f, 0.f, 156.f), FVector(19.5f, 19.5f, 34.f), Hair, FRotator::ZeroRotator, false);

		// She first looks up at our window.
		const FVector ToWindow = InhLayout::WindowCenter - InhLayout::MaraPoint;
		H.MaraYaw = FMath::RadiansToDegrees(FMath::Atan2(float(ToWindow.Y), float(ToWindow.X)));
		H.MaraPivot->SetRelativeRotation(FRotator(0.f, H.MaraYaw, 0.f));
		H.SetMaraVisible(false);
	}
};
}

// --- AInhHarbourBuilder ---------------------------------------------------------------------------------------------

AInhHarbourBuilder::AInhHarbourBuilder()
	: Twinkle(InhHarbourParts::TownSeed + 1)
{
	using namespace InhHarbourParts;
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// The lighthouse beam turns about the lamp; it is the only light out here that would be worth a shadow, and on
	// phones it does without.
	BeamPivot = CreateDefaultSubobject<USceneComponent>(TEXT("BeamPivot"));
	BeamPivot->SetupAttachment(Root);
	BeamPivot->SetMobility(EComponentMobility::Movable);
	BeamPivot->SetRelativeLocation(InhLayout::LighthouseLamp);

	Beam = CreateDefaultSubobject<USpotLightComponent>(TEXT("Beam"));
	Beam->SetupAttachment(BeamPivot);
	Beam->SetMobility(EComponentMobility::Movable);
	Beam->SetRelativeRotation(FRotator(-2.5f, 0.f, 0.f));
	Beam->SetIntensityUnits(ELightUnits::Candelas);
	Beam->SetIntensity(BeamCandela);
	// Out across the bay, so the sweep still passes over the far town's waterfront and lower slopes.
	Beam->SetAttenuationRadius(25000.f);
	Beam->SetOuterConeAngle(6.f);
	Beam->SetInnerConeAngle(2.5f);
	Beam->SetLightColor(FLinearColor(1.f, 0.88f, 0.65f));
	Beam->SetVolumetricScatteringIntensity(4.f);
	Beam->SetCastShadows(false);

	FerryPivot = CreateDefaultSubobject<USceneComponent>(TEXT("FerryPivot"));
	FerryPivot->SetupAttachment(Root);
	FerryPivot->SetMobility(EComponentMobility::Movable);
	FerryPivot->SetRelativeLocation(InhLayout::FerrymanStart);

	FerryLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FerryLight"));
	FerryLight->SetupAttachment(FerryPivot);
	FerryLight->SetMobility(EComponentMobility::Movable);
	FerryLight->SetIntensityUnits(ELightUnits::Candelas);
	FerryLight->SetIntensity(FerryLightCandela);
	FerryLight->SetAttenuationRadius(520.f);
	FerryLight->SetLightColor(FLinearColor(1.f, 0.62f, 0.30f));
	FerryLight->SetSourceRadius(3.f);
	FerryLight->SetCastShadows(false);

	MaraPivot = CreateDefaultSubobject<USceneComponent>(TEXT("MaraPivot"));
	MaraPivot->SetupAttachment(Root);
	MaraPivot->SetMobility(EComponentMobility::Movable);
	MaraPivot->SetRelativeLocation(InhLayout::MaraPoint);
}

float AInhHarbourBuilder::FerrymanDurationFor(float SecondsToPassing)
{
	return SecondsToPassing / InhHarbourParts::FerryPassingFraction;
}

void AInhHarbourBuilder::BeginPlay()
{
	Super::BeginPlay();
	if (!bBuilt)
	{
		InhHarbourParts::FInhHarbourBuild Build(*this);
		Build.Run();
		bBuilt = true;
	}
	SetLifeProgress(Progress);
}

void AInhHarbourBuilder::SetLifeProgress(float InProgress)
{
	using namespace InhHarbourParts;
	Progress = FMath::Clamp(InProgress, 0.f, 1.f);
	if (!bBuilt)
	{
		return;
	}
	RefreshWindows(WindowLights, Windows);
	RefreshWindows(FarWindowLights, FarWindows);

	// Stars come out after dusk and fade before dawn, the faint ones first; the moon pales and goes as day breaks.
	const float Bright = FMath::SmoothStep(0.03f, 0.18f, Progress) * (1.f - FMath::SmoothStep(0.80f, 0.95f, Progress));
	const float Faint = FMath::SmoothStep(0.10f, 0.26f, Progress) * (1.f - FMath::SmoothStep(0.70f, 0.86f, Progress));
	const float MoonFade = FMath::Lerp(0.45f, 1.f, FMath::SmoothStep(0.f, 0.2f, Progress)) * (1.f - FMath::SmoothStep(0.86f, 0.99f, Progress));
	SetGlow(BrightStarsMaterial, StarGlow, BrightStarStrength, Bright);
	SetGlow(FaintStarsMaterial, StarGlow, FaintStarStrength, Faint);
	SetGlow(MoonMaterial, MoonGlow, MoonStrength, MoonFade);
	if (BrightStars)
	{
		BrightStars->SetVisibility(Bright > 0.01f);
	}
	if (FaintStars)
	{
		FaintStars->SetVisibility(Faint > 0.01f);
	}
	if (Moon)
	{
		Moon->SetVisibility(MoonFade > 0.01f);
	}

	// The sea: steel at dusk, near black at night, a little warmth coming into it at dawn.
	const float IntoNight = FMath::SmoothStep(0.f, 0.2f, Progress);
	const float IntoDawn = FMath::SmoothStep(0.8f, 1.f, Progress);
	if (SeaMaterial)
	{
		static const FName DeepColorName(TEXT("DeepColor"));
		static const FName ShallowColorName(TEXT("ShallowColor"));
		const FLinearColor Deep = FMath::Lerp(FMath::Lerp(FLinearColor(0.006f, 0.020f, 0.030f), FLinearColor(0.002f, 0.008f, 0.016f), IntoNight),
			FLinearColor(0.012f, 0.022f, 0.032f), IntoDawn);
		const FLinearColor Shallow = FMath::Lerp(FMath::Lerp(FLinearColor(0.040f, 0.090f, 0.100f), FLinearColor(0.012f, 0.035f, 0.050f), IntoNight),
			FLinearColor(0.070f, 0.090f, 0.100f), IntoDawn);
		SeaMaterial->SetVectorParameterValue(DeepColorName, Deep);
		SeaMaterial->SetVectorParameterValue(ShallowColorName, Shallow);
	}
	else
	{
		const FLinearColor Flat = FMath::Lerp(FMath::Lerp(FLinearColor(0.012f, 0.036f, 0.044f), FLinearColor(0.006f, 0.018f, 0.026f), IntoNight),
			FLinearColor(0.020f, 0.036f, 0.044f), IntoDawn);
		InhShapes::SetColor(Sea, Flat);
		InhShapes::SetColor(OuterSea, Flat);
	}

	// Gulls are about at dusk and at dawn, not in the dead of night.
	const bool bGulls = Progress < 0.16f || Progress > 0.84f;
	if (bGulls != bGullsVisible)
	{
		bGullsVisible = bGulls;
		for (const int32 Set : { GullBodySet, GullWingSet })
		{
			if (MovingSets.IsValidIndex(Set) && MovingSets[Set])
			{
				MovingSets[Set]->SetVisibility(bGulls);
			}
		}
	}

	// Lamps burn all night and pale against the sunrise.
	LightLevel = 1.f - 0.7f * FMath::SmoothStep(0.9f, 1.f, Progress);
	for (UPointLightComponent* Light : StreetLights)
	{
		if (Light)
		{
			Light->SetIntensity(StreetLightCandela * LightLevel);
		}
	}
	SetGlow(LampGlowMaterial, LampGlow, LampStrength, LightLevel);
	Beam->SetIntensity(BeamCandela * (1.f - 0.6f * FMath::SmoothStep(0.92f, 1.f, Progress)));

	// A new life begins: the Ferryman has gone, and Mara goes back to waiting for the dawn. Both leave at once, under
	// the next life's fade from black, even if the sunrise was skipped while the boat was still on its way out.
	if (Progress < 0.5f)
	{
		bMaraRequested = false;
		MaraAlpha = 0.f;
		if (bMaraVisible)
		{
			SetMaraVisible(false);
		}
		bFerryRunning = false;
		if (bFerryVisible)
		{
			SetFerryVisible(false);
		}
	}
}

void AInhHarbourBuilder::PlayFerryman(float Seconds)
{
	if (!bBuilt)
	{
		UE_LOG(LogInheritance, Warning, TEXT("PlayFerryman called before the harbour was built."));
		return;
	}
	FerryDuration = FMath::Max(Seconds, 2.f);
	FerryTime = 0.f;
	bFerryRunning = true;
	SetFerryVisible(true);
	// On the water at the start right away, before the first tick.
	TickFerryman(0.f);
}

void AInhHarbourBuilder::ShowMara(bool bVisible)
{
	bMaraRequested = bVisible;
}

FVector AInhHarbourBuilder::GetFerrymanLocation() const
{
	return FerryPivot ? FerryPivot->GetComponentLocation() : InhLayout::FerrymanStart;
}

void AInhHarbourBuilder::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBuilt)
	{
		return;
	}
	Time += DeltaSeconds;
	TickBoatsAndGulls();
	TickLighthouse();
	TickFerryman(DeltaSeconds);
	TickMara(DeltaSeconds);
	TickTwinkle();
}

void AInhHarbourBuilder::TickBoatsAndGulls()
{
	for (const FInhBoat& Boat : Boats)
	{
		const float T = Time + Boat.Phase;
		// The long swell rolls in from the open water, so neighbouring boats rise one after another; a short chop on top.
		const float SwellPhase = Time * 0.9f - float(Boat.Mooring.X) * 0.0016f - float(Boat.Mooring.Y) * 0.0006f;
		const float Heave = (3.5f * FMath::Sin(SwellPhase) + 1.0f * FMath::Sin(T * 2.3f) + 0.5f * FMath::Sin(T * 3.7f + 1.3f)) * Boat.Liveliness;
		const float Pitch = (1.2f * FMath::Cos(SwellPhase) + 0.4f * FMath::Sin(T * 1.9f + 2.f)) * Boat.Liveliness;
		const float Roll = (2.6f * FMath::Sin(T * 1.1f + 0.4f) + 0.6f * FMath::Sin(T * 2.7f)) * Boat.Liveliness;
		// Tugging gently at the mooring lines.
		const float Swing = 2.5f * FMath::Sin(T * 0.23f);
		const FTransform BoatTransform(FRotator(Pitch, Boat.Yaw + Swing, Roll), Boat.Mooring + FVector(0.f, 0.f, Heave));
		for (int32 i = Boat.FirstPart; i < Boat.FirstPart + Boat.NumParts; ++i)
		{
			const FInhMovingPart& Part = BoatParts[i];
			MovingTransforms[Part.Set][Part.Instance] = Part.Local * BoatTransform;
		}
	}

	const bool bGullSets = bGullsVisible && MovingTransforms.IsValidIndex(GullBodySet) && MovingTransforms.IsValidIndex(GullWingSet);
	if (bGullSets)
	{
		TArray<FTransform>& Bodies = MovingTransforms[GullBodySet];
		TArray<FTransform>& Wings = MovingTransforms[GullWingSet];
		const FVector BodyScale(0.34f, 0.11f, 0.10f);
		const FVector WingScale(0.15f, 0.46f, 0.015f);
		for (const FInhGull& Gull : Gulls)
		{
			if (!Bodies.IsValidIndex(Gull.Body) || !Wings.IsValidIndex(Gull.Wings + 1))
			{
				continue;
			}
			const float Angle = Gull.Phase + Gull.AngularSpeed * Time;
			const float Radius = Gull.Radius + 35.f * FMath::Sin(Time * 0.37f + Gull.Phase);
			const FVector Location = Gull.Center + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 50.f * FMath::Sin(Time * 0.5f + Gull.Phase * 1.7f));
			// Heading along the circle, banked into the turn (the centre is on the right when circling with positive speed).
			const float Turn = Gull.AngularSpeed >= 0.f ? 1.f : -1.f;
			const float Yaw = FMath::RadiansToDegrees(Angle) + 90.f * Turn;
			// Bouts of flapping between long glides.
			const float Glide = FMath::SmoothStep(-0.3f, 0.5f, FMath::Sin(Time * 0.33f + Gull.Phase * 2.1f));
			const float Beat = Time * 7.5f + Gull.Phase * 5.f;
			const float Flap = FMath::Lerp(10.f + 32.f * FMath::Sin(Beat), 14.f + 2.f * FMath::Sin(Time * 1.3f), Glide);
			const float Bob = 3.f * FMath::Sin(Beat + 1.6f) * (1.f - Glide);
			const FTransform GullTransform(FRotator(0.f, Yaw, 16.f * Turn), Location + FVector(0.f, 0.f, Bob));
			Bodies[Gull.Body] = FTransform(FRotator::ZeroRotator, FVector::ZeroVector, BodyScale) * GullTransform;
			for (int32 w = 0; w < 2; ++w)
			{
				// Wing tips lift with the beat: the right wing rolls negative, the left positive.
				const float Side = w == 0 ? -1.f : 1.f;
				const FRotator WingTurn(0.f, 0.f, -Side * Flap);
				const FVector WingCenter = FVector(2.f, Side * 5.f, 3.f) + WingTurn.RotateVector(FVector(0.f, Side * 23.f, 0.f));
				Wings[Gull.Wings + w] = FTransform(WingTurn, WingCenter, WingScale) * GullTransform;
			}
		}
	}

	// One upload per moving set per frame.
	for (int32 Set = 0; Set < MovingSets.Num(); ++Set)
	{
		const bool bGull = Set == GullBodySet || Set == GullWingSet;
		if (!MovingSets[Set] || MovingTransforms[Set].Num() == 0 || (bGull && !bGullsVisible))
		{
			continue;
		}
		MovingSets[Set]->BatchUpdateInstancesTransforms(0, MovingTransforms[Set], false, true, false);
	}
}

void AInhHarbourBuilder::TickLighthouse()
{
	using namespace InhHarbourParts;
	BeamYaw = FMath::Fmod(Time * 24.f, 360.f);
	BeamPivot->SetRelativeRotation(FRotator(0.f, BeamYaw, 0.f));
	// The lens flashes as the beam sweeps across the town and our window.
	const FVector ToTown = (InhLayout::WindowCenter - InhLayout::LighthouseLamp).GetSafeNormal2D();
	const float Toward = float(FVector::DotProduct(FRotator(0.f, BeamYaw, 0.f).Vector(), ToTown));
	const float Flash = FMath::Pow(FMath::Max(Toward, 0.f), 24.f);
	SetGlow(LensMaterial, LensGlow, LensStrength + 260.f * Flash, FMath::Max(LightLevel, 0.5f) * (0.6f + 0.4f * Flash));
}

void AInhHarbourBuilder::TickFerryman(float DeltaSeconds)
{
	if (!bFerryVisible || !FerryPivot)
	{
		return;
	}
	if (bFerryRunning)
	{
		FerryTime += DeltaSeconds;
		if (FerryTime >= FerryDuration)
		{
			FerryTime = FerryDuration;
			bFerryRunning = false;
		}
	}
	FVector Location;
	FVector Direction;
	FerryEvaluate(FerryDistanceAt(FerryTime), Location, Direction);
	if (!Direction.IsNearlyZero())
	{
		FerryYaw = FMath::RadiansToDegrees(FMath::Atan2(float(Direction.Y), float(Direction.X)));
	}
	// A slow, heavy bob; the boat never quite settles.
	const float Heave = 2.5f * FMath::Sin(Time * 1.25f) + 1.2f * FMath::Sin(Time * 2.3f + 1.f);
	const float Pitch = 1.2f * FMath::Sin(Time * 1.05f + 0.5f);
	const float Roll = 1.8f * FMath::Sin(Time * 0.85f) + 0.6f * FMath::Sin(Time * 2.1f);
	FerryPivot->SetRelativeLocationAndRotation(Location + FVector(0.f, 0.f, Heave), FRotator(Pitch, FerryYaw, Roll));

	if (FerryPole)
	{
		// He sculls with the pole over the stern: it sweeps a little side to side, its foot under the water.
		const float Stroke = FMath::Sin(Time * 0.9f);
		const FVector Grip(-128.f, 22.f, 150.f);
		const FVector Down = FRotator(-62.f + 5.f * Stroke, 180.f + 12.f * Stroke, 0.f).Vector();
		FerryPole->SetRelativeLocationAndRotation(Grip + Down * 80.f, FRotationMatrix::MakeFromZ(Down).Rotator());
	}
	if (FerryLight)
	{
		FerryLight->SetIntensity(InhHarbourParts::FerryLightCandela *(1.f + 0.07f * FMath::Sin(Time * 11.3f) + 0.05f * FMath::Sin(Time * 17.9f + 2.f)));
	}
}

void AInhHarbourBuilder::TickMara(float DeltaSeconds)
{
	if (!MaraPivot)
	{
		return;
	}
	const float Target = bMaraRequested ? 1.f : FMath::SmoothStep(0.93f, 1.f, Progress);
	MaraAlpha = FMath::FInterpConstantTo(MaraAlpha, Target, DeltaSeconds, 0.4f);
	if (MaraAlpha <= 0.001f)
	{
		if (bMaraVisible)
		{
			SetMaraVisible(false);
		}
		return;
	}
	if (!bMaraVisible)
	{
		SetMaraVisible(true);
	}

	// She looks up at our window, and turns to watch the Ferryman's boat while it is near.
	const FVector ToWindow = InhLayout::WindowCenter - InhLayout::MaraPoint;
	float WantYaw = FMath::RadiansToDegrees(FMath::Atan2(float(ToWindow.Y), float(ToWindow.X)));
	if (bFerryVisible && FerryPivot)
	{
		const FVector ToBoat = FerryPivot->GetRelativeLocation() - InhLayout::MaraPoint;
		if (ToBoat.SizeSquared2D() < FMath::Square(1000.0))
		{
			WantYaw = FMath::RadiansToDegrees(FMath::Atan2(float(ToBoat.Y), float(ToBoat.X)));
		}
	}
	// A slow, deliberate turn of the head and shoulders.
	MaraYaw = FMath::FixedTurn(MaraYaw, WantYaw, 55.f * DeltaSeconds);

	// She gathers out of the dawn mist: from a thin pale wisp to her full shape, rising a hand's breadth.
	const float Shape = FMath::SmoothStep(0.f, 1.f, MaraAlpha);
	const float Thin = FMath::Lerp(0.15f, 1.f, Shape);
	const float Sway = 0.8f * FMath::Sin(Time * 0.6f);
	MaraPivot->SetRelativeLocationAndRotation(InhLayout::MaraPoint + FVector(0.f, 0.f, -8.f * (1.f - Shape)), FRotator(0.f, MaraYaw, Sway));
	MaraPivot->SetRelativeScale3D(FVector(Thin, Thin, FMath::Lerp(0.94f, 1.f, Shape)));
}

void AInhHarbourBuilder::TickTwinkle()
{
	// One household at a time puts a lamp on or out for a while.
	if (NearBlink.Window != INDEX_NONE)
	{
		if (Time >= NearBlink.Until)
		{
			FlipWindow(WindowLights, Windows, NearBlink.Window);
			NearBlink.Window = INDEX_NONE;
			NextNearBlink = Time + Twinkle.FRandRange(4.f, 10.f);
		}
	}
	else if (Time >= NextNearBlink && Windows.Num() > 0)
	{
		NearBlink.Window = Twinkle.RandHelper(Windows.Num());
		NearBlink.Until = Time + Twinkle.FRandRange(3.f, 9.f);
		FlipWindow(WindowLights, Windows, NearBlink.Window);
	}

	// Across the water a few lights come and go.
	for (FInhBlink& Blink : FarBlinks)
	{
		if (Blink.Window != INDEX_NONE && Time >= Blink.Until)
		{
			FlipWindow(FarWindowLights, FarWindows, Blink.Window);
			Blink.Window = INDEX_NONE;
		}
	}
	if (Time >= NextFarBlink && FarWindows.Num() > 0)
	{
		NextFarBlink = Time + Twinkle.FRandRange(0.3f, 1.1f);
		for (FInhBlink& Blink : FarBlinks)
		{
			if (Blink.Window == INDEX_NONE)
			{
				const int32 Index = Twinkle.RandHelper(FarWindows.Num());
				if (!FarWindows[Index].bFlipped)
				{
					Blink.Window = Index;
					Blink.Until = Time + Twinkle.FRandRange(1.5f, 6.f);
					FlipWindow(FarWindowLights, FarWindows, Index);
				}
				break;
			}
		}
	}
}

bool AInhHarbourBuilder::WantsLight(const FInhWindowLight& Window) const
{
	return (Progress >= Window.OnFrom && Progress < Window.OffAt) || Progress >= Window.OnAgainAt;
}

void AInhHarbourBuilder::RefreshWindows(UInstancedStaticMeshComponent* Set, TArray<FInhWindowLight>& List)
{
	if (!Set)
	{
		return;
	}
	bool bChanged = false;
	for (int32 i = 0; i < List.Num(); ++i)
	{
		FInhWindowLight& Window = List[i];
		const bool bWant = WantsLight(Window) != Window.bFlipped;
		if (bWant != Window.bLit)
		{
			Window.bLit = bWant;
			Set->UpdateInstanceTransform(i, bWant ? Window.Shown : InhHarbourParts::Hidden(Window.Shown), false, false, false);
			bChanged = true;
		}
	}
	if (bChanged)
	{
		Set->MarkRenderStateDirty();
	}
}

void AInhHarbourBuilder::FlipWindow(UInstancedStaticMeshComponent* Set, TArray<FInhWindowLight>& List, int32 Index)
{
	if (!Set || !List.IsValidIndex(Index))
	{
		return;
	}
	FInhWindowLight& Window = List[Index];
	Window.bFlipped = !Window.bFlipped;
	const bool bWant = WantsLight(Window) != Window.bFlipped;
	if (bWant != Window.bLit)
	{
		Window.bLit = bWant;
		Set->UpdateInstanceTransform(Index, bWant ? Window.Shown : InhHarbourParts::Hidden(Window.Shown), false, true, false);
	}
}

float AInhHarbourBuilder::FerryDistanceAt(float Seconds) const
{
	const float ToPassing = FMath::Max(FerryDuration * InhHarbourParts::FerryPassingFraction, 0.01f);
	const float After = FMath::Max(FerryDuration - ToPassing, 0.01f);
	if (Seconds <= ToPassing)
	{
		// A steady pace in, so the Sunrise camera finds the boat on schedule.
		return FerryLegToPassing * FMath::Max(Seconds, 0.f) / ToPassing;
	}
	// Leave the steps at the same pace, gather way round the pier, then glide to a stop at the end (a monotonic
	// Hermite ease whose starting slope matches the pace so far).
	const float U = FMath::Clamp((Seconds - ToPassing) / After, 0.f, 1.f);
	const float StartSlope = FMath::Clamp((FerryLegToPassing / ToPassing) * After / FerryLegAfter, 0.f, 3.f);
	const float U2 = U * U;
	const float U3 = U2 * U;
	const float Ease = StartSlope * (U - 2.f * U2 + U3) + (3.f * U2 - 2.f * U3);
	return FerryLegToPassing + FerryLegAfter * Ease;
}

void AInhHarbourBuilder::FerryHermite(int32 Segment, float U, FVector& OutLocation, FVector& OutDerivative) const
{
	const FVector& P0 = FerryPoints[Segment];
	const FVector& P1 = FerryPoints[Segment + 1];
	// Tangents scaled by the segment's chord: uneven spacing never overshoots.
	const float Chord = float(FVector::Dist2D(P0, P1));
	const FVector M0 = FerryDirections[Segment] * Chord;
	const FVector M1 = FerryDirections[Segment + 1] * Chord;
	const float U2 = U * U;
	const float U3 = U2 * U;
	OutLocation = P0 * (2.f * U3 - 3.f * U2 + 1.f) + M0 * (U3 - 2.f * U2 + U) + P1 * (-2.f * U3 + 3.f * U2) + M1 * (U3 - U2);
	OutDerivative = P0 * (6.f * U2 - 6.f * U) + M0 * (3.f * U2 - 4.f * U + 1.f) + P1 * (-6.f * U2 + 6.f * U) + M1 * (3.f * U2 - 2.f * U);
}

void AInhHarbourBuilder::FerryEvaluate(float Distance, FVector& OutLocation, FVector& OutDirection) const
{
	using InhHarbourParts::FerryLutSteps;
	const int32 Segments = FerrySegmentEnds.Num();
	if (Segments == 0 || FerryPoints.Num() != Segments + 1 || FerryLut.Num() != Segments * (FerryLutSteps + 1))
	{
		OutLocation = InhLayout::FerrymanStart;
		OutDirection = FVector::ForwardVector;
		return;
	}
	const float D = FMath::Clamp(Distance, 0.f, FerrySegmentEnds.Last());
	int32 Segment = 0;
	while (Segment < Segments - 1 && D > FerrySegmentEnds[Segment])
	{
		++Segment;
	}
	const float Local = D - (Segment > 0 ? FerrySegmentEnds[Segment - 1] : 0.f);
	const int32 First = Segment * (FerryLutSteps + 1);
	int32 Step = 0;
	while (Step < FerryLutSteps - 1 && FerryLut[First + Step + 1] < Local)
	{
		++Step;
	}
	const float Span = FerryLut[First + Step + 1] - FerryLut[First + Step];
	const float Frac = Span > UE_KINDA_SMALL_NUMBER ? FMath::Clamp((Local - FerryLut[First + Step]) / Span, 0.f, 1.f) : 0.f;
	FVector Derivative;
	FerryHermite(Segment, (Step + Frac) / FerryLutSteps, OutLocation, Derivative);
	OutDirection = Derivative.GetSafeNormal2D();
}

void AInhHarbourBuilder::SetFerryVisible(bool bVisible)
{
	bFerryVisible = bVisible;
	if (FerryPivot)
	{
		FerryPivot->SetVisibility(bVisible, true);
	}
}

void AInhHarbourBuilder::SetMaraVisible(bool bVisible)
{
	bMaraVisible = bVisible;
	if (MaraPivot)
	{
		MaraPivot->SetVisibility(bVisible, true);
	}
}

void AInhHarbourBuilder::SetGlow(UMaterialInstanceDynamic* Material, const FLinearColor& Color, float Strength, float Fade) const
{
	if (!Material)
	{
		return;
	}
	static const FName EmissiveColorName(TEXT("EmissiveColor"));
	static const FName EmissiveStrengthName(TEXT("EmissiveStrength"));
	static const FName ColorName(TEXT("Color"));
	if (bEmissiveArt)
	{
		Material->SetVectorParameterValue(EmissiveColorName, Color);
		Material->SetScalarParameterValue(EmissiveStrengthName, Strength * Fade);
	}
	else
	{
		// The basic shape material can't glow: dim the flat colour instead.
		Material->SetVectorParameterValue(ColorName, Color * Fade);
	}
}
