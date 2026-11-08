/*
 * Open X-Com Companion — pure facing/status helpers (M1-WP2).
 */
#include "CompanionSnapshotUnitStateCore.h"
#include <cstring>

namespace OpenXcom
{

namespace
{

bool isSoldierEngineType(const std::string &engineUnitType)
{
	return engineUnitType == "SOLDIER";
}

} // namespace

bool CompanionSnapshotUnitStateCore::isMindControlledRosterObservation(const UnitObservationPolicyInput &input)
{
	return input.rosterSoldierIdentity
	    && input.originalFaction == FACTION_PLAYER
	    && input.faction != FACTION_PLAYER;
}

bool CompanionSnapshotUnitStateCore::shouldExportHostileObservedUnitType(const UnitObservationPolicyInput &input)
{
	if (!input.unitVisible)
	{
		return false;
	}
	if (input.engineUnitType.empty() || isSoldierEngineType(input.engineUnitType))
	{
		return false;
	}
	if (isMindControlledRosterObservation(input))
	{
		return false;
	}
	if (input.originalFaction == FACTION_NEUTRAL && input.faction == FACTION_NEUTRAL)
	{
		return false;
	}
	return input.originalFaction == FACTION_HOSTILE;
}

bool CompanionSnapshotUnitStateCore::shouldExportMindControlledRosterName(const UnitObservationPolicyInput &input)
{
	return isMindControlledRosterObservation(input) && input.unitVisible;
}

void CompanionSnapshotUnitStateCore::directionToMapVector(int direction, int *outDx, int *outDy)
{
	static const int dx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
	static const int dy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
	if (outDx != 0)
	{
		*outDx = (direction >= 0 && direction < 8) ? dx[direction] : 0;
	}
	if (outDy != 0)
	{
		*outDy = (direction >= 0 && direction < 8) ? dy[direction] : 0;
	}
}

const char *CompanionSnapshotUnitStateCore::directionLabel(int direction)
{
	switch (direction)
	{
	case 0: return "north";
	case 1: return "northeast";
	case 2: return "east";
	case 3: return "southeast";
	case 4: return "south";
	case 5: return "southwest";
	case 6: return "west";
	case 7: return "northwest";
	default: return 0;
	}
}

const char *CompanionSnapshotUnitStateCore::unitStatusId(UnitStatus status)
{
	switch (status)
	{
	case STATUS_STANDING: return "standing";
	case STATUS_WALKING: return "walking";
	case STATUS_FLYING: return "flying";
	case STATUS_TURNING: return "turning";
	case STATUS_AIMING: return "aiming";
	case STATUS_COLLAPSING: return "collapsing";
	case STATUS_DEAD: return "dead";
	case STATUS_UNCONSCIOUS: return "unconscious";
	case STATUS_PANICKING: return "panicking";
	case STATUS_BERSERK: return "berserk";
	case STATUS_IGNORE_ME: return "ignore";
	default: return "unknown";
	}
}

const char *CompanionSnapshotUnitStateCore::movementTypeId(MovementType movementType)
{
	switch (movementType)
	{
	case MT_WALK: return "walk";
	case MT_FLY: return "fly";
	case MT_SLIDE: return "slide";
	case MT_FLOAT: return "float";
	case MT_SINK: return "sink";
	default: return "unknown";
	}
}

} // namespace OpenXcom
