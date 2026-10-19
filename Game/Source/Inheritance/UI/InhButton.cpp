#include "UI/InhButton.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"

void UInhButton::Configure(const FText& InLabel, InhStyle::EButton InKind, TFunction<void()> InOnClick, int32 InFontSize)
{
	LabelText = InLabel;
	Kind = InKind;
	OnClick = MoveTemp(InOnClick);
	FontSize = InFontSize;
	if (Label)
	{
		Label->SetText(LabelText);
	}
}

void UInhButton::SetLabel(const FText& InLabel)
{
	LabelText = InLabel;
	if (Label)
	{
		Label->SetText(InLabel);
	}
}

void UInhButton::SetButtonEnabled(bool bInEnabled)
{
	bEnabled = bInEnabled;
	if (Button)
	{
		Button->SetIsEnabled(bInEnabled);
	}
}

TSharedRef<SWidget> UInhButton::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		// At least 56px tall: comfortably above the 48dp minimum touch target.
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Size"));
		Size->SetMinDesiredHeight(56.f);
		WidgetTree->RootWidget = Size;

		Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Button"));
		Button->SetStyle(InhStyle::Button(Kind));
		Button->OnClicked.AddDynamic(this, &UInhButton::HandleClicked);
		Button->SetIsEnabled(bEnabled);
		Size->AddChild(Button);

		Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Label"));
		Label->SetText(LabelText);
		Label->SetFont(InhStyle::Font(FontSize, true));
		Label->SetColorAndOpacity(FSlateColor(InhStyle::ButtonText(Kind)));
		Label->SetJustification(ETextJustify::Center);
		Label->SetAutoWrapText(true);
		if (UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Button->AddChild(Label)))
		{
			ButtonSlot->SetHorizontalAlignment(HAlign_Center);
			ButtonSlot->SetVerticalAlignment(VAlign_Center);
		}
	}
	return Super::RebuildWidget();
}

void UInhButton::HandleClicked()
{
	if (OnClick)
	{
		// Copy first: the callback may rebuild the screen and destroy this button.
		TFunction<void()> Callback = OnClick;
		Callback();
	}
}
