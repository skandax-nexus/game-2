#include "UI/InhScreen.h"
#include "UI/InhButton.h"
#include "Core/InhGameSubsystem.h"
#include "World/InhPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SafeZone.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"

TSharedRef<SWidget> UInhScreen::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildFrame();
		if (Content)
		{
			Build(Content);
		}
	}
	return Super::RebuildWidget();
}

void UInhScreen::BuildFrame()
{
	UOverlay* Root = Make<UOverlay>();
	WidgetTree->RootWidget = Root;

	UBorder* Backdrop = Make<UBorder>();
	Backdrop->SetBrush(FSlateColorBrush(FLinearColor(InhStyle::Backdrop.R, InhStyle::Backdrop.G, InhStyle::Backdrop.B, BackdropOpacity)));
	UOverlaySlot* BackdropSlot = Root->AddChildToOverlay(Backdrop);
	BackdropSlot->SetHorizontalAlignment(HAlign_Fill);
	BackdropSlot->SetVerticalAlignment(VAlign_Fill);
	if (TopShade > 0.f)
	{
		AddShade(Root, true, TopShade);
	}
	if (BottomShade > 0.f)
	{
		AddShade(Root, false, BottomShade);
	}

	USafeZone* Safe = Make<USafeZone>();
	UOverlaySlot* SafeSlot = Root->AddChildToOverlay(Safe);
	SafeSlot->SetHorizontalAlignment(HAlign_Fill);
	SafeSlot->SetVerticalAlignment(VAlign_Fill);

	UScrollBox* Scroll = Make<UScrollBox>();
	Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
	Safe->AddChild(Scroll);

	USizeBox* Width = Make<USizeBox>();
	Width->SetMaxDesiredWidth(760.f);
	if (UScrollBoxSlot* WidthSlot = Cast<UScrollBoxSlot>(Scroll->AddChild(Width)))
	{
		WidthSlot->SetHorizontalAlignment(HAlign_Center);
		WidthSlot->SetPadding(FMargin(20.f, 32.f));
	}

	Content = Make<UVerticalBox>();
	Width->AddChild(Content);
}

void UInhScreen::AddShade(UOverlay* Root, bool bFromTop, float Peak)
{
	// Slate has no gradient brush without a texture, so the shade is a stack of thin bands, each a little darker towards
	// the edge. Over a moving picture the steps don't show.
	constexpr int32 NumBands = 24;
	const float Share = bFromTop ? 0.32f : 0.62f;
	UVerticalBox* Bands = Make<UVerticalBox>();
	Bands->SetVisibility(ESlateVisibility::HitTestInvisible);
	UOverlaySlot* BandsSlot = Root->AddChildToOverlay(Bands);
	BandsSlot->SetHorizontalAlignment(HAlign_Fill);
	BandsSlot->SetVerticalAlignment(VAlign_Fill);

	auto AddBand = [this, Bands](float Weight, float Alpha)
	{
		UBorder* Band = Make<UBorder>();
		Band->SetBrush(FSlateColorBrush(FLinearColor(InhStyle::Backdrop.R, InhStyle::Backdrop.G, InhStyle::Backdrop.B, Alpha)));
		Band->SetPadding(FMargin(0.f));
		FSlateChildSize Size(ESlateSizeRule::Fill);
		Size.Value = Weight;
		Bands->AddChildToVerticalBox(Band)->SetSize(Size);
	};
	if (!bFromTop)
	{
		AddBand(1.f - Share, 0.f);
	}
	for (int32 i = 0; i < NumBands; ++i)
	{
		// 0 at the screen edge, 1 where the shade has faded out.
		const float FromEdge = (bFromTop ? i + 0.5f : NumBands - i - 0.5f) / NumBands;
		AddBand(Share / NumBands, Peak * (1.f - FMath::SmoothStep(0.f, 1.f, FromEdge)));
	}
	if (bFromTop)
	{
		AddBand(1.f - Share, 0.f);
	}
}

void UInhScreen::NativeConstruct()
{
	Super::NativeConstruct();
	if (UInhGameSubsystem* G = Game())
	{
		LifeChangedHandle = G->OnLifeChanged.AddUObject(this, &UInhScreen::HandleLifeChanged);
	}
}

void UInhScreen::NativeDestruct()
{
	if (UInhGameSubsystem* G = Game())
	{
		G->OnLifeChanged.Remove(LifeChangedHandle);
	}
	Super::NativeDestruct();
}

void UInhScreen::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bDirty && Content)
	{
		bDirty = false;
		Content->ClearChildren();
		Build(Content);
	}
}

void UInhScreen::HandleLifeChanged()
{
	if (RefreshOnLifeChange())
	{
		Refresh();
	}
}

void UInhScreen::Pad(UPanelSlot* InSlot, const FMargin& Margin)
{
	if (UVerticalBoxSlot* V = Cast<UVerticalBoxSlot>(InSlot))
	{
		V->SetPadding(Margin);
		V->SetHorizontalAlignment(HAlign_Fill);
	}
	else if (UHorizontalBoxSlot* H = Cast<UHorizontalBoxSlot>(InSlot))
	{
		H->SetPadding(Margin);
		H->SetVerticalAlignment(VAlign_Center);
	}
	else if (UScrollBoxSlot* S = Cast<UScrollBoxSlot>(InSlot))
	{
		S->SetPadding(Margin);
	}
	else if (UOverlaySlot* O = Cast<UOverlaySlot>(InSlot))
	{
		O->SetPadding(Margin);
	}
}

