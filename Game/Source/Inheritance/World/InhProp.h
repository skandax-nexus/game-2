#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InhProp.generated.h"

class UPointLightComponent;
class UStaticMeshComponent;

UENUM()
enum class EInhPropKind : uint8
{
	Item,
	HiddenItem,
	Debt,
	Feud,
	Clue,
	Phone,
	Window,
};

/** Something in the room the player can tap to investigate. */
UCLASS()
class INHERITANCE_API AInhProp : public AActor
{
	GENERATED_BODY()

public:
	AInhProp();

	void Setup(EInhPropKind InKind, const FString& InRefId, const FLinearColor& MarkerColor);
	UStaticMeshComponent* AddPart(UStaticMesh* Mesh, const FVector& Location, const FVector& Size, const FLinearColor& Color,
		const FRotator& Rotation = FRotator::ZeroRotator);

	/** Called when the player taps this prop. Stops the "look here" glow. */
	void MarkInvestigated();
	void SetSelected(bool bInSelected);

	EInhPropKind GetKind() const { return Kind; }
	const FString& GetRefId() const { return RefId; }

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UPointLightComponent> Marker;

	EInhPropKind Kind = EInhPropKind::Item;
	FString RefId;
	FVector BaseLocation = FVector::ZeroVector;
	float Time = 0.f;
	float BaseMarkerIntensity = 30.f;
	bool bInvestigated = false;
	bool bSelected = false;
};
