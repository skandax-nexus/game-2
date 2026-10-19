#pragma once

#include "CoreMinimal.h"

/**
 * One place for the geography of Hollow Bay, shared by the room, the harbour, the cinematic
 * camera and the effects. Units are centimetres; +X points out of the window towards the sea.
 *
 *   Room (diorama, upstairs above the fish market): floor X -400..400, Y -300..300, Z 0..300.
 *   Back wall at X = 400 with the window opening Y -60..100, Z 110..230.
 *   Left wall at Y = -300. The front (-X) and right (+Y) sides are open to the camera.
 *   Below the room is the market building down to street level, then a cobbled quay street,
 *   the quay edge, and the sea. The lighthouse stands on a breakwater across the water;
 *   the rest of the town climbs the hills on the far shore.
 */
namespace InhLayout
{
	// --- room ---
	inline const FVector RoomHalfExtent(400.f, 300.f, 150.f);
	inline const FVector RoomCameraFocus(20.f, -40.f, 70.f);
	inline const FVector WindowCenter(400.f, 20.f, 170.f);
	inline constexpr float WindowMinY = -60.f;
	inline constexpr float WindowMaxY = 100.f;
	inline constexpr float WindowMinZ = 110.f;
	inline constexpr float WindowMaxZ = 230.f;
	/** Just inside the window, looking out: where the camera stands in the sunrise shot. */
	inline const FVector WindowViewpoint(250.f, 20.f, 165.f);

	// --- the street and quay below ---
	inline constexpr float StreetZ = -420.f;
	inline constexpr float SeaLevel = -540.f;
	/** Our building's outer facade (the room's back wall continues down to the street). */
	inline constexpr float FacadeX = 410.f;
	/** The quay edge runs along Y at this X; beyond it is open water. */
	inline constexpr float QuayEdgeX = 900.f;
	/** Neighbouring buildings line the street along Y on both sides of ours. */
	inline constexpr float StreetMinY = -2600.f;
	inline constexpr float StreetMaxY = 2600.f;
	/** A wooden pier runs out from the quay. */
	inline const FVector PierStart(900.f, 420.f, -430.f);
	inline const FVector PierEnd(1700.f, 420.f, -430.f);
	/** Stone steps down to the water, right below our window. Mara appears here at dawn. */
	inline const FVector WaterSteps(900.f, -150.f, SeaLevel);
	inline const FVector MaraPoint(940.f, -150.f, SeaLevel + 8.f);

	// --- across the water ---
	inline const FVector LighthouseBase(2600.f, 950.f, SeaLevel);
	inline constexpr float LighthouseHeight = 1500.f;
	inline const FVector LighthouseLamp(2600.f, 950.f, SeaLevel + 1450.f);
	/** The far shore where the rest of Hollow Bay climbs the hills. */
	inline constexpr float FarShoreX = 4200.f;
	inline constexpr float FarShoreMinY = -5000.f;
	inline constexpr float FarShoreMaxY = 5000.f;

	// --- the Ferryman's route at sunrise: in from the south, past the steps, out to sea ---
	inline const FVector FerrymanStart(1500.f, -2400.f, SeaLevel);
	inline const FVector FerrymanPassing(1120.f, -150.f, SeaLevel);
	inline const FVector FerrymanEnd(3600.f, 2200.f, SeaLevel);

	// --- sky ---
	/** Yaw (degrees) the dawn sun rises from: it shines in through the window. */
	inline constexpr float SunriseYaw = 195.f;
}
