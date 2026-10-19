#include "World/InhShapes.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	UStaticMesh* LoadMesh(const TCHAR* Path)
	{
		return LoadObject<UStaticMesh>(nullptr, Path);
	}

	UMaterialInterface* BaseMaterial()
	{
		return LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
}

UStaticMesh* InhShapes::Cube() { return LoadMesh(TEXT("/Engine/BasicShapes/Cube.Cube")); }
UStaticMesh* InhShapes::Sphere() { return LoadMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere")); }
UStaticMesh* InhShapes::Cylinder() { return LoadMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")); }
UStaticMesh* InhShapes::Cone() { return LoadMesh(TEXT("/Engine/BasicShapes/Cone.Cone")); }

UStaticMeshComponent* InhShapes::AddPart(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Location,
	const FVector& Size, const FLinearColor& Color, const FRotator& Rotation, bool bCollide)
{
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Owner);
	Part->SetStaticMesh(Mesh);
	Part->SetupAttachment(Parent);
	Part->SetMobility(EComponentMobility::Movable);
	Part->SetRelativeLocationAndRotation(Location, Rotation);
	Part->SetRelativeScale3D(Size / 100.f);
	Part->SetCollisionEnabled(bCollide ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	Part->SetCollisionResponseToAllChannels(ECR_Block);
	Part->RegisterComponent();
	Owner->AddInstanceComponent(Part);
	SetColor(Part, Color);
	return Part;
}

void InhShapes::SetColor(UStaticMeshComponent* Part, const FLinearColor& Color)
{
	if (!Part)
	{
		return;
	}
	UMaterialInstanceDynamic* Mat = Cast<UMaterialInstanceDynamic>(Part->GetMaterial(0));
	if (!Mat)
	{
		Mat = UMaterialInstanceDynamic::Create(BaseMaterial(), Part);
		Part->SetMaterial(0, Mat);
	}
	Mat->SetVectorParameterValue(TEXT("Color"), Color);
}
