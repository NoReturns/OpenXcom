#pragma once
/*
 * Open X-Com Companion — remote HWP command actor authority (M1 HWP-S3A).
 *
 * Separate from roster soldier authority (CompanionTacticalAuthorityCore).
 * Does not enable HWP MOVE/FIRE/TURN execution.
 */
#include "CompanionCommandCore.h"
#include <string>

namespace OpenXcom
{

class BattleUnit;
class SavedBattleGame;
class SavedGame;

namespace CompanionTacticalHwpAuthorityCore
{

/// Always false until a future HWP command execution work package (S3A lock).
bool hwpTacticalCommandExecutionSupported();

struct HwpRemoteAuthorityInput
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

CompanionCommandCore::MoveValidationResult evaluateHwpRemoteCommandAuthority(
	const HwpRemoteAuthorityInput &input);

/// Future HWP executors: binding + actor authority, then execution lock (always rejects).
CompanionCommandCore::MoveValidationResult validateHwpTacticalCommandExecutionPreflight(
	const HwpRemoteAuthorityInput &input);

struct HwpMoveValidationInput
{
	HwpRemoteAuthorityInput authority;
	int destX;
	int destY;
	int destZ;
	int mapSizeX;
	int mapSizeY;
	int mapSizeZ;
};

/// S3B contract: authority + in-bounds destination only (no pathfinding).
CompanionCommandCore::MoveValidationResult validateHwpMoveCommand(const HwpMoveValidationInput &input);

/// Authority + destination + execution lock (always rejects until S3 execution WP).
CompanionCommandCore::MoveValidationResult validateHwpMoveExecutionPreflight(const HwpMoveValidationInput &input);

const BattleUnit *findBattleUnitById(SavedBattleGame *battle, int unitId);

bool isOperationalFriendlyHwpForRemoteCommands(const BattleUnit *unit);

} // namespace CompanionTacticalHwpAuthorityCore

} // namespace OpenXcom