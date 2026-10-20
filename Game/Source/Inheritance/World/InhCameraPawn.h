#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "World/InhCinematics.h"
#include "InhCameraPawn.generated.h"

class UCineCameraComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnInhShotStarted, int32 /*Index*/);

/** What the camera shows in one frame, before handheld micro-motion is added. */
struct FInhCameraView
{
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	float FocalLength = 40.f;
	float Aperture = 2.f;
	/** Along the view axis, in cm. */
	float FocusDistance = 1000.f;
	float Handheld = 0.f;
};

/**
 * The game's only camera: a cine camera on a Super 35 filmback, so lenses, apertures and depth of field behave like
 * a real film camera. Normally it orbits the diorama room (drag to turn, pinch or scroll to zoom); it can also fly
 * authored shots (InhCinematics) and then blend back into the orbit.
 */
UCLASS()
class INHERITANCE_API AInhCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	AInhCameraPawn();

	// --- orbit (Orbit and Zoom are ignored while shots play) ---
	void Orbit(const FVector2D& DeltaPixels);
	/** Factor > 1 moves away, < 1 moves closer. */
	void Zoom(float Factor);
	/** Smoothly turns towards a world location, e.g. the prop the player just tapped, and racks focus onto it. */
	void LookAt(const FVector& WorldLocation);
	void ResetView();

	// --- authored shots ---
	/**
	 * Flies Shots in order, looping if bLoop. OnFinished runs when the last shot ends (never when looping or stopped
	 * early), as the camera starts its ~1.5 s blend back into the orbit. A first shot that starts in or beside the room
	 * blends in from the current view; anything else starts with a cut. The orbit rig is quietly reset to its home
	 * pose meanwhile, so every cinematic returns to the same framing.
	 */
	void PlayShots(const TArray<FInhShot>& Shots, TFunction<void()> OnFinished, bool bLoop = false);
	/** Ends playback without calling OnFinished: blends back into the orbit from wherever the camera is, or cuts to it. */
	void StopShots(bool bBlendBack = true);
	bool IsPlayingShots() const { return bPlayingShots; }
	/** INDEX_NONE when no shots are playing. */
	int32 GetCurrentShotIndex() const { return bPlayingShots ? ShotIndex : INDEX_NONE; }

	/** Fired from Tick as each shot begins; index 0 arrives on the tick after PlayShots, so listeners can set up first. */
	FOnInhShotStarted OnShotStarted;

	virtual void Tick(float DeltaSeconds) override;

private:
	void TickOrbitRig(float DeltaSeconds);
	void ResetOrbitRig();
	void AdvanceShots(float DeltaSeconds);
	void FinishShots();
	void BeginBlendBack(const FInhCameraView& From);
	FInhCameraView OrbitView() const;
	FInhCameraView ShotView() const;
	FInhCameraView BlendBackView(float Alpha) const;
	void ApplyView(const FInhCameraView& View, float DeltaSeconds);
	void UpdateMobileDepthOfField();
	float OrbitDistanceScale() const;

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<UCineCameraComponent> Camera;

	// Orbit rig: current values ease towards their targets.
	FVector Focus = FVector::ZeroVector;
	FVector TargetFocus = FVector::ZeroVector;
	float Yaw = InhCinematics::HomeYaw;
	float Pitch = InhCinematics::HomePitch;
	float ArmLength = InhCinematics::HomeArmLength;
	float TargetYaw = Yaw;
	float TargetPitch = Pitch;
	float TargetArmLength = ArmLength;
	/** The tapped prop focus is racked to; otherwise the orbit focus. */
	FVector LookTarget = FVector::ZeroVector;
	bool bHasLookTarget = false;

	// Shot playback.
	TArray<FInhShot> Shots;
	TFunction<void()> OnShotsFinished;
	int32 ShotIndex = 0;
	float ShotTime = 0.f;
	bool bPlayingShots = false;
	bool bLoopShots = false;
	bool bPendingFirstShotEvent = false;
	/** Bumped whenever playback starts or stops, so callbacks that restart or stop it are noticed mid-update. */
	uint32 PlaybackSerial = 0;

	// Blends between the orbit and shots.
	FInhCameraView EntryFrom;
	float EntryBlendTime = 0.f;
	float EntryBlendDuration = 0.f;
	FInhCameraView BlendFrom;
	float BlendBackTime = 0.f;
	float BlendBackDuration = 0.f;
	bool bBlendingBack = false;
	/** Returning from outside: rise over the rooftops instead of cutting through walls. */
	bool bBlendOverRoofs = false;

	// What was last shown, and the smoothed lens state.
	FInhCameraView LastView;
	bool bHasView = false;
	bool bSnapNext = true;
	float FocusDistance = 1000.f;
	float FocusVelocity = 0.f;
	float Aperture = InhCinematics::OrbitAperture;
	float ApertureVelocity = 0.f;
	float Handheld = 0.f;
	/** Seconds since spawn, in double so the handheld noise stays smooth however long the game runs. */
	double Time = 0.0;
};
