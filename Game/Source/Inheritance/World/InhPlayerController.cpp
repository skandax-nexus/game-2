#include "World/InhPlayerController.h"
#include "World/InhAtmosphereFX.h"
#include "World/InhCameraPawn.h"
#include "World/InhHarbourBuilder.h"
#include "World/InhRoomBuilder.h"
#include "Audio/InhAmbience.h"
#include "Core/InhGameSubsystem.h"
#include "UI/InhCinemaOverlay.h"
#include "UI/InhScreen.h"
#include "UI/InhTitleScreen.h"
#include "UI/InhWillScreen.h"
#include "UI/InhHudScreen.h"
#include "UI/InhSunriseScreen.h"
#include "Components/InputComponent.h"
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

	/** The world's clock on the title screen: deep night, lamps lit, stars out. */
	constexpr float TitleProgress = 0.3f;
	/** A cinematic that opens on a cut fades to black first, over this long. */
	constexpr float CutFadeSeconds = 0.5f;
	/** A skip fades out over this long before the camera cuts away. */
	constexpr float SkipFadeSeconds = 0.3f;
	/** The Sunrise cinematic brings the dawn up over this long, ending on full daybreak. */
	constexpr float DawnRiseSeconds = 11.f;
	/** Where the dawn rise starts at the latest, so even a life skipped to sunrise sees the sun come up. */
	constexpr float DawnRiseFrom = 0.86f;
	/** The Sunrise ends in a fade to this warm white; the sunrise screen fades it away onto the dawn. */
	const FLinearColor DawnWhite(1.f, 0.95f, 0.88f);

	/**
	 * Hollow Bay's weather. The server has none, so each life's id decides it: one night in three brings a shower
	 * somewhere in the small hours, always gone again before the dawn.
	 */
	float RainFor(const FInhLife& Life, float Progress)
	{
		if (!Life.IsValid())
		{
			return 0.f;
		}
		const uint32 Hash = GetTypeHash(Life.Id);
		if (Hash % 3 != 0)
		{
			return 0.f;
		}
		const float Start = 0.15f + float((Hash >> 4) % 40) / 100.f;
		const float Length = 0.2f + float((Hash >> 10) % 25) / 100.f;
		const float Strength = 0.35f + float((Hash >> 16) % 60) / 100.f;
		const float In = FMath::SmoothStep(Start, Start + 0.05f, Progress);
		const float Out = 1.f - FMath::SmoothStep(Start + Length, Start + Length + 0.05f, Progress);
		const float BeforeDawn = 1.f - FMath::SmoothStep(0.85f, 0.9f, Progress);
		return Strength * In * Out * BeforeDawn;
	}
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

	FindWorldActors();

	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	SetInputMode(Mode);

	ToastWidget = CreateWidget<UInhToast>(this, UInhToast::StaticClass());
	ToastWidget->AddToViewport(100);

	// Above the screens and panels, below the toasts; it never takes a touch.
	Cinema = CreateWidget<UInhCinemaOverlay>(this, UInhCinemaOverlay::StaticClass());
	Cinema->AddToViewport(50);

	Ambience = NewObject<UInhAmbience>(this);

	if (UInhGameSubsystem* G = Game())
	{
		LifeChangedHandle = G->OnLifeChanged.AddUObject(this, &AInhPlayerController::OnLifeChanged);
		ToastHandle = G->OnToast.AddUObject(this, &AInhPlayerController::Toast);
	}
	GetWorldTimerManager().SetTimer(ClockTimer, this, &AInhPlayerController::TickClock, 1.f, true);
	ShowTitle();
	Ambience->Start(this);
}

void AInhPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UInhGameSubsystem* G = Game())
	{
		G->OnLifeChanged.Remove(LifeChangedHandle);
		G->OnToast.Remove(ToastHandle);
	}
	if (AInhCameraPawn* Cam = ShotListener.Get())
	{
		Cam->OnShotStarted.Remove(ShotStartedHandle);
	}
	ShotListener = nullptr;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}
	if (Ambience)
	{
		Ambience->Stop();
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
	InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &AInhPlayerController::OnSkipKey);
}

