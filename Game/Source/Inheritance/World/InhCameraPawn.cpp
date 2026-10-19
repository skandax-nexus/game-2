#include "World/InhCameraPawn.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"

namespace
{
	constexpr float MinYaw = -85.f;
	constexpr float MaxYaw = -5.f;
	constexpr float MinPitch = -70.f;
	constexpr float MaxPitch = -15.f;
	constexpr float MinArm = 450.f;
	constexpr float MaxArm = 1700.f;
	const FVector HomeFocus(20.f, -40.f, 70.f);
}

AInhCameraPawn::AInhCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Arm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Arm"));
	Arm->SetupAttachment(Root);
	Arm->bDoCollisionTest = false;
	Arm->bUsePawnControlRotation = false;
	Arm->bEnableCameraLag = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Arm);
	Camera->SetFieldOfView(42.f);
}

void AInhCameraPawn::Orbit(const FVector2D& DeltaPixels)
{
	TargetYaw = FMath::Clamp(TargetYaw + DeltaPixels.X * 0.18f, MinYaw, MaxYaw);
	TargetPitch = FMath::Clamp(TargetPitch - DeltaPixels.Y * 0.15f, MinPitch, MaxPitch);
}

void AInhCameraPawn::Zoom(float Factor)
{
	TargetArmLength = FMath::Clamp(TargetArmLength * Factor, MinArm, MaxArm);
}

void AInhCameraPawn::LookAt(const FVector& WorldLocation)
{
	TargetFocus = FMath::Lerp(HomeFocus, WorldLocation, 0.6f);
	TargetArmLength = FMath::Min(TargetArmLength, 800.f);
}

void AInhCameraPawn::ResetView()
{
	TargetFocus = HomeFocus;
	TargetArmLength = 1150.f;
}

void AInhCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;

	Yaw = FMath::FInterpTo(Yaw, TargetYaw, DeltaSeconds, 6.f);
	Pitch = FMath::FInterpTo(Pitch, TargetPitch, DeltaSeconds, 6.f);
	ArmLength = FMath::FInterpTo(ArmLength, TargetArmLength, DeltaSeconds, 5.f);
	Focus = FMath::VInterpTo(Focus, TargetFocus, DeltaSeconds, 3.f);

	// Phones held upright see less width, so pull back to keep the whole room in frame.
	float AspectScale = 1.f;
	if (GEngine && GEngine->GameViewport)
	{
		FVector2D Size;
		GEngine->GameViewport->GetViewportSize(Size);
		if (Size.X > 0.f && Size.Y > Size.X)
		{
			AspectScale = FMath::Clamp(Size.Y / Size.X, 1.f, 2.2f) * 0.85f;
		}
	}

	// A slow drift keeps the scene alive even when nobody is touching it.
	const float Sway = 1.2f * FMath::Sin(Time * 0.21f);
	SetActorLocation(Focus);
	Arm->SetRelativeRotation(FRotator(Pitch, Yaw + Sway, 0.f));
	Arm->TargetArmLength = ArmLength * FMath::Max(AspectScale, 1.f);
}
