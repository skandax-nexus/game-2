#include "UI/InhCinemaOverlay.h"
#include "UI/InhStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SafeZone.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

#define LOCTEXT_NAMESPACE "Inheritance"

namespace
{
	/** The picture the letterbox keeps in landscape: anamorphic widescreen. */
	constexpr float CinemaAspect = 2.39f;
	/** Each bar's share of the height in portrait, where a true 2.39:1 frame would be a sliver. */
	constexpr float PortraitBarShare = 0.10f;
	constexpr float LetterboxSeconds = 0.8f;

	constexpr int32 TitleSize = 40;
	constexpr int32 SubtitleSize = 18;
	/** How far the title's tracking opens over the life of the card. */
	constexpr int32 TitleSpacingGrowth = 140;
	constexpr float CaptionInSeconds = 1.2f;
	constexpr float SubtitleDelay = 0.45f;
	constexpr float RuleWidth = 120.f;
	/** How far below centre a lower-third card sits, as a share of the letterboxed picture's height. */
	constexpr float LowerThirdDrop = 0.27f;

	float Smooth(float X)
	{
		return FMath::SmoothStep(0.f, 1.f, X);
	}

	/** Not named Construct: inside the widget's members that name is UUserWidget::Construct() and hides this. */
	template <class T>
	T* NewWidget(UWidgetTree* Tree)
	{
		return Tree->ConstructWidget<T>(T::StaticClass());
	}

	USizeBox* AddLetterboxBar(UWidgetTree* Tree, UOverlay* Root, EVerticalAlignment Edge)
	{
		USizeBox* Box = NewWidget<USizeBox>(Tree);
		Box->SetHeightOverride(0.f);
		Box->SetVisibility(ESlateVisibility::Collapsed);
		UOverlaySlot* BoxSlot = Root->AddChildToOverlay(Box);
		BoxSlot->SetHorizontalAlignment(HAlign_Fill);
		BoxSlot->SetVerticalAlignment(Edge);

		UBorder* Bar = NewWidget<UBorder>(Tree);
		Bar->SetBrush(FSlateColorBrush(FLinearColor::Black));
		Bar->SetPadding(FMargin(0.f));
		Box->AddChild(Bar);
		return Box;
	}

	UTextBlock* AddCaptionText(UWidgetTree* Tree, UVerticalBox* Box, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		UTextBlock* Text = NewWidget<UTextBlock>(Tree);
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(Color));
		Text->SetJustification(ETextJustify::Center);
		Text->SetAutoWrapText(true);
		// A soft shadow keeps the card legible over bright fog and dawn skies.
		Text->SetShadowOffset(FVector2D(0.f, 2.f));
		Text->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.55f));
		Text->SetRenderOpacity(0.f);
		UVerticalBoxSlot* TextSlot = Box->AddChildToVerticalBox(Text);
		TextSlot->SetHorizontalAlignment(HAlign_Center);
		return Text;
	}
}

