#include "UI/InhLegacyScreen.h"
#include "UI/InhButton.h"
#include "UI/InhWillScreen.h"
#include "Audio/InhVoiceRecorder.h"
#include "Core/InhGameSubsystem.h"
#include "Core/InhSettings.h"
#include "World/InhPlayerController.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

#define LOCTEXT_NAMESPACE "Inheritance"

namespace
{
	struct FChoice { const TCHAR* Id; FText Label; };

	TArray<FChoice> Choices()
	{
		return {
			{ TEXT("keep"), LOCTEXT("Keep", "Keep") },
			{ TEXT("sell"), LOCTEXT("SellC", "Sell") },
			{ TEXT("hide"), LOCTEXT("Hide", "Hide") },
			{ TEXT("destroy"), LOCTEXT("Destroy", "Destroy") },
		};
	}
}

void UInhLegacyScreen::Build(UVerticalBox* Column)
{
	if (!Recorder)
	{
		Recorder = NewObject<UInhVoiceRecorder>(this);
	}
	AInhPlayerController* PC = Controller();
	UInhGameSubsystem* G = Game();
	const FInhLife& Life = G->GetLife();

	UHorizontalBox* Header = AddRow(Column);
	AddText(Header, LOCTEXT("LegacyTitle", "YOUR LEGACY"), 26, InhStyle::Brass, true);
	AddButton(Header, LOCTEXT("BackL", "Back"), InhStyle::EButton::Quiet, [PC]() { PC->ClosePanel(); }, 18);

	AddText(Column, FText::Format(LOCTEXT("LegacyIntro", "At sunrise ({0}, in {1}) everything you leave passes to a stranger. Choose what they receive. These choices are free and you can change them until you die."),
		InhFormat::LocalTime(Life.DiesAt), InhFormat::Countdown(Life.TimeLeft())), 17, InhStyle::Muted);

	// Belongings.
	for (const FInhItem& I : Life.Estate.Items)
	{
		const FString Id = I.Id;
		const FString Current = Life.LegacyChoiceFor(Id);
		UVerticalBox* Card = AddCard(Column);
		AddText(Card, FText::FromString(I.Name), 19, I.IsLedgerPage() ? InhStyle::Brass : InhStyle::Text, true);
		if (I.IsLedgerPage())
		{
			AddText(Card, LOCTEXT("PageWarn", "Selling or destroying a Ledger page loses it for the whole city."), 15, InhStyle::Danger);
		}
		else if (I.Kind == TEXT("box"))
		{
			AddText(Card, LOCTEXT("BoxHint", "Keep or hide it to turn another keyhole. It opens after five heirs."), 15, InhStyle::Muted);
		}
		UHorizontalBox* Row = AddRow(Card);
		for (const FChoice& C : Choices())
		{
			const FString ChoiceId = C.Id;
			const bool bSelected = Current == ChoiceId;
			AddButton(Row, C.Label, bSelected ? InhStyle::EButton::Primary : InhStyle::EButton::Secondary, [this, Id, ChoiceId]()
			{
				Game()->SetLegacyChoice(Id, ChoiceId, ToastResult());
			}, 15);
		}
	}
	if (Life.Estate.Items.Num() == 0)
	{
		AddText(Column, LOCTEXT("NothingLeft", "You own nothing worth deciding about. Your heir inherits only your coins, debts and enemies."), 17);
	}

	// What passes on regardless.
	UVerticalBox* Passes = AddCard(Column);
	AddLabel(Passes, LOCTEXT("PassesH", "WHAT PASSES ON NO MATTER WHAT"));
	AddText(Passes, FText::Format(LOCTEXT("PassesText", "{0} (minus the Notaries' 10% tax), {1} debts, {2} feuds and every clue you didn't follow."),
		InhFormat::Coins(Life.Estate.Money), FText::AsNumber(Life.Estate.Debts.Num()), FText::AsNumber(Life.Estate.Feuds.Num())), 16);

	// The voice note.
	UVerticalBox* Voice = AddCard(Column, InhStyle::CardRaised);
	AddLabel(Voice, LOCTEXT("VoiceH", "YOUR VOICE NOTE"));
	AddText(Voice, FText::Format(LOCTEXT("VoiceText", "The only thing you can leave in your own words: {0} seconds. Honest advice, a warning, or a lie."),
		FText::AsNumber(Life.VoiceNoteMaxSeconds)), 17);
	if (Life.bHasVoiceNote)
	{
		AddText(Voice, LOCTEXT("Sealed", "✓ Sealed. You can record again to replace it until sunrise."), 16, InhStyle::Good, true);
	}
	RecorderText = AddText(Voice, FText::GetEmpty(), 18, InhStyle::Sea, true, ETextJustify::Center);
	UHorizontalBox* Controls = AddRow(Voice);
	RecordButton = AddButton(Controls, LOCTEXT("Record", "● Record"), InhStyle::EButton::Danger, [this]() { ToggleRecording(); }, 18);
	AddButton(Controls, LOCTEXT("Listen", "Listen back"), InhStyle::EButton::Secondary, [this]()
	{
		if (Recorder && Recorder->HasRecording())
		{
			Game()->PlayOwnVoiceNotePreview(Recorder->ToWav());
		}
	}, 18);
	AddButton(Voice, LOCTEXT("Seal", "Seal it for your heir"), InhStyle::EButton::Primary, [this]()
	{
		if (!Recorder || !Recorder->HasRecording() || bUploading)
		{
			Toast(LOCTEXT("RecordFirst", "Record something first."));
			return;
		}
		bUploading = true;
		TWeakObjectPtr<UInhLegacyScreen> Weak(this);
		const FInhDone Report = ToastResult(false);
		Game()->UploadVoiceNote(Recorder->ToWav(), [Weak, Report](bool bOk, const FText& Msg)
		{
			if (Weak.IsValid())
			{
				Weak->bUploading = false;
			}
			Report(bOk, Msg);
		});
	});
	UpdateRecorderText();

#if !UE_BUILD_SHIPPING
	if (GetDefault<UInhSettings>()->bShowDevTools)
	{
		UVerticalBox* Dev = AddCard(Column);
		AddLabel(Dev, LOCTEXT("DevH", "DEVELOPMENT"));
		AddText(Dev, LOCTEXT("DevText", "End this life now to test inheritance. The server must run with ALLOW_EARLY_SUNRISE=1."), 15, InhStyle::Muted);
		AddButton(Dev, LOCTEXT("Skip", "Skip to sunrise"), InhStyle::EButton::Danger, [this]()
		{
			Game()->SkipToSunrise(ToastResult());
		}, 17);
	}
#endif

	AddSpacer(Column, 16.f);
	AddButton(Column, LOCTEXT("BackToRoomL", "Back to the room"), InhStyle::EButton::Secondary, [PC]() { PC->ClosePanel(); });
}

