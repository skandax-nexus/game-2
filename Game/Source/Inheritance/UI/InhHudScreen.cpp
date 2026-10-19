#include "UI/InhHudScreen.h"
#include "UI/InhButton.h"
#include "UI/InhWillScreen.h"
#include "UI/InhEstateScreen.h"
#include "UI/InhLegacyScreen.h"
#include "UI/InhCityScreen.h"
#include "UI/InhLineageScreen.h"
#include "Core/InhGameSubsystem.h"
#include "World/InhPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

#define LOCTEXT_NAMESPACE "Inheritance"

void UInhHudScreen::BuildFrame()
{
	// No backdrop: the room is the screen. Empty space lets touches through to the world.
	UOverlay* Root = Make<UOverlay>();
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Root;

	USafeZone* Safe = Make<USafeZone>();
	Safe->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UOverlaySlot* SafeSlot = Root->AddChildToOverlay(Safe);
	SafeSlot->SetHorizontalAlignment(HAlign_Fill);
	SafeSlot->SetVerticalAlignment(VAlign_Fill);

	UVerticalBox* Layout = Make<UVerticalBox>();
	Layout->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Safe->AddChild(Layout);

	auto AddBar = [&](float Top, float Bottom) -> UVerticalBox*
	{
		UVerticalBox* Bar = Make<UVerticalBox>();
		Bar->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		UVerticalBoxSlot* BarSlot = Layout->AddChildToVerticalBox(Bar);
		BarSlot->SetPadding(FMargin(14.f, Top, 14.f, Bottom));
		return Bar;
	};

	TopBar = AddBar(12.f, 0.f);

	USpacer* Middle = Make<USpacer>();
	Middle->SetVisibility(ESlateVisibility::HitTestInvisible);
	Layout->AddChildToVerticalBox(Middle)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	UVerticalBox* CardHolder = AddBar(0.f, 6.f);
	PropCardBox = Make<UBorder>();
	PropCardBox->SetBrush(InhStyle::Rounded(InhStyle::Card, 20.f));
	PropCardBox->SetPadding(FMargin(20.f, 16.f));
	PropCardBox->SetVisibility(ESlateVisibility::Collapsed);
	CardHolder->AddChildToVerticalBox(PropCardBox);
	PropCard = Make<UVerticalBox>();
	PropCardBox->SetContent(PropCard);

	BottomBar = AddBar(0.f, 12.f);

	// Build() fills Content; the HUD uses it for the top bar.
	Content = TopBar;
}

void UInhHudScreen::Build(UVerticalBox* Column)
{
	const FInhLife& Life = Game()->GetLife();

	// Top: where you are, how long you have, what you have.
	UVerticalBox* Top = AddCard(Column);
	UHorizontalBox* Row = AddRow(Top);
	UTextBlock* House = AddText(Row, FText::FromString(Life.Estate.HouseName), 18, InhStyle::Text, true);
	House->SetAutoWrapText(false);
	UTextBlock* Money = AddText(Row, InhFormat::Coins(Life.Estate.Money), 18, InhStyle::Brass, true, ETextJustify::Right);
	Money->SetAutoWrapText(false);

	UHorizontalBox* Row2 = AddRow(Top);
	ClockText = AddText(Row2, FText::GetEmpty(), 17, InhStyle::Sea, true);
	ClockText->SetAutoWrapText(false);
	const FLinearColor ActionColor = Life.ActionsLeft > 0 ? InhStyle::Text : InhStyle::Danger;
	UTextBlock* Actions = AddText(Row2, FText::Format(LOCTEXT("ActionsLeft", "{0} / {1} actions"), FText::AsNumber(Life.ActionsLeft),
		FText::AsNumber(Life.ActionsMax)), 17, ActionColor, true, ETextJustify::Right);
	Actions->SetAutoWrapText(false);
	UpdateClock();

	if (Life.Log.Num() > 0)
	{
		AddText(Top, FText::FromString(Life.Log.Last()), 14, InhStyle::Muted);
	}

	// Bottom: the four places a player goes.
	BottomBar->ClearChildren();
	UHorizontalBox* Menu = AddRow(BottomBar);
	AddButton(Menu, LOCTEXT("Estate", "Estate"), InhStyle::EButton::Secondary, [this]() { Controller()->OpenPanel<UInhEstateScreen>(); }, 18);
	AddButton(Menu, LOCTEXT("Legacy", "Legacy"), InhStyle::EButton::Primary, [this]() { Controller()->OpenPanel<UInhLegacyScreen>(); }, 18);
	AddButton(Menu, LOCTEXT("City", "City"), InhStyle::EButton::Secondary, [this]() { Controller()->OpenPanel<UInhCityScreen>(); }, 18);
	AddButton(Menu, LOCTEXT("Lineage", "Lineage"), InhStyle::EButton::Secondary, [this]() { Controller()->OpenPanel<UInhLineageScreen>(); }, 18);

	if (bPropCardOpen)
	{
		BuildPropCard();
	}
}