TSharedRef<SWidget> UInhCinemaOverlay::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UOverlay* Root = NewWidget<UOverlay>(WidgetTree);
		WidgetTree->RootWidget = Root;
		Root->SetVisibility(ESlateVisibility::HitTestInvisible);

		// Bars first, so a fade covers them too. They run to the true screen edges, under any notch.
		TopBar = AddLetterboxBar(WidgetTree, Root, VAlign_Top);
		BottomBar = AddLetterboxBar(WidgetTree, Root, VAlign_Bottom);

		Fade = NewWidget<UBorder>(WidgetTree);
		Fade->SetBrush(FSlateColorBrush(FLinearColor::White));
		Fade->SetBrushColor(FLinearColor::Transparent);
		Fade->SetVisibility(ESlateVisibility::Collapsed);
		UOverlaySlot* FadeSlot = Root->AddChildToOverlay(Fade);
		FadeSlot->SetHorizontalAlignment(HAlign_Fill);
		FadeSlot->SetVerticalAlignment(VAlign_Fill);

		// Text stays inside the safe zone, clear of notches and rounded corners.
		USafeZone* Safe = NewWidget<USafeZone>(WidgetTree);
		UOverlaySlot* SafeSlot = Root->AddChildToOverlay(Safe);
		SafeSlot->SetHorizontalAlignment(HAlign_Fill);
		SafeSlot->SetVerticalAlignment(VAlign_Fill);
		UOverlay* SafeArea = NewWidget<UOverlay>(WidgetTree);
		Safe->AddChild(SafeArea);

		// Title card: wide-tracked title, a thin brass rule that draws itself, then the subtitle.
		CaptionBox = NewWidget<UVerticalBox>(WidgetTree);
		CaptionBox->SetVisibility(ESlateVisibility::Collapsed);
		UOverlaySlot* CaptionSlot = SafeArea->AddChildToOverlay(CaptionBox);
		CaptionSlot->SetHorizontalAlignment(HAlign_Center);
		CaptionSlot->SetVerticalAlignment(VAlign_Center);
		CaptionSlot->SetPadding(FMargin(28.f, 0.f));

		TitleFont = InhStyle::Title(TitleSize);
		TitleSpacing = TitleFont.LetterSpacing;
		CaptionTitle = AddCaptionText(WidgetTree, CaptionBox, TitleFont, InhStyle::Text);

		CaptionRule = NewWidget<USizeBox>(WidgetTree);
		CaptionRule->SetWidthOverride(0.f);
		CaptionRule->SetHeightOverride(1.5f);
		UBorder* RuleLine = NewWidget<UBorder>(WidgetTree);
		RuleLine->SetBrush(FSlateColorBrush(FLinearColor(InhStyle::Brass.R, InhStyle::Brass.G, InhStyle::Brass.B, 0.85f)));
		RuleLine->SetPadding(FMargin(0.f));
		CaptionRule->AddChild(RuleLine);
		UVerticalBoxSlot* RuleSlot = CaptionBox->AddChildToVerticalBox(CaptionRule);
		RuleSlot->SetHorizontalAlignment(HAlign_Center);
		RuleSlot->SetPadding(FMargin(0.f, 14.f, 0.f, 12.f));

		FSlateFontInfo SubtitleFont = InhStyle::Font(SubtitleSize);
		SubtitleFont.LetterSpacing = 60;
		CaptionSubtitle = AddCaptionText(WidgetTree, CaptionBox, SubtitleFont, InhStyle::Brass);

		SkipHint = NewWidget<UTextBlock>(WidgetTree);
		SkipHint->SetText(LOCTEXT("CinemaSkipHint", "Tap to skip"));
		FSlateFontInfo HintFont = InhStyle::Font(14);
		HintFont.LetterSpacing = 120;
		SkipHint->SetFont(HintFont);
		SkipHint->SetColorAndOpacity(FSlateColor(InhStyle::Muted));
		SkipHint->SetShadowOffset(FVector2D(0.f, 1.f));
		SkipHint->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.5f));
		SkipHint->SetRenderOpacity(0.f);
		SkipHint->SetVisibility(ESlateVisibility::Collapsed);
		UOverlaySlot* HintSlot = SafeArea->AddChildToOverlay(SkipHint);
		HintSlot->SetHorizontalAlignment(HAlign_Center);
		HintSlot->SetVerticalAlignment(VAlign_Bottom);
		HintSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 18.f));

		// Anything requested before the widgets existed is drawn on the first tick.
		BarHeight = -1.f;
		bFadeDirty = true;
		bCaptionTextDirty = bCaptionActive;
		bSkipShown = false;
	}
	return Super::RebuildWidget();
}