void AInhPlayerController::FindWorldActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!Room)
	{
		for (TActorIterator<AInhRoomBuilder> It(World); It; ++It)
		{
			Room = *It;
			break;
		}
	}
	if (!Harbour)
	{
		for (TActorIterator<AInhHarbourBuilder> It(World); It; ++It)
		{
			Harbour = *It;
			break;
		}
	}
	if (!Atmosphere)
	{
		for (TActorIterator<AInhAtmosphereFX> It(World); It; ++It)
		{
			Atmosphere = *It;
			break;
		}
	}
}

void AInhPlayerController::SetWorldProgress(float Progress)
{
	WorldProgress = FMath::Clamp(Progress, 0.f, 1.f);
	if (!Room || !Atmosphere)
	{
		FindWorldActors();
	}
	// The room forwards the clock to the harbour and the air.
	if (Room)
	{
		Room->SetLifeProgress(WorldProgress);
	}
	const UInhGameSubsystem* G = Game();
	const float Rain = G ? RainFor(G->GetLife(), WorldProgress) : 0.f;
	if (Atmosphere)
	{
		Atmosphere->SetRain(Rain);
	}
	if (Ambience)
	{
		Ambience->SetLifeProgress(WorldProgress);
		Ambience->SetRain(Rain);
	}
}

// --- flow ---------------------------------------------------------------------------------------

void AInhPlayerController::ShowTitle()
{
	bInRoom = false;
	ShowMain<UInhTitleScreen>();
	SetWorldProgress(TitleProgress);
	StartTitleAttract();
}

void AInhPlayerController::StartTitleAttract()
{
	if (!Cast<UInhTitleScreen>(MainScreen) || (bCinematicPlaying && CurrentCinematic == EInhCinematic::TitleAttract))
	{
		return;
	}
	if (!CameraPawn())
	{
		// The camera is spawned with the player; try again shortly rather than start the title without its backdrop.
		GetWorldTimerManager().SetTimer(PendingTimer, this, &AInhPlayerController::StartTitleAttract, 0.25f, false);
		return;
	}
	PlayCinematic(EInhCinematic::TitleAttract, nullptr);
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
	if (!G->GetLife().IsAlive())
	{
		StartNewLife();
		return;
	}
	// Back to a life in progress: fly in over the bay, then pick up where it was left.
	const bool bReadWill = G->HasReadWillForCurrentLife();
	TWeakObjectPtr<AInhPlayerController> Weak(this);
	PlayCinematic(EInhCinematic::Opening, [Weak, bReadWill]()
	{
		if (!Weak.IsValid()) return;
		if (bReadWill)
		{
			Weak->EnterRoom();
		}
		else
		{
			Weak->ShowWillReading();
		}
	});
}

void AInhPlayerController::StartNewLife()
{
	TWeakObjectPtr<AInhPlayerController> Weak(this);
	Game()->InheritNewLife([Weak](bool bOk, const FText& Error)
	{
		if (!Weak.IsValid()) return;
		if (!bOk)
		{
			if (UInhTitleScreen* Title = Cast<UInhTitleScreen>(Weak->MainScreen))
			{
				Title->ShowError(Error);
			}
			else
			{
				Weak->Toast(Error);
			}
			return;
		}
		// A new life begins at dusk: over the bay, down to the quay and in through the window, then the will.
		TWeakObjectPtr<AInhPlayerController> Inner = Weak;
		Weak->PlayCinematic(EInhCinematic::Opening, [Inner]()
		{
			if (Inner.IsValid())
			{
				Inner->ShowWillReading();
			}
		});
	});
}

