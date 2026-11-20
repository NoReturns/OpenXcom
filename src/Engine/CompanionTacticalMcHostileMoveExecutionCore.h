#pragma once
/*
 * Open X-Com Companion — MC_MOVE native execution (M1 MC-02B).
 */
#include "CompanionCommandCore.h"
#include "CompanionTacticalMcHostileAuthorityCore.h"
#include "../Battlescape/Position.h"
#include <string>

namespace OpenXcom
{

class BattlescapeGame;

namespace CompanionTacticalMcHostileMoveExecutionCore
{

struct McMoveStartOutcome
{
	bool started;
	std::string rejectCode;
	std::string rejectReason;
	Position startPos;
	Position requestedDest;
	int tuAtAcceptance;
};

bool tryStartMcMove(
	BattlescapeGame *game,
	const CompanionTacticalMcHostileAuthorityCore::McMoveValidationInput &moveInput,
	McMoveStartOutcome *outcome);

struct McMoveLifecycleObservation
{
	Position startPos;
	Position requestedDest;
	std::string walkActionResult;
};

CompanionCommandCore::CommandLifecycleStatus observeMcMoveLifecycle(
	BattlescapeGame *game,
	const CompanionTacticalMcHostileAuthorityCore::McRemoteAuthorityInput &authority,
	const McMoveLifecycleObservation &observation,
	CompanionCommandCore::WalkStopDiagnostics *diagnosticsOut);

} // namespace CompanionTacticalMcHostileMoveExecutionCore

} // namespace OpenXcom