void UInhCinemaOverlay::NativeConstruct()
{
	Super::NativeConstruct();
	// Never take a touch: taps fall through to the game, which handles skipping.
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UInhCinemaOverlay::SetLetterbox(bool bOn)
{
	bLetterbox = bOn;
}

void UInhCinemaOverlay::FadeTo(const FLinearColor& Color, float TargetAlpha, float Seconds)
{
	// Start from what is on screen now; a fade up from nothing takes the new colour straight away.
	FadeFromColor = FadeAlpha > 0.002f ? FadeColor : Color;
	FadeFromAlpha = FadeAlpha;
	FadeToColor = Color;
	FadeToAlpha = FMath::Clamp(TargetAlpha, 0.f, 1.f);
	FadeSeconds = FMath::Max(Seconds, 0.f);
	FadeElapsed = 0.f;
	FadeColor = FadeSeconds > 0.f ? FadeFromColor : FadeToColor;
	FadeAlpha = FadeSeconds > 0.f ? FadeFromAlpha : FadeToAlpha;
	bFadeDirty = true;
}

void UInhCinemaOverlay::ShowCaption(const FText& Title, const FText& Subtitle, float Seconds, bool bLowerThird)
{
	if (Title.IsEmpty() && Subtitle.IsEmpty())
	{
		HideCaption();
		return;
	}
	TitleText = Title;
	SubtitleText = Subtitle;
	const float Total = FMath::Max(Seconds, 2.5f);
	CaptionOutSeconds = FMath::Min(1.2f, Total * 0.3f);
	CaptionHoldUntil = Total - CaptionOutSeconds;
	CaptionTime = 0.f;
	bCaptionLowerThird = bLowerThird;
	bCaptionActive = true;
	bCaptionTextDirty = true;
}

void UInhCinemaOverlay::HideCaption()
{
	if (bCaptionActive && CaptionTime < CaptionHoldUntil)
	{
		CaptionHoldUntil = CaptionTime;
		CaptionOutSeconds = 0.5f;
	}
}

void UInhCinemaOverlay::SetSkipHint(bool bVisible)
{
	bSkipHint = bVisible;
}

void UInhCinemaOverlay::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// Clamped, so the first frame after a load hitch still shows the start of a fade instead of skipping it.
	const float DeltaSeconds = FMath::Min(InDeltaTime, 0.1f);
	Clock += DeltaSeconds;
	const FVector2D ScreenSize = MyGeometry.GetLocalSize();
	TickLetterbox(ScreenSize, DeltaSeconds);
	TickFade(DeltaSeconds);
	TickCaption(DeltaSeconds);
	TickSkipHint(DeltaSeconds);
}

void UInhCinemaOverlay::TickLetterbox(const FVector2D& ScreenSize, float DeltaSeconds)
{
	BarAmount = FMath::FInterpConstantTo(BarAmount, bLetterbox ? 1.f : 0.f, DeltaSeconds, 1.f / LetterboxSeconds);

	// Sized from the screen every frame, so it stays right through rotation and window resizes.
	float FullHeight = 0.f;
	const float Width = float(ScreenSize.X);
	const float Height = float(ScreenSize.Y);
	if (Width > 0.f && Height > 0.f)
	{
		FullHeight = Width >= Height ? FMath::Max(0.f, (Height - Width / CinemaAspect) * 0.5f) : Height * PortraitBarShare;
	}
	PictureHeight = FMath::Max(0.f, Height - 2.f * FullHeight);
	const float NewHeight = FullHeight * Smooth(BarAmount);
	if (NewHeight == BarHeight)
	{
		return;
	}
	BarHeight = NewHeight;
	const ESlateVisibility BarVisibility = NewHeight > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	for (USizeBox* Bar : { TopBar.Get(), BottomBar.Get() })
	{
		if (Bar)
		{
			Bar->SetHeightOverride(NewHeight);
			Bar->SetVisibility(BarVisibility);
		}
	}
}

