/*
 * Open X-Com Companion — native Psi-Amp panic (M1-WP10 S2a).
 */
#include "CompanionPsiExecutionCore.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/PsiAttackBState.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Mod/RuleItem.h"

namespace OpenXcom
{

namespace CompanionPsiExecutionCore
{

bool executePanic(
	BattlescapeGame *game,
	BattleUnit *actor,
	BattleItem *psiAmp,
	BattleUnit *target,
	std::string *rejectReason,
	int *tuSpentOut,
	bool *psychicSucceededOut)
{
	if (game == 0 || actor == 0 || psiAmp == 0 || target == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Invalid panic parameters.";
		}
		return false;
	}

	BattleAction action;
	action.type = BA_PANIC;
	action.actor = actor;
	action.weapon = psiAmp;
	action.target = target->getPosition();
	action.TU = actor->getActionTUs(BA_PANIC, psiAmp);
	action.targeting = true;
	action.playerOrdered = true;

	return PsiAttackBState::companionExecutePanic(game, action, rejectReason, tuSpentOut, psychicSucceededOut);
}

bool executeMindControl(
	BattlescapeGame *game,
	BattleUnit *actor,
	BattleItem *psiAmp,
	BattleUnit *target,
	std::string *rejectReason,
	int *tuSpentOut,
	bool *psychicSucceededOut)
{
	if (game == 0 || actor == 0 || psiAmp == 0 || target == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Invalid mind control parameters.";
		}
		return false;
	}

	BattleAction action;
	action.type = BA_MINDCONTROL;
	action.actor = actor;
	action.weapon = psiAmp;
	action.target = target->getPosition();
	action.TU = actor->getActionTUs(BA_MINDCONTROL, psiAmp);
	action.targeting = true;
	action.playerOrdered = true;

	return PsiAttackBState::companionExecuteMindControl(game, action, rejectReason, tuSpentOut, psychicSucceededOut);
}

} // namespace CompanionPsiExecutionCore

} // namespace OpenXcom