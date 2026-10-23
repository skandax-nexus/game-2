#include "UI/InhCityScreen.h"
#include "UI/InhButton.h"
#include "Core/InhGameSubsystem.h"
#include "World/InhPlayerController.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

#define LOCTEXT_NAMESPACE "Inheritance"

void UInhCityScreen::NativeConstruct()
{
	Super::NativeConstruct();
	TWeakObjectPtr<UInhCityScreen> Weak(this);
	Game()->RefreshSeason([Weak](bool, const FText&) { if (Weak.IsValid()) Weak->Refresh(); });
}

void UInhCityScreen::Build(UVerticalBox* Column)
{
	AInhPlayerController* PC = Controller();
	UInhGameSubsystem* G = Game();
	const FInhSeason& S = G->GetSeason();
	const FInhPlayer& Me = G->GetPlayer();

	UHorizontalBox* Header = AddRow(Column);
	AddText(Header, LOCTEXT("CityTitle", "HOLLOW BAY"), 26, InhStyle::Brass, true);
	AddButton(Header, LOCTEXT("BackC", "Back"), InhStyle::EButton::Quiet, [PC]() { PC->ClosePanel(); }, 18);

	if (!S.bValid)
	{
		AddText(Column, LOCTEXT("Loading", "The fog is too thick to see the city. Trying again…"), 18, InhStyle::Muted);
		return;
	}

	UVerticalBox* Act = AddCard(Column, InhStyle::CardRaised);
	AddLabel(Act, FText::Format(LOCTEXT("SeasonDay", "SEASON 1 · DAY {0} OF {1}"), FText::AsNumber(S.Day), FText::AsNumber(S.LengthDays)));
	AddText(Act, FText::FromString(S.ActName), 24, InhStyle::Brass, true);
	AddText(Act, FText::FromString(S.ActSummary), 17);

	UVerticalBox* Ledger = AddCard(Column);
	AddLabel(Ledger, LOCTEXT("LedgerH", "THE LEDGER"));
	AddText(Ledger, FText::Format(LOCTEXT("Pages", "{0} pages found by the city · {1} lost forever"), FText::AsNumber(S.PagesFound), FText::AsNumber(S.PagesLost)), 18);
	AddText(Ledger, FText::Format(LOCTEXT("Citizens", "{0} citizens alive tonight · {1} rowed out by the Ferryman"),
		FText::AsNumber(S.CitizensAlive), FText::AsNumber(S.CitizensDeparted)), 16, InhStyle::Muted);

	AddLabel(Column, LOCTEXT("FactionsH", "KEEPERS AND BREAKERS"));
	if (!S.bFactionsUnlocked)
	{
		AddText(Column, LOCTEXT("NotYet", "The city has not split yet. When Act 2 begins, every citizen must choose a side."), 17, InhStyle::Muted);
	}
	for (const FInhFaction& F : S.Factions)
	{
		const FString Id = F.Id;
		const bool bMine = Me.Faction == F.Id;
		UVerticalBox* Card = AddCard(Column, bMine ? InhStyle::CardRaised : InhStyle::Card);
		AddText(Card, FText::Format(LOCTEXT("FactionScore", "{0} · {1} pages"), FText::FromString(F.Name), FText::AsNumber(F.Score)), 20, InhStyle::Text, true);
		AddText(Card, FText::FromString(F.Creed), 16, InhStyle::Muted);
		if (bMine)
		{
			AddText(Card, LOCTEXT("YouStand", "You stand with them, in every life this season."), 16, InhStyle::Good, true);
		}
		else if (S.bFactionsUnlocked && Me.Faction.IsEmpty())
		{
			AddButton(Card, FText::Format(LOCTEXT("Join", "Join {0}"), FText::FromString(F.Name)), InhStyle::EButton::Primary, [this, Id]()
			{
				Game()->ChooseFaction(Id, ToastResult());
			}, 18);
		}
	}

	if (S.Twists.Num() > 0)
	{
		AddLabel(Column, LOCTEXT("TwistsH", "WHAT THE CITY HAS LEARNED"));
		for (const FInhTwist& T : S.Twists)
		{
			UVerticalBox* Card = AddCard(Column);
			AddText(Card, FText::FromString(T.Title), 19, InhStyle::Brass, true);
			AddText(Card, FText::FromString(T.Text), 16);
		}
	}

	if (G->GetCharacters().Num() > 0)
	{
		AddLabel(Column, LOCTEXT("PeopleH", "THE PEOPLE OF HOLLOW BAY"));
		for (const FInhCharacter& C : G->GetCharacters())
		{
			UVerticalBox* Card = AddCard(Column);
			AddText(Card, FText::Format(LOCTEXT("CharName", "{0}, {1}"), FText::FromString(C.Name), FText::FromString(C.Title)), 18, InhStyle::Text, true);
			AddText(Card, FText::FromString(C.Bio), 15, InhStyle::Muted);
		}
	}

	AddSpacer(Column, 16.f);
	AddButton(Column, LOCTEXT("BackToRoomC", "Back to the room"), InhStyle::EButton::Secondary, [PC]() { PC->ClosePanel(); });
}

#undef LOCTEXT_NAMESPACE
