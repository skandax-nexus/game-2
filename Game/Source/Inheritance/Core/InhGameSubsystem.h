#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Core/InhTypes.h"
#include "InhGameSubsystem.generated.h"

class UInhBackendSubsystem;
class UInhSaveGame;
class UAudioComponent;
struct FInhResponse;

using FInhDone = TFunction<void(bool bOk, const FText& Message)>;

DECLARE_MULTICAST_DELEGATE(FOnInhLifeChanged);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnInhToast, const FText&);

/**
 * Owns the player's state for the session: who they are, the life they are living,
 * and the season around them. Screens call into this and listen to OnLifeChanged.
 */
UCLASS()
class INHERITANCE_API UInhGameSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// --- session ---
	bool IsRegistered() const;
	FString GetSavedDisplayName() const;
	/** Registers on first launch, then loads the player and any life in progress. */
	void Connect(const FString& DisplayName, FInhDone OnDone);
	void InheritNewLife(FInhDone OnDone);
	void RefreshLife(FInhDone OnDone);
	void RefreshSeason(FInhDone OnDone = nullptr);

	// --- living ---
	/** Spends one action. Field is the id field the action needs (debtId, feudId, clueId, itemId). */
	void PerformAction(const FString& Type, const FString& Field, const FString& Value, FInhDone OnDone);
	void SetLegacyChoice(const FString& ItemId, const FString& Choice, FInhDone OnDone);
	void UploadVoiceNote(const TArray<uint8>& Wav, FInhDone OnDone);
	void PlayPredecessorVoiceNote(FInhDone OnDone);
	void ReportPredecessorVoiceNote(FInhDone OnDone);
	void PlayOwnVoiceNotePreview(const TArray<uint8>& Wav);
	void StopVoicePlayback();
	void ChooseFaction(const FString& FactionId, FInhDone OnDone);
	void FetchFamilyTree(TFunction<void(bool, const TArray<FInhFamilyEntry>&)> OnDone);
	/** Development only: ends the current life now (server must allow it). */
	void SkipToSunrise(FInhDone OnDone);

	// --- state ---
	const FInhLife& GetLife() const { return Life; }
	const FInhPlayer& GetPlayer() const { return Player; }
	const FInhSeason& GetSeason() const { return Season; }
	const TArray<FInhCharacter>& GetCharacters() const { return Characters; }
	int32 GetLivesLived() const { return LivesLived; }

	bool HasReadWillForCurrentLife() const;
	void MarkWillRead();

	FOnInhLifeChanged OnLifeChanged;
	FOnInhToast OnToast;

private:
	void SetLife(const TSharedPtr<FJsonObject>& LifeJson);
	void LoadMe(FInhDone OnDone);
	void Save();
	void LoadStoryCharacters();
	void PlayWav(const TArray<uint8>& Wav);
	static FText ErrorText(const FInhResponse& Response);

	UPROPERTY() TObjectPtr<UInhBackendSubsystem> Backend;
	UPROPERTY() TObjectPtr<UInhSaveGame> SaveData;
	UPROPERTY() TObjectPtr<UAudioComponent> VoicePlayback;

	FInhLife Life;
	FInhPlayer Player;
	FInhSeason Season;
	TArray<FInhCharacter> Characters;
	int32 LivesLived = 0;
};
