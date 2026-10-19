#pragma once

#include "CoreMinimal.h"
#include "UI/InhScreen.h"
#include "World/InhProp.h"
#include "InhHudScreen.generated.h"

class UBorder;

/** The in-room overlay: sunrise clock, actions, coins, the card for a tapped prop, and the main menu. */
UCLASS()
class INHERITANCE_API UInhHudScreen : public UInhScreen
{
	GENERATED_BODY()

public:
	void UpdateClock();
	void ShowPropCard(EInhPropKind Kind, const FString& RefId);
	void HidePropCard();

protected:
	virtual void BuildFrame() override;
	virtual void Build(UVerticalBox* Column) override;

private:
	void BuildPropCard();

	UPROPERTY() TObjectPtr<UVerticalBox> TopBar;
	UPROPERTY() TObjectPtr<UBorder> PropCardBox;
	UPROPERTY() TObjectPtr<UVerticalBox> PropCard;
	UPROPERTY() TObjectPtr<UVerticalBox> BottomBar;
	UPROPERTY() TObjectPtr<UTextBlock> ClockText;

	bool bPropCardOpen = false;
	EInhPropKind CardKind = EInhPropKind::Item;
	FString CardRefId;
};
