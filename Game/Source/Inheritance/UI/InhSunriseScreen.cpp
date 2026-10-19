#include "UI/InhSunriseScreen.h"
#include "UI/InhButton.h"
#include "World/InhPlayerController.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

#define LOCTEXT_NAMESPACE "Inheritance"

UInhSunriseScreen::UInhSunriseScreen(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Let the dawn show through.
	BackdropOpacity = 0.55f;
}

void UInhSunriseScreen::Build(UVerticalBox* Column)
{
	AddSpacer(Column, 120.f);
	UTextBlock* Title = AddText(Column, LOCTEXT("SunriseTitle", "SUNRISE"), 48, InhStyle::Brass, true, ETextJustify::Center);
	Title->SetFont(InhStyle::Title(48));
	AddText(Column, LOCTEXT("Ferryman", "The Ferryman rows you out past the harbour wall. He does not speak."), 21, InhStyle::Text, false, ETextJustify::Center);
	AddText(Column, LOCTEXT("Passed", "Everything you had now belongs to a stranger somewhere in the world. Your voice is the only thing that goes with it."),
		18, InhStyle::Muted, false, ETextJustify::Center);
	AddSpacer(Column, 40.f);
	AddButton(Column, LOCTEXT("WakeNew", "Wake in a new life"), InhStyle::EButton::Primary, [this]()
	{
		if (AInhPlayerController* PC = Controller())
		{
			PC->StartNewLife();
		}
	}, 24);
}

#undef LOCTEXT_NAMESPACE
