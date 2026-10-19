#include "Core/InhGameSubsystem.h"
#include "Core/InhSaveGame.h"
#include "Online/InhBackendSubsystem.h"
#include "Audio/InhWav.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sound/SoundWaveProcedural.h"
#include "Inheritance.h"

#define LOCTEXT_NAMESPACE "Inheritance"

void UInhGameSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Backend = Collection.InitializeDependency<UInhBackendSubsystem>();

	SaveData = Cast<UInhSaveGame>(UGameplayStatics::LoadGameFromSlot(UInhSaveGame::GetSlotName(), 0));
	if (!SaveData)
	{
		SaveData = Cast<UInhSaveGame>(UGameplayStatics::CreateSaveGameObject(UInhSaveGame::StaticClass()));
	}
	Backend->SetToken(SaveData->Token);
	LoadStoryCharacters();
}

bool UInhGameSubsystem::IsRegistered() const
{
	return !SaveData->Token.IsEmpty();
}

FString UInhGameSubsystem::GetSavedDisplayName() const
{
	return SaveData->DisplayName;
}

void UInhGameSubsystem::Save()
{
	UGameplayStatics::SaveGameToSlot(SaveData, UInhSaveGame::GetSlotName(), 0);
}

FText UInhGameSubsystem::ErrorText(const FInhResponse& Response)
{
	return FText::FromString(Response.ErrorMessage.IsEmpty() ? TEXT("Something went wrong.") : Response.ErrorMessage);
}

void UInhGameSubsystem::Connect(const FString& DisplayName, FInhDone OnDone)
{
	TWeakObjectPtr<UInhGameSubsystem> Weak(this);
	if (IsRegistered())
	{
		LoadMe(MoveTemp(OnDone));
		return;
	}

	// Only the time zone is shared, never a location: the server estimates sunrise from it.
	const int32 UtcOffsetMinutes = FMath::RoundToInt((FDateTime::Now() - FDateTime::UtcNow()).GetTotalMinutes());
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("displayName"), DisplayName);
	Body->SetNumberField(TEXT("utcOffsetMinutes"), UtcOffsetMinutes);

	Backend->SendJson(TEXT("POST"), TEXT("/v1/players"), Body, [Weak, OnDone](const FInhResponse& R)
	{
		if (!Weak.IsValid()) return;
		if (!R.bOk)
		{
			OnDone(false, ErrorText(R));
			return;
		}
		Weak->SaveData->Token = InhJson::Str(R.Json, TEXT("token"));
		Weak->Player = FInhPlayer::FromJson(InhJson::Obj(R.Json, TEXT("player")));
		Weak->SaveData->DisplayName = Weak->Player.DisplayName;
		Weak->Backend->SetToken(Weak->SaveData->Token);
		Weak->Save();
		Weak->LoadMe(OnDone);
	});
}

void UInhGameSubsystem::LoadMe(FInhDone OnDone)
{
	TWeakObjectPtr<UInhGameSubsystem> Weak(this);
	Backend->SendJson(TEXT("GET"), TEXT("/v1/me"), nullptr, [Weak, OnDone](const FInhResponse& R)
	{
		if (!Weak.IsValid()) return;
		if (R.Status == 401)
		{
			// The server forgot us (for example after a reset). Start over as a new citizen.
			Weak->SaveData->Token.Reset();
			Weak->Backend->SetToken(FString());
			Weak->Save();
			Weak->Connect(Weak->SaveData->DisplayName, OnDone);
			return;
		}
		if (!R.bOk)
		{
			OnDone(false, ErrorText(R));
			return;
		}
		Weak->Player = FInhPlayer::FromJson(InhJson::Obj(R.Json, TEXT("player")));
		Weak->LivesLived = InhJson::Int(R.Json, TEXT("livesLived"));
		Weak->SetLife(InhJson::Obj(R.Json, TEXT("currentLife")));
		Weak->RefreshSeason();
		OnDone(true, FText::GetEmpty());
	});
}

void UInhGameSubsystem::SetLife(const TSharedPtr<FJsonObject>& LifeJson)
{
	Life = FInhLife::FromJson(LifeJson);
	OnLifeChanged.Broadcast();
}

void UInhGameSubsystem::InheritNewLife(FInhDone OnDone)
{
	TWeakObjectPtr<UInhGameSubsystem> Weak(this);
	Backend->SendJson(TEXT("POST"), TEXT("/v1/lives"), nullptr, [Weak, OnDone](const FInhResponse& R)
	{
		if (!Weak.IsValid()) return;
		if (R.ErrorCode == TEXT("already_alive"))
		{
			Weak->RefreshLife(OnDone);
			return;
		}
		if (!R.bOk)
		{
			OnDone(false, ErrorText(R));
			return;
		}
		Weak->LivesLived++;
		Weak->SetLife(InhJson::Obj(R.Json, TEXT("life")));
		Weak->RefreshSeason();
		OnDone(true, FText::GetEmpty());
	});
}

