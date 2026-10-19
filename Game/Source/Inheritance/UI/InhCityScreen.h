#pragma once

#include "CoreMinimal.h"
#include "UI/InhScreen.h"
#include "InhCityScreen.generated.h"

/** The season: acts, revealed twists, the Ledger race, the factions and the people of Hollow Bay. */
UCLASS()
class INHERITANCE_API UInhCityScreen : public UInhScreen
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void Build(UVerticalBox* Column) override;
};
