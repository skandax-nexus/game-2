#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextWidgetTypes.h"
#include "UI/InhStyle.h"
#include "Core/InhGameSubsystem.h"
#include "InhScreen.generated.h"

class UInhButton;
class UInhGameSubsystem;
class AInhPlayerController;
class UVerticalBox;
class UHorizontalBox;
class UTextBlock;
class UPanelWidget;
class UPanelSlot;
class UOverlay;

/**
 * Base for every screen. Screens are built entirely in C++ (no Blueprint assets needed),
 * with a dimmed backdrop (or soft shades at the edges, so a cinematic can play behind),
 * safe-zone padding for notches, and a scrolling column.
 * Override Build() to fill the column; call Refresh() to rebuild it.
 */
UCLASS(Abstract)
class INHERITANCE_API UInhScreen : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Rebuilds the content on the next frame (safe to call from a button callback). */
	void Refresh() { bDirty = true; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Creates the root layout. The default is a dimmed, scrolling, centred column in Content. */
	virtual void BuildFrame();
	virtual void Build(UVerticalBox* Column) {}
	virtual bool RefreshOnLifeChange() const { return true; }

	// --- building blocks ---
	template <class T>
	T* Make() { return WidgetTree->ConstructWidget<T>(T::StaticClass()); }

	UTextBlock* AddText(UPanelWidget* Parent, const FText& Text, int32 Size = 20, const FLinearColor& Color = InhStyle::Text,
		bool bBold = false, ETextJustify::Type Justify = ETextJustify::Left);
	UTextBlock* AddHeading(UPanelWidget* Parent, const FText& Text, bool bCentered = false);
	UTextBlock* AddLabel(UPanelWidget* Parent, const FText& Text);
	UInhButton* AddButton(UPanelWidget* Parent, const FText& Label, InhStyle::EButton Kind, TFunction<void()> OnClick, int32 FontSize = 22);
	UVerticalBox* AddCard(UPanelWidget* Parent, const FLinearColor& Color = InhStyle::Card);
	UHorizontalBox* AddRow(UPanelWidget* Parent);
	void AddSpacer(UPanelWidget* Parent, float Height);
	static void Pad(UPanelSlot* InSlot, const FMargin& Margin);

	UInhGameSubsystem* Game() const;
	AInhPlayerController* Controller() const;
	void Toast(const FText& Message) const;
	/** Callback for async requests that toasts the result even if this screen has closed meanwhile. */
	FInhDone ToastResult(bool bOnlyFailures = true) const;

	UPROPERTY() TObjectPtr<UVerticalBox> Content;
	float BackdropOpacity = 0.88f;
	/** Peak opacity of a soft dark shade falling from the top edge / rising from the bottom edge (0 = none). */
	float TopShade = 0.f;
	float BottomShade = 0.f;

private:
	void HandleLifeChanged();
	void AddShade(UOverlay* Root, bool bFromTop, float Peak);

	FDelegateHandle LifeChangedHandle;
	bool bDirty = false;
};

/** A message that floats at the top of the screen for a few seconds. */
UCLASS()
class INHERITANCE_API UInhToast : public UUserWidget
{
	GENERATED_BODY()

public:
	void Show(const FText& Message);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	UPROPERTY() TObjectPtr<class UBorder> Box;
	UPROPERTY() TObjectPtr<UTextBlock> Label;
	float TimeLeft = 0.f;
};
