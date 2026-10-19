#include "World/InhPlayerController.h"
#include "World/InhCameraPawn.h"
#include "World/InhRoomBuilder.h"
#include "Core/InhGameSubsystem.h"
#include "UI/InhScreen.h"
#include "UI/InhTitleScreen.h"
#include "UI/InhWillScreen.h"
#include "UI/InhHudScreen.h"
#include "UI/InhSunriseScreen.h"
#include "EngineUtils.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "Inheritance"

namespace
{
	FString PropKey(EInhPropKind Kind, const FString& RefId)
	{
		return FString::Printf(TEXT("%d:%s"), int32(Kind), *RefId);
	}

	constexpr float TapMaxPixels = 14.f;
	constexpr double TapMaxSeconds = 0.35;
}

AInhPlayerController::AInhPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = false;
	bEnableTouchEvents = false;
}

UInhGameSubsystem* AInhPlayerController::Game() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UInhGameSubsystem>() : nullptr;
}

AInhCameraPawn* AInhPlayerController::CameraPawn() const
{
	return Cast<AInhCameraPawn>(GetPawn());
}

void AInhPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}

	for (TActorIterator<AInhRoomBuilder> It(GetWorld()); It; ++It)
	{
		Room = *It;
		break;
	}

	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	SetInputMode(Mode);

	ToastWidget = CreateWidget<UInhToast>(this, UInhToast::StaticClass());
	ToastWidget->AddToViewport(100);

	if (UInhGameSubsystem* G = Game())
	{
		LifeChangedHandle = G->OnLifeChanged.AddUObject(this, &AInhPlayerController::OnLifeChanged);
		ToastHandle = G->OnToast.AddUObject(this, &AInhPlayerController::Toast);
	}
	GetWorldTimerManager().SetTimer(ClockTimer, this, &AInhPlayerController::TickClock, 1.f, true);
	ShowTitle();
}

void AInhPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UInhGameSubsystem* G = Game())
	{
		G->OnLifeChanged.Remove(LifeChangedHandle);
		G->OnToast.Remove(ToastHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void AInhPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindTouch(IE_Pressed, this, &AInhPlayerController::OnTouchPressed);
	InputComponent->BindTouch(IE_Repeat, this, &AInhPlayerController::OnTouchMoved);
	InputComponent->BindTouch(IE_Released, this, &AInhPlayerController::OnTouchReleased);
	InputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &AInhPlayerController::OnZoomIn);
	InputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AInhPlayerController::OnZoomOut);
}

// --- flow ---------------------------------------------------------------------------------------

void AInhPlayerController::ShowTitle()
{
	bInRoom = false;
	ShowMain<UInhTitleScreen>();
}

void AInhPlayerController::BeginSession(const FString& DisplayName)
{
	UInhGameSubsystem* G = Game();
	TWeakObjectPtr<AInhPlayerController> Weak(this);
	G->Connect(DisplayName, [Weak](bool bOk, const FText& Error)
	{
		if (!Weak.IsValid()) return;
		if (!bOk)
		{
			if (UInhTitleScreen* Title = Cast<UInhTitleScreen>(Weak->MainScreen))
			{
				Title->ShowError(Error);
			}
			return;
		}
		Weak->AfterConnect();
	});
}

void AInhPlayerController::AfterConnect()
{
	UInhGameSubsystem* G = Game();
	if (G->GetLife().IsAlive())
	{
		if (G->HasReadWillForCurrentLife())
		{
			EnterRoom();
		}
		else
		{
			ShowWillReading();
		}
		return;
	}
	StartNewLife();
}

void AInhPlayerController::StartNewLife()
{
	TWeakObjectPtr<AInhPlayerController> Weak(this);
	Game()->InheritNewLife([Weak](bool bOk, const FText& Error)
	{
		if (!Weak.IsValid()) return;
		if (!bOk)
		{
			Weak->Toast(Error);
			return;
		}
		Weak->ShowWillReading();
	});
}

void AInhPlayerController::ShowWillReading()
{
	bInRoom = false;
	RefreshRoom();
	ShowMain<UInhWillScreen>();
	if (AInhCameraPawn* Cam = CameraPawn())
	{
		Cam->ResetView();
	}
}

