#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "World/InhCinematics.h"
#include "World/InhProp.h"
#include "UI/InhScreen.h"
#include "Blueprint/UserWidget.h"
#include "InhPlayerController.generated.h"

class AInhRoomBuilder;
class AInhHarbourBuilder;
class AInhAtmosphereFX;
class AInhCameraPawn;
class UInhAmbience;
class UInhCinemaOverlay;
class UInhHudScreen;
class UInhToast;
class UInhGameSubsystem;

/**
 * Runs the flow of a life: title -> Opening cinematic -> reading of the will -> the room -> Sunrise cinematic ->
 * sunrise -> a new life. Also turns touches into camera moves and prop taps, plays the cinematics with their letterbox,
 * captions and fades, and keeps the world's clock (room, harbour, air and ambience) in step with the life.
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

	// --- cinematics ---
	/**
	 * Plays one of the authored camera moves (InhCinematics): hides the screens, letterboxes, shows captions as their
	 * shots begin, and lets a tap skip it; then unletterboxes, brings the screens back and calls OnDone. TitleAttract
	 * is different: it loops quietly behind the title screen (no letterbox, no skipping) and never calls OnDone.
	 */
	void PlayCinematic(EInhCinematic Which, TFunction<void()> OnDone);
	bool IsCinematicPlaying() const { return bCinematicPlaying; }

	// --- panels over the room ---
	template <class T>
	T* OpenPanel();
	void ClosePanel();

	/** Spends an action on the server and reports the outcome. */
	void RunAction(const FString& Type, const FString& Field = FString(), const FString& Id = FString());
	bool IsActionInFlight() const { return bActionInFlight; }
	void Toast(const FText& Message);
	void SelectProp(EInhPropKind Kind, FString RefId);
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
	void ShowRoomHud();
	void FindWorldActors();
	/** Sets the time of night everywhere: the room's sky (and through it the harbour and the air), the ambience and the rain. */
	void SetWorldProgress(float Progress);

	// --- cinematics ---
	void StartTitleAttract();
	void BeginCinematic(EInhCinematic Which, TFunction<void()> OnDone);
	void StartPendingCinematic();
	/** World staging that goes with a cinematic: fades, the Ferryman and Mara, the dawn. */
	void StageCinematic(EInhCinematic Which, float Seconds);
	void OnShotStarted(int32 Index);
	void OnCinematicShotsDone();
	void SkipCinematic();
	void FinishSkip();
	void FinishCinematic();
	/** Leaving the title without a cinematic: the attract loop glides back into the room. */
	void StopAttract();
	void FadeToDawnWhite();
	void TickDawn();
	void EndDawn();
	void ListenToShots(AInhCameraPawn* Cam);
	/** Keeps the camera on the last frame of the cinematic that just played, instead of going home. */
	void HoldFinalShot();
	void SetScreensHidden(bool bInHidden);
	void ApplyScreenVisibility(UUserWidget* Widget) const;

	void OnTouchPressed(ETouchIndex::Type Finger, FVector Location);
	void OnTouchMoved(ETouchIndex::Type Finger, FVector Location);
	void OnTouchReleased(ETouchIndex::Type Finger, FVector Location);
	void OnZoomIn();
	void OnZoomOut();
	void OnSkipKey();
	void HandleTap(const FVector2D& ScreenPosition);

	UInhGameSubsystem* Game() const;
	AInhCameraPawn* CameraPawn() const;

	UPROPERTY() TObjectPtr<AInhRoomBuilder> Room;
	UPROPERTY() TObjectPtr<AInhHarbourBuilder> Harbour;
	UPROPERTY() TObjectPtr<AInhAtmosphereFX> Atmosphere;
	UPROPERTY() TObjectPtr<UInhScreen> MainScreen;
	UPROPERTY() TObjectPtr<UInhScreen> Panel;
	UPROPERTY() TObjectPtr<UInhToast> ToastWidget;
	UPROPERTY() TObjectPtr<UInhCinemaOverlay> Cinema;
	UPROPERTY() TObjectPtr<UInhAmbience> Ambience;

	TWeakObjectPtr<AInhProp> SelectedProp;
	TSet<FString> Investigated;
	FString InvestigatedForLife;

	TMap<ETouchIndex::Type, FVector2D> Touches;
	FVector2D PressPosition = FVector2D::ZeroVector;
	double PressTime = 0.0;
	float PinchDistance = 0.f;
	bool bDragged = false;
	/** Set by a touch that begins during a skippable cinematic: only such a touch skips it when lifted. */
	bool bSkipArmed = false;

	FTimerHandle ClockTimer;
	FDelegateHandle LifeChangedHandle;
	FDelegateHandle ToastHandle;
	double LastDeathCheck = 0.0;
	bool bInRoom = false;
	bool bActionInFlight = false;

	// Cinematics.
	/** A copy of the playing shots, for their captions. */
	TArray<FInhShot> CinematicShots;
	TFunction<void()> CinematicDone;
	TFunction<void()> PendingDone;
	EInhCinematic CurrentCinematic = EInhCinematic::TitleAttract;
	EInhCinematic PendingCinematic = EInhCinematic::Opening;
	TWeakObjectPtr<AInhCameraPawn> ShotListener;
	FDelegateHandle ShotStartedHandle;
	/** The Sunrise's fade to white, and the fade before a skip lands. */
	FTimerHandle CinematicTimer;
	/** The fade to black before a cinematic that opens on a cut (and retries of the title loop). */
	FTimerHandle PendingTimer;
	FTimerHandle DawnTimer;
	/** Bumped whenever a cinematic starts or ends, so stale callbacks from the camera are ignored. */
	uint32 CinematicSerial = 0;
	bool bCinematicPlaying = false;
	bool bCinematicSkippable = false;
	bool bScreensHidden = false;

	// The world's clock.
	float WorldProgress = 0.3f;
	float DawnFrom = 0.f;
	double DawnStartTime = 0.0;
	bool bDawnRising = false;
};

template <class T>
T* AInhPlayerController::OpenPanel()
{
	ClosePanel();
	T* Widget = CreateWidget<T>(this, T::StaticClass());
	Widget->AddToViewport(10);
	Panel = Widget;
	ApplyScreenVisibility(Widget);
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
	// While a cinematic owns the screen, a new screen waits hidden and appears with everything else when it ends.
	ApplyScreenVisibility(Widget);
	return Widget;
}