void UInhCinemaOverlay::TickFade(float DeltaSeconds)
{
	if (FadeElapsed < FadeSeconds)
	{
		FadeElapsed = FMath::Min(FadeElapsed + DeltaSeconds, FadeSeconds);
		const float T = Smooth(FadeElapsed / FadeSeconds);
		FadeColor = FMath::Lerp(FadeFromColor, FadeToColor, T);
		FadeAlpha = FMath::Lerp(FadeFromAlpha, FadeToAlpha, T);
		bFadeDirty = true;
	}
	if (bFadeDirty && Fade)
	{
		bFadeDirty = false;
		Fade->SetBrushColor(FLinearColor(FadeColor.R, FadeColor.G, FadeColor.B, FadeAlpha));
		Fade->SetVisibility(FadeAlpha > 0.002f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UInhCinemaOverlay::TickCaption(float DeltaSeconds)
{
	if (!bCaptionActive || !CaptionBox || !CaptionTitle || !CaptionSubtitle || !CaptionRule)
	{
		return;
	}
	if (bCaptionTextDirty)
	{
		bCaptionTextDirty = false;
		const bool bHasTitle = !TitleText.IsEmpty();
		const bool bHasSubtitle = !SubtitleText.IsEmpty();
		CaptionTitle->SetText(TitleText);
		CaptionSubtitle->SetText(SubtitleText);
		CaptionTitle->SetVisibility(bHasTitle ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		CaptionSubtitle->SetVisibility(bHasSubtitle ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		CaptionRule->SetVisibility(bHasTitle && bHasSubtitle ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		CaptionBox->SetVisibility(ESlateVisibility::HitTestInvisible);
		TitleSpacing = -1;
	}

	CaptionTime += DeltaSeconds;
	const float Total = CaptionHoldUntil + CaptionOutSeconds;
	const float Out = 1.f - Smooth((CaptionTime - CaptionHoldUntil) / FMath::Max(CaptionOutSeconds, 0.01f));
	const float TitleIn = Smooth(CaptionTime / CaptionInSeconds);
	const float SubtitleIn = Smooth((CaptionTime - SubtitleDelay) / CaptionInSeconds);
	const float RuleIn = Smooth((CaptionTime - 0.25f) / 1.6f);

	// A lower-third card sits low in the picture (from the bars' full size, so it doesn't ride them in), clear of a
	// subject framed at the centre.
	CaptionBox->SetRenderTranslation(FVector2D(0.f, bCaptionLowerThird ? PictureHeight * LowerThirdDrop : 0.f));

	// Each line drifts up a few pixels as it appears, settling onto the frame.
	CaptionTitle->SetRenderOpacity(FMath::Min(TitleIn, Out));
	CaptionTitle->SetRenderTranslation(FVector2D(0.f, 10.f * (1.f - TitleIn)));
	CaptionSubtitle->SetRenderOpacity(FMath::Min(SubtitleIn, Out));
	CaptionSubtitle->SetRenderTranslation(FVector2D(0.f, 8.f * (1.f - SubtitleIn)));
	CaptionRule->SetWidthOverride(RuleWidth * RuleIn);
	CaptionRule->SetRenderOpacity(Out);

	// The title's tracking opens slowly over the whole card. Re-laid out only every few units, not every frame.
	const int32 Spacing = TitleFont.LetterSpacing + int32(float(TitleSpacingGrowth) * FMath::Clamp(CaptionTime / Total, 0.f, 1.f));
	if (FMath::Abs(Spacing - TitleSpacing) >= 4)
	{
		TitleSpacing = Spacing;
		FSlateFontInfo Font = TitleFont;
		Font.LetterSpacing = Spacing;
		CaptionTitle->SetFont(Font);
	}

	if (CaptionTime >= Total)
	{
		bCaptionActive = false;
		CaptionBox->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UInhCinemaOverlay::TickSkipHint(float DeltaSeconds)
{
	SkipAlpha = FMath::FInterpTo(SkipAlpha, bSkipHint ? 0.7f : 0.f, DeltaSeconds, 4.f);
	if (!SkipHint)
	{
		return;
	}
	const bool bShow = SkipAlpha > 0.01f;
	if (bShow != bSkipShown)
	{
		bSkipShown = bShow;
		SkipHint->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (bShow)
	{
		// A slow breath, so it reads as an invitation rather than a warning.
		SkipHint->SetRenderOpacity(SkipAlpha * (0.85f + 0.15f * FMath::Sin(Clock * 2.2f)));
	}
}

#undef LOCTEXT_NAMESPACE
