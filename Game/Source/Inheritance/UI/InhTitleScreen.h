#pragma once

#include "CoreMinimal.h"
#include "UI/InhScreen.h"
#include "InhTitleScreen.generated.h"

class UEditableTextBox;

UCLASS()
class INHERITANCE_API UInhTitleScreen : public UInhScreen
{
	GENERATED_BODY()

public:
	void ShowError(const FText& Error);

protected:
	virtual void Build(UVerticalBox* Column) override;
	virtual bool RefreshOnLifeChange() const override { return false; }

private:
	void Begin();

	UPROPERTY() TObjectPtr<UEditableTextBox> NameBox;
	UPROPERTY() TObjectPtr<UTextBlock> Status;
	UPROPERTY() TObjectPtr<UInhButton> BeginButton;
};
