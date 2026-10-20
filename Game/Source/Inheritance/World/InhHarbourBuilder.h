#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "InhHarbourBuilder.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class USpotLightComponent;
class UStaticMeshComponent;

namespace InhHarbourParts
{
	/** Builds the harbour in BeginPlay (InhHarbourBuilder.cpp). */
	struct FInhHarbourBuild;
}

/** One piece of a moored boat: an instance in one of the moving instanced sets, placed relative to its boat. */
struct FInhMovingPart
{
	int32 Set = 0;
	int32 Instance = 0;
	FTransform Local = FTransform::Identity;
};

/** A boat riding the swell at its mooring. Its parts follow it every frame. */
struct FInhBoat
{
	FVector Mooring = FVector::ZeroVector;
	float Yaw = 0.f;
	float Phase = 0.f;
	/** Light boats heave, pitch and roll more than heavy ones. */
	float Liveliness = 1.f;
	int32 FirstPart = 0;
	int32 NumParts = 0;
};

/** A gull circling over the water. */
struct FInhGull
{
	FVector Center = FVector::ZeroVector;
	float Radius = 300.f;
	/** Radians per second; the sign picks the way round. */
	float AngularSpeed = 1.f;
	float Phase = 0.f;
	int32 Body = 0;
	/** The left wing; the right wing is the next instance. */
	int32 Wings = 0;
};

/** A window that can light up: an instance in an emissive set, hidden by shrinking it to nothing. */
struct FInhWindowLight
{
	FTransform Shown = FTransform::Identity;
	/** Life progress (0 dusk .. 1 dawn) when the lamp comes on, goes out for the night, and comes on again before dawn. */
	float OnFrom = 0.f;
	float OffAt = 1.f;
	float OnAgainAt = 2.f;
	bool bLit = true;
	/** Someone has briefly switched it the other way (the twinkle in Tick). */
	bool bFlipped = false;
};

/** A window the twinkle has flipped, until a moment. */
struct FInhBlink
{
	int32 Window = INDEX_NONE;
	float Until = 0.f;
};

/**
 * Hollow Bay outside the room: our building's facade down to the cobbled quay street, the neighbouring houses, the quay
 * wall and water steps, the pier and its boats, the lighthouse on its breakwater, the far shore climbing its hills, the
 * sea and the night sky. Built procedurally in BeginPlay from InhLayout with a fixed seed, so the town is identical on
 * every launch; repeated pieces share instanced components to stay within a phone's draw-call budget.
 * Also stages the dawn: the Ferryman's boat and Mara at the water steps.
 * Spawn it once at the world origin (InhLayout coordinates are world space).
 */
UCLASS()
class INHERITANCE_API AInhHarbourBuilder : public AActor
{
	GENERATED_BODY()

	friend struct InhHarbourParts::FInhHarbourBuild;

public:
	AInhHarbourBuilder();

	/** 0 = dusk when a life starts, 1 = dawn at sunrise. Drives lit windows, Mara's visibility, sea colour. */
	void SetLifeProgress(float Progress);
	/**
	 * Starts the Ferryman's boat along InhLayout::FerrymanStart -> FerrymanPassing -> FerrymanEnd over Seconds.
	 * It keeps a steady pace to FerrymanPassing, which it reaches exactly halfway through (see FerrymanDurationFor),
	 * then rounds the pier end, slips out past the lighthouse and glides to a stop at FerrymanEnd, where it waits
	 * until the next life begins.
	 */
	void PlayFerryman(float Seconds);
	/**
	 * Mara shows near dawn by herself; true calls her now, false leaves her to the dawn. She fades in and out.
	 * A new life (SetLifeProgress below 0.5) sends her back to waiting for the dawn, and the Ferryman away.
	 */
	void ShowMara(bool bVisible);
	FVector GetFerrymanLocation() const;