void UInhGameSubsystem::RefreshLife(FInhDone OnDone)
{
	TWeakObjectPtr<UInhGameSubsystem> Weak(this);
	Backend->SendJson(TEXT("GET"), TEXT("/v1/lives/current"), nullptr, [Weak, OnDone](const FInhResponse& R)
	{
		if (!Weak.IsValid()) return;
		if (R.ErrorCode == TEXT("no_life"))
		{
			Weak->SetLife(nullptr);
			if (OnDone) OnDone(true, FText::GetEmpty());
			return;
		}
		if (!R.bOk)
		{
			if (OnDone) OnDone(false, ErrorText(R));
			return;
		}
		Weak->SetLife(InhJson::Obj(R.Json, TEXT("life")));
		if (OnDone) OnDone(true, FText::GetEmpty());
	});
}

void UInhGameSubsystem::RefreshSeason(FInhDone OnDone)
{
	TWeakObjectPtr<UInhGameSubsystem> Weak(this);
	Backend->SendJson(TEXT("GET"), TEXT("/v1/season"), nullptr, [Weak, OnDone](const FInhResponse& R)
	{
		if (!Weak.IsValid()) return;
		if (R.bOk)
		{
			Weak->Season = FInhSeason::FromJson(R.Json);
		}
		if (OnDone) OnDone(R.bOk, R.bOk ? FText::GetEmpty() : ErrorText(R));
	});
}

void UInhGameSubsystem::PerformAction(const FString& Type, const FString& Field, const FString& Value, FInhDone OnDone)
{
	if (!Life.IsAlive())
	{
		OnDone(false, LOCTEXT("NotAlive", "You are between lives."));
		return;
	}
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("type"), Type);
	if (!Field.IsEmpty())
	{
		Body->SetStringField(Field, Value);
	}

	TWeakObjectPtr<UInhGameSubsystem> Weak(this);
	Backend->SendJson(TEXT("POST"), FString::Printf(TEXT("/v1/lives/%s/actions"), *Life.Id), Body, [Weak, OnDone](const FInhResponse& R)
	{
		if (!Weak.IsValid()) return;
		if (!R.bOk)
		{
			if (R.ErrorCode == TEXT("life_over"))
			{
				Weak->RefreshLife(nullptr);
			}
			OnDone(false, ErrorText(R));
			return;
		}
		const FText Message = FText::FromString(InhJson::Str(R.Json, TEXT("message")));
		Weak->SetLife(InhJson::Obj(R.Json, TEXT("life")));
		Weak->OnToast.Broadcast(Message);
		Weak->RefreshSeason();
		OnDone(true, Message);
	});
}

void UInhGameSubsystem::SetLegacyChoice(const FString& ItemId, const FString& Choice, FInhDone OnDone)
{
	TSharedPtr<FJsonObject> Decisions = MakeShared<FJsonObject>();
	for (const auto& Pair : Life.Legacy)
	{
		Decisions->SetStringField(Pair.Key, Pair.Value);
	}
	Decisions->SetStringField(ItemId, Choice);
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("decisions"), Decisions);

	TWeakObjectPtr<UInhGameSubsystem> Weak(this);
	Backend->SendJson(TEXT("PUT"), FString::Printf(TEXT("/v1/lives/%s/legacy"), *Life.Id), Body, [Weak, OnDone](const FInhResponse& R)
	{
		if (!Weak.IsValid()) return;
		if (R.bOk)
		{
			Weak->SetLife(InhJson::Obj(R.Json, TEXT("life")));
		}
		OnDone(R.bOk, R.bOk ? FText::GetEmpty() : ErrorText(R));
	});
}

void UInhGameSubsystem::UploadVoiceNote(const TArray<uint8>& Wav, FInhDone OnDone)
{
	TWeakObjectPtr<UInhGameSubsystem> Weak(this);
	Backend->SendBytes(TEXT("PUT"), FString::Printf(TEXT("/v1/lives/%s/voice-note"), *Life.Id), Wav, TEXT("audio/wav"),
		[Weak, OnDone](const FInhResponse& R)
		{
			if (!Weak.IsValid()) return;
			if (R.bOk)
			{
				Weak->SetLife(InhJson::Obj(R.Json, TEXT("life")));
			}
			OnDone(R.bOk, R.bOk ? LOCTEXT("NoteSaved", "Your voice note is sealed for your heir.") : ErrorText(R));
		});
}

void UInhGameSubsystem::PlayPredecessorVoiceNote(FInhDone OnDone)
{
	if (!Life.bHasPredecessor || !Life.bPredecessorHasVoiceNote)
	{
		OnDone(false, LOCTEXT("NoNote", "Your predecessor left no voice note. Only silence."));
		return;
	}
	TWeakObjectPtr<UInhGameSubsystem> Weak(this);
	Backend->GetBytes(FString::Printf(TEXT("/v1/lives/%s/voice-note"), *Life.PredecessorLifeId), [Weak, OnDone](const FInhResponse& R)
	{
		if (!Weak.IsValid()) return;
		if (!R.bOk || R.Bytes.Num() == 0)
		{
			OnDone(false, R.bOk ? LOCTEXT("EmptyNote", "The voice note is empty.") : ErrorText(R));
			return;
		}
		Weak->PlayWav(R.Bytes);
		OnDone(true, FText::GetEmpty());
	});
}

