#include "Core/InhTypes.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace InhJson
{
	FString Str(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		FString Value;
		if (Json.IsValid())
		{
			Json->TryGetStringField(Field, Value);
		}
		return Value;
	}

	int32 Int(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field, int32 Default)
	{
		double Value = Default;
		if (Json.IsValid())
		{
			Json->TryGetNumberField(Field, Value);
		}
		return FMath::RoundToInt(Value);
	}

	bool Bool(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		bool Value = false;
		if (Json.IsValid())
		{
			Json->TryGetBoolField(Field, Value);
		}
		return Value;
	}

	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Value = nullptr;
		if (Json.IsValid() && Json->TryGetObjectField(Field, Value) && Value)
		{
			return *Value;
		}
		return nullptr;
	}

	TArray<TSharedPtr<FJsonObject>> Objects(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		TArray<TSharedPtr<FJsonObject>> Result;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Json.IsValid() && Json->TryGetArrayField(Field, Values) && Values)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				const TSharedPtr<FJsonObject>* Object = nullptr;
				if (Value.IsValid() && Value->TryGetObject(Object) && Object)
				{
					Result.Add(*Object);
				}
			}
		}
		return Result;
	}

	FDateTime Date(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		FDateTime Value;
		FDateTime::ParseIso8601(*Str(Json, Field), Value);
		return Value;
	}

	TSharedPtr<FJsonObject> Parse(const FString& Text)
	{
		TSharedPtr<FJsonObject> Result;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Result);
		return Result;
	}

	FString Write(const TSharedRef<FJsonObject>& Json)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Json, Writer);
		return Out;
	}
}

using namespace InhJson;

FInhDebt FInhDebt::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	FInhDebt D;
	D.Id = Str(Json, TEXT("id"));
	D.Creditor = Str(Json, TEXT("creditor"));
	D.Amount = Int(Json, TEXT("amount"));
	D.Note = Str(Json, TEXT("note"));
	return D;
}

FInhItem FInhItem::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	FInhItem I;
	I.Id = Str(Json, TEXT("id"));
	I.Kind = Str(Json, TEXT("kind"));
	I.Name = Str(Json, TEXT("name"));
	I.Description = Str(Json, TEXT("description"));
	I.Value = Int(Json, TEXT("value"));
	I.bHidden = Bool(Json, TEXT("hidden"));
	const TSharedPtr<FJsonObject> Data = Obj(Json, TEXT("data"));
	I.Streak = Int(Data, TEXT("streak"));
	I.PageNumber = Int(Data, TEXT("number"));
	return I;
}

FInhFeud FInhFeud::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	FInhFeud F;
	F.Id = Str(Json, TEXT("id"));
	F.Rival = Str(Json, TEXT("rival"));
	F.Reason = Str(Json, TEXT("reason"));
	F.Severity = Str(Json, TEXT("severity"));
	return F;
}

FInhClue FInhClue::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	FInhClue C;
	C.Id = Str(Json, TEXT("id"));
	C.Title = Str(Json, TEXT("title"));
	C.Text = Str(Json, TEXT("text"));
	C.bResolved = Bool(Json, TEXT("resolved"));
	return C;
}

FInhPersonalStory FInhPersonalStory::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	FInhPersonalStory S;
	S.bValid = Json.IsValid();
	S.Kind = Str(Json, TEXT("kind"));
	S.Title = Str(Json, TEXT("title"));
	S.Text = Str(Json, TEXT("text"));
	S.Heirs = Int(Json, TEXT("heirs"));
	return S;
}

FInhEstate FInhEstate::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	FInhEstate E;
	E.Money = Int(Json, TEXT("money"));
	const TSharedPtr<FJsonObject> House = Obj(Json, TEXT("house"));
	E.HouseName = Str(House, TEXT("name"));
	E.HouseDescription = Str(House, TEXT("description"));
	for (const auto& O : Objects(Json, TEXT("debts"))) E.Debts.Add(FInhDebt::FromJson(O));
	for (const auto& O : Objects(Json, TEXT("items"))) E.Items.Add(FInhItem::FromJson(O));
	for (const auto& O : Objects(Json, TEXT("feuds"))) E.Feuds.Add(FInhFeud::FromJson(O));
	for (const auto& O : Objects(Json, TEXT("clues"))) E.Clues.Add(FInhClue::FromJson(O));
	E.PersonalStory = FInhPersonalStory::FromJson(Obj(Json, TEXT("personalStory")));
	for (const auto& O : Objects(Json, TEXT("history"))) E.PreviousOwners.Add(Str(O, TEXT("displayName")));
	return E;
}

float FInhLife::Progress() const
{
	const double Total = (DiesAt - StartedAt).GetTotalSeconds();
	if (Total <= 0.0)
	{
		return 1.f;
	}
	return FMath::Clamp(float((FDateTime::UtcNow() - StartedAt).GetTotalSeconds() / Total), 0.f, 1.f);
}

FString FInhLife::LegacyChoiceFor(const FString& ItemId) const
{
	const FString* Choice = Legacy.Find(ItemId);
	return Choice ? *Choice : FString(TEXT("keep"));
}

