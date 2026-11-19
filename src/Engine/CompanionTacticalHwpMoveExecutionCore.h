#pragma once
/*
 * Open X-Com Companion — HWP_MOVE native execution (M1 HWP-S3B-02).
 *
 * Uses BattalionGame::queueCompanionPlayerOrderedWalk + UnitWalkBState.
 * Relay inbox dispatch remains disabled (hwpTacticalCommandExecutionSupported() == false).
 */
#include "CompanionCommandCore.h"
#include "CompanionTacticalHwpAuthorityCore.h"
#include "../Battlescape/Position.h"
#include <string>

namespace OpenXcom
{

class BattlescapeGame;

namespace CompanionTacticalHwpMoveExecutionCore
{

struct HwpMoveStartOutcome
{
	bool started;
	std::string rejectCode;
	std::string rejectReason;
	Position startPos;
	Position requestedDest;
	int tuAtAcceptance;
};

/// Authority + in-bounds destination + battlescape idle checks; queues native walk (no relay lock).
bool tryStartHwpMove(
	BattlescapeGame *game,
	const CompanionTacticalHwpAuthorityCore::HwpMoveValidationInput &moveInput,
	HwpMoveStartOutcome *outcome);

struct HwpMoveLifecycleObservation
{
	Position startPos;
	Position requestedDest;
	std::string walkActionResult;
};

/// Poll walk completion using the same lifecycle rules as roster MOVE relay results.
CompanionCommandCore::CommandLifecycleStatus observeHwpMoveLifecycle(
	BattlescapeGame *game,
	const CompanionTacticalHwpAuthorityCore::HwpRemoteAuthorityInput &authority,
	const HwpMoveLifecycleObservation &observation,
	CompanionCommandCore::WalkStopDiagnostics *diagnosticsOut);

} // namespace CompanionTacticalHwpMoveExecutionCore

} // namespace OpenXcom