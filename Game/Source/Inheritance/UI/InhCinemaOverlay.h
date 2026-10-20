#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Fonts/SlateFontInfo.h"
#include "InhCinemaOverlay.generated.h"

class UBorder;
class USizeBox;
class UTextBlock;
class UVerticalBox;

/**
 * Everything drawn over a cinematic: letterbox bars, full-screen fades, title cards and a "Tap to skip" hint.
 * Built in C++ like the screens (no assets), animated in NativeTick, and HitTestInvisible so it never takes a touch.
 */
UCLASS()
class INHERITANCE_API UInhCinemaOverlay : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Slides the bars in or out (~0.8 s): a 2.39:1 picture in landscape, thinner bars (10% each) in portrait. */
	void SetLetterbox(bool bOn);
	/** Fades a full-screen colour to TargetAlpha over Seconds (0 = at once). Color's own alpha is ignored. */
	void FadeTo(const FLinearColor& Color, float TargetAlpha, float Seconds);
	/**
	 * A title card (e.g. "HOLLOW BAY" / "Day 3 · The Will") that fades in, holds and fades out within Seconds.
	 * bLowerThird sets it low in the picture, for shots whose subject is framed at the centre.
	 */
	void ShowCaption(const FText& Title, const FText& Subtitle, float Seconds, bool bLowerThird = false);
	/** Fades out a caption that is still up, e.g. when the cinematic is skipped. */
	void HideCaption();
	/** A small "Tap to skip" at the bottom of the screen. */
	void SetSkipHint(bool bVisible);

	bool IsLetterboxed() const { return bLetterbox; }
	float GetFadeAlpha() const { return FadeAlpha; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	void TickLetterbox(const FVector2D& ScreenSize, float DeltaSeconds);
	void TickFade(float DeltaSeconds);
	void TickCaption(float DeltaSeconds);
	void TickSkipHint(float DeltaSeconds);

	UPROPERTY() TObjectPtr<USizeBox> TopBar;
	UPROPERTY() TObjectPtr<USizeBox> BottomBar;
	UPROPERTY() TObjectPtr<UBorder> Fade;
	UPROPERTY() TObjectPtr<UVerticalBox> CaptionBox;
	UPROPERTY() TObjectPtr<UTextBlock> CaptionTitle;
	UPROPERTY() TObjectPtr<USizeBox> CaptionRule;
	UPROPERTY() TObjectPtr<UTextBlock> CaptionSubtitle;
	UPROPERTY() TObjectPtr<UTextBlock> SkipHint;

	// Letterbox: BarAmount runs 0..1 at a constant rate and is eased when drawn.
	bool bLetterbox = false;
	float BarAmount = 0.f;
	float BarHeight = -1.f;
	/** Height of the picture between the bars at full size, for placing lower-third cards. */
	float PictureHeight = 0.f;

	// Fade.
	FLinearColor FadeFromColor = FLinearColor::Black;
	FLinearColor FadeToColor = FLinearColor::Black;
	FLinearColor FadeColor = FLinearColor::Black;
	float FadeFromAlpha = 0.f;
	float FadeToAlpha = 0.f;
	float FadeAlpha = 0.f;
	float FadeSeconds = 0.f;
	float FadeElapsed = 0.f;
	bool bFadeDirty = true;

	// Caption.
	FText TitleText;
	FText SubtitleText;
	FSlateFontInfo TitleFont;
	float CaptionTime = 0.f;
	float CaptionHoldUntil = 0.f;
	float CaptionOutSeconds = 1.f;
	int32 TitleSpacing = 0;
	bool bCaptionActive = false;
	bool bCaptionLowerThird = false;
	bool bCaptionTextDirty = false;

	// Skip hint.
	bool bSkipHint = false;
	bool bSkipShown = false;
	float SkipAlpha = 0.f;
	float Clock = 0.f;
};
