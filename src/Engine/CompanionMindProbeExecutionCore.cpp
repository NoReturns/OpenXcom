/*
 * Open X-Com Companion — headless Mind Probe BA_USE (M1 MIND-PROBE-18).
 */
#include "CompanionMindProbeExecutionCore.h"
#include "CompanionMindProbeMemory.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Mod/RuleItem.h"
#include <algorithm>

namespace OpenXcom
{

namespace CompanionMindProbeExecutionCore
{

static bool companionMindProbeTargetEligible(BattleUnit *actor, BattleUnit *target, RuleItem *rules, std::string *rejectReason)
{
	if (target == 0 || target->isOut())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Target unit not available.";
		}
		return false;
	}
	if (actor == 0 || target->getFaction() == actor->getFaction())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Target must be a non-friendly unit.";
		}
		return false;
	}
	if (!target->getVisible())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Target is not visible.";
		}
		return false;
	}
	if (rules != 0 && rules->isLOSRequired())
	{
		std::vector<BattleUnit *> *visible = actor->getVisibleUnits();
		if (visible == 0 ||
		    std::find(visible->begin(), visible->end(), target) == visible->end())
		{
			if (rejectReason != 0)
			{
				*rejectReason = "No line of sight to target.";
			}
			return false;
		}
	}
	return true;
}

MindProbeUseResult applyMindProbeUse(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame *campaign,
	BattleUnit *actor,
	BattleItem *mindProbe,
	BattleUnit *target,
	int *tuSpentOut,
	std::string *rejectReason)
{
	if (tuSpentOut != 0)
	{
		*tuSpentOut = 0;
	}
	if (battle == 0 || actor == 0 || mindProbe == 0 || target == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Mind Probe parameters invalid.";
		}
		return MIND_PROBE_TARGET_INELIGIBLE;
	}
	RuleItem *rules = mindProbe->getRules();
	if (rules == 0 || rules->getBattleType() != BT_MINDPROBE)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Item is not a Mind Probe.";
		}
		return MIND_PROBE_TARGET_INELIGIBLE;
	}
	if (!companionMindProbeTargetEligible(actor, target, rules, rejectReason))
	{
		return MIND_PROBE_TARGET_INELIGIBLE;
	}
	const int tu = actor->getActionTUs(BA_USE, mindProbe);
	if (tu <= 0 || !actor->spendTimeUnits(tu))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units.";
		}
		return MIND_PROBE_INSUFFICIENT_TU;
	}
	battle->getCompanionMindProbeMemory().recordSuccessfulProbe(battle, campaign, target, battle->getTurn());
	if (tuSpentOut != 0)
	{
		*tuSpentOut = tu;
	}
	return MIND_PROBE_APPLIED;
}

} // namespace CompanionMindProbeExecutionCore

} // namespace OpenXcom