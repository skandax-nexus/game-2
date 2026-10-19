#pragma once

#include "CoreMinimal.h"
#include "UI/InhScreen.h"
#include "InhLegacyScreen.generated.h"

class UInhVoiceRecorder;

/** Decide what to keep, sell, hide or destroy, and record the voice note for your heir. */
UCLASS()
class INHERITANCE_API UInhLegacyScreen : public UInhScreen
{
	GENERATED_BODY()

protected:
	virtual void Build(UVerticalBox* Column) override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

private:
	void ToggleRecording();
	void UpdateRecorderText();

	UPROPERTY() TObjectPtr<UInhVoiceRecorder> Recorder;
	UPROPERTY() TObjectPtr<UTextBlock> RecorderText;
	UPROPERTY() TObjectPtr<UInhButton> RecordButton;

	bool bUploading = false;
	bool bWasRecording = false;
};
