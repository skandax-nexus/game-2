#include "World/InhGameMode.h"
#include "World/InhCameraPawn.h"
#include "World/InhPlayerController.h"
#include "World/InhRoomBuilder.h"
#include "EngineUtils.h"
#include "Engine/World.h"

AInhGameMode::AInhGameMode()
{
	DefaultPawnClass = AInhCameraPawn::StaticClass();
	PlayerControllerClass = AInhPlayerController::StaticClass();
}

void AInhGameMode::StartPlay()
{
	if (!TActorIterator<AInhRoomBuilder>(GetWorld()))
	{
		GetWorld()->SpawnActor<AInhRoomBuilder>(AInhRoomBuilder::StaticClass(), FTransform::Identity);
	}
	Super::StartPlay();
}

void AInhGameMode::RestartPlayer(AController* NewPlayer)
{
	RestartPlayerAtTransform(NewPlayer, FTransform(FVector(20.f, -40.f, 70.f)));
}
