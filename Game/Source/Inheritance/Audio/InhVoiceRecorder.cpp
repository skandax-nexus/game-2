#include "Audio/InhVoiceRecorder.h"
#include "Audio/InhWav.h"
#include "Inheritance.h"
#include "Misc/ScopeLock.h"

#if PLATFORM_ANDROID
#include "AndroidPermissionFunctionLibrary.h"
#endif

#define LOCTEXT_NAMESPACE "Inheritance"

namespace
{
	// Sample rate of uploaded notes: small files, clear speech.
	constexpr int32 UploadSampleRate = 16000;
}

bool UInhVoiceRecorder::Start(float InMaxSeconds, FText& OutError)
{
	if (bRecording)
	{
		return true;
	}

#if PLATFORM_ANDROID
	const FString MicPermission = TEXT("android.permission.RECORD_AUDIO");
	if (!UAndroidPermissionFunctionLibrary::CheckPermission(MicPermission))
	{
		UAndroidPermissionFunctionLibrary::AcquirePermissions({ MicPermission });
		OutError = LOCTEXT("MicPermission", "Allow the microphone, then tap Record again.");
		return false;
	}
#endif

	MaxSeconds = InMaxSeconds;
	{
		FScopeLock ScopeLock(&Lock);
		Samples.Reset();
	}

	if (!bStreamOpen)
	{
		Audio::FCaptureDeviceInfo Info;
		if (Capture.GetCaptureDeviceInfo(Info))
		{
			DeviceSampleRate = Info.PreferredSampleRate;
		}

		Audio::FAudioCaptureDeviceParams Params;
		Audio::FOnAudioCaptureFunction OnCapture =
			[this](const void* InAudio, int32 NumFrames, int32 NumChannels, int32 SampleRate, double, bool)
			{
				if (!bRecording || NumChannels <= 0)
				{
					return;
				}
				const float* Pcm = static_cast<const float*>(InAudio);
				FScopeLock ScopeLock(&Lock);
				DeviceSampleRate = SampleRate;
				const int32 MaxSamples = int32(MaxSeconds * SampleRate);
				for (int32 Frame = 0; Frame < NumFrames && Samples.Num() < MaxSamples; ++Frame)
				{
					float Mono = 0.f;
					for (int32 C = 0; C < NumChannels; ++C)
					{
						Mono += Pcm[Frame * NumChannels + C];
					}
					Samples.Add(Mono / NumChannels);
				}
			};

		if (!Capture.OpenAudioCaptureStream(Params, MoveTemp(OnCapture), 1024))
		{
			OutError = LOCTEXT("NoMic", "No microphone was found.");
			return false;
		}
		bStreamOpen = true;
	}

	bRecording = true;
	if (!Capture.StartStream())
	{
		bRecording = false;
		OutError = LOCTEXT("MicFailed", "The microphone could not start.");
		return false;
	}
	return true;
}

void UInhVoiceRecorder::Stop()
{
	if (!bRecording)
	{
		return;
	}
	bRecording = false;
	Capture.StopStream();
}

float UInhVoiceRecorder::GetRecordedSeconds() const
{
	FScopeLock ScopeLock(&Lock);
	return DeviceSampleRate > 0 ? float(Samples.Num()) / float(DeviceSampleRate) : 0.f;
}

bool UInhVoiceRecorder::HasRecording() const
{
	return !bRecording && GetRecordedSeconds() > 0.5f;
}

TArray<uint8> UInhVoiceRecorder::ToWav() const
{
	TArray<float> Copy;
	int32 Rate;
	{
		FScopeLock ScopeLock(&Lock);
		Copy = Samples;
		Rate = DeviceSampleRate;
	}

	// Normalise quiet recordings so a whispered warning is still audible.
	float Peak = 0.f;
	for (const float S : Copy) Peak = FMath::Max(Peak, FMath::Abs(S));
	if (Peak > 0.01f && Peak < 0.7f)
	{
		const float Gain = 0.7f / Peak;
		for (float& S : Copy) S *= Gain;
	}
	return InhWav::EncodeMono16(InhWav::Resample(Copy, Rate, UploadSampleRate), UploadSampleRate);
}

void UInhVoiceRecorder::BeginDestroy()
{
	bRecording = false;
	if (bStreamOpen)
	{
		Capture.StopStream();
		Capture.CloseStream();
		bStreamOpen = false;
	}
	Super::BeginDestroy();
}

#undef LOCTEXT_NAMESPACE
