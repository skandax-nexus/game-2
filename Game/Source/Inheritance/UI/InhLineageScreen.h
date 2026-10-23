#pragma once

#include "CoreMinimal.h"
#include "UI/InhScreen.h"
#include "Core/InhTypes.h"
#include "InhLineageScreen.generated.h"

/** Your family tree: every life you lived, who you inherited from and who you passed things to. */
UCLASS()
class INHERITANCE_API UInhLineageScreen : public UInhScreen
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void Build(UVerticalBox* Column) override;
	virtual bool RefreshOnLifeChange() const override { return false; }

private:
	TArray<FInhFamilyEntry> Entries;
	bool bLoaded = false;
};
