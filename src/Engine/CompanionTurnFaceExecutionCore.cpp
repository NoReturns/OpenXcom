/*
 * Open X-Com Companion — native turn-in-place (M1 WP-TURN-1b).
 */
#include "CompanionTurnFaceExecutionCore.h"
#include "CompanionTuVisibilityCore.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/TileEngine.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"

namespace OpenXcom
{

namespace CompanionTurnFaceExecutionCore
{

bool executeTurnFace(
	BattlescapeGame *game,
	BattleUnit *actor,
	const Position &target,
	std::string *rejectReason,
	ExecutionOutcome *outcome)
{
	if (outcome != 0)
	{
		*outcome = ExecutionOutcome();
	}
	if (game == 0 || actor == 0)
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

	const int desiredDir = actor->directionTo(target);
	const int currentDir = actor->getDirection();
	if (desiredDir == currentDir && actor->getStatus() != STATUS_TURNING)
	{
		if (outcome != 0)
		{
			outcome->tuSpentTurning = 0;
			outcome->finalDirection = currentDir;
		}
		return true;
	}

	const int steps = CompanionTuVisibilityCore::estimateDirectedFireFacingSteps(actor, target);
	if (steps < 0)
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Cannot estimate turn toward target.";
		}
		return false;
	}
	if (steps == 0)
	{
		if (outcome != 0)
		{
			outcome->tuSpentTurning = 0;
			outcome->finalDirection = currentDir;
		}
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

	const int tuStart = actor->getTimeUnits();
	const bool turret = false;
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

	const int tuSpent = tuStart - actor->getTimeUnits();
	if (actor->directionTo(target) != actor->getDirection())
	{
		if (rejectReason != 0)
		{
			*rejectReason = "Unit did not reach target facing.";
		}
		return false;
	}

	if (outcome != 0)
	{
		outcome->tuSpentTurning = tuSpent;
		outcome->finalDirection = actor->getDirection();
	}
	return true;
}

} // namespace CompanionTurnFaceExecutionCore

} // namespace OpenXcom