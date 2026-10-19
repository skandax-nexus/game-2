#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/InhTypes.h"
#include "World/InhProp.h"
#include "InhRoomBuilder.generated.h"

class UDirectionalLightComponent;
class USkyLightComponent;
class USkyAtmosphereComponent;
class UExponentialHeightFogComponent;
class UPostProcessComponent;
class UPointLightComponent;
class USpotLightComponent;

/**
 * Builds the inherited room as a lit diorama, with Hollow Bay's harbour and lighthouse outside
 * the window, and fills it with props for everything in the estate.
 */
UCLASS()
class INHERITANCE_API AInhRoomBuilder : public AActor
{
	GENERATED_BODY()

public:
	AInhRoomBuilder();

	/** Rebuilds every prop from the life's estate. */
	void ShowLife(const FInhLife& Life);
	/** Moves the sky from dusk (0) through night to dawn (1). */
	void SetLifeProgress(float Progress);
	/** A hidden item stops being a loose floorboard once found. */
	void RevealHiddenItem(const FString& ItemId);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void BuildShell();
	void BuildOutside();
	void ClearProps();
	AInhProp* SpawnProp(EInhPropKind Kind, const FString& RefId, const FVector& Location, const FLinearColor& MarkerColor);
	void SpawnItem(const FInhItem& Item, int32& TableSlot, int32& ShelfSlot, int32& FloorSlot);
	UStaticMeshComponent* Shell(UStaticMesh* Mesh, const FVector& Location, const FVector& Size, const FLinearColor& Color,
		const FRotator& Rotation = FRotator::ZeroRotator, bool bCollide = true);

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> Sun;
	UPROPERTY() TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY() TObjectPtr<USkyAtmosphereComponent> Atmosphere;
	UPROPERTY() TObjectPtr<UExponentialHeightFogComponent> Fog;
	UPROPERTY() TObjectPtr<UPostProcessComponent> Post;
	UPROPERTY() TObjectPtr<UPointLightComponent> Lamp;
	UPROPERTY() TObjectPtr<USpotLightComponent> LighthouseBeam;
	UPROPERTY() TArray<TObjectPtr<AInhProp>> Props;

	FInhLife ShownLife;
	TSet<FString> RevealedItems;
	float Time = 0.f;
	bool bShellBuilt = false;
};
