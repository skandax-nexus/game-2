#include "Audio/InhAmbience.h"
#include "Inheritance.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundGroups.h"

#define LOCTEXT_NAMESPACE "Inheritance"

// The synthesiser below only uses FMath and plain arrays: it runs on the audio thread, so it never allocates, locks
// or touches UObjects. Everything is sized at construction on the game thread.
namespace InhAmbienceDsp
{
	constexpr int32 SampleRate = 48000;
	constexpr int32 Channels = 2;
	constexpr float Rate = float(SampleRate);
	constexpr float Pi = 3.14159265f;
	constexpr float TwoPi = 6.28318531f;
	/** Frames between control-rate updates: envelopes, schedulers and filter sweeps run here, not per sample. */
	constexpr int32 Block = 32;
	constexpr float BlockSeconds = float(Block) / Rate;
	/** Life progress at which the church bell greets the sunrise. */
	constexpr float SunriseBellAt = 0.9f;

	float SmoothStep(float Edge0, float Edge1, float X)
	{
		const float T = FMath::Clamp((X - Edge0) / (Edge1 - Edge0), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	/** Per-sample multiplier that decays by 1/e over Seconds. */
	float DecayPerSample(float Seconds)
	{
		return FMath::Exp(-1.f / (Seconds * Rate));
	}

	/** sin(2 pi Phase) for Phase in [0, 1), within about 0.1%: cheap enough to run per sample. */
	float FastSin(float Phase)
	{
		const float X = Phase * 2.f - 1.f;
		float Y = 4.f * X * (1.f - FMath::Abs(X));
		Y = 0.225f * (Y * FMath::Abs(Y) - Y) + Y;
		return -Y;
	}

	float Wrap01(float Phase)
	{
		return Phase >= 1.f ? Phase - 1.f : Phase;
	}

	/** Constant-power pan: 0 = left, 1 = right. */
	void PanGains(float Pan, float& OutL, float& OutR)
	{
		OutL = FMath::Cos(Pan * Pi * 0.5f);
		OutR = FMath::Sin(Pan * Pi * 0.5f);
	}

	/** Linear up to about -9 dBFS, then a smooth knee that never goes past -6 dBFS. */
	float SoftClip(float X)
	{
		constexpr float Knee = 0.35f;
		constexpr float Ceiling = 0.5f;
		const float A = FMath::Abs(X);
		if (A <= Knee)
		{
			return X;
		}
		const float U = (A - Knee) / (Ceiling - Knee);
		const float Y = Knee + (Ceiling - Knee) * U / (1.f + U);
		return X < 0.f ? -Y : Y;
	}

	/** xorshift32: allocation-free and plenty random for noise. */
	struct FRng
	{
		uint32 State = 0x2545F491u;

		uint32 Next()
		{
			State ^= State << 13;
			State ^= State >> 17;
			State ^= State << 5;
			return State;
		}
		/** Uniform in [0, 1). */
		float Unit() { return float(Next() >> 8) * (1.f / 16777216.f); }
		/** Uniform in [-1, 1). */
		float Signed() { return Unit() * 2.f - 1.f; }
		float Range(float Min, float Max) { return Min + (Max - Min) * Unit(); }
	};

	/** One-pole low-pass. */
	struct FOnePole
	{
		float Coef = 1.f;
		float Z = 0.f;

		void SetCutoff(float Hz) { Coef = 1.f - FMath::Exp(-TwoPi * Hz / Rate); }
		float Process(float X)
		{
			Z += Coef * (X - Z);
			return Z;
		}
	};

	/** Topology-preserving state-variable filter (Zavalishin): stays stable while its cutoff sweeps every block. */
	struct FSvf
	{
		float A1 = 0.f;
		float A2 = 0.f;
		float A3 = 0.f;
		float K = 1.f;
		float Ic1 = 0.f;
		float Ic2 = 0.f;

		void Set(float Hz, float Q)
		{
			const float G = FMath::Tan(Pi * FMath::Clamp(Hz, 20.f, Rate * 0.45f) / Rate);
			K = 1.f / Q;
			A1 = 1.f / (1.f + G * (G + K));
			A2 = G * A1;
			A3 = G * A2;
		}
		void Reset()
		{
			Ic1 = 0.f;
			Ic2 = 0.f;
		}
		/** Unity gain at the centre frequency, whatever the Q. */
		float BandPass(float X)
		{
			float V1 = 0.f;
			float V2 = 0.f;
			Step(X, V1, V2);
			return K * V1;
		}
		float HighPass(float X)
		{
			float V1 = 0.f;
			float V2 = 0.f;
			Step(X, V1, V2);
			return X - K * V1 - V2;
		}

	private:
		void Step(float X, float& V1, float& V2)
		{
			const float V3 = X - Ic2;
			V1 = A1 * Ic1 + A2 * V3;
			V2 = Ic2 + A2 * Ic1 + A3 * V3;
			Ic1 = 2.f * V1 - Ic1;
			Ic2 = 2.f * V2 - Ic2;
		}
	};

	/** Feedback comb with a damped loop, as in Freeverb. Fixed storage, so nothing is allocated. */
	struct FComb
	{
		static constexpr int32 MaxLength = 1600;
		float Buffer[MaxLength] = {};
		int32 Length = MaxLength;
		int32 Pos = 0;
		float Store = 0.f;

		float Process(float X, float Feedback, float Damp)
		{
			const float Y = Buffer[Pos];
			Store = Y * (1.f - Damp) + Store * Damp;
			Buffer[Pos] = X + Store * Feedback;
			if (++Pos >= Length)
			{
				Pos = 0;
			}
			return Y;
		}
	};

	struct FAllPass
	{
		static constexpr int32 MaxLength = 700;
		float Buffer[MaxLength] = {};
		int32 Length = MaxLength;
		int32 Pos = 0;

		float Process(float X)
		{
			const float B = Buffer[Pos];
			Buffer[Pos] = X + B * 0.5f;
			if (++Pos >= Length)
			{
				Pos = 0;
			}
			return B - X;
		}
	};

	/** About a second of stone-street reverb, so distant sounds sit in the harbour instead of in your ear. */
	struct FReverb
	{
		FComb CombL[4];
		FComb CombR[4];
		FAllPass PassL[2];
		FAllPass PassR[2];

		FReverb()
		{
			// Mutually prime lengths (Freeverb's, scaled to 48 kHz) with a small left/right offset for width.
			const int32 Combs[4] = { 1213, 1291, 1389, 1475 };
			const int32 Passes[2] = { 605, 479 };
			for (int32 i = 0; i < 4; ++i)
			{
				CombL[i].Length = Combs[i];
				CombR[i].Length = Combs[i] + 25;
			}
			for (int32 i = 0; i < 2; ++i)
			{
				PassL[i].Length = Passes[i];
				PassR[i].Length = Passes[i] + 25;
			}
		}

		void Process(float In, float& OutL, float& OutR)
		{
			constexpr float Feedback = 0.83f;
			constexpr float Damp = 0.4f;
			float L = 0.f;
			float R = 0.f;
			for (int32 i = 0; i < 4; ++i)
			{
				L += CombL[i].Process(In, Feedback, Damp);
				R += CombR[i].Process(In, Feedback, Damp);
			}
			for (int32 i = 0; i < 2; ++i)
			{
				L = PassL[i].Process(L);
				R = PassR[i].Process(R);
			}
			OutL = L * 0.25f;
			OutR = R * 0.25f;
		}
	};

	/** One swell running up the quay wall and washing back: brown-noise body that brightens at the crest, plus foam hiss. */
	struct FWave
	{
		bool bActive = false;
		float T = 0.f;
		float Rise = 2.f;
		float Fall = 3.f;
		float Amp = 0.f;
		float Brown = 0.f;
		float BodyGain = 0.f;
		float FoamGain = 0.f;
		float GainL = 0.7f;
		float GainR = 0.7f;
		FOnePole Body;
		FSvf Foam;
	};

	/** Water slapping the quay stones and the moored hulls: a short band-passed noise burst. */
	struct FLap
	{
		bool bActive = false;
		float Attack = 0.f;
		float AttackMul = 0.f;
		float Decay = 0.f;
		float DecayMul = 0.f;
		float Amp = 0.f;
		float GainL = 0.7f;
		float GainR = 0.7f;
		FSvf Filter;
	};

	/** A mooring line or mast groaning: a stick-slip pulse train ringing two wooden resonances. */
	struct FCreak
	{
		bool bActive = false;
		float T = 0.f;
		float Length = 0.4f;
		float RateFrom = 30.f;
		float RateTo = 40.f;
		float PulseRate = 30.f;
		float Phase = 0.f;
		float Env = 0.f;
		float Amp = 0.f;
		float GainL = 0.7f;
		float GainR = 0.7f;
		FSvf Body;
		FSvf Overtone;
	};

	struct FBellPartial
	{
		float Ratio;
		float Amp;
		float Decay;
	};

	/** Inharmonic partials of a cast bronze bell: hum, prime (with a beating twin), tierce, quint, nominal and above. */
	constexpr int32 NumBellPartials = 8;
	const FBellPartial BellPartials[NumBellPartials] = {
		{ 0.5f, 0.55f, 7.f },
		{ 1.f, 0.6f, 5.f },
		{ 1.0028f, 0.25f, 4.5f },
		{ 1.183f, 0.45f, 3.6f },
		{ 1.506f, 0.3f, 2.8f },
		{ 2.f, 0.5f, 2.4f },
		{ 2.514f, 0.22f, 1.6f },
		{ 3.011f, 0.12f, 1.f },
	};

	/** A struck bell: each partial is a decaying quadrature oscillator, plus the clank of the clapper. */
	struct FBell
	{
		static constexpr int32 NumPartials = NumBellPartials;
		float X[NumPartials] = {};
		float Y[NumPartials] = {};
		float C[NumPartials] = {};
		float S[NumPartials] = {};
		float Amp[NumPartials] = {};
		float ClankEnv = 0.f;
		float ClankDecay = 0.f;
		float GainL = 0.7f;
		float GainR = 0.7f;
		bool bRinging = false;
		FSvf Clank;
		FOnePole Distance;

		void Configure(float Prime, float DecayScale, float DistanceHz, float Pan)
		{
			for (int32 i = 0; i < NumPartials; ++i)
			{
				const float W = TwoPi * Prime * BellPartials[i].Ratio / Rate;
				const float D = DecayPerSample(BellPartials[i].Decay * DecayScale);
				C[i] = D * FMath::Cos(W);
				S[i] = D * FMath::Sin(W);
				Amp[i] = BellPartials[i].Amp;
			}
			Clank.Set(Prime * 7.1f, 1.5f);
			ClankDecay = DecayPerSample(0.012f);
			Distance.SetCutoff(DistanceHz);
			PanGains(Pan, GainL, GainR);
		}

		/** Adds energy to the cosine phase, so the output (the sine phase) starts from where it is: no click. */
		void Strike(float Velocity)
		{
			for (int32 i = 0; i < NumPartials; ++i)
			{
				X[i] += Velocity * Amp[i];
			}
			ClankEnv += Velocity;
			bRinging = true;
		}

		float Process(float Noise)
		{
			float Sum = 0.f;
			for (int32 i = 0; i < NumPartials; ++i)
			{
				const float Xi = X[i];
				const float Yi = Y[i];
				X[i] = Xi * C[i] - Yi * S[i];
				Y[i] = Xi * S[i] + Yi * C[i];
				Sum += Y[i];
			}
			Sum += Clank.BandPass(Noise) * ClankEnv;
			ClankEnv *= ClankDecay;
			return Distance.Process(Sum);
		}

		/** Per block: parks the bell once inaudible, so the oscillators never sink into denormals. */
		void CheckSilence()
		{
			float Energy = ClankEnv;
			for (int32 i = 0; i < NumPartials; ++i)
			{
				Energy += FMath::Abs(X[i]) + FMath::Abs(Y[i]);
			}
			if (Energy < 1e-5f)
			{
				for (int32 i = 0; i < NumPartials; ++i)
				{
					X[i] = 0.f;
					Y[i] = 0.f;
				}
				ClankEnv = 0.f;
				Clank.Reset();
				Distance.Z = 0.f;
				bRinging = false;
			}
		}
	};

	/** A herring gull call: a run of harsh, swooping notes. */
	struct FGull
	{
		bool bActive = false;
		bool bInNote = false;
		bool bFirstNote = true;
		int32 NotesLeft = 0;
		float T = 0.f;
		float NoteLength = 0.3f;
		float GapLength = 0.1f;
		float Base = 1000.f;
		float Peak = 1300.f;
		float End = 800.f;
		float Freq = 1000.f;
		float Phase = 0.f;
		float RoughPhase = 0.f;
		float RoughRate = 70.f;
		float Env = 0.f;
		float EnvTarget = 0.f;
		float Amp = 0.f;
		float GainL = 0.7f;
		float GainR = 0.7f;
		FOnePole Tone;
	};

	/** The mix in one place: linear gains per layer. */
	struct FLevels
	{
		float Bed = 0.4f;
		float Waves = 0.36f;
		float Laps = 1.6f;
		float Wind = 0.55f;
		float Whistle = 0.7f;
		float Creaks = 0.3f;
		float Bells = 0.08f;
		float Gulls = 0.11f;
		float Rain = 0.09f;
		float Reverb = 0.6f;
	};

	struct FSynth
	{
		FLevels Levels;

		FSynth();
		/** Writes Frames interleaved stereo frames. */
		void Render(int16* Out, int32 Frames, float TargetProgress, float TargetRain, float TargetVolume);

	private:
		void Control(float TargetProgress, float TargetRain, float TargetVolume);
		void ControlSea(float Swell);
		void ControlWind();
		void ControlCreaks();
		void ControlBells();
		void ControlGulls();
		void Synthesize(float& OutL, float& OutR);

		FRng Rng;
		int32 BlockLeft = 0;
		bool bFirstBlock = true;

		// Controls, smoothed here so game-thread changes never step.
		float Progress = 0.f;
		float RainAmount = 0.f;
		float NightAmount = 0.f;
		float SeaAmount = 1.f;
		float WindAmount = 0.5f;
		float GullChance = 0.f;
		float Master = 1.f;
		float Fade = 0.f;
		float SwellPhaseA = 0.f;
		float SwellPhaseB = 0.37f;

		// Sea
		float BrownL = 0.f;
		float BrownR = 0.f;
		float BedGain = 0.f;
		FOnePole BedL;
		FOnePole BedR;
		FWave Waves[3];
		float WaveCountdown = 0.3f;
		FLap Laps[3];
		float LapCountdown = 0.8f;

		// Wind
		FSvf WindL;
		FSvf WindR;
		FSvf Whistle;
		float Gust = 0.3f;
		float GustTarget = 0.3f;
		float GustHold = 2.f;
		float WindPhase = 0.f;
		float WindGain = 0.f;
		float WhistleGain = 0.f;

		// Rigging
		FCreak Creaks[2];
		float CreakCountdown = 3.f;

		// Bells
		FBell Church;
		FBell Buoy;
		float BuoyCountdown = 12.f;
		float BuoyEcho = -1.f;
		int32 SunriseStrikes = 0;
		float SunriseCountdown = 0.f;
		bool bSunriseRung = false;

		// Gulls
		FGull Gulls[2];
		float GullCountdown = 2.f;

		// Rain
		FSvf HissL;
		FSvf HissR;
		FOnePole HissTopL;
		FOnePole HissTopR;
		FSvf DropL;
		FSvf DropR;
		float RainGain = 0.f;
		float DropChance = 0.f;

		// Space and output
		FReverb Reverb;
		FOnePole AirL;
		FOnePole AirR;
		float DcInL = 0.f;
		float DcInR = 0.f;
		float DcOutL = 0.f;
		float DcOutR = 0.f;
	};

	FSynth::FSynth()
	{
		BedL.SetCutoff(110.f);
		BedR.SetCutoff(120.f);
		// The church across the water rings low and long; the bell buoy out in the bay is smaller, nearer and clangier.
		Church.Configure(311.f, 1.f, 2400.f, 0.62f);
		Buoy.Configure(523.f, 0.55f, 2000.f, 0.3f);
		HissL.Set(1400.f, 0.7f);
		HissR.Set(1500.f, 0.7f);
		HissTopL.SetCutoff(5000.f);
		HissTopR.SetCutoff(4600.f);
		DropL.Set(3000.f, 5.f);
		DropR.Set(3500.f, 5.f);
		for (FGull& Gull : Gulls)
		{
			Gull.Tone.SetCutoff(3800.f);
		}
		// Air absorption: everything here is metres to kilometres away, so the very top octave is soft.
		AirL.SetCutoff(10000.f);
		AirR.SetCutoff(10000.f);
	}

	void FSynth::Render(int16* Out, int32 Frames, float TargetProgress, float TargetRain, float TargetVolume)
	{
		// Faded out and asked to stay quiet: the caller's buffer is already zeroed, so skip the synthesis.
		if (TargetVolume <= 0.f && Fade <= 0.f && !bFirstBlock)
		{
			return;
		}
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			if (BlockLeft <= 0)
			{
				Control(TargetProgress, TargetRain, TargetVolume);
				BlockLeft = Block;
			}
			--BlockLeft;

			float L = 0.f;
			float R = 0.f;
			Synthesize(L, R);
			L = AirL.Process(L);
			R = AirR.Process(R);

			// DC blocker (~20 Hz), then the soft clip that keeps the peak at -6 dBFS.
			DcOutL = L - DcInL + 0.9974f * DcOutL;
			DcInL = L;
			DcOutR = R - DcInR + 0.9974f * DcOutR;
			DcInR = R;
			Out[Frame * 2] = int16(SoftClip(DcOutL) * 32767.f);
			Out[Frame * 2 + 1] = int16(SoftClip(DcOutR) * 32767.f);
		}
	}

	void FSynth::Control(float TargetProgress, float TargetRain, float TargetVolume)
	{
		TargetProgress = FMath::Clamp(TargetProgress, 0.f, 1.f);
		TargetRain = FMath::Clamp(TargetRain, 0.f, 1.f);
		if (bFirstBlock)
		{
			// Start in the right mood instead of gliding there, and don't ring the sunrise bell for a game resumed at dawn.
			bFirstBlock = false;
			Progress = TargetProgress;
			RainAmount = TargetRain;
			bSunriseRung = Progress >= SunriseBellAt;
		}
		Progress += (TargetProgress - Progress) * (BlockSeconds / 3.f);
		RainAmount += (TargetRain - RainAmount) * (BlockSeconds / 4.f);

		// Dusk has a few gulls, the night is quieter but windier, dawn calms the wind and wakes the gulls.
		NightAmount = SmoothStep(0.08f, 0.35f, Progress) * (1.f - SmoothStep(0.7f, 0.92f, Progress));
		const float Dawn = SmoothStep(0.72f, 0.98f, Progress);
		const float Dusk = 1.f - SmoothStep(0.f, 0.15f, Progress);
		SeaAmount = 0.85f - 0.25f * NightAmount + 0.2f * RainAmount;
		WindAmount = FMath::Clamp(0.45f + 0.55f * NightAmount - 0.2f * Dawn + 0.4f * RainAmount, 0.f, 1.2f);
		GullChance = FMath::Max(Dawn, 0.3f * Dusk) * (1.f - 0.7f * RainAmount);
		Master += ((1.f - 0.25f * NightAmount) - Master) * (BlockSeconds / 2.f);
		const float FadeTarget = FMath::Clamp(TargetVolume, 0.f, 1.f);
		Fade = Fade < FadeTarget
			? FMath::Min(FadeTarget, Fade + BlockSeconds / 2.5f)
			: FMath::Max(FadeTarget, Fade - BlockSeconds / 1.5f);

		// Two slow, incommensurate swells: the sea breathes without an audible loop.
		SwellPhaseA = Wrap01(SwellPhaseA + BlockSeconds * 0.071f);
		SwellPhaseB = Wrap01(SwellPhaseB + BlockSeconds * 0.113f);
		const float Swell = 0.5f + 0.25f * FastSin(SwellPhaseA) + 0.25f * FastSin(SwellPhaseB);

		ControlSea(Swell);
		ControlWind();
		ControlCreaks();
		ControlBells();
		ControlGulls();

		RainGain = Levels.Rain * RainAmount;
		DropChance = RainAmount * 70.f / Rate;
	}

	void FSynth::ControlSea(float Swell)
	{
		BedGain = Levels.Bed * SeaAmount * (0.55f + 0.45f * Swell);

		WaveCountdown -= BlockSeconds;
		if (WaveCountdown <= 0.f)
		{
			WaveCountdown = Rng.Range(3.f, 7.5f) * (1.f - 0.3f * RainAmount);
			for (FWave& Wave : Waves)
			{
				if (!Wave.bActive)
				{
					Wave.bActive = true;
					Wave.T = 0.f;
					Wave.Rise = Rng.Range(1.2f, 2.6f);
					Wave.Fall = Rng.Range(1.6f, 3.2f);
					Wave.Amp = Rng.Range(0.5f, 1.f) * (0.7f + 0.6f * Swell);
					PanGains(Rng.Range(0.15f, 0.85f), Wave.GainL, Wave.GainR);
					break;
				}
			}
		}
		for (FWave& Wave : Waves)
		{
			if (!Wave.bActive)
			{
				continue;
			}
			Wave.T += BlockSeconds;
			float E;
			if (Wave.T < Wave.Rise)
			{
				E = 0.5f - 0.5f * FMath::Cos(Pi * Wave.T / Wave.Rise);
			}
			else
			{
				E = FMath::Exp(-(Wave.T - Wave.Rise) / Wave.Fall);
				if (E < 0.004f)
				{
					Wave.bActive = false;
					continue;
				}
			}
			Wave.Body.SetCutoff(140.f + 1500.f * E * E);
			Wave.Foam.Set(1500.f + 2500.f * E, 0.7f);
			const float Gain = Levels.Waves * SeaAmount * Wave.Amp;
			Wave.BodyGain = Gain * E;
			Wave.FoamGain = Gain * 0.35f * E * E;
		}

		LapCountdown -= BlockSeconds;
		if (LapCountdown <= 0.f)
		{
			LapCountdown = Rng.Range(0.2f, 1.4f) / FMath::Max(0.3f, SeaAmount * (0.5f + Swell));
			for (FLap& Lap : Laps)
			{
				if (!Lap.bActive)
				{
					Lap.bActive = true;
					Lap.Filter.Reset();
					Lap.Filter.Set(Rng.Range(200.f, 700.f), Rng.Range(1.5f, 4.f));
					Lap.Attack = 1.f;
					Lap.AttackMul = DecayPerSample(Rng.Range(0.003f, 0.012f));
					Lap.Decay = 1.f;
					Lap.DecayMul = DecayPerSample(Rng.Range(0.04f, 0.14f));
					Lap.Amp = Levels.Laps * SeaAmount * Rng.Range(0.3f, 1.f);
					PanGains(Rng.Range(0.1f, 0.9f), Lap.GainL, Lap.GainR);
					break;
				}
			}
		}
		for (FLap& Lap : Laps)
		{
			if (Lap.bActive && Lap.Decay < 0.001f)
			{
				Lap.bActive = false;
			}
		}
	}

	void FSynth::ControlWind()
	{
		GustHold -= BlockSeconds;
		if (GustHold <= 0.f)
		{
			// Mostly gentle with the odd strong gust; stronger ones at night.
			const float U = Rng.Unit();
			GustTarget = 0.1f + U * U * (0.6f + 0.3f * NightAmount);
			GustHold = Rng.Range(1.5f, 6.f);
		}
		Gust += (GustTarget - Gust) * (BlockSeconds / 1.8f);
		WindPhase = Wrap01(WindPhase + BlockSeconds * 0.37f);
		const float Waver = 40.f * FastSin(WindPhase);
		WindL.Set(250.f + 650.f * Gust + Waver, 1.2f);
		WindR.Set(280.f + 600.f * Gust - Waver, 1.2f);
		Whistle.Set(900.f + 900.f * Gust, 14.f);
		WindGain = Levels.Wind * WindAmount * (0.15f + 0.85f * Gust);
		WhistleGain = Levels.Whistle * WindAmount * FMath::Max(0.f, Gust - 0.5f) * 2.f;
	}

	void FSynth::ControlCreaks()
	{
		CreakCountdown -= BlockSeconds;
		if (CreakCountdown <= 0.f)
		{
			// The harder the wind and swell work the boats, the more the rigging talks.
			CreakCountdown = Rng.Range(2.5f, 9.f) / (0.5f + 0.5f * WindAmount + 0.5f * Gust);
			for (FCreak& Creak : Creaks)
			{
				if (!Creak.bActive)
				{
					const float BodyHz = Rng.Range(150.f, 380.f);
					Creak.bActive = true;
					Creak.T = 0.f;
					Creak.Phase = 0.f;
					Creak.Length = Rng.Range(0.25f, 0.7f);
					Creak.RateFrom = Rng.Range(18.f, 40.f);
					Creak.RateTo = Creak.RateFrom * Rng.Range(0.7f, 1.6f);
					Creak.Body.Reset();
					Creak.Body.Set(BodyHz, Rng.Range(10.f, 20.f));
					Creak.Overtone.Reset();
					Creak.Overtone.Set(BodyHz * Rng.Range(2.2f, 2.7f), 12.f);
					Creak.Amp = Levels.Creaks * Rng.Range(0.4f, 1.f) * (0.6f + 0.4f * WindAmount);
					PanGains(Rng.Range(0.1f, 0.9f), Creak.GainL, Creak.GainR);
					break;
				}
			}
		}
		for (FCreak& Creak : Creaks)
		{
			if (!Creak.bActive)
			{
				continue;
			}
			Creak.T += BlockSeconds;
			// Keep running a little past the last pulse so the wood rings out instead of cutting off.
			if (Creak.T >= Creak.Length + 0.4f)
			{
				Creak.bActive = false;
				continue;
			}
			const float U = FMath::Min(1.f, Creak.T / Creak.Length);
			Creak.Env = Creak.T < Creak.Length ? FMath::Sin(Pi * U) : 0.f;
			Creak.PulseRate = FMath::Lerp(Creak.RateFrom, Creak.RateTo, U);
		}
	}

	void FSynth::ControlBells()
	{
		// The bell buoy out in the bay: rare, mostly heard at night, sometimes rocking back for a second stroke.
		BuoyCountdown -= BlockSeconds;
		if (BuoyCountdown <= 0.f)
		{
			BuoyCountdown = Rng.Range(20.f, 50.f);
			Buoy.Strike(Rng.Range(0.5f, 1.f) * (0.35f + 0.65f * NightAmount));
			BuoyEcho = Rng.Unit() < 0.6f ? Rng.Range(0.7f, 1.4f) : -1.f;
		}
		if (BuoyEcho > 0.f)
		{
			BuoyEcho -= BlockSeconds;
			if (BuoyEcho <= 0.f)
			{
				Buoy.Strike(Rng.Range(0.2f, 0.45f) * (0.35f + 0.65f * NightAmount));
			}
		}

		// The church greets the sunrise once per dawn; a new life (back to dusk) re-arms it.
		if (Progress < 0.5f)
		{
			bSunriseRung = false;
		}
		if (!bSunriseRung && Progress >= SunriseBellAt)
		{
			bSunriseRung = true;
			SunriseStrikes = 5;
			SunriseCountdown = 0.f;
		}
		if (SunriseStrikes > 0)
		{
			SunriseCountdown -= BlockSeconds;
			if (SunriseCountdown <= 0.f)
			{
				Church.Strike(SunriseStrikes == 5 ? 1.f : Rng.Range(0.75f, 0.9f));
				--SunriseStrikes;
				SunriseCountdown = 2.8f;
			}
		}
		Church.CheckSilence();
		Buoy.CheckSilence();
	}

	void FSynth::ControlGulls()
	{
		GullCountdown -= BlockSeconds;
		if (GullCountdown <= 0.f)
		{
			GullCountdown = Rng.Range(2.f, 7.f);
			if (Rng.Unit() < GullChance)
			{
				for (FGull& Gull : Gulls)
				{
					if (!Gull.bActive)
					{
						Gull.bActive = true;
						Gull.bInNote = false;
						Gull.bFirstNote = true;
						Gull.T = 0.f;
						Gull.GapLength = Rng.Range(0.f, 0.3f);
						Gull.NotesLeft = 2 + int32(Rng.Unit() * 5.f);
						Gull.Base = Rng.Range(850.f, 1250.f);
						Gull.RoughRate = Rng.Range(55.f, 90.f);
						Gull.Env = 0.f;
						Gull.EnvTarget = 0.f;
						// Some wheel overhead, most call from the rooftops and the far pier.
						Gull.Amp = Levels.Gulls * Rng.Range(0.3f, 1.f);
						PanGains(Rng.Range(0.05f, 0.95f), Gull.GainL, Gull.GainR);
						break;
					}
				}
			}
		}
		for (FGull& Gull : Gulls)
		{
			if (!Gull.bActive)
			{
				continue;
			}
			Gull.T += BlockSeconds;
			if (Gull.bInNote)
			{
				const float U = Gull.T / Gull.NoteLength;
				if (U >= 1.f)
				{
					Gull.bInNote = false;
					Gull.T = 0.f;
					Gull.EnvTarget = 0.f;
					--Gull.NotesLeft;
				}
				else
				{
					// "Kee-ow": up to a peak early in the note, then a long fall.
					Gull.Freq = U < 0.3f
						? FMath::Lerp(Gull.Base, Gull.Peak, SmoothStep(0.f, 1.f, U / 0.3f))
						: FMath::Lerp(Gull.Peak, Gull.End, (U - 0.3f) / 0.7f);
					Gull.EnvTarget = FMath::Min(1.f, Gull.T / 0.02f) * FMath::Min(1.f, (Gull.NoteLength - Gull.T) / 0.05f);
				}
			}
			else if (Gull.T >= Gull.GapLength)
			{
				if (Gull.NotesLeft > 0)
				{
					// The first note is the long cry, the rest are shorter, quicker "kek"s.
					Gull.NoteLength = Gull.bFirstNote ? Rng.Range(0.3f, 0.45f) : Rng.Range(0.14f, 0.24f);
					Gull.bFirstNote = false;
					Gull.bInNote = true;
					Gull.T = 0.f;
					Gull.GapLength = Rng.Range(0.07f, 0.15f);
					Gull.Base *= Rng.Range(0.96f, 1.04f);
					Gull.Peak = Gull.Base * Rng.Range(1.25f, 1.45f);
					Gull.End = Gull.Base * Rng.Range(0.7f, 0.85f);
					Gull.Freq = Gull.Base;
				}
				else if (Gull.Env < 0.001f)
				{
					Gull.bActive = false;
				}
			}
		}
	}

	void FSynth::Synthesize(float& OutL, float& OutR)
	{
		const float NoiseL = Rng.Signed();
		const float NoiseR = Rng.Signed();
		float L = 0.f;
		float R = 0.f;
		float Send = 0.f;

		// Sea bed: brown-noise rumble, decorrelated left and right.
		BrownL = BrownL * 0.996f + NoiseL * 0.04f;
		BrownR = BrownR * 0.996f + NoiseR * 0.04f;
		L += BedL.Process(BrownL) * BedGain;
		R += BedR.Process(BrownR) * BedGain;

		for (FWave& Wave : Waves)
		{
			if (Wave.bActive)
			{
				const float N = Rng.Signed();
				Wave.Brown = Wave.Brown * 0.985f + N * 0.12f;
				const float S = Wave.Body.Process(Wave.Brown) * Wave.BodyGain + Wave.Foam.BandPass(N) * Wave.FoamGain;
				L += S * Wave.GainL;
				R += S * Wave.GainR;
				Send += S * 0.08f;
			}
		}

		for (FLap& Lap : Laps)
		{
			if (Lap.bActive)
			{
				Lap.Attack *= Lap.AttackMul;
				Lap.Decay *= Lap.DecayMul;
				const float S = Lap.Filter.BandPass(Rng.Signed()) * Lap.Amp * (1.f - Lap.Attack) * Lap.Decay;
				L += S * Lap.GainL;
				R += S * Lap.GainR;
				Send += S * 0.2f;
			}
		}

		// Wind: two broad bands that move with the gusts, and a thin whistle in the rigging when it blows hard.
		const float Whistling = WhistleGain > 0.f ? Whistle.BandPass(NoiseL + NoiseR) * WhistleGain : 0.f;
		L += WindL.BandPass(NoiseL) * WindGain + Whistling * 0.8f;
		R += WindR.BandPass(NoiseR) * WindGain + Whistling * 0.6f;

		for (FCreak& Creak : Creaks)
		{
			if (Creak.bActive)
			{
				Creak.Phase += Creak.PulseRate / Rate;
				float Excite = NoiseL * 0.03f * Creak.Env;
				if (Creak.Phase >= 1.f)
				{
					Creak.Phase -= 1.f;
					Excite += Creak.Env * (0.6f + 0.4f * Rng.Unit()) * 60.f;
				}
				const float S = (Creak.Body.BandPass(Excite) + 0.5f * Creak.Overtone.BandPass(Excite)) * Creak.Amp;
				L += S * Creak.GainL;
				R += S * Creak.GainR;
				Send += S * 0.3f;
			}
		}

		if (Church.bRinging)
		{
			const float S = Church.Process(NoiseR) * Levels.Bells;
			L += S * Church.GainL;
			R += S * Church.GainR;
			Send += S * 0.7f;
		}
		if (Buoy.bRinging)
		{
			const float S = Buoy.Process(NoiseL) * Levels.Bells * 0.35f;
			L += S * Buoy.GainL;
			R += S * Buoy.GainR;
			Send += S * 0.8f;
		}

		for (FGull& Gull : Gulls)
		{
			if (Gull.bActive)
			{
				Gull.Env += (Gull.EnvTarget - Gull.Env) * 0.02f;
				Gull.Phase = Wrap01(Gull.Phase + Gull.Freq / Rate);
				Gull.RoughPhase = Wrap01(Gull.RoughPhase + Gull.RoughRate / Rate);
				// Harmonics by waveshaping the sine (cos 2x and sin 3x), a rough flutter, and breath.
				const float X = FastSin(Gull.Phase);
				float V = X + 0.5f * (1.f - 2.f * X * X) + 0.3f * X * (3.f - 4.f * X * X);
				V *= 0.7f + 0.3f * FastSin(Gull.RoughPhase);
				V += NoiseR * 0.2f;
				const float S = Gull.Tone.Process(V) * Gull.Env * Gull.Amp;
				L += S * Gull.GainL;
				R += S * Gull.GainR;
				Send += S * 0.5f;
			}
		}

		// Rain: high hiss on the water and roofs, and drops ticking on the glass at random pitches.
		if (RainGain > 0.0005f)
		{
			float DropInL = 0.f;
			float DropInR = 0.f;
			if (Rng.Unit() < DropChance)
			{
				const float Height = Rng.Range(0.2f, 1.f);
				const float Side = Rng.Unit();
				DropInL = Height * (1.f - Side);
				DropInR = Height * Side;
				const float Hz = Rng.Range(1800.f, 5000.f);
				DropL.Set(Hz, 5.f);
				DropR.Set(Hz * 1.07f, 5.f);
			}
			L += RainGain * (HissTopL.Process(HissL.HighPass(NoiseL)) + DropL.BandPass(DropInL) * 6.f);
			R += RainGain * (HissTopR.Process(HissR.HighPass(NoiseR)) + DropR.BandPass(DropInR) * 6.f);
		}

		float WetL = 0.f;
		float WetR = 0.f;
		Reverb.Process(Send, WetL, WetR);
		const float Gain = Master * Fade;
		OutL = (L + WetL * Levels.Reverb) * Gain;
		OutR = (R + WetR * Levels.Reverb) * Gain;
	}
}

UInhAmbienceSound* UInhAmbienceSound::Create(UObject* Outer)
{
	UInhAmbienceSound* Sound = NewObject<UInhAmbienceSound>(Outer);
	Sound->SetSampleRate(InhAmbienceDsp::SampleRate);
	Sound->NumChannels = InhAmbienceDsp::Channels;
	Sound->Duration = INDEFINITELY_LOOPING_DURATION;
	Sound->bLooping = true;
	Sound->SoundGroup = SOUNDGROUP_Default;
	Sound->Synth = new InhAmbienceDsp::FSynth();
	return Sound;
}

int32 UInhAmbienceSound::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
	if (NumSamples <= 0)
	{
		return 0;
	}
	// The engine hands back the same buffer every callback and Reset keeps its capacity: no allocation after the first.
	OutAudio.Reset();
	OutAudio.AddZeroed(NumSamples * int32(sizeof(int16)));
	if (Synth)
	{
		Synth->Render(reinterpret_cast<int16*>(OutAudio.GetData()), NumSamples / InhAmbienceDsp::Channels,
			LifeProgress.load(std::memory_order_relaxed), Rain.load(std::memory_order_relaxed),
			FadeTarget.load(std::memory_order_relaxed));
	}
	return NumSamples;
}

