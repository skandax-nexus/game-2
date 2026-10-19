#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class UStaticMeshComponent;
class USceneComponent;

/**
 * Builds geometry from the engine's basic shapes. These stand in for art until real meshes
 * are imported (see docs/ART_PIPELINE.md); swap a part's mesh and everything else still works.
 */
namespace InhShapes
{
	UStaticMesh* Cube();
	UStaticMesh* Sphere();
	UStaticMesh* Cylinder();
	UStaticMesh* Cone();

	/**
	 * Adds a coloured mesh part to an actor. Size is in centimetres (basic shapes are 100cm),
	 * Location is the centre of the part relative to Parent.
	 */
	UStaticMeshComponent* AddPart(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Location,
		const FVector& Size, const FLinearColor& Color, const FRotator& Rotation = FRotator::ZeroRotator, bool bCollide = true);

	void SetColor(UStaticMeshComponent* Part, const FLinearColor& Color);
}
