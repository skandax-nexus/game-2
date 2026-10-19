#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "InhGameMode.generated.h"

UCLASS()
class INHERITANCE_API AInhGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AInhGameMode();

	virtual void StartPlay() override;
	/** Hollow Bay is built at runtime, so there is no PlayerStart: spawn the camera at the room. */
	virtual void RestartPlayer(AController* NewPlayer) override;
};