	/** The Seconds to pass to PlayFerryman so the boat is at FerrymanPassing SecondsToPassing after it starts. */
	static float FerrymanDurationFor(float SecondsToPassing);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void TickBoatsAndGulls();
	void TickLighthouse();
	void TickFerryman(float DeltaSeconds);
	void TickMara(float DeltaSeconds);
	void TickTwinkle();

	/** Shows or hides each window to match the life progress (and any twinkle). */
	void RefreshWindows(UInstancedStaticMeshComponent* Set, TArray<FInhWindowLight>& List);
	/** Flips one window's twinkle state and updates it straight away. */
	void FlipWindow(UInstancedStaticMeshComponent* Set, TArray<FInhWindowLight>& List, int32 Index);
	bool WantsLight(const FInhWindowLight& Window) const;

	/** Distance along the Ferryman's route Seconds after PlayFerryman. */
	float FerryDistanceAt(float Seconds) const;
	/** Point and heading at a distance along the route. */
	void FerryEvaluate(float Distance, FVector& OutLocation, FVector& OutDirection) const;
	/** Point and derivative at U (0..1) along one Hermite segment of the route. */
	void FerryHermite(int32 Segment, float U, FVector& OutLocation, FVector& OutDerivative) const;
	void SetFerryVisible(bool bVisible);
	void SetMaraVisible(bool bVisible);
	/** A glow made by InhShapes' emissive helpers, faded through its dynamic material without touching the asset system. */
	void SetGlow(UMaterialInstanceDynamic* Material, const FLinearColor& Color, float Strength, float Fade) const;

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<USceneComponent> BeamPivot;
	UPROPERTY() TObjectPtr<USpotLightComponent> Beam;
	UPROPERTY() TObjectPtr<USceneComponent> FerryPivot;
	UPROPERTY() TObjectPtr<UPointLightComponent> FerryLight;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> FerryPole;
	UPROPERTY() TObjectPtr<USceneComponent> MaraPivot;
	UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> StreetLights;

	/** Boats and gulls: every instance is rewritten each frame from the arrays below. */
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> MovingSets;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> WindowLights;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> FarWindowLights;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> BrightStars;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> FaintStars;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Moon;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Sea;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> OuterSea;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Lens;

	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SeaMaterial;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> LensMaterial;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BrightStarsMaterial;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FaintStarsMaterial;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MoonMaterial;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> LampGlowMaterial;

	TArray<TArray<FTransform>> MovingTransforms;
	TArray<FInhMovingPart> BoatParts;
	TArray<FInhBoat> Boats;
	TArray<FInhGull> Gulls;
	int32 GullBodySet = INDEX_NONE;
	int32 GullWingSet = INDEX_NONE;
	bool bGullsVisible = true;

	TArray<FInhWindowLight> Windows;
	TArray<FInhWindowLight> FarWindows;
	FInhBlink NearBlink;
	FInhBlink FarBlinks[6];
	float NextNearBlink = 6.f;
	float NextFarBlink = 1.f;
	/** Runtime randomness only (twinkles); the town itself comes from the fixed seed. */
	FRandomStream Twinkle;

	/** The Ferryman's route: Hermite segments through these points, with an arc-length table per segment. */
	TArray<FVector> FerryPoints;
	TArray<FVector> FerryDirections;
	TArray<float> FerrySegmentEnds;
	TArray<float> FerryLut;
	float FerryLegToPassing = 1.f;
	float FerryLegAfter = 1.f;
	float FerryDuration = 20.f;
	float FerryTime = 0.f;
	float FerryYaw = 0.f;
	bool bFerryRunning = false;
	bool bFerryVisible = false;

	bool bMaraRequested = false;
	bool bMaraVisible = false;
	float MaraAlpha = 0.f;
	float MaraYaw = 0.f;

	bool bEmissiveArt = false;
	bool bBuilt = false;
	float Progress = 0.3f;
	float Time = 0.f;
	float BeamYaw = 0.f;
	float LightLevel = 1.f;
};