void UInhLegacyScreen::ToggleRecording()
{
	if (!Recorder)
	{
		return;
	}
	if (Recorder->IsRecording())
	{
		Recorder->Stop();
	}
	else
	{
		Game()->StopVoicePlayback();
		FText Error;
		if (!Recorder->Start(float(Game()->GetLife().VoiceNoteMaxSeconds), Error))
		{
			Toast(Error);
		}
	}
	UpdateRecorderText();
}

void UInhLegacyScreen::UpdateRecorderText()
{
	if (!Recorder || !RecorderText)
	{
		return;
	}
	const float Seconds = Recorder->GetRecordedSeconds();
	const float Max = Recorder->GetMaxSeconds();
	if (Recorder->IsRecording())
	{
		RecorderText->SetText(FText::Format(LOCTEXT("Recording", "Recording… {0} / {1} s"),
			FText::AsNumber(FMath::FloorToInt(Seconds)), FText::AsNumber(FMath::RoundToInt(Max))));
		if (RecordButton) RecordButton->SetLabel(LOCTEXT("Stop", "■ Stop"));
	}
	else
	{
		RecorderText->SetText(Recorder->HasRecording()
			? FText::Format(LOCTEXT("Recorded", "{0} seconds recorded"), FText::AsNumber(FMath::RoundToInt(Seconds)))
			: LOCTEXT("NotRecorded", "Nothing recorded yet"));
		if (RecordButton) RecordButton->SetLabel(LOCTEXT("RecordAgain", "● Record"));
	}
}

void UInhLegacyScreen::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!Recorder)
	{
		return;
	}
	if (Recorder->IsRecording())
	{
		if (Recorder->GetRecordedSeconds() >= Recorder->GetMaxSeconds() - 0.05f)
		{
			Recorder->Stop();
		}
		UpdateRecorderText();
	}
	else if (bWasRecording)
	{
		UpdateRecorderText();
	}
	bWasRecording = Recorder->IsRecording();
}

void UInhLegacyScreen::NativeDestruct()
{
	if (Recorder)
	{
		Recorder->Stop();
	}
	Super::NativeDestruct();
}

#undef LOCTEXT_NAMESPACE
