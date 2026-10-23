#include "UI/InhStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"

FSlateFontInfo InhStyle::Font(int32 Size, bool bBold)
{
	return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
}

FSlateFontInfo InhStyle::Title(int32 Size)
{
	FSlateFontInfo Info = FCoreStyle::GetDefaultFontStyle("Bold", Size);
	Info.LetterSpacing = 220;
	return Info;
}

FSlateBrush InhStyle::Rounded(const FLinearColor& Color, float Radius)
{
	return FSlateRoundedBoxBrush(Color, Radius);
}

FLinearColor InhStyle::ButtonText(EButton Kind)
{
	return Kind == EButton::Primary ? FLinearColor(0.06f, 0.04f, 0.02f) : Text;
}

FButtonStyle InhStyle::Button(EButton Kind)
{
	FLinearColor Base;
	switch (Kind)
	{
	case EButton::Primary: Base = Brass; break;
	case EButton::Danger: Base = FLinearColor(0.45f, 0.10f, 0.09f); break;
	case EButton::Quiet: Base = FLinearColor(0.f, 0.f, 0.f, 0.f); break;
	default: Base = FLinearColor(0.10f, 0.14f, 0.19f); break;
	}
	const FLinearColor Hover = Kind == EButton::Quiet ? FLinearColor(1.f, 1.f, 1.f, 0.06f) : Base * 1.15f;
	const FLinearColor Pressed = Kind == EButton::Quiet ? FLinearColor(1.f, 1.f, 1.f, 0.12f) : Base * 0.8f;

	FButtonStyle Style;
	Style.SetNormal(FSlateRoundedBoxBrush(Base, 14.f));
	Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(Hover.R, Hover.G, Hover.B, FMath::Max(Hover.A, Base.A)), 14.f));
	Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(Pressed.R, Pressed.G, Pressed.B, FMath::Max(Pressed.A, Base.A)), 14.f));
	Style.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(0.08f, 0.09f, 0.11f, 0.8f), 14.f));
	Style.SetNormalPadding(FMargin(18.f, 12.f));
	Style.SetPressedPadding(FMargin(18.f, 14.f, 18.f, 10.f));
	return Style;
}
