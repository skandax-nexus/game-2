#include "World/InhGameMode.h"
#include "World/InhAtmosphereFX.h"
#include "World/InhCameraPawn.h"
#include "World/InhHarbourBuilder.h"
#include "World/InhLayout.h"
#include "World/InhPlayerController.h"
#include "World/InhRoomBuilder.h"
#include "EngineUtils.h"
#include "Engine/World.h"

// A named namespace: unity builds put several .cpp files into one translation unit.
namespace InhGameModeParts
{
	/** Hollow Bay is built at runtime: each builder exists once, at the world origin (InhLayout is world space). */
	template <class T>
	void SpawnOnce(UWorld* World)
	{
		if (!TActorIterator<T>(World))
		{
			World->SpawnActor<T>(T::StaticClass(), FTransform::Identity);
		}
	}
}

AInhGameMode::AInhGameMode()
{
	DefaultPawnClass = AInhCameraPawn::StaticClass();
	PlayerControllerClass = AInhPlayerController::StaticClass();
}

void AInhGameMode::StartPlay()
{
	// The harbour and the air first, so the room finds them when it starts its clock.
	UWorld* World = GetWorld();
	InhGameModeParts::SpawnOnce<AInhHarbourBuilder>(World);
	InhGameModeParts::SpawnOnce<AInhAtmosphereFX>(World);
	InhGameModeParts::SpawnOnce<AInhRoomBuilder>(World);
	Super::StartPlay();
}

void AInhGameMode::RestartPlayer(AController* NewPlayer)
{
	RestartPlayerAtTransform(NewPlayer, FTransform(InhLayout::RoomCameraFocus));
}
