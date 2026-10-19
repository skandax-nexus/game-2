#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InhCameraPawn.generated.h"

class USpringArmComponent;
class UCameraComponent;

/** A camera that orbits the diorama room: drag to turn, pinch or scroll to zoom. */
UCLASS()
class INHERITANCE_API AInhCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	AInhCameraPawn();

	void Orbit(const FVector2D& DeltaPixels);
	/** Factor > 1 moves away, < 1 moves closer. */
	void Zoom(float Factor);
	/** Smoothly turns towards a world location, e.g. the prop the player just tapped. */
	void LookAt(const FVector& WorldLocation);
	void ResetView();

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<USpringArmComponent> Arm;
	UPROPERTY() TObjectPtr<UCameraComponent> Camera;

	FVector Focus = FVector(20.f, -40.f, 70.f);
	FVector TargetFocus = Focus;
	float Yaw = -40.f;
	float Pitch = -38.f;
	float ArmLength = 1150.f;
	float TargetYaw = Yaw;
	float TargetPitch = Pitch;
	float TargetArmLength = ArmLength;
	float Time = 0.f;
};