void UInhAmbienceSound::FinishDestroy()
{
	// By now the audio device has stopped every source using this wave (USoundWave waits for that before finishing).
	delete Synth;
	Synth = nullptr;
	Super::FinishDestroy();
}

void UInhAmbience::Start(UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	if (IsValid(Component) && Component->GetWorld() != World)
	{
		// A new world: let the old component go with its world, and give the new one its own wave so two sources
		// never render the same synthesiser at once.
		Component->Stop();
		Component = nullptr;
		Sound = nullptr;
	}
	if (!IsValid(Component))
	{
		Component = nullptr;
		Sound = UInhAmbienceSound::Create(this);
		Sound->SetLifeProgress(LifeProgress);
		Sound->SetRain(Rain);
		Component = UGameplayStatics::CreateSound2D(WorldContext, Sound, 1.f, 1.f, 0.f, nullptr, false, false);
		if (!Component)
		{
			// No audio device (dedicated server, -nosound): the game simply runs silent.
			UE_LOG(LogInheritance, Log, TEXT("Ambience: no audio device, running silent."));
			return;
		}
	}

	// Stop only fades the synthesiser, so the component keeps playing: restarting a procedural wave could briefly
	// render it from two sources.
	if (Sound)
	{
		Sound->SetFadeTarget(1.f);
	}
	if (!Component->IsPlaying())
	{
		Component->Play();
	}
}

void UInhAmbience::Stop()
{
	if (Sound)
	{
		Sound->SetFadeTarget(0.f);
	}
}

void UInhAmbience::SetLifeProgress(float Progress)
{
	LifeProgress = FMath::Clamp(Progress, 0.f, 1.f);
	if (Sound)
	{
		Sound->SetLifeProgress(LifeProgress);
	}
}

void UInhAmbience::SetRain(float Amount01)
{
	Rain = FMath::Clamp(Amount01, 0.f, 1.f);
	if (Sound)
	{
		Sound->SetRain(Rain);
	}
}

#undef LOCTEXT_NAMESPACE
