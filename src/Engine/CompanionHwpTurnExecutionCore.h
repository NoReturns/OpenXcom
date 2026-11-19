#pragma once
/*
 * Open X-Com Companion — HWP turn-in-place (M1 HWP-S4-1b).
 */
#include "../Battlescape/Position.h"
#include <string>

namespace OpenXcom
{

class BattlescapeGame;
class BattleUnit;

namespace CompanionHwpTurnExecutionCore
{

enum class HwpTurnPart
{
	Body,
	Turret
};

struct ExecutionOutcome
{
	int tuSpentTurning;
	int finalDirection;
	int finalTurretDirection;
};

int estimateTurnSteps(const BattleUnit *unit, const Position &target, HwpTurnPart part);

/// Full preflight (TU, reserve, strafe for turret) with zero mutation. Returns true when turn may proceed or is a no-op.
bool preflightHwpTurn(
	BattlescapeGame *game,
	BattleUnit *actor,
	const Position &target,
	HwpTurnPart part,
	std::string *rejectReason,
	int *stepsOut);

/// Synchronous turn; calls preflight before any lookAt/spendTimeUnits.
bool executeHwpTurn(
	BattlescapeGame *game,
	BattleUnit *actor,
	const Position &target,
	HwpTurnPart part,
	std::string *rejectReason,
	ExecutionOutcome *outcome);

} // namespace CompanionHwpTurnExecutionCore

} // namespace OpenXcom