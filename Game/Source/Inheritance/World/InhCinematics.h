#pragma once

#include "CoreMinimal.h"
#include "InhCinematics.generated.h"

/** How a shot travels from its From values to its To values. */
UENUM()
enum class EInhEase : uint8 { Linear, EaseInOut, EaseOut, EaseIn };

/**
 * One camera move: the camera glides from CameraFrom to CameraTo while aiming from LookFrom to LookTo, and the lens
 * zooms from FocalFrom to FocalTo, all on the same easing curve. Focus follows the look point.
 * A shot that starts exactly where the previous one ended continues the move; anything else is a cut.
 * EaseIn followed by a collinear EaseOut flows through at matching speed when both cover the same cm per second.
 */
USTRUCT()
struct FInhShot
{
	GENERATED_BODY()

	UPROPERTY() FVector CameraFrom = FVector::ZeroVector;  // world positions
	UPROPERTY() FVector CameraTo = FVector::ZeroVector;
	UPROPERTY() FVector LookFrom = FVector::ZeroVector;    // world points the camera aims at
	UPROPERTY() FVector LookTo = FVector::ZeroVector;
	UPROPERTY() float FocalFrom = 35.f;                    // mm, on a Super 35 filmback
	UPROPERTY() float FocalTo = 35.f;
	UPROPERTY() float Aperture = 2.8f;                     // f-stop; focus tracks the look point
	UPROPERTY() float Duration = 4.f;
	UPROPERTY() EInhEase Ease = EInhEase::EaseInOut;
	UPROPERTY() float Handheld = 0.15f;                    // 0..1 amount of handheld micro-motion
	UPROPERTY() FText Caption;                             // optional title card during this shot
	UPROPERTY() FText Subcaption;
};

UENUM()
enum class EInhCinematic : uint8 { TitleAttract, Opening, WillReveal, Sunrise };

namespace InhCinematics
{
	/** The orbit the room camera rests in between cinematics. The Opening lands exactly on it. */
	inline constexpr float HomeYaw = -40.f;
	inline constexpr float HomePitch = -38.f;
	inline constexpr float HomeArmLength = 1150.f;
	/** A touch longer than a normal lens: calmer perspective and softer background for the diorama. */
	inline constexpr float OrbitFocalLength = 40.f;
	inline constexpr float OrbitAperture = 2.f;

	/**
	 * Seconds into the Sunrise cinematic when the camera looks at InhLayout::FerrymanPassing. The camera follows a
	 * boat that leaves FerrymanStart as the cinematic starts and reaches FerrymanPassing at this moment, so start
	 * the boat together with the cinematic and time it to arrive then.
	 */
	inline constexpr float SunriseFerrymanPassTime = 10.5f;

	/** Shots for each cinematic, built from InhLayout. TitleAttract is meant to loop. */
	TArray<FInhShot> Build(EInhCinematic Which);

	/** Sum of the shot durations, in seconds. */
	float TotalDuration(const TArray<FInhShot>& Shots);

	/** Maps a linear 0..1 progress through Kind's curve. */
	float Ease(EInhEase Kind, float Alpha);

	/** Camera position, aim point and focal length Time seconds into Shot. */
	void Evaluate(const FInhShot& Shot, float Time, FVector& OutCamera, FVector& OutLook, float& OutFocal);

	/** World location of an orbit camera at Yaw/Pitch (degrees) and ArmLength from Focus, looking at Focus. */
	FVector OrbitLocation(const FVector& Focus, float Yaw, float Pitch, float ArmLength);

	/** How much farther than its arm length the orbit camera sits on the current screen: 1 in landscape, more upright. */
	float OrbitDistanceScale();
}
