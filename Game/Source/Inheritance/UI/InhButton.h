#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/InhStyle.h"
#include "InhButton.generated.h"

class UButton;
class UTextBlock;

/** A large, touch-friendly button that runs a C++ callback. */
UCLASS()
class INHERITANCE_API UInhButton : public UUserWidget
{
	GENERATED_BODY()

public:
	void Configure(const FText& InLabel, InhStyle::EButton InKind, TFunction<void()> InOnClick, int32 InFontSize = 22);
	void SetLabel(const FText& InLabel);
	void SetButtonEnabled(bool bEnabled);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UFUNCTION() void HandleClicked();

	UPROPERTY() TObjectPtr<UButton> Button;
	UPROPERTY() TObjectPtr<UTextBlock> Label;

	FText LabelText;
	InhStyle::EButton Kind = InhStyle::EButton::Secondary;
	TFunction<void()> OnClick;
	int32 FontSize = 22;
	bool bEnabled = true;
};