void UInhGameSubsystem::ReportPredecessorVoiceNote(FInhDone OnDone)
{
	if (!Life.bHasPredecessor)
	{
		return;
	}
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("reason"), TEXT("reported from the Reading of the Will"));
	TWeakObjectPtr<UInhGameSubsystem> Weak(this);
	Backend->SendJson(TEXT("POST"), FString::Printf(TEXT("/v1/lives/%s/voice-note/report"), *Life.PredecessorLifeId), Body,
		[Weak, OnDone](const FInhResponse& R)
		{
			if (!Weak.IsValid()) return;
			OnDone(R.bOk, R.bOk ? LOCTEXT("Reported", "Thank you. Notes reported by several heirs are removed.") : ErrorText(R));
		});
}

void UInhGameSubsystem::PlayOwnVoiceNotePreview(const TArray<uint8>& Wav)
{
	PlayWav(Wav);
}

void UInhGameSubsystem::PlayWav(const TArray<uint8>& Wav)
{
	StopVoicePlayback();
	USoundWaveProcedural* Sound = InhWav::CreateSound(this, Wav);
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!Sound || !World)
	{
		OnToast.Broadcast(LOCTEXT("BadAudio", "The voice note could not be played."));
		return;
	}
	VoicePlayback = UGameplayStatics::CreateSound2D(World, Sound, 1.f, 1.f, 0.f, nullptr, false, false);
	if (VoicePlayback)
	{
		VoicePlayback->Play();
	}
}

void UInhGameSubsystem::StopVoicePlayback()
{
	if (VoicePlayback)
	{
		VoicePlayback->Stop();
		VoicePlayback = nullptr;
	}
}

void UInhGameSubsystem::ChooseFaction(const FString& FactionId, FInhDone OnDone)
{
	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("faction"), FactionId);
	TWeakObjectPtr<UInhGameSubsystem> Weak(this);
	Backend->SendJson(TEXT("POST"), TEXT("/v1/me/faction"), Body, [Weak, OnDone](const FInhResponse& R)
	{
		if (!Weak.IsValid()) return;
		if (R.bOk)
		{
			Weak->Player = FInhPlayer::FromJson(InhJson::Obj(R.Json, TEXT("player")));
			Weak->OnLifeChanged.Broadcast();
		}
		OnDone(R.bOk, R.bOk ? FText::GetEmpty() : ErrorText(R));
	});
}

void UInhGameSubsystem::FetchFamilyTree(TFunction<void(bool, const TArray<FInhFamilyEntry>&)> OnDone)
{
	TWeakObjectPtr<UInhGameSubsystem> Weak(this);
	Backend->SendJson(TEXT("GET"), TEXT("/v1/me/family-tree"), nullptr, [Weak, OnDone](const FInhResponse& R)
	{
		if (!Weak.IsValid()) return;
		TArray<FInhFamilyEntry> Entries;
		for (const auto& O : InhJson::Objects(R.Json, TEXT("lives")))
		{
			Entries.Add(FInhFamilyEntry::FromJson(O));
		}
		OnDone(R.bOk, Entries);
	});
}

void UInhGameSubsystem::SkipToSunrise(FInhDone OnDone)
{
	TWeakObjectPtr<UInhGameSubsystem> Weak(this);
	Backend->SendJson(TEXT("POST"), FString::Printf(TEXT("/v1/lives/%s/sunrise"), *Life.Id), nullptr, [Weak, OnDone](const FInhResponse& R)
	{
		if (!Weak.IsValid()) return;
		if (R.bOk)
		{
			Weak->SetLife(nullptr);
		}
		OnDone(R.bOk, R.bOk ? FText::GetEmpty() : ErrorText(R));
	});
}

bool UInhGameSubsystem::HasReadWillForCurrentLife() const
{
	return Life.IsValid() && SaveData->LastWillReadForLife == Life.Id;
}

void UInhGameSubsystem::MarkWillRead()
{
	SaveData->LastWillReadForLife = Life.Id;
	Save();
}

void UInhGameSubsystem::LoadStoryCharacters()
{
	const FString Path = FPaths::ProjectContentDir() / TEXT("Data/Story/Season1.json");
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogInheritance, Warning, TEXT("Story file not found at %s"), *Path);
		return;
	}
	for (const auto& O : InhJson::Objects(InhJson::Parse(Text), TEXT("characters")))
	{
		FInhCharacter C;
		C.Name = InhJson::Str(O, TEXT("name"));
		C.Title = InhJson::Str(O, TEXT("title"));
		C.Bio = InhJson::Str(O, TEXT("bio"));
		Characters.Add(C);
	}
}

#undef LOCTEXT_NAMESPACE
