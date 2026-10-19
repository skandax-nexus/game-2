#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InhSettings.generated.h"

/** Project Settings > Game > Inheritance. Stored in Config/DefaultGame.ini. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Inheritance"))
class INHERITANCE_API UInhSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Base URL of the Inheritance server, without a trailing slash. */
	UPROPERTY(Config, EditAnywhere, Category = "Server")
	FString BackendUrl = TEXT("http://127.0.0.1:8080");

	/** Show developer buttons such as "Skip to sunrise". Never shown in Shipping builds. */
	UPROPERTY(Config, EditAnywhere, Category = "Development")
	bool bShowDevTools = true;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
