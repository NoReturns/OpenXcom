#pragma once
/*
 * Open X-Com Companion — pure facing/status helpers testable without full BattleUnit linkage (M1-WP2).
 */
#include "../Savegame/BattleUnit.h"
#include "../Mod/MapData.h"

namespace OpenXcom
{

class CompanionSnapshotUnitStateCore
{
public:
	static void directionToMapVector(int direction, int *outDx, int *outDy);
	static const char *directionLabel(int direction);
	static const char *unitStatusId(UnitStatus status);
	static const char *movementTypeId(MovementType movementType);
};

} // namespace OpenXcom
