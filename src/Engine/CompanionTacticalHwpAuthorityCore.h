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

/// False — FIRE/TURN and other non-MOVE HWP commands remain unsupported on the relay inbox.
bool hwpTacticalCommandExecutionSupported();

/// True when HWP_MOVE relay execution is enabled (action-specific; does not enable other HWP command types).
bool hwpMoveRelayExecutionSupported();

/// True when HWP_TURN relay execution is enabled (S4-1c).
bool hwpTurnRelayExecutionSupported();

/// False until HWP_FIRE relay certification (S5-1c).
bool hwpFireRelayExecutionSupported();

/// True when HWP_GUIDED_LAUNCH relay execution is enabled (S6-GUIDED).
bool hwpGuidedLaunchRelayExecutionSupported();

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

struct HwpTurnValidationInput
{
	HwpRemoteAuthorityInput authority;
	CompanionCommandCore::HwpTurnValidationInput contract;
};

/// S3B contract: authority + in-bounds destination only (no pathfinding).
CompanionCommandCore::MoveValidationResult validateHwpMoveCommand(const HwpMoveValidationInput &input);

/// Authority + destination + HWP_MOVE relay execution gate (other HWP types use validateHwpTacticalCommandExecutionPreflight).
CompanionCommandCore::MoveValidationResult validateHwpMoveExecutionPreflight(const HwpMoveValidationInput &input);

CompanionCommandCore::MoveValidationResult validateHwpTurnExecutionPreflight(const HwpTurnValidationInput &input);

struct HwpFireValidationInput
{
	HwpRemoteAuthorityInput authority;
	CompanionCommandCore::HwpFireValidationInput contract;
};

CompanionCommandCore::MoveValidationResult validateHwpFireExecutionPreflight(const HwpFireValidationInput &input);

struct HwpGuidedLaunchValidationInput
{
	HwpRemoteAuthorityInput authority;
	CompanionCommandCore::HwpGuidedLaunchValidationInput contract;
};

CompanionCommandCore::MoveValidationResult validateHwpGuidedLaunchExecutionPreflight(
	const HwpGuidedLaunchValidationInput &input);

const BattleUnit *findBattleUnitById(SavedBattleGame *battle, int unitId);

bool isOperationalFriendlyHwpForRemoteCommands(const BattleUnit *unit);

} // namespace CompanionTacticalHwpAuthorityCore

} // namespace OpenXcom