void UInhHudScreen::UpdateClock()
{
	if (ClockText)
	{
		const FInhLife& Life = Game()->GetLife();
		ClockText->SetText(FText::Format(LOCTEXT("Sunrise", "Sunrise in {0}"), InhFormat::Countdown(Life.TimeLeft())));
	}
}

void UInhHudScreen::ShowPropCard(EInhPropKind Kind, const FString& RefId)
{
	bPropCardOpen = true;
	CardKind = Kind;
	CardRefId = RefId;
	BuildPropCard();
}

void UInhHudScreen::HidePropCard()
{
	bPropCardOpen = false;
	if (PropCardBox)
	{
		PropCardBox->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (AInhPlayerController* PC = Controller())
	{
		PC->ClearSelection();
	}
}

void UInhHudScreen::BuildPropCard()
{
	UInhGameSubsystem* G = Game();
	AInhPlayerController* PC = Controller();
	const FInhLife& Life = G->GetLife();
	const FInhEstate& E = Life.Estate;
	const bool bCanAct = Life.ActionsLeft > 0;
	PropCard->ClearChildren();

	auto Spend = [](const FText& Label) { return FText::Format(LOCTEXT("CostsAction", "{0} · 1 action"), Label); };
	bool bFound = true;

	switch (CardKind)
	{
	case EInhPropKind::Phone:
	{
		AddLabel(PropCard, LOCTEXT("PhoneLabel", "THE PHONE"));
		if (Life.bHasPrologue)
		{
			AddText(PropCard, FText::Format(LOCTEXT("Quote2", "“{0}”"), FText::FromString(Life.PrologueTranscript)), 20, InhStyle::Brass);
		}
		else if (Life.bHasPredecessor && Life.bPredecessorHasVoiceNote)
		{
			AddText(PropCard, FText::Format(LOCTEXT("LastWords", "{0}'s last words to you."), FText::FromString(Life.PredecessorName)), 19);
			AddButton(PropCard, LOCTEXT("Replay", "Play the voice note"), InhStyle::EButton::Primary, [this]()
			{
				Game()->PlayPredecessorVoiceNote(ToastResult());
			});
		}
		else
		{
			AddText(PropCard, LOCTEXT("NoMessages", "No messages. Whoever owned this phone said nothing at all."), 19);
		}
		break;
	}
	case EInhPropKind::Window:
	{
		AddLabel(PropCard, LOCTEXT("WindowLabel", "HOLLOW BAY"));
		AddText(PropCard, FText::Format(LOCTEXT("ActTitle", "Act {0}: {1}"), FText::AsNumber(Life.ActId), FText::FromString(Life.ActName)), 20, InhStyle::Brass, true);
		AddText(PropCard, FText::FromString(Life.ActSummary), 17);
		AddButton(PropCard, LOCTEXT("OpenCity", "Look out over the city"), InhStyle::EButton::Secondary, [PC]() { PC->OpenPanel<UInhCityScreen>(); });
		break;
	}
	case EInhPropKind::Debt:
	{
		const FInhDebt* D = E.FindDebt(CardRefId);
		if (!D) { bFound = false; break; }
		const FString Id = D->Id;
		AddLabel(PropCard, LOCTEXT("DemandLabel", "A DEMAND FOR PAYMENT"));
		AddText(PropCard, FText::Format(LOCTEXT("Demands", "{0} demands {1}."), FText::FromString(D->Creditor), InhFormat::Coins(D->Amount)), 20, InhStyle::Danger, true);
		if (!D->Note.IsEmpty()) AddText(PropCard, FText::FromString(D->Note), 16, InhStyle::Muted);
		UHorizontalBox* Row = AddRow(PropCard);
		UInhButton* Pay = AddButton(Row, Spend(LOCTEXT("Pay", "Pay")), InhStyle::EButton::Primary, [PC, Id]() { PC->RunAction(TEXT("pay_debt"), TEXT("debtId"), Id); }, 18);
		Pay->SetButtonEnabled(bCanAct && E.Money >= D->Amount);
		UInhButton* Dodge = AddButton(Row, Spend(LOCTEXT("Dodge", "Dodge")), InhStyle::EButton::Secondary, [PC, Id]() { PC->RunAction(TEXT("dodge_debt"), TEXT("debtId"), Id); }, 18);
		Dodge->SetButtonEnabled(bCanAct);
		break;
	}
	case EInhPropKind::Feud:
	{
		const FInhFeud* F = E.FindFeud(CardRefId);
		if (!F) { bFound = false; break; }
		const FString Id = F->Id;
		AddLabel(PropCard, F->IsBlood() ? LOCTEXT("BloodLabel", "A BLOOD FEUD") : LOCTEXT("FeudLabel", "A FEUD"));
		AddText(PropCard, FText::FromString(F->Rival), 20, InhStyle::Text, true);
		AddText(PropCard, FText::Format(LOCTEXT("Over", "Over {0}."), FText::FromString(F->Reason)), 17, InhStyle::Muted);
		UInhButton* Settle = AddButton(PropCard, Spend(F->IsBlood() ? LOCTEXT("Forgive", "Forgive and end the chain") : LOCTEXT("Settle", "Settle it (30 coins)")),
			InhStyle::EButton::Primary, [PC, Id]() { PC->RunAction(TEXT("settle_feud"), TEXT("feudId"), Id); }, 18);
		Settle->SetButtonEnabled(bCanAct && (F->IsBlood() || E.Money >= 30));
		break;
	}
	case EInhPropKind::Clue:
	{
		const FInhClue* C = E.FindClue(CardRefId);
		if (!C) { bFound = false; break; }
		const FString Id = C->Id;
		AddLabel(PropCard, LOCTEXT("ClueLabel", "A CLUE"));
		AddText(PropCard, FText::FromString(C->Title), 20, InhStyle::Brass, true);
		AddText(PropCard, FText::FromString(C->Text), 17);
		UInhButton* Chase = AddButton(PropCard, Spend(LOCTEXT("Chase", "Follow this clue")), InhStyle::EButton::Primary,
			[PC, Id]() { PC->RunAction(TEXT("chase_clue"), TEXT("clueId"), Id); }, 18);
		Chase->SetButtonEnabled(bCanAct && !C->bResolved);
		break;
	}
	case EInhPropKind::Item:
	case EInhPropKind::HiddenItem:
	{
		const FInhItem* I = E.FindItem(CardRefId);
		if (!I) { bFound = false; break; }
		const FString Id = I->Id;
		AddLabel(PropCard, I->IsLedgerPage() ? LOCTEXT("PageLabel", "A PAGE OF THE LEDGER") : LOCTEXT("ItemLabel", "YOURS, FOR NOW"));
		AddText(PropCard, FText::FromString(I->Name), 20, I->IsLedgerPage() ? InhStyle::Brass : InhStyle::Text, true);
		AddText(PropCard, FText::FromString(I->Description), 17);
		if (I->Kind == TEXT("box"))
		{
			AddText(PropCard, FText::Format(LOCTEXT("Keyholes", "{0} of 5 keyholes turned. Keep it and pass it on."), FText::AsNumber(I->Streak)), 16, InhStyle::Muted);
		}
		AddText(PropCard, FText::Format(LOCTEXT("AtSunrise", "At sunrise you will: {0}"), FText::FromString(Life.LegacyChoiceFor(Id))), 15, InhStyle::Sea);

		UHorizontalBox* Row = AddRow(PropCard);
		if (I->Kind == TEXT("letter"))
		{
			UInhButton* Deliver = AddButton(Row, Spend(LOCTEXT("Deliver", "Deliver it")), InhStyle::EButton::Primary,
				[PC, Id]() { PC->RunAction(TEXT("deliver_letter"), TEXT("itemId"), Id); }, 18);
			Deliver->SetButtonEnabled(bCanAct);
		}
		else if (I->IsSellable())
		{
			const FText SellLabel = I->IsLedgerPage() ? LOCTEXT("SellThrane", "Sell to House Thrane (300)")
				: FText::Format(LOCTEXT("SellFor", "Sell ({0})"), FText::AsNumber(FMath::FloorToInt(I->Value * 0.8f)));
			UInhButton* Sell = AddButton(Row, Spend(SellLabel), I->IsLedgerPage() ? InhStyle::EButton::Danger : InhStyle::EButton::Secondary,
				[PC, Id]() { PC->RunAction(TEXT("sell_item"), TEXT("itemId"), Id); }, 18);
			Sell->SetButtonEnabled(bCanAct);
		}
		AddButton(Row, LOCTEXT("DecideLegacy", "Decide its fate"), InhStyle::EButton::Secondary, [PC]() { PC->OpenPanel<UInhLegacyScreen>(); }, 18);
		break;
	}
	}

	if (!bFound)
	{
		// The thing is gone (paid, sold, resolved).
		HidePropCard();
		return;
	}
	if (!bCanAct && CardKind != EInhPropKind::Phone && CardKind != EInhPropKind::Window)
	{
		AddText(PropCard, LOCTEXT("NoActions", "You have no actions left. Decide your legacy and record your voice note before sunrise."), 15, InhStyle::Muted);
	}
	AddButton(PropCard, LOCTEXT("Close", "Close"), InhStyle::EButton::Quiet, [this]() { HidePropCard(); }, 16);
	PropCardBox->SetVisibility(ESlateVisibility::Visible);
}

#undef LOCTEXT_NAMESPACE
