#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateTypes.h"

/** Colours and type for every screen: fog navy, parchment and brass. */
namespace InhStyle
{
	const FLinearColor Backdrop(0.012f, 0.018f, 0.03f, 0.88f);
	const FLinearColor Card(0.035f, 0.05f, 0.075f, 0.94f);
	const FLinearColor CardRaised(0.06f, 0.08f, 0.11f, 0.97f);
	const FLinearColor Text(0.93f, 0.89f, 0.80f);
	const FLinearColor Muted(0.58f, 0.62f, 0.68f);
	const FLinearColor Brass(0.86f, 0.62f, 0.26f);
	const FLinearColor Danger(0.80f, 0.24f, 0.20f);
	const FLinearColor Sea(0.25f, 0.55f, 0.75f);
	const FLinearColor Good(0.45f, 0.75f, 0.50f);

	enum class EButton : uint8 { Primary, Secondary, Danger, Quiet };

	FSlateFontInfo Font(int32 Size, bool bBold = false);
	FSlateFontInfo Title(int32 Size);
	FButtonStyle Button(EButton Kind);
	FLinearColor ButtonText(EButton Kind);
	FSlateBrush Rounded(const FLinearColor& Color, float Radius = 18.f);
}
