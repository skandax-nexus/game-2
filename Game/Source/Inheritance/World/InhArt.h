#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UStaticMesh;
class UTextureCube;

/**
 * Looks up the real art produced by the art pipeline (tools/assets/build-art.*, see docs/ART_PIPELINE.md).
 * Everything lives under /Game/Inheritance and is addressed by the logical keys in
 * Content/Data/Art/ArtManifest.json. Every lookup is cached, misses included, and returns nullptr
 * quietly when the art hasn't been built, so callers always keep a basic-shape fallback.
 * Game thread only.
 */
namespace InhArt
{
	/** MI_<Key> under /Game/Inheritance/Materials (e.g. "FloorWood"). Cached. nullptr if the art build hasn't run. */
	UMaterialInterface* Material(FName Key);
	/** SM_<Key> under /Game/Inheritance/Meshes (e.g. "Table"). nullptr if absent. */
	UStaticMesh* Mesh(FName Key);
	/** HDRI_<Key> cubemap under /Game/Inheritance/Sky ("Night", "Dawn"). nullptr if absent. */
	UTextureCube* Cubemap(FName Key);
	/** True once the art pipeline has imported anything (checks MI_FloorWood). */
	bool IsArtInstalled();
}
