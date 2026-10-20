#pragma once

#include "CoreMinimal.h"

class AActor;
class UStaticMesh;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class USceneComponent;

/**
 * Builds geometry from the engine's basic shapes, dressed with the imported art when the art pipeline
 * has run (see InhArt and docs/ART_PIPELINE.md). Every art-aware helper falls back to flat colours on
 * basic shapes, so the game looks complete either way.
 */
namespace InhShapes
{
	UStaticMesh* Cube();
	UStaticMesh* Sphere();
	UStaticMesh* Cylinder();
	UStaticMesh* Cone();
	/** /Engine/BasicShapes/Plane: 100cm square, facing +Z. */
	UStaticMesh* Plane();

	/**
	 * Adds a coloured mesh part to an actor. Size is in centimetres (basic shapes are 100cm),
	 * Location is the centre of the part relative to Parent.
	 */
	UStaticMeshComponent* AddPart(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Location,
		const FVector& Size, const FLinearColor& Color, const FRotator& Rotation = FRotator::ZeroRotator, bool bCollide = true);

	void SetColor(UStaticMeshComponent* Part, const FLinearColor& Color);

	/**
	 * Like AddPart, but uses the art material MaterialKey when installed (as-is, no tint); otherwise a flat FallbackColor.
	 * Surface materials are world-aligned, so textures keep their real-world scale however the part is sized.
	 * A Plane() with the "Water" key becomes the finely subdivided SM_WaterGrid when installed, so the waves can move.
	 */
	UStaticMeshComponent* AddSurface(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Location,
		const FVector& Size, FName MaterialKey, const FLinearColor& FallbackColor,
		const FRotator& Rotation = FRotator::ZeroRotator, bool bCollide = true);

	/** An imported art mesh uniformly scaled to fit inside FitSize, its bottom centre at BottomCenter. nullptr if not installed (caller builds a primitive fallback). */
	UStaticMeshComponent* AddArtMesh(AActor* Owner, USceneComponent* Parent, FName MeshKey, const FVector& BottomCenter,
		const FVector& FitSize, const FRotator& Rotation = FRotator::ZeroRotator, bool bCollide = true);

	/** A glowing part (lit windows, lamp glass, lighthouse lens, stars). MI_Emissive when installed, else a bright flat colour. */
	UStaticMeshComponent* AddEmissive(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FVector& Location,
		const FVector& Size, const FLinearColor& Color, float Strength, const FRotator& Rotation = FRotator::ZeroRotator);

	void SetEmissive(UStaticMeshComponent* Part, const FLinearColor& Color, float Strength);

	/**
	 * Instanced variant for repeated pieces: one component, many transforms. Material from MaterialKey or flat FallbackColor.
	 * Check CanInstance(MaterialKey) first wherever the look may be a flat colour.
	 */
	UInstancedStaticMeshComponent* AddInstanced(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, FName MaterialKey,
		const FLinearColor& FallbackColor, bool bCollide = false);

	/** Emissive instanced variant (e.g. all lit windows of the town in one draw call). See CanInstanceEmissive. */
	UInstancedStaticMeshComponent* AddInstancedEmissive(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh,
		const FLinearColor& Color, float Strength);

	/**
	 * Whether AddInstanced can draw MaterialKey's look: its art material, or the flat colour when the key has none
	 * installed. Always true in the editor, which sets a missing usage flag on the fly. A cooked game can't, and draws
	 * instances whose material wasn't saved "Used with Instanced Static Meshes" with the default grid material. The art
	 * masters have the flag; the engine's basic shape material behind the flat colours may not. When this is false,
	 * build the pieces one by one (AddSurface, same transforms) or, for numerous decoration, leave them out.
	 */
	bool CanInstance(FName MaterialKey);

	/** The same for AddInstancedEmissive (MI_Emissive, or the flat colour without the art). */
	bool CanInstanceEmissive();
}