void AInhPlayerController::EnterRoom()
{
	Game()->MarkWillRead();
	bInRoom = true;
	RefreshRoom();
	ShowMain<UInhHudScreen>();
}

void AInhPlayerController::ShowSunrise()
{
	bInRoom = false;
	ClearSelection();
	Game()->StopVoicePlayback();
	if (Room)
	{
		Room->SetLifeProgress(1.f);
	}
	ShowMain<UInhSunriseScreen>();
}

void AInhPlayerController::ClosePanel()
{
	if (Panel)
	{
		Panel->RemoveFromParent();
		Panel = nullptr;
	}
}

void AInhPlayerController::Toast(const FText& Message)
{
	if (ToastWidget)
	{
		ToastWidget->Show(Message);
	}
}

void AInhPlayerController::OnLifeChanged()
{
	const FInhLife& Life = Game()->GetLife();
	if (!Life.IsValid())
	{
		if (bInRoom)
		{
			ShowSunrise();
		}
		return;
	}
	RefreshRoom();
}

void AInhPlayerController::RefreshRoom()
{
	if (!Room)
	{
		return;
	}
	const FInhLife& Life = Game()->GetLife();
	if (Life.Id != InvestigatedForLife)
	{
		Investigated.Reset();
		InvestigatedForLife = Life.Id;
	}

	const FString SelectedKey = SelectedProp.IsValid() ? PropKey(SelectedProp->GetKind(), SelectedProp->GetRefId()) : FString();
	Room->ShowLife(Life);
	Room->SetLifeProgress(Life.Progress());

	SelectedProp = nullptr;
	for (TActorIterator<AInhProp> It(GetWorld()); It; ++It)
	{
		const FString Key = PropKey(It->GetKind(), It->GetRefId());
		if (Investigated.Contains(Key))
		{
			It->MarkInvestigated();
		}
		if (Key == SelectedKey)
		{
			It->SetSelected(true);
			SelectedProp = *It;
		}
	}
}

void AInhPlayerController::TickClock()
{
	UInhGameSubsystem* G = Game();
	const FInhLife& Life = G->GetLife();
	if (!bInRoom || !Life.IsAlive())
	{
		return;
	}
	if (Room)
	{
		Room->SetLifeProgress(Life.Progress());
	}
	if (UInhHudScreen* Hud = Cast<UInhHudScreen>(MainScreen))
	{
		Hud->UpdateClock();
	}

	// At sunrise, ask the server; it ends the life and OnLifeChanged moves us to the sunrise screen.
	const double Now = FPlatformTime::Seconds();
	if (Life.TimeLeft().GetTotalSeconds() <= 0.0 && Now - LastDeathCheck > 10.0)
	{
		LastDeathCheck = Now;
		G->RefreshLife(nullptr);
	}
}

void AInhPlayerController::RunAction(const FString& Type, const FString& Field, const FString& Id)
{
	if (bActionInFlight)
	{
		return;
	}
	bActionInFlight = true;
	TWeakObjectPtr<AInhPlayerController> Weak(this);
	Game()->PerformAction(Type, Field, Id, [Weak](bool bOk, const FText& Message)
	{
		if (!Weak.IsValid()) return;
		Weak->bActionInFlight = false;
		if (!bOk)
		{
			Weak->Toast(Message);
		}
		else if (UInhHudScreen* Hud = Cast<UInhHudScreen>(Weak->MainScreen))
		{
			Hud->HidePropCard();
		}
	});
}

// --- props --------------------------------------------------------------------------------------

