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

namespace
{
	/** Lifts text off a moving picture. */
	void AddTextShadow(UTextBlock* Text)
	{
		Text->SetShadowOffset(FVector2D(0.f, 2.f));
		Text->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f));
	}
}

UInhTitleScreen::UInhTitleScreen(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// No dim: the harbour cinematic is the backdrop. A soft shade at the top keeps the title readable. In portrait
	// everything sits in the upper part and the card carries its own background, so the bottom shade only grounds the
	// frame and must stay light, or it hides the water and quay of the attract shots.
	BackdropOpacity = 0.f;
	TopShade = 0.5f;
	BottomShade = 0.35f;
}

void UInhTitleScreen::Build(UVerticalBox* Column)
{
	AddSpacer(Column, 64.f);
	UTextBlock* Title = AddText(Column, LOCTEXT("GameTitle", "INHERITANCE"), 52, InhStyle::Brass, true, ETextJustify::Center);
	Title->SetFont(InhStyle::Title(52));
	AddTextShadow(Title);
	UTextBlock* Tagline = AddText(Column, LOCTEXT("Tagline", "Every life lasts one day.\nA stranger inherits whatever you leave behind."),
		21, InhStyle::Text, false, ETextJustify::Center);
	AddTextShadow(Tagline);
	// Leave the middle of the screen to the harbour.
	AddSpacer(Column, 220.f);

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
	UTextBlock* Server = AddText(Column, FText::Format(LOCTEXT("Server", "Server: {0}"), FText::FromString(Backend ? Backend->GetBaseUrl() : FString())),
		13, InhStyle::Muted, false, ETextJustify::Center);
	AddTextShadow(Server);
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