void AInhPlayerController::ShowWillReading()
{
	StopAttract();
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
	StopAttract();
	// Straight from the will, the camera first drifts round the phone and its voice note.
	const bool bFromWill = Cast<UInhWillScreen>(MainScreen) != nullptr;
	Game()->MarkWillRead();
	RefreshRoom();
	if (bFromWill)
	{
		TWeakObjectPtr<AInhPlayerController> Weak(this);
		PlayCinematic(EInhCinematic::WillReveal, [Weak]()
		{
			if (Weak.IsValid())
			{
				Weak->ShowRoomHud();
			}
		});
		return;
	}
	ShowRoomHud();
}

void AInhPlayerController::ShowRoomHud()
{
	if (!Game()->GetLife().IsValid())
	{
		// The life ended while the camera was busy.
		ShowSunrise();
		return;
	}
	bInRoom = true;
	ShowMain<UInhHudScreen>();
}

void AInhPlayerController::ShowSunrise()
{
	bInRoom = false;
	ClearSelection();
	Game()->StopVoicePlayback();
	ClosePanel();
	// The dawn comes up, the Ferryman passes Mara at the steps, and everything fades to white before the sunrise screen.
	TWeakObjectPtr<AInhPlayerController> Weak(this);
	PlayCinematic(EInhCinematic::Sunrise, [Weak]()
	{
		if (!Weak.IsValid()) return;
		Weak->ShowMain<UInhSunriseScreen>();
		if (Weak->Cinema)
		{
			Weak->Cinema->FadeTo(DawnWhite, 0.f, 2.5f);
		}
	});
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
	// Only redraw the room when it is what the player is looking at. Elsewhere (the title, the sunrise screen, a
	// cinematic) the next screen redraws it, under a fade, so the sky never jumps in plain view.
	if (bInRoom || Cast<UInhWillScreen>(MainScreen))
	{
		RefreshRoom();
	}
}

