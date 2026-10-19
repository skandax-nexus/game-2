#include "UI/InhEstateScreen.h"
#include "UI/InhButton.h"
#include "UI/InhWillScreen.h"
#include "Core/InhGameSubsystem.h"
#include "World/InhPlayerController.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

#define LOCTEXT_NAMESPACE "Inheritance"

void UInhEstateScreen::Build(UVerticalBox* Column)
{
	AInhPlayerController* PC = Controller();
	const FInhLife& Life = Game()->GetLife();
	const FInhEstate& E = Life.Estate;
	const bool bCanAct = Life.ActionsLeft > 0;
	auto Spend = [](const FText& Label) { return FText::Format(LOCTEXT("EstateCost", "{0} · 1 action"), Label); };

	UHorizontalBox* Header = AddRow(Column);
	AddText(Header, LOCTEXT("EstateTitle", "YOUR ESTATE"), 26, InhStyle::Brass, true);
	AddButton(Header, LOCTEXT("Back", "Back"), InhStyle::EButton::Quiet, [PC]() { PC->ClosePanel(); }, 18);

	AddText(Column, FText::Format(LOCTEXT("EstateSummary", "{0} · {1} · {2} of {3} actions left"),
		FText::FromString(E.HouseName), InhFormat::Coins(E.Money), FText::AsNumber(Life.ActionsLeft), FText::AsNumber(Life.ActionsMax)), 17, InhStyle::Muted);
	if (!bCanAct)
	{
		AddText(Column, LOCTEXT("SpentAll", "You have spent every action. What remains is your legacy."), 17, InhStyle::Danger);
	}

	if (E.Debts.Num() > 0)
	{
		AddLabel(Column, LOCTEXT("DebtsH", "DEBTS"));
		for (const FInhDebt& D : E.Debts)
		{
			const FString Id = D.Id;
			UVerticalBox* Card = AddCard(Column);
			AddText(Card, FText::Format(LOCTEXT("Owe", "You owe {0} to {1}."), InhFormat::Coins(D.Amount), FText::FromString(D.Creditor)), 19, InhStyle::Text, true);
			if (!D.Note.IsEmpty()) AddText(Card, FText::FromString(D.Note), 15, InhStyle::Muted);
			UHorizontalBox* Row = AddRow(Card);
			AddButton(Row, Spend(LOCTEXT("PayIt", "Pay")), InhStyle::EButton::Primary, [PC, Id]() { PC->RunAction(TEXT("pay_debt"), TEXT("debtId"), Id); }, 17)
				->SetButtonEnabled(bCanAct && E.Money >= D.Amount);
			AddButton(Row, Spend(LOCTEXT("DodgeIt", "Dodge (+50%)")), InhStyle::EButton::Secondary, [PC, Id]() { PC->RunAction(TEXT("dodge_debt"), TEXT("debtId"), Id); }, 17)
				->SetButtonEnabled(bCanAct);
		}
	}

	if (E.Feuds.Num() > 0)
	{
		AddLabel(Column, LOCTEXT("FeudsH", "FEUDS"));
		for (const FInhFeud& F : E.Feuds)
		{
			const FString Id = F.Id;
			UVerticalBox* Card = AddCard(Column);
			AddText(Card, FText::FromString(F.Rival), 19, F.IsBlood() ? InhStyle::Danger : InhStyle::Text, true);
			AddText(Card, FText::Format(LOCTEXT("Over2", "Over {0}."), FText::FromString(F.Reason)), 16, InhStyle::Muted);
			AddButton(Card, Spend(F.IsBlood() ? LOCTEXT("Forgive2", "Forgive") : LOCTEXT("Settle2", "Settle (30 coins)")), InhStyle::EButton::Secondary,
				[PC, Id]() { PC->RunAction(TEXT("settle_feud"), TEXT("feudId"), Id); }, 17)
				->SetButtonEnabled(bCanAct && (F.IsBlood() || E.Money >= 30));
		}
	}

	TArray<const FInhClue*> Open;
	for (const FInhClue& C : E.Clues)
	{
		if (!C.bResolved) Open.Add(&C);
	}
	if (Open.Num() > 0)
	{
		AddLabel(Column, LOCTEXT("CluesH", "CLUES"));
		for (const FInhClue* C : Open)
		{
			const FString Id = C->Id;
			UVerticalBox* Card = AddCard(Column);
			AddText(Card, FText::FromString(C->Title), 19, InhStyle::Brass, true);
			AddText(Card, FText::FromString(C->Text), 16);
			AddButton(Card, Spend(LOCTEXT("Follow", "Follow it")), InhStyle::EButton::Primary, [PC, Id]() { PC->RunAction(TEXT("chase_clue"), TEXT("clueId"), Id); }, 17)
				->SetButtonEnabled(bCanAct);
		}
	}

	if (E.Items.Num() > 0)
	{
		AddLabel(Column, LOCTEXT("ItemsH", "BELONGINGS"));
		for (const FInhItem& I : E.Items)
		{
			const FString Id = I.Id;
			UVerticalBox* Card = AddCard(Column);
			AddText(Card, FText::FromString(I.bHidden ? FString::Printf(TEXT("%s (hidden)"), *I.Name) : I.Name), 19,
				I.IsLedgerPage() ? InhStyle::Brass : InhStyle::Text, true);
			AddText(Card, FText::FromString(I.Description), 15, InhStyle::Muted);
			if (I.Kind == TEXT("letter"))
			{
				AddButton(Card, Spend(LOCTEXT("Deliver2", "Deliver the letter")), InhStyle::EButton::Primary,
					[PC, Id]() { PC->RunAction(TEXT("deliver_letter"), TEXT("itemId"), Id); }, 17)->SetButtonEnabled(bCanAct);
			}
			else if (I.IsSellable())
			{
				const FText Label = I.IsLedgerPage() ? LOCTEXT("Sell2Thrane", "Sell to House Thrane for 300 (lost forever)")
					: FText::Format(LOCTEXT("Sell2", "Sell for {0}"), FText::AsNumber(FMath::FloorToInt(I.Value * 0.8f)));
				AddButton(Card, Spend(Label), I.IsLedgerPage() ? InhStyle::EButton::Danger : InhStyle::EButton::Secondary,
					[PC, Id]() { PC->RunAction(TEXT("sell_item"), TEXT("itemId"), Id); }, 17)->SetButtonEnabled(bCanAct);
			}
		}
	}

	AddLabel(Column, LOCTEXT("CityH", "THE CITY"));
	UVerticalBox* Bribe = AddCard(Column);
	AddText(Bribe, LOCTEXT("BribeText", "A Notary clerk drinks at the Drowned Lamb. For 50 coins he remembers things."), 17);
	AddButton(Bribe, Spend(LOCTEXT("Bribe", "Bribe a Notary clerk (50)")), InhStyle::EButton::Secondary,
		[PC]() { PC->RunAction(TEXT("bribe_notary")); }, 17)->SetButtonEnabled(bCanAct && E.Money >= 50);

	UVerticalBox* Feud = AddCard(Column);
	AddText(Feud, LOCTEXT("StartFeudText", "Start a feud. Your heir will inherit it, whether they like it or not."), 17);
	RivalBox = Make<UEditableTextBox>();
	RivalBox->SetHintText(LOCTEXT("RivalHint", "Who wronged you?"));
	Pad(Feud->AddChild(RivalBox), FMargin(0.f, 6.f));
	AddButton(Feud, Spend(LOCTEXT("StartFeud", "Declare the feud")), InhStyle::EButton::Danger, [this, PC]()
	{
		const FString Rival = RivalBox ? RivalBox->GetText().ToString().TrimStartAndEnd() : FString();
		if (Rival.IsEmpty())
		{
			Toast(LOCTEXT("NameRival", "Name your rival first."));
			return;
		}
		PC->RunAction(TEXT("start_feud"), TEXT("rival"), Rival);
	}, 17)->SetButtonEnabled(bCanAct);

	AddSpacer(Column, 16.f);
	AddButton(Column, LOCTEXT("BackToRoom", "Back to the room"), InhStyle::EButton::Secondary, [PC]() { PC->ClosePanel(); });
}

#undef LOCTEXT_NAMESPACE
