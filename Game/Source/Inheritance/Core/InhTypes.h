#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "InhTypes.generated.h"

// Plain data mirrored from the server's JSON (see server/src/app.js). Parsed by hand so field
// names never depend on reflection naming rules.

USTRUCT(BlueprintType)
struct FInhDebt
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString Id;
	UPROPERTY(BlueprintReadOnly) FString Creditor;
	UPROPERTY(BlueprintReadOnly) int32 Amount = 0;
	UPROPERTY(BlueprintReadOnly) FString Note;

	static FInhDebt FromJson(const TSharedPtr<FJsonObject>& Json);
};

USTRUCT(BlueprintType)
struct FInhItem
{
	GENERATED_BODY()

	// trinket, property, letter, box or ledger_page
	UPROPERTY(BlueprintReadOnly) FString Id;
	UPROPERTY(BlueprintReadOnly) FString Kind;
	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) FString Description;
	UPROPERTY(BlueprintReadOnly) int32 Value = 0;
	UPROPERTY(BlueprintReadOnly) bool bHidden = false;
	UPROPERTY(BlueprintReadOnly) int32 Streak = 0;
	UPROPERTY(BlueprintReadOnly) int32 PageNumber = 0;

	bool IsLedgerPage() const { return Kind == TEXT("ledger_page"); }
	bool IsSellable() const { return IsLedgerPage() || Value > 0; }

	static FInhItem FromJson(const TSharedPtr<FJsonObject>& Json);
};

USTRUCT(BlueprintType)
struct FInhFeud
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString Id;
	UPROPERTY(BlueprintReadOnly) FString Rival;
	UPROPERTY(BlueprintReadOnly) FString Reason;
	// grudge or blood
	UPROPERTY(BlueprintReadOnly) FString Severity;

	bool IsBlood() const { return Severity == TEXT("blood"); }

	static FInhFeud FromJson(const TSharedPtr<FJsonObject>& Json);
};

USTRUCT(BlueprintType)
struct FInhClue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString Id;
	UPROPERTY(BlueprintReadOnly) FString Title;
	UPROPERTY(BlueprintReadOnly) FString Text;
	UPROPERTY(BlueprintReadOnly) bool bResolved = false;

	static FInhClue FromJson(const TSharedPtr<FJsonObject>& Json);
};

USTRUCT(BlueprintType)
struct FInhPersonalStory
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) bool bValid = false;
	UPROPERTY(BlueprintReadOnly) FString Kind;
	UPROPERTY(BlueprintReadOnly) FString Title;
	UPROPERTY(BlueprintReadOnly) FString Text;
	UPROPERTY(BlueprintReadOnly) int32 Heirs = 0;

	static FInhPersonalStory FromJson(const TSharedPtr<FJsonObject>& Json);
};

USTRUCT(BlueprintType)
struct FInhEstate
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 Money = 0;
	UPROPERTY(BlueprintReadOnly) FString HouseName;
	UPROPERTY(BlueprintReadOnly) FString HouseDescription;
	UPROPERTY(BlueprintReadOnly) TArray<FInhDebt> Debts;
	UPROPERTY(BlueprintReadOnly) TArray<FInhItem> Items;
	UPROPERTY(BlueprintReadOnly) TArray<FInhFeud> Feuds;
	UPROPERTY(BlueprintReadOnly) TArray<FInhClue> Clues;
	UPROPERTY(BlueprintReadOnly) FInhPersonalStory PersonalStory;
	// Display names of everyone who owned this estate before, oldest first.
	UPROPERTY(BlueprintReadOnly) TArray<FString> PreviousOwners;

	const FInhItem* FindItem(const FString& Id) const { return Items.FindByPredicate([&](const FInhItem& I) { return I.Id == Id; }); }
	const FInhDebt* FindDebt(const FString& Id) const { return Debts.FindByPredicate([&](const FInhDebt& D) { return D.Id == Id; }); }
	const FInhFeud* FindFeud(const FString& Id) const { return Feuds.FindByPredicate([&](const FInhFeud& F) { return F.Id == Id; }); }
	const FInhClue* FindClue(const FString& Id) const { return Clues.FindByPredicate([&](const FInhClue& C) { return C.Id == Id; }); }

	static FInhEstate FromJson(const TSharedPtr<FJsonObject>& Json);
};

