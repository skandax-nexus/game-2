#include "UI/InhWillScreen.h"
#include "UI/InhButton.h"
#include "Core/InhGameSubsystem.h"
#include "World/InhPlayerController.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"

#define LOCTEXT_NAMESPACE "Inheritance"

FText InhFormat::Countdown(const FTimespan& Left)
{
	if (Left.GetTicks() <= 0)
	{
		return LOCTEXT("Now", "now");
	}
	const int32 Hours = int32(Left.GetTotalHours());
	return FText::FromString(FString::Printf(TEXT("%dh %02dm %02ds"), Hours, Left.GetMinutes(), Left.GetSeconds()));
}

FText InhFormat::LocalTime(const FDateTime& Utc)
{
	const FDateTime Local = Utc + (FDateTime::Now() - FDateTime::UtcNow());
	return FText::FromString(Local.ToString(TEXT("%H:%M")));
}

FText InhFormat::Coins(int32 Amount)
{
	return FText::Format(LOCTEXT("CoinsFmt", "{0} coins"), FText::AsNumber(Amount));
}

void UInhWillScreen::Build(UVerticalBox* Column)
{
	UInhGameSubsystem* G = Game();
	const FInhLife& Life = G->GetLife();
	const FInhEstate& E = Life.Estate;

	AddHeading(Column, LOCTEXT("WillTitle", "THE READING OF THE WILL"), true);
	AddText(Column, FText::Format(LOCTEXT("ActLine", "Act {0} · {1} · Day {2}"), FText::AsNumber(Life.ActId),
		FText::FromString(Life.ActName), FText::AsNumber(Life.SeasonDay)), 16, InhStyle::Muted, true, ETextJustify::Center);

	// Who you are inheriting from, and their last words.
	UVerticalBox* Voice = AddCard(Column, InhStyle::CardRaised);
	if (Life.bHasPrologue)
	{
		AddText(Voice, FText::FromString(Life.PrologueSetting), 20);
		AddLabel(Voice, LOCTEXT("OneNote", "THE PHONE HOLDS ONE VOICE NOTE. AN OLD MAN'S VOICE:"));
		AddText(Voice, FText::Format(LOCTEXT("Quote", "“{0}”"), FText::FromString(Life.PrologueTranscript)), 23, InhStyle::Brass);
		AddText(Voice, LOCTEXT("EveryPlayer", "Every new citizen hears this same message. No one knows who recorded it."), 16, InhStyle::Muted);
	}
	else if (Life.bHasPredecessor)
	{
		AddText(Voice, FText::Format(LOCTEXT("YouInherit", "You inherit the life of {0}. They died at sunrise."),
			FText::FromString(Life.PredecessorName)), 21);
		if (Life.bPredecessorHasVoiceNote)
		{
			AddText(Voice, LOCTEXT("TheyLeft", "They left you 30 seconds in their own words. Honest advice, a warning, or a lie."), 17, InhStyle::Muted);
			UHorizontalBox* Row = AddRow(Voice);
			AddButton(Row, LOCTEXT("Play", "Play their voice note"), InhStyle::EButton::Primary, [this]()
			{
				Game()->PlayPredecessorVoiceNote(ToastResult());
			});
			AddButton(Row, LOCTEXT("Report", "Report"), InhStyle::EButton::Quiet, [this]()
			{
				Game()->ReportPredecessorVoiceNote(ToastResult(false));
			}, 16);
		}
		else
		{
			AddText(Voice, LOCTEXT("Silence", "They left no voice note. Only silence."), 18, InhStyle::Muted);
		}
	}
	else
	{
		AddText(Voice, LOCTEXT("Fresh", "No one left this estate to you. The Notaries assigned it from the city's unclaimed dead."), 19);
	}

	// What you got.
	UVerticalBox* House = AddCard(Column);
	AddLabel(House, LOCTEXT("YourHome", "YOUR HOME"));
	AddText(House, FText::FromString(E.HouseName), 24, InhStyle::Text, true);
	AddText(House, FText::FromString(E.HouseDescription), 18, InhStyle::Muted);
	AddText(House, FText::Format(LOCTEXT("Purse", "In the purse: {0}"), InhFormat::Coins(E.Money)), 20, InhStyle::Brass, true);
	if (E.PreviousOwners.Num() > 0)
	{
		AddText(House, FText::Format(LOCTEXT("Chain", "This estate has passed through {0} lives: {1}"), FText::AsNumber(E.PreviousOwners.Num()),
			FText::FromString(FString::Join(E.PreviousOwners, TEXT(" → ")))), 15, InhStyle::Muted);
	}

	if (E.Debts.Num() > 0)
	{
		UVerticalBox* Debts = AddCard(Column);
		AddLabel(Debts, LOCTEXT("Debts", "DEBTS YOU NEVER MADE"));
		for (const FInhDebt& D : E.Debts)
		{
			AddText(Debts, FText::Format(LOCTEXT("DebtLine", "{0} — {1}"), FText::FromString(D.Creditor), InhFormat::Coins(D.Amount)), 19, InhStyle::Danger);
		}
	}

	int32 HiddenCount = 0;
	UVerticalBox* Items = AddCard(Column);
	AddLabel(Items, LOCTEXT("Belongings", "BELONGINGS"));
	for (const FInhItem& I : E.Items)
	{
		if (I.bHidden)
		{
			++HiddenCount;
			continue;
		}
		AddText(Items, FText::FromString(I.Name), 19, I.IsLedgerPage() ? InhStyle::Brass : InhStyle::Text, I.IsLedgerPage());
	}
	if (HiddenCount > 0)
	{
		AddText(Items, LOCTEXT("SomethingHidden", "Your predecessor hid something in the house."), 17, InhStyle::Sea);
	}

	if (E.Feuds.Num() > 0)
	{
		UVerticalBox* Feuds = AddCard(Column);
		AddLabel(Feuds, LOCTEXT("Enemies", "ENEMIES YOU HAVE NEVER MET"));
		for (const FInhFeud& F : E.Feuds)
		{
			AddText(Feuds, FText::Format(LOCTEXT("FeudLine", "{0}, over {1}"), FText::FromString(F.Rival), FText::FromString(F.Reason)), 18);
		}
	}

	if (E.PersonalStory.bValid)
	{
		UVerticalBox* Story = AddCard(Column, InhStyle::CardRaised);
		AddLabel(Story, LOCTEXT("PersonalStory", "A STORY HANDED DOWN"));
		AddText(Story, FText::FromString(E.PersonalStory.Title), 21, InhStyle::Brass, true);
		AddText(Story, FText::FromString(E.PersonalStory.Text), 18);
		if (E.PersonalStory.Heirs > 0)
		{
			AddText(Story, FText::Format(LOCTEXT("Heirs", "{0} heirs before you carried this."), FText::AsNumber(E.PersonalStory.Heirs)), 15, InhStyle::Muted);
		}
	}

	UVerticalBox* Clock = AddCard(Column);
	AddText(Clock, FText::Format(LOCTEXT("YouDie", "You will die at sunrise, {0} your time."), InhFormat::LocalTime(Life.DiesAt)), 20, InhStyle::Text, true);
	AddText(Clock, FText::Format(LOCTEXT("Actions", "You have {0} actions before then. Spend them well, decide what to leave, and record a voice note for whoever comes next."),
		FText::AsNumber(Life.ActionsLeft)), 17, InhStyle::Muted);

	AddButton(Column, LOCTEXT("BeginDay", "Begin your day"), InhStyle::EButton::Primary, [this]()
	{
		if (AInhPlayerController* PC = Controller())
		{
			PC->EnterRoom();
		}
	}, 24);
	AddSpacer(Column, 30.f);
}

#undef LOCTEXT_NAMESPACE
