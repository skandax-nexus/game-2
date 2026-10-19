#pragma once

#include "CoreMinimal.h"

class USoundWaveProcedural;

/** 16-bit PCM WAV helpers for voice notes. */
namespace InhWav
{
	/** Encodes mono float samples (-1..1) as a 16-bit PCM WAV file. */
	TArray<uint8> EncodeMono16(const TArray<float>& Samples, int32 SampleRate);

	/** Linear resampling, good enough for speech. */
	TArray<float> Resample(const TArray<float>& Samples, int32 FromRate, int32 ToRate);

	/** Builds a playable sound from WAV bytes. Returns null if the bytes aren't 16-bit PCM WAV. */
	USoundWaveProcedural* CreateSound(UObject* Outer, const TArray<uint8>& WavBytes);
}