FInhLife FInhLife::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	FInhLife L;
	if (!Json.IsValid())
	{
		return L;
	}
	L.Id = Str(Json, TEXT("id"));
	L.Status = Str(Json, TEXT("status"));
	L.StartedAt = Date(Json, TEXT("startedAt"));
	L.DiesAt = Date(Json, TEXT("diesAt"));
	L.ActionsLeft = Int(Json, TEXT("actionsLeft"));
	L.ActionsMax = Int(Json, TEXT("actionsMax"), 10);
	L.SeasonDay = Int(Json, TEXT("seasonDay"), 1);

	const TSharedPtr<FJsonObject> Act = Obj(Json, TEXT("act"));
	L.ActId = Int(Act, TEXT("id"), 1);
	L.ActName = Str(Act, TEXT("name"));
	L.ActSummary = Str(Act, TEXT("summary"));

	if (const TSharedPtr<FJsonObject> Pred = Obj(Json, TEXT("predecessor")))
	{
		L.bHasPredecessor = true;
		L.PredecessorLifeId = Str(Pred, TEXT("lifeId"));
		L.PredecessorName = Str(Pred, TEXT("displayName"));
		L.bPredecessorHasVoiceNote = Bool(Pred, TEXT("hasVoiceNote"));
	}
	if (const TSharedPtr<FJsonObject> Prologue = Obj(Json, TEXT("prologue")))
	{
		L.bHasPrologue = true;
		L.PrologueSetting = Str(Prologue, TEXT("setting"));
		L.PrologueTranscript = Str(Prologue, TEXT("voiceNoteTranscript"));
	}

	L.Estate = FInhEstate::FromJson(Obj(Json, TEXT("estate")));
	if (const TSharedPtr<FJsonObject> LegacyJson = Obj(Json, TEXT("legacy")))
	{
		for (const auto& Pair : LegacyJson->Values)
		{
			L.Legacy.Add(Pair.Key, Pair.Value.IsValid() ? Pair.Value->AsString() : FString());
		}
	}
	L.bHasVoiceNote = Bool(Json, TEXT("hasVoiceNote"));
	L.VoiceNoteMaxSeconds = Int(Json, TEXT("voiceNoteMaxSeconds"), 30);

	const TArray<TSharedPtr<FJsonValue>>* LogValues = nullptr;
	if (Json->TryGetArrayField(TEXT("log"), LogValues) && LogValues)
	{
		for (const auto& V : *LogValues)
		{
			L.Log.Add(V->AsString());
		}
	}
	return L;
}

FInhPlayer FInhPlayer::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	FInhPlayer P;
	P.Id = Str(Json, TEXT("id"));
	P.DisplayName = Str(Json, TEXT("displayName"));
	P.Faction = Str(Json, TEXT("faction"));
	return P;
}

FInhSeason FInhSeason::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	FInhSeason S;
	S.bValid = Json.IsValid();
	S.Title = Str(Json, TEXT("title"));
	S.Day = Int(Json, TEXT("day"), 1);
	S.LengthDays = Int(Json, TEXT("lengthDays"), 56);
	const TSharedPtr<FJsonObject> Act = Obj(Json, TEXT("act"));
	S.ActName = Str(Act, TEXT("name"));
	S.ActSummary = Str(Act, TEXT("summary"));
	S.bFactionsUnlocked = Bool(Json, TEXT("factionsUnlocked"));
	for (const auto& O : Objects(Json, TEXT("factions")))
	{
		FInhFaction F;
		F.Id = Str(O, TEXT("id"));
		F.Name = Str(O, TEXT("name"));
		F.Creed = Str(O, TEXT("creed"));
		F.Score = Int(O, TEXT("score"));
		S.Factions.Add(F);
	}
	for (const auto& O : Objects(Json, TEXT("twists")))
	{
		FInhTwist T;
		T.Day = Int(O, TEXT("day"));
		T.Title = Str(O, TEXT("title"));
		T.Text = Str(O, TEXT("text"));
		S.Twists.Add(T);
	}
	const TSharedPtr<FJsonObject> Ledger = Obj(Json, TEXT("ledger"));
	S.PagesFound = Int(Ledger, TEXT("pagesFound"));
	S.PagesLost = Int(Ledger, TEXT("pagesLost"));
	const TSharedPtr<FJsonObject> Citizens = Obj(Json, TEXT("citizens"));
	S.CitizensAlive = Int(Citizens, TEXT("alive"));
	S.CitizensDeparted = Int(Citizens, TEXT("departed"));
	return S;
}

FInhFamilyEntry FInhFamilyEntry::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	FInhFamilyEntry E;
	E.LifeId = Str(Json, TEXT("lifeId"));
	E.Status = Str(Json, TEXT("status"));
	E.StartedAt = Date(Json, TEXT("startedAt"));
	E.House = Str(Json, TEXT("house"));
	E.InheritedFrom = Str(Json, TEXT("inheritedFrom"));
	E.PassedTo = Str(Json, TEXT("passedTo"));
	E.bHasVoiceNote = Bool(Json, TEXT("hasVoiceNote"));
	return E;
}
