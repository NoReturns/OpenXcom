#pragma once
/*
 * Open X-Com Companion — remote MC hostile command actor authority (M1 MC-02B).
 *
 * Mind-controlled aliens (originalFaction hostile, faction player), separate from HWP/roster.
 */
#include "CompanionCommandCore.h"
#include <string>

namespace OpenXcom
{

class BattleUnit;
class SavedBattleGame;
class SavedGame;

namespace CompanionTacticalMcHostileAuthorityCore
{

bool mcTacticalCommandExecutionSupported();

bool mcMoveRelayExecutionSupported();
bool mcTurnFaceRelayExecutionSupported();
bool mcKneelRelayExecutionSupported();

struct McRemoteAuthorityInput
{
	SavedGame *campaign;
	SavedBattleGame *battle;
	int actorUnitId;
	std::string controlFileJson;
	bool controlFileReadOk;
	std::string battleSessionId;
	std::string relayCampaignId;
	std::string relayTacticalBattleId;
	std::string commandSnapshotSha256;
	std::string lastSnapshotSha256;
	std::string commandSessionId;
	int commandTurn;
	int turn;
	int commandStateVersion;
	int lastExportedStateVersion;
	bool battleInitialized;
	bool playerTurn;
	bool battleBusy;
	bool unitsFalling;
	bool actorNativeActionInProgress;
	bool requireBindingFreshness;
};

CompanionCommandCore::MoveValidationResult evaluateMcRemoteCommandAuthority(
	const McRemoteAuthorityInput &input);

CompanionCommandCore::MoveValidationResult validateMcTacticalCommandExecutionPreflight(
	const McRemoteAuthorityInput &input);

struct McMoveValidationInput
{
	McRemoteAuthorityInput authority;
	int destX;
	int destY;
	int destZ;
	int mapSizeX;
	int mapSizeY;
	int mapSizeZ;
};

CompanionCommandCore::MoveValidationResult validateMcMoveCommand(const McMoveValidationInput &input);
CompanionCommandCore::MoveValidationResult validateMcMoveExecutionPreflight(const McMoveValidationInput &input);

const BattleUnit *findBattleUnitById(SavedBattleGame *battle, int unitId);

bool isXcomControlledMcHostileBattleUnit(const BattleUnit *unit);

bool isOperationalMcHostileForRemoteCommands(const BattleUnit *unit);

} // namespace CompanionTacticalMcHostileAuthorityCore

} // namespace OpenXcom