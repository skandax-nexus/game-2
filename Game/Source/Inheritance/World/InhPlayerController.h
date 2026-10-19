#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "World/InhProp.h"
#include "UI/InhScreen.h"
#include "Blueprint/UserWidget.h"
#include "InhPlayerController.generated.h"

class AInhRoomBuilder;
class AInhCameraPawn;
class UInhHudScreen;
class UInhToast;
class UInhGameSubsystem;

/**
 * Runs the flow of a life: title -> reading of the will -> the room -> sunrise -> a new life.
 * Also turns touches into camera moves and prop taps.
 */
UCLASS()
class INHERITANCE_API AInhPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AInhPlayerController();

	// --- flow ---
	void BeginSession(const FString& DisplayName);
	void ShowTitle();
	void ShowWillReading();
	void EnterRoom();
	void StartNewLife();
	void ShowSunrise();

	// --- panels over the room ---
	template <class T>
	T* OpenPanel();
	void ClosePanel();

	/** Spends an action on the server and reports the outcome. */
	void RunAction(const FString& Type, const FString& Field = FString(), const FString& Id = FString());
	bool IsActionInFlight() const { return bActionInFlight; }
	void Toast(const FText& Message);
	void SelectProp(EInhPropKind Kind, const FString& RefId);
	void ClearSelection();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	template <class T>
	T* ShowMain();
	void OnLifeChanged();
	void TickClock();
	void AfterConnect();
	void RefreshRoom();

	void OnTouchPressed(ETouchIndex::Type Finger, FVector Location);
	void OnTouchMoved(ETouchIndex::Type Finger, FVector Location);
	void OnTouchReleased(ETouchIndex::Type Finger, FVector Location);
	void OnZoomIn();
	void OnZoomOut();
	void HandleTap(const FVector2D& ScreenPosition);

	UInhGameSubsystem* Game() const;
	AInhCameraPawn* CameraPawn() const;

	UPROPERTY() TObjectPtr<AInhRoomBuilder> Room;
	UPROPERTY() TObjectPtr<UInhScreen> MainScreen;
	UPROPERTY() TObjectPtr<UInhScreen> Panel;
	UPROPERTY() TObjectPtr<UInhToast> ToastWidget;

	TWeakObjectPtr<AInhProp> SelectedProp;
	TSet<FString> Investigated;
	FString InvestigatedForLife;

	TMap<ETouchIndex::Type, FVector2D> Touches;
	FVector2D PressPosition = FVector2D::ZeroVector;
	double PressTime = 0.0;
	float PinchDistance = 0.f;
	bool bDragged = false;

	FTimerHandle ClockTimer;
	FDelegateHandle LifeChangedHandle;
	FDelegateHandle ToastHandle;
	double LastDeathCheck = 0.0;
	bool bInRoom = false;
	bool bActionInFlight = false;
};

template <class T>
T* AInhPlayerController::OpenPanel()
{
	ClosePanel();
	T* Widget = CreateWidget<T>(this, T::StaticClass());
	Widget->AddToViewport(10);
	Panel = Widget;
	return Widget;
}

template <class T>
T* AInhPlayerController::ShowMain()
{
	ClosePanel();
	if (MainScreen)
	{
		MainScreen->RemoveFromParent();
	}
	T* Widget = CreateWidget<T>(this, T::StaticClass());
	Widget->AddToViewport(0);
	MainScreen = Widget;
	return Widget;
}
