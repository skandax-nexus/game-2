#include "UI/InhLineageScreen.h"
#include "UI/InhButton.h"
#include "Core/InhGameSubsystem.h"
#include "World/InhPlayerController.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

#define LOCTEXT_NAMESPACE "Inheritance"

void UInhLineageScreen::NativeConstruct()
{
	Super::NativeConstruct();
	TWeakObjectPtr<UInhLineageScreen> Weak(this);
	Game()->FetchFamilyTree([Weak](bool bOk, const TArray<FInhFamilyEntry>& Result)
	{
		if (!Weak.IsValid()) return;
		Weak->Entries = Result;
		Weak->bLoaded = true;
		Weak->Refresh();
	});
}

void UInhLineageScreen::Build(UVerticalBox* Column)
{
	AInhPlayerController* PC = Controller();

	UHorizontalBox* Header = AddRow(Column);
	AddText(Header, LOCTEXT("LineageTitle", "YOUR LINEAGE"), 26, InhStyle::Brass, true);
	AddButton(Header, LOCTEXT("BackT", "Back"), InhStyle::EButton::Quiet, [PC]() { PC->ClosePanel(); }, 18);

	if (!bLoaded)
	{
		AddText(Column, LOCTEXT("Reading", "Reading the Notaries' records…"), 18, InhStyle::Muted);
		return;
	}
	AddText(Column, FText::Format(LOCTEXT("LivesCount", "{0} lives lived. Each one began with a stranger's leftovers and ended with yours."),
		FText::AsNumber(Entries.Num())), 17, InhStyle::Muted);

	// Newest first.
	for (int32 i = Entries.Num() - 1; i >= 0; --i)
	{
		const FInhFamilyEntry& E = Entries[i];
		const bool bAlive = E.Status == TEXT("alive");
		UVerticalBox* Card = AddCard(Column, bAlive ? InhStyle::CardRaised : InhStyle::Card);
		AddLabel(Card, FText::Format(LOCTEXT("LifeN", "LIFE {0} · {1}"), FText::AsNumber(i + 1), FText::FromString(E.StartedAt.ToString(TEXT("%d %b %Y")))));
		AddText(Card, FText::FromString(E.House), 19, InhStyle::Text, true);
		AddText(Card, E.InheritedFrom.IsEmpty() ? LOCTEXT("FreshEstate", "Began with an unclaimed estate.")
			: FText::Format(LOCTEXT("From", "Inherited from {0}."), FText::FromString(E.InheritedFrom)), 16);
		FText Fate;
		if (bAlive)
		{
			Fate = LOCTEXT("LivingNow", "You are living this life now.");
		}
		else if (!E.PassedTo.IsEmpty())
		{
			Fate = FText::Format(LOCTEXT("PassedTo", "Passed to {0}."), FText::FromString(E.PassedTo));
		}
		else
		{
			Fate = LOCTEXT("Waiting", "Waiting for a stranger to inherit it.");
		}
		AddText(Card, Fate, 16, bAlive ? InhStyle::Good : InhStyle::Sea);
		if (E.bHasVoiceNote)
		{
			AddText(Card, LOCTEXT("LeftNote", "You left a voice note."), 14, InhStyle::Muted);
		}
	}

	AddSpacer(Column, 16.f);
	AddButton(Column, LOCTEXT("BackToRoomT", "Back to the room"), InhStyle::EButton::Secondary, [PC]() { PC->ClosePanel(); });
}

#undef LOCTEXT_NAMESPACE
