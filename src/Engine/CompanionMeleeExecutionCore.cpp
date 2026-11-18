/*
 * Open X-Com Companion — native melee attack (M1 melee S2a).
 */
#include "CompanionMeleeExecutionCore.h"
#include "CompanionCommandRuntimeSupport.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/MeleeAttackBState.h"
#include "../Battlescape/TileEngine.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Mod/RuleItem.h"

namespace OpenXcom
{

namespace CompanionMeleeExecutionCore
{

int estimateMeleeTurnSteps(const BattleUnit *actor, const BattleUnit *target)
{
	if (actor == 0 || target == 0)
	{
		return 0;
	}
	const int fromDir = actor->getDirection();
	const int toDir = actor->directionTo(target->getPosition());
	return CompanionCommandRuntimeSupport::shortestTurnStepsBetweenDirections(fromDir, toDir);
}

static bool isHeadlessHarness(BattlescapeGame *game)
{
	return game != 0 && game->getMap() == 0;
}

static int turnActorToward(BattlescapeGame *game, BattleUnit *actor, const Position &target, bool allowHarnessFacingSnap)
{
	if (actor == 0)
	{
		return -1;
	}
	const int tuStart = actor->getTimeUnits();
	const bool turret = actor->getTurretType() != -1;
	actor->lookAt(target, turret);
	int turnSteps = 0;
	while (actor->getStatus() == STATUS_TURNING && turnSteps < 8)
	{
		if (!actor->spendTimeUnits(1))
		{
			actor->abortTurn();
			return -1;
		}
		actor->turn(turret);
		if (game != 0 && game->getTileEngine() != 0)
		{
			game->getTileEngine()->calculateFOV(actor);
		}
		++turnSteps;
	}
	if (allowHarnessFacingSnap && actor->getStatus() == STATUS_TURNING)
	{
		actor->lookAt(actor->directionTo(target), true);
		actor->abortTurn();
	}
	return tuStart - actor->getTimeUnits();
}

bool executeMelee(
	BattlescapeGame *game,
	BattleUnit *actor,
	BattleItem *meleeWeapon,
	BattleUnit *target,
	std::string *rejectReason,
	ExecutionOutcome *outcome)
{
	if (outcome != 0)
	{
		*outcome = ExecutionOutcome();
	}
	if (game == 0 || actor == 0 || meleeWeapon == 0 || target == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Invalid melee parameters.";
		}
		return false;
	}
	SavedBattleGame *save = game->getSave();
	if (save == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Battle unavailable.";
		}
		return false;
	}
	TileEngine *tileEngine = game->getTileEngine();
	if (tileEngine == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Battle unavailable.";
		}
		return false;
	}

	const int meleeTu = actor->getActionTUs(BA_HIT, meleeWeapon);
	if (meleeTu <= 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Melee weapon has no action cost.";
		}
		return false;
	}
	const int turnStepsNeeded = estimateMeleeTurnSteps(actor, target);
	if (!game->checkReservedTU(actor, meleeTu + turnStepsNeeded, true))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units for melee.";
		}
		return false;
	}

	const bool headlessHarness = isHeadlessHarness(game);
	const int turnTu = turnActorToward(game, actor, target->getPosition(), headlessHarness);
	if (turnTu < 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units to turn toward target.";
		}
		return false;
	}
	if (actor->getStatus() == STATUS_TURNING)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Unit could not complete turn toward target.";
		}
		return false;
	}
	if (!game->checkReservedTU(actor, meleeTu, true))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units for melee.";
		}
		return false;
	}

	Position meleeDest;
	if (!tileEngine->validMeleeRange(
		actor->getPosition(),
		actor->getDirection(),
		actor,
		target,
		&meleeDest))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Target is not in melee range.";
		}
		return false;
	}

	BattleAction action;
	action.type = BA_HIT;
	action.actor = actor;
	action.weapon = meleeWeapon;
	action.target = meleeDest;
	action.TU = meleeTu;
	action.playerOrdered = true;
	// Synchronous companion path always skips ExplosionBState push; cosmetic audio uses map + !companionHeadlessMelee.
	action.companionHeadlessMelee = true;

	bool meleeHit = false;
	bool unconscious = false;
	bool killed = false;
	if (!MeleeAttackBState::companionExecuteMelee(
		game, action, target, rejectReason, &meleeHit, &unconscious, &killed))
	{
		return false;
	}

	if (outcome != 0)
	{
		outcome->meleeHit = meleeHit;
		outcome->targetUnconscious = unconscious;
		outcome->targetKilled = killed;
		outcome->tuSpentMelee = meleeTu;
		outcome->tuSpentTurning = turnTu;
	}
	return true;
}

} // namespace CompanionMeleeExecutionCore

} // namespace OpenXcom