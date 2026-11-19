#pragma once
/*
 * Open X-Com Companion — HWP directed fire (M1 HWP-S5-1b).
 *
 * Reuses BattlescapeGame::tryExecuteCompanionDirectedFire; relay inbox not wired until S5-1c.
 */
#include "CompanionCommandCore.h"
#include "../Battlescape/Position.h"
#include <string>

namespace OpenXcom
{

class BattleItem;
class BattleUnit;
class BattlescapeGame;

namespace CompanionHwpFireExecutionCore
{

struct HwpFireStartOutcome
{
	bool started;
	std::string rejectReason;
	int tuAtAcceptance;
	int facingStepsEstimated;
};

BattleItem *findBattleItemOnActorById(BattleUnit *actor, int battleItemId);

bool isFriendlyHwpBattleActor(const BattleUnit *unit);

/// Non-mutating: weapon resolve, HWP actor, native directed-fire preflight.
bool preflightHwpFire(
	BattlescapeGame *game,
	BattleUnit *actor,
	const Position &target,
	const std::string &fireMode,
	int weaponBattleItemId,
	std::string *rejectReason,
	int *facingStepsOut = 0);

/// Queues UnitTurnBState + ProjectileFlyBState when preflight passes; does not await projectile completion.
bool tryStartHwpFire(
	BattlescapeGame *game,
	BattleUnit *actor,
	const Position &target,
	const std::string &fireMode,
	int weaponBattleItemId,
	std::string *rejectReason,
	HwpFireStartOutcome *outcome);

CompanionCommandCore::MoveValidationResult buildHwpFireContractValidation(
	BattlescapeGame *game,
	BattleUnit *actor,
	const Position &target,
	const std::string &fireMode,
	int weaponBattleItemId);

} // namespace CompanionHwpFireExecutionCore

} // namespace OpenXcom