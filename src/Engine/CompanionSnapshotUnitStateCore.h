#pragma once
/*
 * Open X-Com Companion — pure facing/status helpers testable without full BattleUnit linkage (M1-WP2).
 */
#include "../Savegame/BattleUnit.h"
#include "../Mod/MapData.h"
#include <string>

namespace OpenXcom
{

struct UnitObservationPolicyInput
{
	bool unitVisible;
	bool tileCurrentlySeen;
	UnitFaction originalFaction;
	UnitFaction faction;
	bool rosterSoldierIdentity;
	std::string engineUnitType;
};

class CompanionSnapshotUnitStateCore
{
public:
	static void directionToMapVector(int direction, int *outDx, int *outDy);
	static const char *directionLabel(int direction);
	static const char *unitStatusId(UnitStatus status);
	static const char *movementTypeId(MovementType movementType);

	static bool isMindControlledRosterObservation(const UnitObservationPolicyInput &input);
	/// Mod unit type (e.g. STR_SECTOID_SOLDIER) when spot + current friendly LOS on tile — not race alone.
	static bool shouldExportHostileObservedUnitType(const UnitObservationPolicyInput &input);
	/// Roster soldier name when MC — requires current LOS; not friendly tactical detail export.
	static bool shouldExportMindControlledRosterName(const UnitObservationPolicyInput &input);
};

} // namespace OpenXcom
