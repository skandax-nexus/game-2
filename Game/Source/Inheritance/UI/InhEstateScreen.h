#pragma once

#include "CoreMinimal.h"
#include "UI/InhScreen.h"
#include "InhEstateScreen.generated.h"

class UEditableTextBox;

/** Everything you inherited, and what you can do about it with your remaining actions. */
UCLASS()
class INHERITANCE_API UInhEstateScreen : public UInhScreen
{
	GENERATED_BODY()

protected:
	virtual void Build(UVerticalBox* Column) override;

private:
	UPROPERTY() TObjectPtr<UEditableTextBox> RivalBox;
};
