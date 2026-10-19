#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dom/JsonObject.h"
#include "InhBackendSubsystem.generated.h"

struct FInhResponse
{
	bool bOk = false;
	int32 Status = 0;
	TSharedPtr<FJsonObject> Json;
	TArray<uint8> Bytes;
	FString ErrorCode;
	FString ErrorMessage;
};

using FInhResponseHandler = TFunction<void(const FInhResponse&)>;

/** Thin HTTP client for the Inheritance server. All callbacks run on the game thread. */
UCLASS()
class INHERITANCE_API UInhBackendSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	void SetToken(const FString& InToken) { Token = InToken; }
	bool HasToken() const { return !Token.IsEmpty(); }
	const FString& GetBaseUrl() const { return BaseUrl; }

	void SendJson(const FString& Verb, const FString& Path, const TSharedPtr<FJsonObject>& Body, FInhResponseHandler OnDone);
	void SendBytes(const FString& Verb, const FString& Path, const TArray<uint8>& Body, const FString& ContentType, FInhResponseHandler OnDone);
	void GetBytes(const FString& Path, FInhResponseHandler OnDone);

private:
	void Dispatch(TSharedRef<class IHttpRequest, ESPMode::ThreadSafe> Request, bool bExpectJson, FInhResponseHandler OnDone);

	FString BaseUrl;
	FString Token;
};
