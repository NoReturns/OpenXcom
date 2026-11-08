/*
 * Open X-Com Companion — roster casualty settlement detection for terminal snapshot export.
 */
#include "CompanionCommandCasualtySettlement.h"
#include "CompanionPerceptionLogCore.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/UnitDieBState.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/Soldier.h"

namespace OpenXcom
{

namespace CompanionCommandCasualtySettlement
{

namespace
{

bool isRosterCasualtyBattleUnit(const BattleUnit *unit)
{
	if (unit == 0)
	{
		return false;
	}
	Soldier *soldier = unit->getGeoscapeSoldier();
	CompanionPerceptionLogCore::RosterCasualtySubjectInput input = {};
	input.unitId = unit->getId();
	input.hasGeoscapeSoldierLink = soldier != 0;
	input.geoscapeSoldierId = soldier != 0 ? soldier->getId() : -1;
	input.originalFaction = static_cast<int>(unit->getOriginalFaction());
	return CompanionPerceptionLogCore::isOriginalXcomRosterCasualtySubject(input);
}

} // namespace

bool isRosterCasualtySnapshotWorkUnsettled(BattlescapeGame *game, SavedBattleGame *battle)
{
	if (battle == 0)
	{
		return false;
	}
	if (game != 0)
	{
		const std::list<BattleState*> &states = game->getStates();
		for (std::list<BattleState*>::const_iterator i = states.begin(); i != states.end(); ++i)
		{
			BattleState *state = *i;
			if (state == 0)
			{
				continue;
			}
			UnitDieBState *die = dynamic_cast<UnitDieBState*>(state);
			if (die != 0 && isRosterCasualtyBattleUnit(die->getDyingUnit()))
			{
				return true;
			}
		}
	}
	std::vector<BattleUnit*> *units = battle->getUnits();
	if (units == 0)
	{
		return false;
	}
	for (std::vector<BattleUnit*>::const_iterator i = units->begin(); i != units->end(); ++i)
	{
		BattleUnit *unit = *i;
		if (unit != 0 && isRosterCasualtyBattleUnit(unit) && unit->getStatus() == STATUS_COLLAPSING)
		{
			return true;
		}
	}
	return false;
}

} // namespace CompanionCommandCasualtySettlement

} // namespace OpenXcom
