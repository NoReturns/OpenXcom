/*
 * Open X-Com Companion — friendly unit facing, condition, and readiness for tactical snapshots (M1-WP2).
 */
#include "CompanionSnapshotUnitState.h"
#include "CompanionSnapshotUnitStateCore.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/SavedGame.h"
#include "../Mod/MapData.h"

namespace OpenXcom
{

void CompanionSnapshotUnitState::directionToMapVector(int direction, int *outDx, int *outDy)
{
	CompanionSnapshotUnitStateCore::directionToMapVector(direction, outDx, outDy);
}

const char *CompanionSnapshotUnitState::directionLabel(int direction)
{
	return CompanionSnapshotUnitStateCore::directionLabel(direction);
}

const char *CompanionSnapshotUnitState::unitStatusId(UnitStatus status)
{
	return CompanionSnapshotUnitStateCore::unitStatusId(status);
}

const char *CompanionSnapshotUnitState::movementTypeId(MovementType movementType)
{
	return CompanionSnapshotUnitStateCore::movementTypeId(movementType);
}

bool CompanionSnapshotUnitState::isLiveRosterFriendlyUnit(const BattleUnit *unit, const SavedGame *campaign)
{
	if (unit == 0 || campaign == 0)
	{
		return false;
	}
	if (unit->getStatus() == STATUS_IGNORE_ME)
	{
		return false;
	}
	if (unit->getOriginalFaction() != FACTION_PLAYER || unit->getFaction() != FACTION_PLAYER)
	{
		return false;
	}
	Soldier *soldier = unit->getGeoscapeSoldier();
	if (soldier == 0 || soldier->getId() != unit->getId())
	{
		return false;
	}
	if (unit->getId() <= 0 || unit->getId() >= BattleUnit::MAX_SOLDIER_ID)
	{
		return false;
	}
	return campaign->isLiveRosterSoldier(unit->getId());
}

namespace
{

bool isLiveRosterSoldierUnitIdentity(const BattleUnit *unit, const SavedGame *campaign)
{
	if (unit == 0 || campaign == 0)
	{
		return false;
	}
	if (unit->getOriginalFaction() != FACTION_PLAYER)
	{
		return false;
	}
	Soldier *soldier = unit->getGeoscapeSoldier();
	if (soldier == 0 || soldier->getId() != unit->getId())
	{
		return false;
	}
	if (unit->getId() <= 0 || unit->getId() >= BattleUnit::MAX_SOLDIER_ID)
	{
		return false;
	}
	return campaign->isLiveRosterSoldier(unit->getId());
}

bool shouldIncludeLiveRosterSoldierUnit(const BattleUnit *unit, const SavedGame *campaign)
{
	if (!isLiveRosterSoldierUnitIdentity(unit, campaign))
	{
		return false;
	}
	if (unit->getOriginalFaction() == FACTION_PLAYER && unit->getFaction() == FACTION_PLAYER)
	{
		return true;
	}
	if (unit->getVisible())
	{
		return true;
	}
	switch (unit->getStatus())
	{
	case STATUS_DEAD:
	case STATUS_UNCONSCIOUS:
	case STATUS_IGNORE_ME:
		return true;
	default:
		return false;
	}
}

} // namespace

bool CompanionSnapshotUnitState::shouldIncludeUnitInSnapshot(const BattleUnit *unit, const SavedGame *campaign)
{
	if (unit == 0)
	{
		return false;
	}
	if (shouldIncludeLiveRosterSoldierUnit(unit, campaign))
	{
		return true;
	}
	if (unit->isOut())
	{
		return false;
	}
	if (unit->getFaction() == FACTION_PLAYER)
	{
		return true;
	}
	return unit->getVisible();
}

bool CompanionSnapshotUnitState::shouldExportFacing(const BattleUnit *unit)
{
	if (unit == 0)
	{
		return false;
	}
	const UnitStatus status = unit->getStatus();
	if (status == STATUS_COLLAPSING || status == STATUS_DEAD)
	{
		return false;
	}
	const int direction = unit->getDirection();
	return direction >= 0 && direction < 8;
}

void CompanionSnapshotUnitState::appendFacing(YAML::Node &entry, const BattleUnit *unit)
{
	if (!shouldExportFacing(unit))
	{
		return;
	}
	const int direction = unit->getDirection();
	int dx = 0;
	int dy = 0;
	directionToMapVector(direction, &dx, &dy);
	YAML::Node facing;
	facing["direction"] = direction;
	const char *label = directionLabel(direction);
	if (label != 0)
	{
		facing["label"] = label;
	}
	YAML::Node vector;
	vector["dx"] = dx;
	vector["dy"] = dy;
	facing["vector"] = vector;
	if (unit->getStatus() == STATUS_TURNING)
	{
		facing["turning"] = true;
	}
	entry["facing"] = facing;
}

void CompanionSnapshotUnitState::appendFriendlyCondition(YAML::Node &entry, BattleUnit *unit)
{
	if (unit == 0)
	{
		return;
	}
	YAML::Node condition;
	condition["energy"] = unit->getEnergy();
	if (unit->getBaseStats() != 0)
	{
		condition["maxEnergy"] = unit->getBaseStats()->stamina;
	}
	condition["morale"] = unit->getMorale();
	condition["stunLevel"] = unit->getStunlevel();
	condition["alive"] = unit->getStatus() != STATUS_DEAD;
	condition["conscious"] = unit->getStatus() != STATUS_UNCONSCIOUS && unit->getStatus() != STATUS_DEAD;
	condition["engineStatus"] = unitStatusId(unit->getStatus());
	condition["engineStatusId"] = static_cast<int>(unit->getStatus());
	condition["floating"] = unit->isFloating();
	condition["movementTypeId"] = static_cast<int>(unit->getMovementType());
	condition["movementType"] = movementTypeId(unit->getMovementType());
	entry["condition"] = condition;
}

namespace
{

const char *readinessSummaryForUnit(const BattleUnit *unit)
{
	if (unit == 0)
	{
		return "unknown";
	}
	switch (unit->getStatus())
	{
	case STATUS_DEAD:
		return "dead";
	case STATUS_UNCONSCIOUS:
		return "unconscious";
	case STATUS_PANICKING:
		return "panicking";
	case STATUS_BERSERK:
		return "berserk";
	case STATUS_COLLAPSING:
		return "collapsing";
	case STATUS_WALKING:
	case STATUS_FLYING:
	case STATUS_TURNING:
	case STATUS_AIMING:
		return "battle_animation";
	default:
		break;
	}
	if (unit->isOut())
	{
		return "incapacitated";
	}
	return "ready_to_act";
}

} // namespace

void CompanionSnapshotUnitState::appendFriendlyReadiness(YAML::Node &entry, const BattleUnit *unit, const SavedGame *campaign)
{
	if (unit == 0)
	{
		return;
	}
	YAML::Node readiness;
	readiness["summary"] = readinessSummaryForUnit(unit);
	readiness["engineStatus"] = unitStatusId(unit->getStatus());
	readiness["liveRosterSoldier"] = isLiveRosterFriendlyUnit(unit, campaign);
	readiness["engineActingUnitAvailable"] =
		unit->getFaction() == FACTION_PLAYER
		&& unit->getOriginalFaction() == FACTION_PLAYER
		&& unit->getGeoscapeSoldier() != 0
		&& !unit->isOut();
	readiness["observationNote"] =
		"Observational only: not Companion command authorization. Ownership, AI/human assignment, "
		"session/turn/binding freshness, battle-busy state, and per-command executor checks remain authoritative. "
		"Per-action TU affordability remains in actionCosts.";
	entry["readiness"] = readiness;
}

void CompanionSnapshotUnitState::appendVisibleHostileObservations(YAML::Node &entry, const BattleUnit *unit)
{
	if (unit == 0)
	{
		return;
	}
	if (unit->getOriginalFaction() == FACTION_PLAYER && unit->getFaction() == FACTION_PLAYER)
	{
		return;
	}
	if (!unit->getVisible())
	{
		return;
	}
	entry["kneeled"] = unit->isKneeled();
	appendFacing(entry, unit);
}

} // namespace OpenXcom