void AInhPlayerController::SelectProp(EInhPropKind Kind, FString RefId)
{
	UInhHudScreen* Hud = Cast<UInhHudScreen>(MainScreen);
	if (!Hud)
	{
		return;
	}
	if (Kind == EInhPropKind::HiddenItem)
	{
		Investigated.Add(PropKey(Kind, RefId));
		Toast(LOCTEXT("FoundHidden", "A floorboard is loose. You pry it up and find something hidden underneath."));
		if (Room)
		{
			Room->RevealHiddenItem(RefId);
		}
		RefreshRoom();
		Kind = EInhPropKind::Item;
	}

	ClearSelection();
	const FString Key = PropKey(Kind, RefId);
	Investigated.Add(Key);
	for (TActorIterator<AInhProp> It(GetWorld()); It; ++It)
	{
		if (PropKey(It->GetKind(), It->GetRefId()) == Key)
		{
			It->MarkInvestigated();
			It->SetSelected(true);
			SelectedProp = *It;
			if (AInhCameraPawn* Cam = CameraPawn())
			{
				Cam->LookAt(It->GetActorLocation());
			}
			break;
		}
	}
	Hud->ShowPropCard(Kind, RefId);
}

void AInhPlayerController::ClearSelection()
{
	if (SelectedProp.IsValid())
	{
		SelectedProp->SetSelected(false);
	}
	SelectedProp = nullptr;
}

// --- touch --------------------------------------------------------------------------------------

void AInhPlayerController::OnTouchPressed(ETouchIndex::Type Finger, FVector Location)
{
	const FVector2D Pos(Location.X, Location.Y);
	Touches.Add(Finger, Pos);
	if (Touches.Num() == 1)
	{
		PressPosition = Pos;
		PressTime = FPlatformTime::Seconds();
		bDragged = false;
	}
	else if (Touches.Num() == 2)
	{
		TArray<FVector2D> Points;
		Touches.GenerateValueArray(Points);
		PinchDistance = FVector2D::Distance(Points[0], Points[1]);
		bDragged = true;
	}
}

void AInhPlayerController::OnTouchMoved(ETouchIndex::Type Finger, FVector Location)
{
	FVector2D* Previous = Touches.Find(Finger);
	if (!Previous)
	{
		return;
	}
	const FVector2D Pos(Location.X, Location.Y);
	const FVector2D Delta = Pos - *Previous;
	*Previous = Pos;

	AInhCameraPawn* Cam = CameraPawn();
	if (!Cam)
	{
		return;
	}
	if (Touches.Num() >= 2)
	{
		TArray<FVector2D> Points;
		Touches.GenerateValueArray(Points);
		const float Distance = FVector2D::Distance(Points[0], Points[1]);
		if (PinchDistance > 1.f && Distance > 1.f)
		{
			Cam->Zoom(PinchDistance / Distance);
		}
		PinchDistance = Distance;
		return;
	}
	if (FVector2D::Distance(Pos, PressPosition) > TapMaxPixels)
	{
		bDragged = true;
	}
	if (bDragged)
	{
		Cam->Orbit(Delta);
	}
}

void AInhPlayerController::OnTouchReleased(ETouchIndex::Type Finger, FVector Location)
{
	const bool bWasSingle = Touches.Num() == 1;
	Touches.Remove(Finger);
	if (bWasSingle && !bDragged && FPlatformTime::Seconds() - PressTime < TapMaxSeconds)
	{
		HandleTap(FVector2D(Location.X, Location.Y));
	}
}

void AInhPlayerController::OnZoomIn()
{
	if (AInhCameraPawn* Cam = CameraPawn()) Cam->Zoom(0.9f);
}

void AInhPlayerController::OnZoomOut()
{
	if (AInhCameraPawn* Cam = CameraPawn()) Cam->Zoom(1.1f);
}

void AInhPlayerController::HandleTap(const FVector2D& ScreenPosition)
{
	if (!bInRoom || Panel)
	{
		return;
	}
	FHitResult Hit;
	if (GetHitResultAtScreenPosition(ScreenPosition, ECC_Visibility, false, Hit))
	{
		if (AInhProp* Prop = Cast<AInhProp>(Hit.GetActor()))
		{
			SelectProp(Prop->GetKind(), Prop->GetRefId());
			return;
		}
	}
	// Tapping empty space puts the camera back and closes the prop card.
	ClearSelection();
	if (UInhHudScreen* Hud = Cast<UInhHudScreen>(MainScreen))
	{
		Hud->HidePropCard();
	}
	if (AInhCameraPawn* Cam = CameraPawn())
	{
		Cam->ResetView();
	}
}

#undef LOCTEXT_NAMESPACE
