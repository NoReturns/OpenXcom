/*
 * Open X-Com Companion — HWP_MOVE native execution (M1 HWP-S3B-02).
 */
#include "CompanionTacticalHwpMoveExecutionCore.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/SavedBattleGame.h"

namespace OpenXcom
{

namespace CompanionTacticalHwpMoveExecutionCore
{

using CompanionCommandCore::CommandLifecycleStatus;
using CompanionCommandCore::MoveValidationResult;
using CompanionCommandCore::WalkCompletionInput;
using CompanionCommandCore::WalkStopDiagnostics;
using CompanionTacticalHwpAuthorityCore::evaluateHwpRemoteCommandAuthority;
using CompanionTacticalHwpAuthorityCore::findBattleUnitById;
using CompanionTacticalHwpAuthorityCore::HwpMoveValidationInput;
using CompanionTacticalHwpAuthorityCore::HwpRemoteAuthorityInput;
using CompanionTacticalHwpAuthorityCore::validateHwpMoveCommand;

static bool hwpStillAiCommandAuthorized(const HwpRemoteAuthorityInput &authority)
{
	const MoveValidationResult verdict = evaluateHwpRemoteCommandAuthority(authority);
	return verdict.accepted;
}

bool tryStartHwpMove(BattlescapeGame *game, const HwpMoveValidationInput &moveInput, HwpMoveStartOutcome *outcome)
{
	if (outcome != 0)
	{
		*outcome = HwpMoveStartOutcome();
	}
	if (game == 0 || moveInput.authority.battle == 0)
	{
		if (outcome != 0)
		{
			outcome->rejectCode = "NO_BATTLE_CONTEXT";
			outcome->rejectReason = "Battle context missing.";
		}
		return false;
	}

	const MoveValidationResult contract = validateHwpMoveCommand(moveInput);
	if (!contract.accepted)
	{
		if (outcome != 0)
		{
			outcome->rejectCode = contract.rejectCode;
			outcome->rejectReason = contract.publicReason;
		}
		return false;
	}

	if (moveInput.authority.battleBusy || game->isBusy())
	{
		if (outcome != 0)
		{
			outcome->rejectCode = "BATTLE_BUSY";
			outcome->rejectReason = "Battle is busy with animation or actions.";
		}
		return false;
	}

	BattleUnit *actor = const_cast<BattleUnit *>(
		findBattleUnitById(moveInput.authority.battle, moveInput.authority.actorUnitId));
	if (actor == 0)
	{
		if (outcome != 0)
		{
			outcome->rejectCode = "INVALID_ACTOR";
			outcome->rejectReason = "Actor not present on current battlescape.";
		}
		return false;
	}

	const Position dest(moveInput.destX, moveInput.destY, moveInput.destZ);
	outcome->requestedDest = dest;

	std::string rejectReason;
	if (!game->tryExecuteCompanionHwpMove(actor, dest, &rejectReason))
	{
		if (outcome != 0)
		{
			outcome->rejectCode = "MOVE_START_FAILED";
			outcome->rejectReason = rejectReason;
		}
		return false;
	}

	if (outcome != 0)
	{
		outcome->started = true;
		outcome->startPos = actor->getPosition();
		outcome->tuAtAcceptance = actor->getTimeUnits();
	}
	return true;
}

CommandLifecycleStatus observeHwpMoveLifecycle(
	BattlescapeGame *game,
	const HwpRemoteAuthorityInput &authority,
	const HwpMoveLifecycleObservation &observation,
	WalkStopDiagnostics *diagnosticsOut)
{
	if (diagnosticsOut != 0)
	{
		*diagnosticsOut = WalkStopDiagnostics();
	}
	if (game == 0 || authority.battle == 0)
	{
		return CompanionCommandCore::CMD_FAILED;
	}

	const BattleUnit *unit = findBattleUnitById(authority.battle, authority.actorUnitId);
	WalkCompletionInput done = {};
	done.requestedDest = observation.requestedDest;
	done.startPos = observation.startPos;
	done.currentPos = unit != 0 ? unit->getPosition() : observation.startPos;
	done.battleBusy = game->isBusy();
	done.actorAlive = unit != 0 && !unit->isOut();
	done.actorStillAiControlled = hwpStillAiCommandAuthorized(authority);
	done.actionResult = observation.walkActionResult;

	const CommandLifecycleStatus terminal = CompanionCommandCore::evaluateWalkTerminalStatus(done);
	if (diagnosticsOut != 0)
	{
		*diagnosticsOut = CompanionCommandCore::resolveWalkStopDiagnostics(terminal, done);
	}
	return terminal;
}

} // namespace CompanionTacticalHwpMoveExecutionCore

} // namespace OpenXcom