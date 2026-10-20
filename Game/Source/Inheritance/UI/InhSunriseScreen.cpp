#include "UI/InhSunriseScreen.h"
#include "UI/InhButton.h"
#include "World/InhPlayerController.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

#define LOCTEXT_NAMESPACE "Inheritance"

UInhSunriseScreen::UInhSunriseScreen(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Let the dawn show through: barely dimmed, with a deeper shade low down where the words sit.
	BackdropOpacity = 0.12f;
	TopShade = 0.35f;
	BottomShade = 0.85f;
}

void UInhSunriseScreen::Build(UVerticalBox* Column)
{
	// The upper half of the screen belongs to the sunrise.
	AddSpacer(Column, 300.f);
	UTextBlock* Title = AddText(Column, LOCTEXT("SunriseTitle", "SUNRISE"), 48, InhStyle::Brass, true, ETextJustify::Center);
	Title->SetFont(InhStyle::Title(48));
	Title->SetShadowOffset(FVector2D(0.f, 2.f));
	Title->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f));
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
