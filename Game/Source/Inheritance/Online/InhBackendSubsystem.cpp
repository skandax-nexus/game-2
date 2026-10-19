#include "Online/InhBackendSubsystem.h"
#include "Core/InhSettings.h"
#include "Core/InhTypes.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Inheritance.h"

void UInhBackendSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	BaseUrl = GetDefault<UInhSettings>()->BackendUrl;
	BaseUrl.RemoveFromEnd(TEXT("/"));
}

void UInhBackendSubsystem::SendJson(const FString& Verb, const FString& Path, const TSharedPtr<FJsonObject>& Body, FInhResponseHandler OnDone)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(Verb);
	Request->SetURL(BaseUrl + Path);
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	if (Body.IsValid())
	{
		Request->SetContentAsString(InhJson::Write(Body.ToSharedRef()));
	}
	Dispatch(Request, true, MoveTemp(OnDone));
}

void UInhBackendSubsystem::SendBytes(const FString& Verb, const FString& Path, const TArray<uint8>& Body, const FString& ContentType, FInhResponseHandler OnDone)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(Verb);
	Request->SetURL(BaseUrl + Path);
	Request->SetHeader(TEXT("Content-Type"), ContentType);
	Request->SetContent(Body);
	Dispatch(Request, true, MoveTemp(OnDone));
}

void UInhBackendSubsystem::GetBytes(const FString& Path, FInhResponseHandler OnDone)
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb(TEXT("GET"));
	Request->SetURL(BaseUrl + Path);
	Dispatch(Request, false, MoveTemp(OnDone));
}

void UInhBackendSubsystem::Dispatch(TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request, bool bExpectJson, FInhResponseHandler OnDone)
{
	if (!Token.IsEmpty())
	{
		Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *Token));
	}
	Request->SetTimeout(20.f);

	const FString Url = Request->GetURL();
	Request->OnProcessRequestComplete().BindLambda(
		[OnDone = MoveTemp(OnDone), bExpectJson, Url](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			FInhResponse Result;
			if (!bConnected || !Response.IsValid())
			{
				Result.ErrorCode = TEXT("offline");
				Result.ErrorMessage = TEXT("Can't reach Hollow Bay. Check your connection and the server address.");
				UE_LOG(LogInheritance, Warning, TEXT("Request to %s failed to connect"), *Url);
				OnDone(Result);
				return;
			}

			Result.Status = Response->GetResponseCode();
			Result.bOk = Result.Status >= 200 && Result.Status < 300;
			const FString ContentType = Response->GetContentType();
			if (ContentType.Contains(TEXT("json")))
			{
				Result.Json = InhJson::Parse(Response->GetContentAsString());
			}
			else
			{
				Result.Bytes = Response->GetContent();
			}

			if (!Result.bOk)
			{
				const TSharedPtr<FJsonObject> Error = InhJson::Obj(Result.Json, TEXT("error"));
				Result.ErrorCode = InhJson::Str(Error, TEXT("code"));
				Result.ErrorMessage = InhJson::Str(Error, TEXT("message"));
				if (Result.ErrorMessage.IsEmpty())
				{
					Result.ErrorMessage = FString::Printf(TEXT("The server answered %d."), Result.Status);
				}
				UE_LOG(LogInheritance, Log, TEXT("%s -> %d %s"), *Url, Result.Status, *Result.ErrorCode);
			}
			else if (bExpectJson && !Result.Json.IsValid())
			{
				Result.bOk = false;
				Result.ErrorCode = TEXT("bad_response");
				Result.ErrorMessage = TEXT("The server sent something unexpected.");
			}
			OnDone(Result);
		});
	Request->ProcessRequest();
}
