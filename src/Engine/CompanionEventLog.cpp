/*
 * Open X-Com Companion — weapon fire recording (game binary only).
 */
#include "CompanionEventLog.h"
#include "CompanionEventLogTestSupport.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"

namespace OpenXcom
{

namespace
{

bool shouldRecordDeliberatePlayerWeaponFire(SavedBattleGame *battle, const BattleAction &action, BattleUnit *actor)
{
	if (battle == 0 || actor == 0 || action.weapon == 0)
	{
		return false;
	}
	CompanionEventLogTestSupport::EligibilityInput input;
	input.debugMode = battle->getDebugMode();
	input.battleSide = battle->getSide();
	input.actionType = action.type;
	input.playerOrdered = action.playerOrdered;
	input.actorFaction = actor->getFaction();
	input.actorOriginalFaction = actor->getOriginalFaction();
	input.actorHasGeoscapeSoldier = actor->getGeoscapeSoldier() != 0;
	return CompanionEventLogTestSupport::shouldRecordDeliberatePlayerWeaponFire(input);
}

} // namespace

void CompanionEventLog::tryRecordWeaponFired(SavedBattleGame *battle, const BattleAction &action, BattleUnit *actor)
{
	if (!shouldRecordDeliberatePlayerWeaponFire(battle, action, actor))
	{
		return;
	}

	if (_events.size() >= MAX_RETAINED)
	{
		_events.erase(_events.begin());
		++_droppedCount;
	}

	CompanionWeaponFireEvent entry;
	entry.seq = _nextSeq++;
	entry.turn = battle->getTurn();
	entry.actorUnitId = actor->getId();
	entry.weaponItemType = action.weapon->getRules()->getType();
	entry.actionType = action.type;
	entry.target = action.target;
	entry.autoShotIndex = action.type == BA_AUTOSHOT ? action.autoShotCounter : 0;
	_events.push_back(entry);
}

}
