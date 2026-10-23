#pragma once

#include "CoreMinimal.h"
#include "UI/InhScreen.h"
#include "InhSunriseScreen.generated.h"

/** The end of a life. The Ferryman rows you out, and a new life waits. */
UCLASS()
class INHERITANCE_API UInhSunriseScreen : public UInhScreen
{
	GENERATED_BODY()

public:
	UInhSunriseScreen(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void Build(UVerticalBox* Column) override;
	virtual bool RefreshOnLifeChange() const override { return false; }
};
