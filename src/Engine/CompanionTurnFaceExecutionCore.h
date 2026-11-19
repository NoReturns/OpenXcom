#pragma once
/*
 * Open X-Com Companion — native turn-in-place (M1 WP-TURN-1b).
 */
#include "../Battlescape/Position.h"
#include <string>

namespace OpenXcom
{

class BattlescapeGame;
class BattleUnit;

namespace CompanionTurnFaceExecutionCore
{

struct ExecutionOutcome
{
	int tuSpentTurning;
	int finalDirection;
};

/// Synchronous body turn toward map bearing; mirrors UnitTurnBState TU loop (no turret strafe).
bool executeTurnFace(
	BattlescapeGame *game,
	BattleUnit *actor,
	const Position &target,
	std::string *rejectReason,
	ExecutionOutcome *outcome);

} // namespace CompanionTurnFaceExecutionCore

} // namespace OpenXcom