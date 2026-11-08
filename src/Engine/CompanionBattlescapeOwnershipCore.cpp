/*
 * Open X-Com Companion — in-battlescape soldier ownership UI/policy (M1-WP7A).
 */
#include "CompanionBattlescapeOwnershipCore.h"
#include "CompanionOwnershipHandoffCore.h"
#include "CompanionCampaignOwnershipCore.h"
#include "CompanionOnDemandRelayCore.h"
#include "CompanionSnapshot.h"
#include "CompanionTacticalAuthorityCore.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/Soldier.h"
#include "../Engine/Language.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleInterface.h"
#include <algorithm>

namespace OpenXcom
{

namespace CompanionBattlescapeOwnershipCore
{

namespace
{

using CompanionSoldierOwnershipCore::CONTROL_AI;
using CompanionSoldierOwnershipCore::CONTROL_HUMAN;

bool unitMindControlledHostile(const BattleUnit *unit)
{
	return unit != 0 && unit->getOriginalFaction() == FACTION_PLAYER && unit->getFaction() != FACTION_PLAYER;
}

bool unitNativeActionInProgress(const BattleUnit *unit, bool battleBusy)
{
	if (unit == 0 || !battleBusy)
	{
		return false;
	}
	switch (unit->getStatus())
	{
	case STATUS_WALKING:
	case STATUS_FLYING:
	case STATUS_TURNING:
	case STATUS_AIMING:
	case STATUS_COLLAPSING:
		return true;
	default:
		return false;
	}
}

const BattleUnit *findUnitBySoldierId(SavedBattleGame *battle, int soldierId)
{
	if (battle == 0 || soldierId <= 0)
	{
		return 0;
	}
	for (std::vector<BattleUnit*>::const_iterator i = battle->getUnits()->begin();
	     i != battle->getUnits()->end(); ++i)
	{
		if (*i != 0 && (*i)->getId() == soldierId)
		{
			return *i;
		}
	}
	return 0;
}

std::string statusNoteForUnit(const BattleUnit *unit)
{
	if (unit == 0)
	{
		return std::string();
	}
	if (unitMindControlledHostile(unit))
	{
		return "STR_COMPANION_STATUS_MIND_CONTROLLED";
	}
	if (unit->getStatus() == STATUS_DEAD)
	{
		return "STR_COMPANION_STATUS_DEAD";
	}
	if (unit->getStatus() == STATUS_UNCONSCIOUS)
	{
		return "STR_COMPANION_STATUS_UNCONSCIOUS";
	}
	if (unit->getStatus() == STATUS_PANICKING)
	{
		return "STR_PANICKED";
	}
	if (unit->getStatus() == STATUS_BERSERK)
	{
		return "STR_COMPANION_STATUS_BERSERK";
	}
	return std::string();
}

void rollbackOwnership(
	SavedGame *campaign,
	int soldierId,
	bool wasAiBefore)
{
	if (campaign == 0 || soldierId <= 0)
	{
		return;
	}
	if (wasAiBefore)
	{
		campaign->assignCompanionSoldierToAi(soldierId);
	}
	else
	{
		campaign->reclaimCompanionSoldierForHuman(soldierId);
	}
}

} // namespace

bool isDeployedRosterSoldierUnit(const BattleUnit *unit, const SavedGame *campaign)
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

CompanionSoldierOwnershipCore::SoldierControlMode effectiveTacticalOwner(
	const SavedBattleGame *battle,
	int soldierId,
	bool liveRoster)
{
	if (battle == 0)
	{
		return CONTROL_HUMAN;
	}
	return CompanionCampaignOwnershipCore::resolveCampaignSoldierControlMode(
		battle->getCompanionTacticalAiSoldierIds(), soldierId, liveRoster);
}

OwnershipHandoffBlockReason evaluateHandoffBlock(
	const BattleUnit *unit,
	bool battleBusy,
	int inFlightCompanionActorUnitId)
{
	if (unit == 0)
	{
		return HANDOFF_NOT_ASSIGNABLE;
	}
	return CompanionOwnershipHandoffCore::evaluateHandoffBlockForActor(
		unit->getId(),
		battleBusy,
		inFlightCompanionActorUnitId,
		unitNativeActionInProgress(unit, battleBusy));
}

bool soldierEligibleForOwnershipToggle(const BattleUnit *unit, const SavedGame *campaign)
{
	if (!CompanionTacticalAuthorityCore::isLiveCampaignPlayerSoldierUnit(unit, campaign))
	{
		return false;
	}
	if (unitMindControlledHostile(unit))
	{
		return false;
	}
	if (unit->getStatus() == STATUS_DEAD || unit->getStatus() == STATUS_UNCONSCIOUS)
	{
		return false;
	}
	return true;
}

std::vector<SquadOwnershipRow> buildSquadOwnershipRows(
	SavedGame *campaign,
	SavedBattleGame *battle,
	BattlescapeGame *game,
	Language *lang,
	int inFlightCompanionActorUnitId)
{
	std::vector<SquadOwnershipRow> rows;
	if (campaign == 0 || battle == 0)
	{
		return rows;
	}
	const bool battleBusy = game != 0 && game->isBusy();
	for (std::vector<BattleUnit*>::const_iterator i = battle->getUnits()->begin();
	     i != battle->getUnits()->end(); ++i)
	{
		BattleUnit *unit = *i;
		if (!isDeployedRosterSoldierUnit(unit, campaign))
		{
			continue;
		}
		SquadOwnershipRow row = {};
		row.soldierId = unit->getId();
		row.displayName = unit->getName(lang, false);
		row.effectiveOwner = effectiveTacticalOwner(
			battle, row.soldierId, campaign->isLiveRosterSoldier(row.soldierId));
		row.statusNote = statusNoteForUnit(unit);
		row.unsavedVsGeoscapeSave = campaign->isCompanionSoldierOwnershipUnsaved(row.soldierId);
		row.handoffBlock = evaluateHandoffBlock(unit, battleBusy, inFlightCompanionActorUnitId);
		const bool eligible = soldierEligibleForOwnershipToggle(unit, campaign);
		if (eligible && row.handoffBlock == HANDOFF_OK)
		{
			row.canAssignToAster = row.effectiveOwner == CONTROL_HUMAN;
			row.canReclaimForHuman = row.effectiveOwner == CONTROL_AI;
		}
		rows.push_back(row);
	}
	std::sort(rows.begin(), rows.end(), [](const SquadOwnershipRow &a, const SquadOwnershipRow &b) {
		return a.soldierId < b.soldierId;
	});
	return rows;
}

OwnershipChangeOutcome tryApplyOwnershipToggle(
	SavedGame *campaign,
	SavedBattleGame *battle,
	BattlescapeGame *game,
	int soldierId,
	bool assignToAster,
	int inFlightCompanionActorUnitId)
{
	OwnershipChangeOutcome outcome = {};
	outcome.ok = false;
	outcome.error = CHANGE_NOT_ASSIGNABLE;
	outcome.handoffBlock = HANDOFF_NOT_ASSIGNABLE;
	outcome.effectiveOwnerAfter = CONTROL_HUMAN;

	if (campaign == 0 || battle == 0 || soldierId <= 0)
	{
		return outcome;
	}

	const BattleUnit *unit = findUnitBySoldierId(battle, soldierId);
	if (!soldierEligibleForOwnershipToggle(unit, campaign))
	{
		outcome.error = CHANGE_NOT_ASSIGNABLE;
		return outcome;
	}

	const bool battleBusy = game != 0 && game->isBusy();
	outcome.handoffBlock = evaluateHandoffBlock(unit, battleBusy, inFlightCompanionActorUnitId);
	if (outcome.handoffBlock != HANDOFF_OK)
	{
		outcome.error = CHANGE_HANDOFF_BLOCKED;
		return outcome;
	}

	const bool wasAi = effectiveTacticalOwner(
		battle, soldierId, campaign->isLiveRosterSoldier(soldierId)) == CONTROL_AI;
	const bool wantAi = assignToAster;
	if (wasAi == wantAi)
	{
		outcome.ok = true;
		outcome.error = CHANGE_OK;
		outcome.effectiveOwnerAfter = wasAi ? CONTROL_AI : CONTROL_HUMAN;
		return outcome;
	}

	bool applied = false;
	if (wantAi)
	{
		applied = campaign->assignCompanionSoldierToAi(soldierId);
	}
	else
	{
		applied = campaign->reclaimCompanionSoldierForHuman(soldierId);
	}
	if (!applied)
	{
		outcome.error = CHANGE_ASSIGN_FAILED;
		return outcome;
	}

	if (CompanionOnDemandRelayCore::companionSnapshotPathConfigured())
	{
		if (!CompanionSnapshot::tryPublishSoldierControl(battle, campaign))
		{
			rollbackOwnership(campaign, soldierId, wasAi);
			outcome.error = CHANGE_PUBLISH_FAILED;
			return outcome;
		}
	}

	outcome.ok = true;
	outcome.error = CHANGE_OK;
	outcome.handoffBlock = HANDOFF_OK;
	outcome.effectiveOwnerAfter = effectiveTacticalOwner(
		battle, soldierId, campaign->isLiveRosterSoldier(soldierId));
	return outcome;
}

std::string handoffBlockMessageKey(OwnershipHandoffBlockReason reason)
{
	switch (reason)
	{
	case HANDOFF_COMPANION_COMMAND_IN_FLIGHT:
	case HANDOFF_NATIVE_ACTION_IN_PROGRESS:
		return "STR_COMPANION_HANDOFF_BLOCKED";
	case HANDOFF_NOT_ASSIGNABLE:
		return "STR_COMPANION_STATUS_NOT_COMMANDABLE";
	default:
		return std::string();
	}
}

Uint8 defaultBattlescapeSoldierNameColor(const Mod *mod)
{
	if (mod == 0)
	{
		return 128;
	}
	return mod->getInterface("battlescape")->getElement("textName")->color;
}

Uint8 soldierNameColorForEffectiveOwner(
	const Mod *mod,
	CompanionSoldierOwnershipCore::SoldierControlMode effectiveOwner)
{
	if (effectiveOwner == CompanionSoldierOwnershipCore::CONTROL_AI)
	{
		return BATTLESCAPE_ASTER_SOLDIER_NAME_COLOR;
	}
	return defaultBattlescapeSoldierNameColor(mod);
}

std::string changeErrorMessageKey(OwnershipChangeError error)
{
	switch (error)
	{
	case CHANGE_PUBLISH_FAILED:
		return "STR_COMPANION_PUBLISH_FAILED";
	case CHANGE_HANDOFF_BLOCKED:
		return "STR_COMPANION_HANDOFF_BLOCKED";
	case CHANGE_NOT_ASSIGNABLE:
	case CHANGE_ASSIGN_FAILED:
		return "STR_COMPANION_STATUS_NOT_COMMANDABLE";
	default:
		return std::string();
	}
}

} // namespace CompanionBattlescapeOwnershipCore

} // namespace OpenXcom
