#include "UI/InhTitleScreen.h"
#include "UI/InhButton.h"
#include "Core/InhGameSubsystem.h"
#include "Online/InhBackendSubsystem.h"
#include "World/InhPlayerController.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/GameInstance.h"

#define LOCTEXT_NAMESPACE "Inheritance"

void UInhTitleScreen::Build(UVerticalBox* Column)
{
	AddSpacer(Column, 80.f);
	UTextBlock* Title = AddText(Column, LOCTEXT("GameTitle", "INHERITANCE"), 52, InhStyle::Brass, true, ETextJustify::Center);
	Title->SetFont(InhStyle::Title(52));
	AddText(Column, LOCTEXT("Tagline", "Every life lasts one day.\nA stranger inherits whatever you leave behind."),
		21, InhStyle::Text, false, ETextJustify::Center);
	AddSpacer(Column, 40.f);

	UVerticalBox* Card = AddCard(Column);
	UInhGameSubsystem* G = Game();
	if (G && !G->IsRegistered())
	{
		AddText(Card, LOCTEXT("Welcome", "Welcome to Hollow Bay, a foggy port where no one lives past one sunrise."), 19);
		AddLabel(Card, LOCTEXT("NameLabel", "WHAT SHOULD THE CITY CALL YOU?"));
		NameBox = Make<UEditableTextBox>();
		NameBox->SetHintText(LOCTEXT("NameHint", "Leave empty for a random name"));
		Pad(Card->AddChild(NameBox), FMargin(0.f, 6.f));
		BeginButton = AddButton(Card, LOCTEXT("BeginFirst", "Wake up"), InhStyle::EButton::Primary, [this]() { Begin(); });
	}
	else
	{
		const FString Name = G ? G->GetSavedDisplayName() : FString();
		AddText(Card, FText::Format(LOCTEXT("WelcomeBack", "Welcome back, {0}. Hollow Bay remembers you only through what you left behind."),
			FText::FromString(Name)), 19);
		BeginButton = AddButton(Card, LOCTEXT("Continue", "Return to Hollow Bay"), InhStyle::EButton::Primary, [this]() { Begin(); });
	}

	Status = AddText(Card, FText::GetEmpty(), 17, InhStyle::Muted, false, ETextJustify::Center);

	AddSpacer(Column, 24.f);
	const UInhBackendSubsystem* Backend = GetGameInstance() ? GetGameInstance()->GetSubsystem<UInhBackendSubsystem>() : nullptr;
	AddText(Column, FText::Format(LOCTEXT("Server", "Server: {0}"), FText::FromString(Backend ? Backend->GetBaseUrl() : FString())),
		13, InhStyle::Muted, false, ETextJustify::Center);
}

void UInhTitleScreen::Begin()
{
	if (BeginButton)
	{
		BeginButton->SetButtonEnabled(false);
	}
	if (Status)
	{
		Status->SetColorAndOpacity(FSlateColor(InhStyle::Muted));
		Status->SetText(LOCTEXT("Connecting", "The fog parts..."));
	}
	const FString Name = NameBox ? NameBox->GetText().ToString() : FString();
	if (AInhPlayerController* PC = Controller())
	{
		PC->BeginSession(Name);
	}
}

void UInhTitleScreen::ShowError(const FText& Error)
{
	if (BeginButton)
	{
		BeginButton->SetButtonEnabled(true);
	}
	if (Status)
	{
		Status->SetColorAndOpacity(FSlateColor(InhStyle::Danger));
		Status->SetText(Error);
	}
}

#undef LOCTEXT_NAMESPACE
