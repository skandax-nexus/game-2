#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Sound/SoundWaveProcedural.h"
#include <atomic>
#include "InhAmbience.generated.h"

class UAudioComponent;

namespace InhAmbienceDsp
{
	struct FSynth;
}

/**
 * The harbour soundscape as an endless procedural sound: no audio assets, nothing to stream or cook.
 * 16-bit stereo PCM at 48 kHz, synthesised on the audio thread. The game thread only writes the atomics below;
 * every bit of DSP state belongs to the audio thread.
 */
UCLASS()
class INHERITANCE_API UInhAmbienceSound : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	/** A ready-to-play sound with its synthesiser allocated up front, so the generator never allocates. */
	static UInhAmbienceSound* Create(UObject* Outer);

	void SetLifeProgress(float Progress) { LifeProgress.store(Progress, std::memory_order_relaxed); }
	void SetRain(float Amount01) { Rain.store(Amount01, std::memory_order_relaxed); }
	/** 0 fades the soundscape out on the audio thread (then it renders cheap silence), 1 fades it back in. */
	void SetFadeTarget(float Level01) { FadeTarget.store(Level01, std::memory_order_relaxed); }

	//~ USoundWaveProcedural: called on the audio thread. NumSamples counts interleaved samples (frames x channels).
	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;
	virtual void FinishDestroy() override;

private:
	/** Raw pointer: freed in FinishDestroy, once the audio device has released this wave. */
	InhAmbienceDsp::FSynth* Synth = nullptr;
	std::atomic<float> LifeProgress { 0.f };
	std::atomic<float> Rain { 0.f };
	std::atomic<float> FadeTarget { 1.f };
};

/**
 * Plays the ambience: sea swell on the quay, wind, rigging creaks, a distant bell buoy, gulls and the sunrise bell
 * at dawn, rain when it falls. Owned by the player controller.
 */
UCLASS()
class INHERITANCE_API UInhAmbience : public UObject
{
	GENERATED_BODY()

public:
	/** Starts the procedurally synthesised soundscape: sea swell on the quay, wind, rigging creaks, distant bell. */
	void Start(UObject* WorldContext);
	void Stop();
	/** 0 = dusk ... 1 = dawn; night is quieter and windier, dawn brings gulls and the sunrise bell. */
	void SetLifeProgress(float Progress);
	void SetRain(float Amount01);

private:
	UPROPERTY() TObjectPtr<UInhAmbienceSound> Sound;
	UPROPERTY() TObjectPtr<UAudioComponent> Component;
	float LifeProgress = 0.f;
	float Rain = 0.f;
};
