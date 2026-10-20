#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "InhAtmosphereFX.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

/** A dust mote floating in the room. */
struct FInhMote
{
	/** Ambient motes: the point they drift about. Motes in the window's light shaft: (along, across, up), each 0..1. */
	FVector Base = FVector::ZeroVector;
	FVector Phase = FVector::ZeroVector;
	FVector Rate = FVector::OneVector;
	float Size = 1.f;
	/** The mote shows while the room's dust amount is above this. */
	float Threshold = 0.f;
	bool bInShaft = false;
};

/** A rain streak; it starts again at the top when it reaches the street or the sea. */
struct FInhRaindrop
{
	FVector Location = FVector::ZeroVector;
	float Speed = 900.f;
	float Length = 50.f;
};

/** A sheet of mist drifting over the water. */
struct FInhFogCard
{
	FVector Location = FVector::ZeroVector;
	float Speed = 20.f;
	float Width = 1600.f;
	float Height = 450.f;
};

/**
 * The air itself: dust motes floating in the room (thickest in the window's light shaft at dawn), rain streaks outside
 * the window and over the quay and the sea, and, when the fog card art is installed, sheets of mist drifting over the
 * water. Each is one instanced component, rewritten every frame from preallocated arrays.
 * Spawn it once at the world origin (InhLayout coordinates are world space).
 */
UCLASS()
class INHERITANCE_API AInhAtmosphereFX : public AActor
{
	GENERATED_BODY()

public:
	AInhAtmosphereFX();

	/** 0 = dusk ... 1 = dawn: denser dust in the dawn shaft, fewer motes at night, thicker mist towards morning. */
	void SetLifeProgress(float Progress);
	/** 0 = none; rain streaks outside the window and on the sea. It eases in and out over a few seconds. */
	void SetRain(float Amount01);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void BuildDust();
	void BuildRain();
	void BuildFog();
	void TickDust(float DeltaSeconds);
	void TickRain(float DeltaSeconds);
	void TickFog(float DeltaSeconds);
	void RespawnDrop(FInhRaindrop& Drop, bool bAnyHeight);
	void UpdateFogLook();

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Dust;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Rain;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> FogCards;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> FogMaterial;

	TArray<FInhMote> Motes;
	TArray<FTransform> DustTransforms;
	TArray<FInhRaindrop> Drops;
	TArray<FTransform> RainTransforms;
	TArray<FInhFogCard> Cards;
	TArray<FTransform> FogTransforms;
	FRandomStream Rng;

	/** The way the low sun (or the moon) shines in through the window. */
	FVector ShaftDirection = FVector(-1.f, 0.f, 0.f);
	FRotator RainTurn = FRotator::ZeroRotator;
	FVector RainDrift = FVector::ZeroVector;
	float Progress = 0.3f;
	float DustAmount = 0.4f;
	float ShaftAmount = 0.5f;
	float RainTarget = 0.f;
	float RainLevel = 0.f;
	bool bRainShown = false;
	bool bBuilt = false;
	float Time = 0.f;
};