USTRUCT(BlueprintType)
struct FInhLife
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString Id;
	UPROPERTY(BlueprintReadOnly) FString Status;
	UPROPERTY(BlueprintReadOnly) FDateTime StartedAt;
	UPROPERTY(BlueprintReadOnly) FDateTime DiesAt;
	UPROPERTY(BlueprintReadOnly) int32 ActionsLeft = 0;
	UPROPERTY(BlueprintReadOnly) int32 ActionsMax = 10;
	UPROPERTY(BlueprintReadOnly) int32 SeasonDay = 1;
	UPROPERTY(BlueprintReadOnly) int32 ActId = 1;
	UPROPERTY(BlueprintReadOnly) FString ActName;
	UPROPERTY(BlueprintReadOnly) FString ActSummary;

	UPROPERTY(BlueprintReadOnly) bool bHasPredecessor = false;
	UPROPERTY(BlueprintReadOnly) FString PredecessorLifeId;
	UPROPERTY(BlueprintReadOnly) FString PredecessorName;
	UPROPERTY(BlueprintReadOnly) bool bPredecessorHasVoiceNote = false;

	UPROPERTY(BlueprintReadOnly) bool bHasPrologue = false;
	UPROPERTY(BlueprintReadOnly) FString PrologueSetting;
	UPROPERTY(BlueprintReadOnly) FString PrologueTranscript;

	UPROPERTY(BlueprintReadOnly) FInhEstate Estate;
	// Item id -> keep / sell / hide / destroy
	UPROPERTY(BlueprintReadOnly) TMap<FString, FString> Legacy;
	UPROPERTY(BlueprintReadOnly) bool bHasVoiceNote = false;
	UPROPERTY(BlueprintReadOnly) int32 VoiceNoteMaxSeconds = 30;
	UPROPERTY(BlueprintReadOnly) TArray<FString> Log;

	bool IsValid() const { return !Id.IsEmpty(); }
	bool IsAlive() const { return IsValid() && Status == TEXT("alive"); }
	FTimespan TimeLeft() const { return DiesAt - FDateTime::UtcNow(); }
	// 0 when the life starts, 1 at sunrise.
	float Progress() const;
	FString LegacyChoiceFor(const FString& ItemId) const;

	static FInhLife FromJson(const TSharedPtr<FJsonObject>& Json);
};

USTRUCT(BlueprintType)
struct FInhPlayer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString Id;
	UPROPERTY(BlueprintReadOnly) FString DisplayName;
	UPROPERTY(BlueprintReadOnly) FString Faction;

	static FInhPlayer FromJson(const TSharedPtr<FJsonObject>& Json);
};

USTRUCT(BlueprintType)
struct FInhFaction
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString Id;
	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) FString Creed;
	UPROPERTY(BlueprintReadOnly) int32 Score = 0;
};

USTRUCT(BlueprintType)
struct FInhTwist
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 Day = 0;
	UPROPERTY(BlueprintReadOnly) FString Title;
	UPROPERTY(BlueprintReadOnly) FString Text;
};

USTRUCT(BlueprintType)
struct FInhSeason
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) bool bValid = false;
	UPROPERTY(BlueprintReadOnly) FString Title;
	UPROPERTY(BlueprintReadOnly) int32 Day = 1;
	UPROPERTY(BlueprintReadOnly) int32 LengthDays = 56;
	UPROPERTY(BlueprintReadOnly) FString ActName;
	UPROPERTY(BlueprintReadOnly) FString ActSummary;
	UPROPERTY(BlueprintReadOnly) bool bFactionsUnlocked = false;
	UPROPERTY(BlueprintReadOnly) TArray<FInhFaction> Factions;
	UPROPERTY(BlueprintReadOnly) TArray<FInhTwist> Twists;
	UPROPERTY(BlueprintReadOnly) int32 PagesFound = 0;
	UPROPERTY(BlueprintReadOnly) int32 PagesLost = 0;
	UPROPERTY(BlueprintReadOnly) int32 CitizensAlive = 0;
	UPROPERTY(BlueprintReadOnly) int32 CitizensDeparted = 0;

	static FInhSeason FromJson(const TSharedPtr<FJsonObject>& Json);
};

USTRUCT(BlueprintType)
struct FInhFamilyEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString LifeId;
	UPROPERTY(BlueprintReadOnly) FString Status;
	UPROPERTY(BlueprintReadOnly) FDateTime StartedAt;
	UPROPERTY(BlueprintReadOnly) FString House;
	UPROPERTY(BlueprintReadOnly) FString InheritedFrom;
	UPROPERTY(BlueprintReadOnly) FString PassedTo;
	UPROPERTY(BlueprintReadOnly) bool bHasVoiceNote = false;

	static FInhFamilyEntry FromJson(const TSharedPtr<FJsonObject>& Json);
};

// Static story text bundled with the game (Content/Data/Story/Season1.json).
USTRUCT(BlueprintType)
struct FInhCharacter
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) FString Title;
	UPROPERTY(BlueprintReadOnly) FString Bio;
};

namespace InhJson
{
	FString Str(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field);
	int32 Int(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field, int32 Default = 0);
	bool Bool(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field);
	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field);
	TArray<TSharedPtr<FJsonObject>> Objects(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field);
	FDateTime Date(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field);
	TSharedPtr<FJsonObject> Parse(const FString& Text);
	FString Write(const TSharedRef<FJsonObject>& Json);
}