void AInhPlayerController::RefreshRoom()
{
	if (!Room)
	{
		FindWorldActors();
	}
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
	SetWorldProgress(Life.Progress());

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
	SetWorldProgress(Life.Progress());
	if (UInhHudScreen* Hud = Cast<UInhHudScreen>(MainScreen))
	{
		Hud->UpdateClock();
	}

	// At sunrise, ask the server; it ends the life and OnLifeChanged moves us to the Sunrise cinematic.
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

// --- cinematics ---------------------------------------------------------------------------------

void AInhPlayerController::PlayCinematic(EInhCinematic Which, TFunction<void()> OnDone)
{
	GetWorldTimerManager().ClearTimer(PendingTimer);
	PendingDone = nullptr;

	// The Opening starts on a cut far out over the bay: dip to black first, unless the screen is black already.
	if (Which == EInhCinematic::Opening && Cinema && CameraPawn() && Cinema->GetFadeAlpha() < 0.99f)
	{
		PendingCinematic = Which;
		PendingDone = MoveTemp(OnDone);
		Cinema->HideCaption();
		Cinema->FadeTo(FLinearColor::Black, 1.f, CutFadeSeconds);
		GetWorldTimerManager().SetTimer(PendingTimer, this, &AInhPlayerController::StartPendingCinematic, CutFadeSeconds, false);
		return;
	}
	BeginCinematic(Which, MoveTemp(OnDone));
}

void AInhPlayerController::StartPendingCinematic()
{
	TFunction<void()> Done = MoveTemp(PendingDone);
	PendingDone = nullptr;
	BeginCinematic(PendingCinematic, MoveTemp(Done));
}

void AInhPlayerController::BeginCinematic(EInhCinematic Which, TFunction<void()> OnDone)
{
	AInhCameraPawn* Cam = CameraPawn();
	TArray<FInhShot> Shots = InhCinematics::Build(Which);
	GetWorldTimerManager().ClearTimer(CinematicTimer);
	EndDawn();
	if (!Cam || Shots.Num() == 0)
	{
		// Nothing to fly: put the world where the cinematic would have left it and carry on with the flow.
		if (Cinema)
		{
			Cinema->FadeTo(FLinearColor::Black, 0.f, 0.5f);
		}
		if (Which == EInhCinematic::Opening)
		{
			RefreshRoom();
		}
		else if (Which == EInhCinematic::Sunrise)
		{
			SetWorldProgress(1.f);
		}
		if (OnDone)
		{
			OnDone();
		}
		return;
	}

	const bool bAttract = Which == EInhCinematic::TitleAttract;
	const uint32 Serial = ++CinematicSerial;
	CurrentCinematic = Which;
	CinematicDone = MoveTemp(OnDone);
	bCinematicPlaying = true;
	bCinematicSkippable = !bAttract;
	// A finger already down (orbiting the room as the night runs out) must not skip what is only just starting.
	bSkipArmed = false;

	// Title cards, from the life being lived.
	const FInhLife& Life = Game()->GetLife();
	if (Which == EInhCinematic::Opening)
	{
		Shots[0].Caption = LOCTEXT("CinemaOpeningTitle", "HOLLOW BAY");
		Shots[0].Subcaption = Life.ActName.IsEmpty()
			? FText::Format(LOCTEXT("CinemaOpeningDayOnly", "Day {0}"), FText::AsNumber(Life.SeasonDay))
			: FText::Format(LOCTEXT("CinemaOpeningDay", "Day {0} · {1}"), FText::AsNumber(Life.SeasonDay), FText::FromString(Life.ActName));
	}
	else if (Which == EInhCinematic::WillReveal && Life.IsValid())
	{
		Shots[0].Caption = FText::FromString(Life.Estate.HouseName.ToUpper());
		Shots[0].Subcaption = FText::Format(LOCTEXT("CinemaRevealSunrise", "Sunrise at {0}"), InhFormat::LocalTime(Life.DiesAt));
	}
	CinematicShots = Shots;

	ListenToShots(Cam);
	if (UInhHudScreen* Hud = Cast<UInhHudScreen>(MainScreen))
	{
		Hud->HidePropCard();
	}
	ClearSelection();
	// The attract loop plays behind the title screen; everything else has the screen to itself.
	SetScreensHidden(!bAttract);
	if (Cinema)
	{
		Cinema->HideCaption();
		Cinema->SetLetterbox(!bAttract);
		Cinema->SetSkipHint(!bAttract);
	}
	StageCinematic(Which, InhCinematics::TotalDuration(Shots));

	TWeakObjectPtr<AInhPlayerController> Weak(this);
	Cam->PlayShots(Shots, [Weak, Serial]()
	{
		if (Weak.IsValid() && Weak->CinematicSerial == Serial)
		{
			Weak->OnCinematicShotsDone();
		}
	}, bAttract);
}

void AInhPlayerController::StageCinematic(EInhCinematic Which, float Seconds)
{
	switch (Which)
	{
	case EInhCinematic::TitleAttract:
		// The game opens on black and fades up on the harbour at night.
		if (Cinema)
		{
			Cinema->FadeTo(FLinearColor::Black, 1.f, 0.f);
			Cinema->FadeTo(FLinearColor::Black, 0.f, 2.f);
		}
		break;
	case EInhCinematic::Opening:
		// Under the black: the room, the sky and the town as they are in this life, then fade up on the bay.
		RefreshRoom();
		if (Cinema)
		{
			Cinema->FadeTo(FLinearColor::Black, 1.f, 0.f);
			Cinema->FadeTo(FLinearColor::Black, 0.f, 1.2f);
		}
		break;
	case EInhCinematic::Sunrise:
		FindWorldActors();
		if (Harbour)
		{
			// The camera finds the boat on its way to the steps and follows it past Mara (see SunriseFerrymanPassTime).
			Harbour->PlayFerryman(AInhHarbourBuilder::FerrymanDurationFor(InhCinematics::SunriseFerrymanPassTime));
			Harbour->ShowMara(true);
		}
		// The dawn rises through the cinematic (it also rings the ambience's sunrise bell on the way past 0.9).
		DawnFrom = FMath::Max(WorldProgress, DawnRiseFrom);
		DawnStartTime = GetWorld()->GetTimeSeconds();
		bDawnRising = true;
		SetWorldProgress(DawnFrom);
		GetWorldTimerManager().SetTimer(DawnTimer, this, &AInhPlayerController::TickDawn, 0.05f, true);
		GetWorldTimerManager().SetTimer(CinematicTimer, this, &AInhPlayerController::FadeToDawnWhite, FMath::Max(Seconds - 2.f, 0.1f), false);
		break;
	default:
		break;
	}
}

void AInhPlayerController::TickDawn()
{
	if (!bDawnRising)
	{
		GetWorldTimerManager().ClearTimer(DawnTimer);
		return;
	}
	const float T = FMath::Clamp(float((GetWorld()->GetTimeSeconds() - DawnStartTime) / DawnRiseSeconds), 0.f, 1.f);
	SetWorldProgress(FMath::Lerp(DawnFrom, 1.f, FMath::SmoothStep(0.f, 1.f, T)));
	if (T >= 1.f)
	{
		EndDawn();
	}
}

void AInhPlayerController::EndDawn()
{
	GetWorldTimerManager().ClearTimer(DawnTimer);
	if (bDawnRising)
	{
		bDawnRising = false;
		SetWorldProgress(1.f);
	}
}

void AInhPlayerController::FadeToDawnWhite()
{
	if (bCinematicPlaying && CurrentCinematic == EInhCinematic::Sunrise && Cinema)
	{
		Cinema->SetSkipHint(false);
		Cinema->FadeTo(DawnWhite, 1.f, 1.6f);
	}
}

void AInhPlayerController::ListenToShots(AInhCameraPawn* Cam)
{
	if (ShotListener.Get() == Cam)
	{
		return;
	}
	if (AInhCameraPawn* Old = ShotListener.Get())
	{
		Old->OnShotStarted.Remove(ShotStartedHandle);
	}
	ShotStartedHandle = Cam->OnShotStarted.AddUObject(this, &AInhPlayerController::OnShotStarted);
	ShotListener = Cam;
}

void AInhPlayerController::OnShotStarted(int32 Index)
{
	if (!bCinematicPlaying || !Cinema || !CinematicShots.IsValidIndex(Index))
	{
		return;
	}
	const FInhShot& Shot = CinematicShots[Index];
	if (!Shot.Caption.IsEmpty() || !Shot.Subcaption.IsEmpty())
	{
		// The will reveal frames the phone at the centre, so its card goes in the lower third, off the subject.
		Cinema->ShowCaption(Shot.Caption, Shot.Subcaption, FMath::Clamp(Shot.Duration - 0.5f, 2.5f, 6.f),
			CurrentCinematic == EInhCinematic::WillReveal);
	}
}

void AInhPlayerController::OnCinematicShotsDone()
{
	if (CurrentCinematic == EInhCinematic::Sunrise)
	{
		// The camera stays on the dawn behind the sunrise screen instead of swooping back over the roofs.
		if (Cinema)
		{
			Cinema->FadeTo(DawnWhite, 1.f, 0.f);
		}
		HoldFinalShot();
		EndDawn();
	}
	FinishCinematic();
}

void AInhPlayerController::SkipCinematic()
{
	if (!bCinematicPlaying || !bCinematicSkippable)
	{
		return;
	}
	bCinematicSkippable = false;
	// From here the skip finishes the cinematic; the camera's own finish is ignored.
	++CinematicSerial;
	GetWorldTimerManager().ClearTimer(CinematicTimer);
	if (Cinema)
	{
		Cinema->SetSkipHint(false);
		Cinema->HideCaption();
		Cinema->FadeTo(CurrentCinematic == EInhCinematic::Sunrise ? DawnWhite : FLinearColor::Black, 1.f, SkipFadeSeconds);
	}
	GetWorldTimerManager().SetTimer(CinematicTimer, this, &AInhPlayerController::FinishSkip, SkipFadeSeconds, false);
}

void AInhPlayerController::FinishSkip()
{
	if (!bCinematicPlaying)
	{
		return;
	}
	if (CurrentCinematic == EInhCinematic::Sunrise)
	{
		// Under the white, jump to the Sunrise's last frame, the dawn on the horizon; the sunrise screen fades the white away.
		HoldFinalShot();
		EndDawn();
	}
	else
	{
		if (AInhCameraPawn* Cam = CameraPawn())
		{
			Cam->StopShots(false);
		}
		if (Cinema)
		{
			Cinema->FadeTo(FLinearColor::Black, 0.f, 0.7f);
		}
	}
	FinishCinematic();
}

void AInhPlayerController::HoldFinalShot()
{
	AInhCameraPawn* Cam = CameraPawn();
	if (!Cam)
	{
		return;
	}
	if (CinematicShots.Num() == 0)
	{
		Cam->StopShots(false);
		return;
	}
	// The screen that follows lets the scene show through, so keep looking at what the cinematic ended on: a still
	// shot that loops seamlessly (it starts where it ends) until the next cinematic cuts away under black.
	FInhShot Hold = CinematicShots.Last();
	Hold.CameraFrom = Hold.CameraTo;
	Hold.LookFrom = Hold.LookTo;
	Hold.FocalFrom = Hold.FocalTo;
	Hold.Duration = 30.f;
	Hold.Caption = FText::GetEmpty();
	Hold.Subcaption = FText::GetEmpty();
	TArray<FInhShot> HoldShots;
	HoldShots.Add(Hold);
	Cam->PlayShots(HoldShots, nullptr, true);
}

void AInhPlayerController::FinishCinematic()
{
	if (!bCinematicPlaying)
	{
		return;
	}
	bCinematicPlaying = false;
	bCinematicSkippable = false;
	++CinematicSerial;
	GetWorldTimerManager().ClearTimer(CinematicTimer);
	if (Cinema)
	{
		Cinema->SetLetterbox(false);
		Cinema->SetSkipHint(false);
	}
	SetScreensHidden(false);
	TFunction<void()> Done = MoveTemp(CinematicDone);
	CinematicDone = nullptr;
	if (Done)
	{
		Done();
	}
}

void AInhPlayerController::StopAttract()
{
	if (!bCinematicPlaying || CurrentCinematic != EInhCinematic::TitleAttract)
	{
		return;
	}
	bCinematicPlaying = false;
	++CinematicSerial;
	if (AInhCameraPawn* Cam = CameraPawn())
	{
		Cam->StopShots(true);
	}
}

void AInhPlayerController::SetScreensHidden(bool bInHidden)
{
	bScreensHidden = bInHidden;
	ApplyScreenVisibility(MainScreen);
	ApplyScreenVisibility(Panel);
}

void AInhPlayerController::ApplyScreenVisibility(UUserWidget* Widget) const
{
	if (Widget)
	{
		Widget->SetVisibility(bScreensHidden ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
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
	if (bCinematicPlaying && bCinematicSkippable)
	{
		bSkipArmed = true;
	}
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
	if (!Cam || bCinematicPlaying)
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
	if (bCinematicPlaying)
	{
		// During a cinematic a touch only ever skips it (the title loop ignores touches), and only a touch that began
		// during it: lifting a finger that was already down when it started never throws the Sunrise away.
		if (Touches.Num() == 0 && bSkipArmed)
		{
			bSkipArmed = false;
			SkipCinematic();
		}
		return;
	}
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

void AInhPlayerController::OnSkipKey()
{
	SkipCinematic();
}

void AInhPlayerController::HandleTap(const FVector2D& ScreenPosition)
{
	if (!bInRoom || Panel || bCinematicPlaying)
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
