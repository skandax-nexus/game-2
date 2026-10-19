#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AudioCaptureCore.h"
#include <atomic>
#include "InhVoiceRecorder.generated.h"

/** Records the 30-second voice note a player leaves for their heir. */
UCLASS()
class INHERITANCE_API UInhVoiceRecorder : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Starts recording from the default microphone. Returns false if the microphone can't be opened.
	 * On Android the first call may only ask for permission; call again once it is granted.
	 */
	bool Start(float InMaxSeconds, FText& OutError);
	void Stop();
	bool IsRecording() const { return bRecording; }
	float GetRecordedSeconds() const;
	float GetMaxSeconds() const { return MaxSeconds; }
	bool HasRecording() const;
	/** The recording as a 16 kHz mono WAV, ready to upload. */
	TArray<uint8> ToWav() const;

	virtual void BeginDestroy() override;

private:
	Audio::FAudioCapture Capture;
	mutable FCriticalSection Lock;
	TArray<float> Samples;
	int32 DeviceSampleRate = 48000;
	float MaxSeconds = 30.f;
	std::atomic<bool> bRecording { false };
	bool bStreamOpen = false;
};
