/*
 * Open X-Com Companion — native medikit target resolution and headless application (M1-WP9 S2).
 */
#include "CompanionMedikitExecutionCore.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/Tile.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/BattleUnitStatistics.h"
#include "../Battlescape/Position.h"
#include "../Battlescape/TileEngine.h"
#include "../Mod/RuleItem.h"

namespace OpenXcom
{

namespace CompanionMedikitExecutionCore
{

BattleUnit *resolveNativeMedikitTarget(SavedBattleGame *battle, BattleUnit *actor)
{
	if (battle == 0 || actor == 0 || actor->isOut())
	{
		return 0;
	}

	BattleUnit *targetUnit = 0;
	std::vector<BattleUnit *> *const units = battle->getUnits();
	for (std::vector<BattleUnit *>::const_iterator i = units->begin(); i != units->end() && targetUnit == 0; ++i)
	{
		if ((*i)->getPosition() == actor->getPosition() && *i != actor && (*i)->getStatus() == STATUS_UNCONSCIOUS &&
		    (*i)->isWoundable())
		{
			targetUnit = *i;
		}
	}

	if (targetUnit == 0)
	{
		Position meleeDest;
		TileEngine *tileEngine = battle->getTileEngine();
		if (tileEngine != 0 &&
		    tileEngine->validMeleeRange(actor->getPosition(), actor->getDirection(), actor, 0, &meleeDest, false))
		{
			Tile *tile = battle->getTile(meleeDest);
			if (tile != 0 && tile->getUnit() != 0 && tile->getUnit()->isWoundable())
			{
				targetUnit = tile->getUnit();
			}
		}
	}

	return targetUnit;
}

static void companionMedikitRecordRevive(BattleUnit *actor, BattleUnit *target)
{
	if (actor == 0 || target == 0)
	{
		return;
	}
	target->setTimeUnits(0);
	if (target->getOriginalFaction() == FACTION_PLAYER)
	{
		actor->getStatistics()->revivedSoldier++;
	}
	else if (target->getOriginalFaction() == FACTION_HOSTILE)
	{
		actor->getStatistics()->revivedHostile++;
	}
	else
	{
		actor->getStatistics()->revivedNeutral++;
	}
}

MedikitApplicationResult applyPainKiller(BattleUnit *actor, BattleItem *medikit, BattleUnit *target, int tu, int *tuSpentOut)
{
	if (actor == 0 || medikit == 0 || target == 0)
	{
		return MEDIKIT_NO_CHARGE;
	}
	int pk = medikit->getPainKillerQuantity();
	if (pk == 0)
	{
		return MEDIKIT_NO_CHARGE;
	}
	if (!actor->spendTimeUnits(tu))
	{
		return MEDIKIT_INSUFFICIENT_TU;
	}
	target->painKillers();
	medikit->setPainKillerQuantity(--pk);
	actor->getStatistics()->appliedPainKill++;
	if (tuSpentOut != 0)
	{
		*tuSpentOut = tu;
	}
	return MEDIKIT_APPLIED;
}

MedikitApplicationResult applyStimulant(
	BattleUnit *actor,
	BattleItem *medikit,
	BattleUnit *target,
	int tu,
	int *tuSpentOut,
	bool *targetRevivedOut)
{
	if (targetRevivedOut != 0)
	{
		*targetRevivedOut = false;
	}
	if (actor == 0 || medikit == 0 || target == 0)
	{
		return MEDIKIT_NO_CHARGE;
	}
	int stimulant = medikit->getStimulantQuantity();
	if (stimulant == 0)
	{
		return MEDIKIT_NO_CHARGE;
	}
	if (!actor->spendTimeUnits(tu))
	{
		return MEDIKIT_INSUFFICIENT_TU;
	}
	RuleItem *rule = medikit->getRules();
	target->stimulant(rule->getEnergyRecovery(), rule->getStunRecovery());
	medikit->setStimulantQuantity(--stimulant);
	actor->getStatistics()->appliedStimulant++;
	if (target->getStatus() == STATUS_UNCONSCIOUS && target->getStunlevel() < target->getHealth() && target->getHealth() > 0)
	{
		companionMedikitRecordRevive(actor, target);
		if (targetRevivedOut != 0)
		{
			*targetRevivedOut = true;
		}
	}
	if (tuSpentOut != 0)
	{
		*tuSpentOut = tu;
	}
	return MEDIKIT_APPLIED;
}

MedikitApplicationResult applyHeal(
	BattleUnit *actor,
	BattleItem *medikit,
	BattleUnit *target,
	int bodyPart,
	int tu,
	int *tuSpentOut,
	bool *targetRevivedOut)
{
	if (targetRevivedOut != 0)
	{
		*targetRevivedOut = false;
	}
	if (actor == 0 || medikit == 0 || target == 0)
	{
		return MEDIKIT_NO_CHARGE;
	}
	int heal = medikit->getHealQuantity();
	if (heal == 0)
	{
		return MEDIKIT_NO_CHARGE;
	}
	if (!actor->spendTimeUnits(tu))
	{
		return MEDIKIT_INSUFFICIENT_TU;
	}
	RuleItem *rule = medikit->getRules();
	target->heal(bodyPart, rule->getWoundRecovery(), rule->getHealthRecovery());
	medikit->setHealQuantity(--heal);

	if (target->getStatus() == STATUS_UNCONSCIOUS && target->getStunlevel() < target->getHealth() && target->getHealth() > 0)
	{
		companionMedikitRecordRevive(actor, target);
		if (targetRevivedOut != 0)
		{
			*targetRevivedOut = true;
		}
	}
	actor->getStatistics()->woundsHealed++;
	if (tuSpentOut != 0)
	{
		*tuSpentOut = tu;
	}
	return MEDIKIT_APPLIED;
}

} // namespace CompanionMedikitExecutionCore

} // namespace OpenXcom