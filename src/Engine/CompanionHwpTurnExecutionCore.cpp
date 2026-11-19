/*
 * Open X-Com Companion — HWP turn-in-place (M1 HWP-S4-1b).
 */
#include "CompanionHwpTurnExecutionCore.h"
#include "CompanionTuVisibilityCore.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/TileEngine.h"
#include "../Engine/Options.h"
#include "../Savegame/BattleUnit.h"

namespace OpenXcom
{

namespace CompanionHwpTurnExecutionCore
{

static int currentFacingForPart(const BattleUnit *unit, HwpTurnPart part)
{
	if (part == HwpTurnPart::Turret)
	{
		return unit->getTurretDirection();
	}
	return unit->getDirection();
}

int estimateTurnSteps(const BattleUnit *unit, const Position &target, HwpTurnPart part)
{
	if (unit == 0)
	{
		return -1;
	}
	const int desired = unit->directionTo(target);
	const int current = currentFacingForPart(unit, part);
	return CompanionTuVisibilityCore::octagonalTurnSteps(current, desired);
}

static bool validateTargetAndActor(
	BattleUnit *actor,
	const Position &target,
	std::string *rejectReason)
{
	if (actor == 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Invalid turn parameters.";
		}
		return false;
	}
	const Position pos = actor->getPosition();
	if (target.x == pos.x && target.y == pos.y)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Target must differ from actor position on the horizontal plane.";
		}
		return false;
	}
	return true;
}

bool preflightHwpTurn(
	BattlescapeGame *game,
	BattleUnit *actor,
	const Position &target,
	HwpTurnPart part,
	std::string *rejectReason,
	int *stepsOut)
{
	if (stepsOut != 0)
	{
		*stepsOut = 0;
	}
	if (game == 0 || actor == 0 || actor->isOut())
	{
		if (rejectReason != 0)
		{
			*rejectReason = actor == 0 || actor->isOut() ? "Unit unavailable." : "Invalid turn parameters.";
		}
		return false;
	}
	if (!validateTargetAndActor(actor, target, rejectReason))
	{
		return false;
	}

	if (part == HwpTurnPart::Turret)
	{
		if (actor->getTurretType() < 0)
		{
			if (rejectReason != 0)
			{
				*rejectReason = "Unit has no turret.";
			}
			return false;
		}
		if (!Options::strafe)
		{
			if (rejectReason != 0)
			{
				*rejectReason = "Strafe option disabled; turret turn unavailable.";
			}
			return false;
		}
	}

	const int steps = estimateTurnSteps(actor, target, part);
	if (steps < 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Cannot estimate turn toward target.";
		}
		return false;
	}
	if (stepsOut != 0)
	{
		*stepsOut = steps;
	}
	if (steps == 0)
	{
		return true;
	}
	if (steps > actor->getTimeUnits())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units to turn.";
		}
		return false;
	}
	if (!game->checkReservedTU(actor, steps, true))
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Not enough time units to turn.";
		}
		return false;
	}
	return true;
}

bool executeHwpTurn(
	BattlescapeGame *game,
	BattleUnit *actor,
	const Position &target,
	HwpTurnPart part,
	std::string *rejectReason,
	ExecutionOutcome *outcome)
{
	if (outcome != 0)
	{
		outcome->tuSpentTurning = 0;
		outcome->finalDirection = actor != 0 ? actor->getDirection() : -1;
		outcome->finalTurretDirection = actor != 0 && actor->getTurretType() >= 0
			? actor->getTurretDirection() : -1;
	}

	int steps = 0;
	if (!preflightHwpTurn(game, actor, target, part, rejectReason, &steps))
	{
		return false;
	}

	const Position startPos = actor->getPosition();
	const int tuStart = actor->getTimeUnits();
	const int hullStart = actor->getDirection();
	const int turretStart = actor->getTurretType() >= 0 ? actor->getTurretDirection() : -1;

	if (steps == 0)
	{
		if (outcome != 0)
		{
			outcome->tuSpentTurning = 0;
			outcome->finalDirection = hullStart;
			outcome->finalTurretDirection = turretStart;
		}
		return true;
	}

	const bool turret = part == HwpTurnPart::Turret;
	actor->lookAt(target, turret);
	int spentSteps = 0;
	while (actor->getStatus() == STATUS_TURNING && spentSteps < 8)
	{
		if (!game->checkReservedTU(actor, 1, true))
		{
			actor->abortTurn();
			if (rejectReason != 0)
			{
				*rejectReason = "Reserved time units block turn.";
			}
			return false;
		}
		if (!actor->spendTimeUnits(1))
		{
			actor->abortTurn();
			if (rejectReason != 0)
			{
				*rejectReason = "Not enough time units to turn.";
			}
			return false;
		}
		actor->turn(turret);
		TileEngine *tileEngine = game->getTileEngine();
		if (tileEngine != 0)
		{
			tileEngine->calculateFOV(actor);
		}
		++spentSteps;
	}

	if (actor->getStatus() == STATUS_TURNING)
	{
		actor->abortTurn();
		if (rejectReason != 0)
		{
			*rejectReason = "Unit could not complete turn.";
		}
		return false;
	}

	const int desired = actor->directionTo(target);
	if (part == HwpTurnPart::Turret)
	{
		if (actor->getTurretDirection() != desired)
		{
			if (rejectReason != 0)
			{
				*rejectReason = "Turret did not reach target bearing.";
			}
			return false;
		}
	}
	else if (actor->getDirection() != desired)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Unit did not reach target facing.";
		}
		return false;
	}

	if (actor->getPosition() != startPos)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Turn caused unexpected movement.";
		}
		return false;
	}

	const int tuSpent = tuStart - actor->getTimeUnits();
	if (outcome != 0)
	{
		outcome->tuSpentTurning = tuSpent;
		outcome->finalDirection = actor->getDirection();
		outcome->finalTurretDirection = actor->getTurretType() >= 0
			? actor->getTurretDirection() : -1;
	}
	return true;
}

} // namespace CompanionHwpTurnExecutionCore

} // namespace OpenXcom