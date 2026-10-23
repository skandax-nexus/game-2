#include "World/InhArt.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Inheritance.h"

namespace
{
	/**
	 * One cached lookup. The pointer is weak so the cache never pins art in memory: if nothing uses an
	 * asset any more and it gets collected, the next lookup simply loads it again.
	 */
	struct FInhArtEntry
	{
		TWeakObjectPtr<UObject> Object;
		/** FPlatformTime::Seconds() of the last failed lookup, or < 0 if it never failed. */
		double MissedAt = -1.0;
	};

	TMap<FName, FInhArtEntry>& Cache()
	{
		static TMap<FName, FInhArtEntry> Entries;
		return Entries;
	}

	bool MayRetry(double MissedAt)
	{
#if WITH_EDITOR
		// The art build can run while the editor is open, so a miss is only trusted for a few seconds.
		return FPlatformTime::Seconds() - MissedAt > 5.0;
#else
		// A packaged game's content never changes: one miss is final.
		return false;
#endif
	}

	/** Loads /Game/Inheritance/<Folder>/<Prefix><Key> at most once, and never warns when it is missing. */
	template <typename T>
	T* Find(const TCHAR* Folder, const TCHAR* Prefix, FName Key)
	{
		if (Key.IsNone())
		{
			return nullptr;
		}
		const FString Name = FString(Prefix) + Key.ToString();
		const FString PathString = FString::Printf(TEXT("/Game/Inheritance/%s/%s.%s"), Folder, *Name, *Name);
		const FName CacheKey(*PathString);

		if (const FInhArtEntry* Entry = Cache().Find(CacheKey))
		{
			if (UObject* Cached = Entry->Object.Get())
			{
				return Cast<T>(Cached);
			}
			if (Entry->MissedAt >= 0.0 && !MayRetry(Entry->MissedAt))
			{
				return nullptr;
			}
		}

		// Already in memory (the editor, or another caller) is free; otherwise a quiet synchronous load.
		const FSoftObjectPath Path(PathString);
		T* Found = Cast<T>(Path.ResolveObject());
		if (!Found)
		{
			Found = LoadObject<T>(nullptr, *PathString, nullptr, LOAD_NoWarn | LOAD_Quiet);
		}

		// Look the entry up again: loading may have run arbitrary PostLoad code.
		FInhArtEntry& Stored = Cache().FindOrAdd(CacheKey);
		Stored.Object = Found;
		Stored.MissedAt = Found ? -1.0 : FPlatformTime::Seconds();
		return Found;
	}
}

bool InhArt::IsArtInstalled()
{
	// The importer always writes MI_FloorWood (with flat parameters if its download failed), so it marks a finished build.
	const bool bInstalled = Find<UMaterialInterface>(TEXT("Materials"), TEXT("MI_"), FName(TEXT("FloorWood"))) != nullptr;

	static bool bReported = false;
	if (!bReported)
	{
		bReported = true;
		if (bInstalled)
		{
			UE_LOG(LogInheritance, Log, TEXT("Art pipeline output found under /Game/Inheritance."));
		}
		else
		{
			UE_LOG(LogInheritance, Log, TEXT("No art under /Game/Inheritance yet, using basic shapes. Run tools/assets/build-art to import CC0 art (docs/ART_PIPELINE.md)."));
		}
	}
	return bInstalled;
}

UMaterialInterface* InhArt::Material(FName Key)
{
	return IsArtInstalled() ? Find<UMaterialInterface>(TEXT("Materials"), TEXT("MI_"), Key) : nullptr;
}

UStaticMesh* InhArt::Mesh(FName Key)
{
	return IsArtInstalled() ? Find<UStaticMesh>(TEXT("Meshes"), TEXT("SM_"), Key) : nullptr;
}

UTextureCube* InhArt::Cubemap(FName Key)
{
	return IsArtInstalled() ? Find<UTextureCube>(TEXT("Sky"), TEXT("HDRI_"), Key) : nullptr;
}
