#include "World/InhShapes.h"
#include "World/InhArt.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformProperties.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Inheritance.h"

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

	/** Shared by every helper: attachment, transform and collision. The material is left to the caller. */
	template <typename TPart>
	TPart* NewPart(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Location, const FVector& Scale,
		const FRotator& Rotation, bool bCollide)
	{
		if (!Owner)
		{
			return nullptr;
		}
		TPart* Part = NewObject<TPart>(Owner);
		Part->SetStaticMesh(Mesh);
		Part->SetupAttachment(Parent);
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetRelativeLocationAndRotation(Location, Rotation);
		Part->SetRelativeScale3D(Scale);
		Part->SetCollisionEnabled(bCollide ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		Part->SetCollisionResponseToAllChannels(ECR_Block);
		Part->RegisterComponent();
		Owner->AddInstanceComponent(Part);
		return Part;
	}

	/** The part's dynamic material if it already derives from Parent, otherwise a new one (so switching looks never stacks MIDs). */
	UMaterialInstanceDynamic* DynamicFrom(UStaticMeshComponent* Part, UMaterialInterface* Parent)
	{
		UMaterialInstanceDynamic* Mat = Cast<UMaterialInstanceDynamic>(Part->GetMaterial(0));
		if (!Mat || Mat->Parent != Parent)
		{
			Mat = UMaterialInstanceDynamic::Create(Parent, Part);
			Part->SetMaterial(0, Mat);
		}
		return Mat;
	}

	/** Art materials are shared as-is (no MID), which keeps them batchable; flat colours need a MID each. */
	void ApplySurface(UStaticMeshComponent* Part, FName MaterialKey, const FLinearColor& FallbackColor)
	{
		if (UMaterialInterface* Art = InhArt::Material(MaterialKey))
		{
			Part->SetMaterial(0, Art);
		}
		else
		{
			InhShapes::SetColor(Part, FallbackColor);
		}
	}

	/**
	 * A cooked game can't compile shaders, so an instanced mesh whose material wasn't saved with the instanced usage
	 * flag draws the default grid material instead (the editor sets the flag on the fly, which hides this in PIE).
	 */
	bool IsInstanceable(const UMaterialInterface* Material)
	{
		if (!FPlatformProperties::RequiresCookedData())
		{
			return true;
		}
		const UMaterial* Base = Material ? Material->GetMaterial() : nullptr;
		return !Base || Base->bUsedWithInstancedStaticMeshes;
	}

	/**
	 * The art masters always have the flag; the engine's basic shape material used by the flat fallback is outside our
	 * control. Callers avoid the problem with InhShapes::CanInstance where that is affordable; for the rest, say once,
	 * clearly, what a packaged build is showing and why.
	 */
	void WarnIfNotInstanceable(UInstancedStaticMeshComponent* Instances)
	{
		static bool bWarned = false;
		if (bWarned)
		{
			return;
		}
		const UMaterialInterface* Applied = Instances->GetMaterial(0);
		if (!IsInstanceable(Applied))
		{
			bWarned = true;
			UE_LOG(LogInheritance, Warning, TEXT("%s isn't marked 'Used with Instanced Static Meshes', so instanced parts using it show the default material in this build. Run tools/assets/build-art before packaging, and tick that usage on any Fab material an art key points at (docs/ART_PIPELINE.md)."),
				*GetPathNameSafe(Applied->GetMaterial()));
		}
	}
}

