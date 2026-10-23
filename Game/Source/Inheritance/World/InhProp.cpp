#include "World/InhProp.h"
#include "World/InhShapes.h"
#include "Components/PointLightComponent.h"

AInhProp::AInhProp()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);

	Marker = CreateDefaultSubobject<UPointLightComponent>(TEXT("Marker"));
	Marker->SetupAttachment(Root);
	Marker->SetMobility(EComponentMobility::Movable);
	Marker->SetRelativeLocation(FVector(0.f, 0.f, 30.f));
	Marker->SetIntensityUnits(ELightUnits::Candelas);
	Marker->SetIntensity(BaseMarkerIntensity);
	Marker->SetAttenuationRadius(120.f);
	Marker->SetCastShadows(false);
}

void AInhProp::Setup(EInhPropKind InKind, const FString& InRefId, const FLinearColor& MarkerColor)
{
	Kind = InKind;
	RefId = InRefId;
	Marker->SetLightColor(MarkerColor);
	BaseLocation = GetActorLocation();
	// Each prop pulses out of step with its neighbours.
	Time = FMath::FRandRange(0.f, 6.f);
}

UStaticMeshComponent* AInhProp::AddPart(UStaticMesh* Mesh, const FVector& Location, const FVector& Size, const FLinearColor& Color, const FRotator& Rotation)
{
	return InhShapes::AddPart(this, Root, Mesh, Location, Size, Color, Rotation, true);
}

void AInhProp::MarkInvestigated()
{
	bInvestigated = true;
}

void AInhProp::SetSelected(bool bInSelected)
{
	bSelected = bInSelected;
}

void AInhProp::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;

	// Unexplored props breathe softly; the selected one glows; explored ones settle down.
	const float Pulse = 0.6f + 0.4f * FMath::Sin(Time * 2.2f);
	float Target = bInvestigated ? BaseMarkerIntensity * 0.15f : BaseMarkerIntensity * Pulse;
	if (bSelected)
	{
		Target = BaseMarkerIntensity * 2.5f;
	}
	Marker->SetIntensity(FMath::FInterpTo(Marker->Intensity, Target, DeltaSeconds, 6.f));

	const float Lift = bSelected ? 4.f + 1.5f * FMath::Sin(Time * 3.f) : 0.f;
	SetActorLocation(BaseLocation + FVector(0.f, 0.f, Lift));
}