UTextBlock* UInhScreen::AddText(UPanelWidget* Parent, const FText& Text, int32 Size, const FLinearColor& Color, bool bBold, ETextJustify::Type Justify)
{
	UTextBlock* Block = Make<UTextBlock>();
	Block->SetText(Text);
	Block->SetFont(InhStyle::Font(Size, bBold));
	Block->SetColorAndOpacity(FSlateColor(Color));
	Block->SetAutoWrapText(true);
	Block->SetJustification(Justify);
	Pad(Parent->AddChild(Block), FMargin(0.f, 4.f));
	if (UHorizontalBoxSlot* H = Cast<UHorizontalBoxSlot>(Block->Slot))
	{
		H->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	return Block;
}

UTextBlock* UInhScreen::AddHeading(UPanelWidget* Parent, const FText& Text, bool bCentered)
{
	UTextBlock* Block = Make<UTextBlock>();
	Block->SetText(Text);
	Block->SetFont(InhStyle::Title(26));
	Block->SetColorAndOpacity(FSlateColor(InhStyle::Brass));
	Block->SetAutoWrapText(true);
	Block->SetJustification(bCentered ? ETextJustify::Center : ETextJustify::Left);
	Pad(Parent->AddChild(Block), FMargin(0.f, 18.f, 0.f, 8.f));
	return Block;
}

UTextBlock* UInhScreen::AddLabel(UPanelWidget* Parent, const FText& Text)
{
	return AddText(Parent, Text, 15, InhStyle::Muted, true);
}

UInhButton* UInhScreen::AddButton(UPanelWidget* Parent, const FText& Label, InhStyle::EButton Kind, TFunction<void()> OnClick, int32 FontSize)
{
	UInhButton* Button = Make<UInhButton>();
	Button->Configure(Label, Kind, MoveTemp(OnClick), FontSize);
	UPanelSlot* NewSlot = Parent->AddChild(Button);
	if (UHorizontalBoxSlot* H = Cast<UHorizontalBoxSlot>(NewSlot))
	{
		H->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		H->SetPadding(FMargin(4.f, 0.f));
		H->SetVerticalAlignment(VAlign_Fill);
	}
	else
	{
		Pad(NewSlot, FMargin(0.f, 6.f));
	}
	return Button;
}

UVerticalBox* UInhScreen::AddCard(UPanelWidget* Parent, const FLinearColor& Color)
{
	UBorder* Card = Make<UBorder>();
	Card->SetBrush(InhStyle::Rounded(Color, 18.f));
	Card->SetPadding(FMargin(20.f, 16.f));
	Pad(Parent->AddChild(Card), FMargin(0.f, 8.f));

	UVerticalBox* Inner = Make<UVerticalBox>();
	Card->SetContent(Inner);
	return Inner;
}

UHorizontalBox* UInhScreen::AddRow(UPanelWidget* Parent)
{
	UHorizontalBox* Row = Make<UHorizontalBox>();
	Pad(Parent->AddChild(Row), FMargin(-4.f, 6.f));
	return Row;
}

void UInhScreen::AddSpacer(UPanelWidget* Parent, float Height)
{
	USpacer* Spacer = Make<USpacer>();
	Spacer->SetSize(FVector2D(1.f, Height));
	Parent->AddChild(Spacer);
}

UInhGameSubsystem* UInhScreen::Game() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UInhGameSubsystem>() : nullptr;
}

AInhPlayerController* UInhScreen::Controller() const
{
	return Cast<AInhPlayerController>(GetOwningPlayer());
}

void UInhScreen::Toast(const FText& Message) const
{
	if (AInhPlayerController* PC = Controller())
	{
		PC->Toast(Message);
	}
}

FInhDone UInhScreen::ToastResult(bool bOnlyFailures) const
{
	TWeakObjectPtr<AInhPlayerController> WeakPC(Controller());
	return [WeakPC, bOnlyFailures](bool bOk, const FText& Message)
	{
		if (WeakPC.IsValid() && (!bOk || !bOnlyFailures))
		{
			WeakPC->Toast(Message);
		}
	};
}

// --- UInhToast --------------------------------------------------------------------------------

TSharedRef<SWidget> UInhToast::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
		WidgetTree->RootWidget = Root;
		Root->SetVisibility(ESlateVisibility::HitTestInvisible);

		USafeZone* Safe = WidgetTree->ConstructWidget<USafeZone>(USafeZone::StaticClass());
		UOverlaySlot* SafeSlot = Root->AddChildToOverlay(Safe);
		SafeSlot->SetHorizontalAlignment(HAlign_Center);
		SafeSlot->SetVerticalAlignment(VAlign_Top);

		USizeBox* Width = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Width->SetMaxDesiredWidth(640.f);
		Safe->AddChild(Width);

		Box = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Box->SetBrush(InhStyle::Rounded(InhStyle::CardRaised, 16.f));
		Box->SetPadding(FMargin(22.f, 14.f));
		Box->SetRenderOpacity(0.f);
		Width->AddChild(Box);

		Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Label->SetFont(InhStyle::Font(19));
		Label->SetColorAndOpacity(FSlateColor(InhStyle::Text));
		Label->SetAutoWrapText(true);
		Label->SetJustification(ETextJustify::Center);
		Box->SetContent(Label);
	}
	return Super::RebuildWidget();
}

void UInhToast::Show(const FText& Message)
{
	if (Message.IsEmpty() || !Label)
	{
		return;
	}
	Label->SetText(Message);
	// Long messages stay up longer: about 15 characters per second, between 3 and 8 seconds.
	TimeLeft = FMath::Clamp(Message.ToString().Len() / 15.f, 3.f, 8.f);
}

void UInhToast::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	TimeLeft = FMath::Max(0.f, TimeLeft - InDeltaTime);
	if (Box)
	{
		const float Target = TimeLeft > 0.f ? 1.f : 0.f;
		Box->SetRenderOpacity(FMath::FInterpTo(Box->GetRenderOpacity(), Target, InDeltaTime, 8.f));
	}
}
