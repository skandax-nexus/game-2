#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "InhSaveGame.generated.h"

/** What the device remembers between sessions. Everything else lives on the server. */
UCLASS()
class INHERITANCE_API UInhSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static const TCHAR* GetSlotName() { return TEXT("InheritanceProfile"); }

	UPROPERTY() FString Token;
	UPROPERTY() FString DisplayName;
	/** Life whose Reading of the Will was already shown, so reopening the app goes straight to the room. */
	UPROPERTY() FString LastWillReadForLife;
	UPROPERTY() float MusicVolume = 0.8f;
};
