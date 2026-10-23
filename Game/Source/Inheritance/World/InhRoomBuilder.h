#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/InhTypes.h"
#include "World/InhProp.h"
#include "InhRoomBuilder.generated.h"

class AInhAtmosphereFX;
class AInhHarbourBuilder;
class UDirectionalLightComponent;
class USkyLightComponent;
class USkyAtmosphereComponent;
class UExponentialHeightFogComponent;
class UPostProcessComponent;
class UPointLightComponent;
class UTextureCube;

/**
 * Builds the inherited room as a lit diorama (a cutaway of the upstairs room above the fish market), lights all of
 * Hollow Bay from dusk to dawn with the film grade on top, and fills the room with props for everything in the estate.
 * The harbour outside belongs to AInhHarbourBuilder and the air (dust, rain, mist) to AInhAtmosphereFX; both follow
 * this room's clock. Surfaces and furniture use the imported art when it is installed, basic shapes otherwise.
 */
UCLASS()
class INHERITANCE_API AInhRoomBuilder : public AActor
{
	GENERATED_BODY()

public:
	AInhRoomBuilder();

	/** Rebuilds every prop from the life's estate. */
	void ShowLife(const FInhLife& Life);
	/** Moves the sky from dusk (0) through night to dawn (1), and the harbour and the air with it. */
	void SetLifeProgress(float Progress);
	/** A hidden item stops being a loose floorboard once found. */
	void RevealHiddenItem(const FString& ItemId);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void BuildShell();
	/** Lights the scene from the photographed night and dawn skies when the art pipeline has imported them. */
	void SetupSky();
	/** The harbour and the air are spawned by the game mode; found once, when first needed. */
	void FindWorldActors();
	void ClearProps();
	AInhProp* SpawnProp(EInhPropKind Kind, const FString& RefId, const FVector& Location, const FLinearColor& MarkerColor);
	void SpawnItem(const FInhItem& Item, int32& TableSlot, int32& ShelfSlot, int32& FloorSlot);

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> Sun;
	UPROPERTY() TObjectPtr<USkyLightComponent> SkyLight;
	UPROPERTY() TObjectPtr<USkyAtmosphereComponent> Atmosphere;
	UPROPERTY() TObjectPtr<UExponentialHeightFogComponent> Fog;
	UPROPERTY() TObjectPtr<UPostProcessComponent> Post;
	UPROPERTY() TObjectPtr<UPointLightComponent> Lamp;
	UPROPERTY() TObjectPtr<AInhHarbourBuilder> Harbour;
	UPROPERTY() TObjectPtr<AInhAtmosphereFX> Air;
	UPROPERTY() TObjectPtr<UTextureCube> NightSky;
	UPROPERTY() TObjectPtr<UTextureCube> DawnSky;
	UPROPERTY() TArray<TObjectPtr<AInhProp>> Props;

	FInhLife ShownLife;
	TSet<FString> RevealedItems;
	float Progress = 0.3f;
	/** The night-to-dawn sky light blend last applied, or < 0 before the first. */
	float SkyBlend = -1.f;
	float Time = 0.f;
	bool bShellBuilt = false;
};
