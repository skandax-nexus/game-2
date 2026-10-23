#pragma once

#include "CoreMinimal.h"
#include "UI/InhScreen.h"
#include "InhWillScreen.generated.h"

/** The Reading of the Will: what you inherited, and the voice note left for you. */
UCLASS()
class INHERITANCE_API UInhWillScreen : public UInhScreen
{
	GENERATED_BODY()

protected:
	virtual void Build(UVerticalBox* Column) override;
};

namespace InhFormat
{
	/** "14h 03m" style countdown. */
	FText Countdown(const FTimespan& Left);
	/** Local wall-clock time of a UTC moment, e.g. "06:12". */
	FText LocalTime(const FDateTime& Utc);
	FText Coins(int32 Amount);
}