UStaticMesh* InhShapes::Cube() { return LoadMesh(TEXT("/Engine/BasicShapes/Cube.Cube")); }
UStaticMesh* InhShapes::Sphere() { return LoadMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere")); }
UStaticMesh* InhShapes::Cylinder() { return LoadMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")); }
UStaticMesh* InhShapes::Cone() { return LoadMesh(TEXT("/Engine/BasicShapes/Cone.Cone")); }
UStaticMesh* InhShapes::Plane() { return LoadMesh(TEXT("/Engine/BasicShapes/Plane.Plane")); }

UStaticMeshComponent* InhShapes::AddPart(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Location,
	const FVector& Size, const FLinearColor& Color, const FRotator& Rotation, bool bCollide)
{
	UStaticMeshComponent* Part = NewPart<UStaticMeshComponent>(Owner, Parent, Mesh, Location, Size / 100.f, Rotation, bCollide);
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

UStaticMeshComponent* InhShapes::AddSurface(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Location,
	const FVector& Size, FName MaterialKey, const FLinearColor& FallbackColor, const FRotator& Rotation, bool bCollide)
{
	// World-position-offset waves need vertices to move: swap the two-triangle plane for the imported grid (same 100cm footprint).
	static const FName WaterKey(TEXT("Water"));
	static const FName WaterGridKey(TEXT("WaterGrid"));
	if (MaterialKey == WaterKey && Mesh && Mesh == Plane())
	{
		if (UStaticMesh* Grid = InhArt::Mesh(WaterGridKey))
		{
			Mesh = Grid;
		}
	}

	UStaticMeshComponent* Part = NewPart<UStaticMeshComponent>(Owner, Parent, Mesh, Location, Size / 100.f, Rotation, bCollide);
	if (Part)
	{
		ApplySurface(Part, MaterialKey, FallbackColor);
	}
	return Part;
}

UStaticMeshComponent* InhShapes::AddArtMesh(AActor* Owner, USceneComponent* Parent, FName MeshKey, const FVector& BottomCenter,
	const FVector& FitSize, const FRotator& Rotation, bool bCollide)
{
	UStaticMesh* Mesh = InhArt::Mesh(MeshKey);
	if (!Owner || !Mesh)
	{
		return nullptr;
	}

	// Bounds are in the mesh's own space; their centre is rarely the pivot (scans usually pivot at the base or the origin).
	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const FVector MeshSize = Bounds.BoxExtent * 2.0;
	double Scale = -1.0;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		// Flat axes (a rug has next to no height) and unconstrained ones don't limit the fit.
		if (MeshSize[Axis] > KINDA_SMALL_NUMBER && FitSize[Axis] > KINDA_SMALL_NUMBER)
		{
			const double AxisScale = FitSize[Axis] / MeshSize[Axis];
			Scale = Scale < 0.0 ? AxisScale : FMath::Min(Scale, AxisScale);
		}
	}
	if (Scale <= 0.0)
	{
		Scale = 1.0;
	}

	// Put the pivot where the scaled, rotated bottom centre of the bounds lands exactly on BottomCenter.
	const FVector LocalBottom(Bounds.Origin.X, Bounds.Origin.Y, Bounds.Origin.Z - Bounds.BoxExtent.Z);
	const FVector Location = BottomCenter - Rotation.RotateVector(LocalBottom * Scale);
	return NewPart<UStaticMeshComponent>(Owner, Parent, Mesh, Location, FVector(Scale), Rotation, bCollide);
}

UStaticMeshComponent* InhShapes::AddEmissive(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Location,
	const FVector& Size, const FLinearColor& Color, float Strength, const FRotator& Rotation)
{
	UStaticMeshComponent* Part = NewPart<UStaticMeshComponent>(Owner, Parent, Mesh, Location, Size / 100.f, Rotation, false);
	if (Part)
	{
		// A light source shouldn't shadow anything, and skipping it saves shadow draw calls on phones.
		Part->SetCastShadow(false);
		SetEmissive(Part, Color, Strength);
	}
	return Part;
}

void InhShapes::SetEmissive(UStaticMeshComponent* Part, const FLinearColor& Color, float Strength)
{
	if (!Part)
	{
		return;
	}
	static const FName EmissiveKey(TEXT("Emissive"));
	if (UMaterialInterface* Art = InhArt::Material(EmissiveKey))
	{
		UMaterialInstanceDynamic* Mat = DynamicFrom(Part, Art);
		Mat->SetVectorParameterValue(TEXT("EmissiveColor"), Color);
		Mat->SetScalarParameterValue(TEXT("EmissiveStrength"), Strength);
	}
	else
	{
		// The basic shape material can't glow; the flat colour still reads as a light against the dusk.
		DynamicFrom(Part, BaseMaterial())->SetVectorParameterValue(TEXT("Color"), Color);
	}
}

UInstancedStaticMeshComponent* InhShapes::AddInstanced(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, FName MaterialKey,
	const FLinearColor& FallbackColor, bool bCollide)
{
	UInstancedStaticMeshComponent* Instances = NewPart<UInstancedStaticMeshComponent>(Owner, Parent, Mesh, FVector::ZeroVector,
		FVector::OneVector, FRotator::ZeroRotator, bCollide);
	if (Instances)
	{
		ApplySurface(Instances, MaterialKey, FallbackColor);
		WarnIfNotInstanceable(Instances);
	}
	return Instances;
}

UInstancedStaticMeshComponent* InhShapes::AddInstancedEmissive(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh,
	const FLinearColor& Color, float Strength)
{
	UInstancedStaticMeshComponent* Instances = NewPart<UInstancedStaticMeshComponent>(Owner, Parent, Mesh, FVector::ZeroVector,
		FVector::OneVector, FRotator::ZeroRotator, false);
	if (Instances)
	{
		Instances->SetCastShadow(false);
		SetEmissive(Instances, Color, Strength);
		WarnIfNotInstanceable(Instances);
	}
	return Instances;
}

bool InhShapes::CanInstance(FName MaterialKey)
{
	UMaterialInterface* Art = InhArt::Material(MaterialKey);
	return IsInstanceable(Art ? Art : BaseMaterial());
}

bool InhShapes::CanInstanceEmissive()
{
	static const FName EmissiveKey(TEXT("Emissive"));
	return CanInstance(EmissiveKey);
}
