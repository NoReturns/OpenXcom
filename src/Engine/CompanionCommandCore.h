#pragma once
/*
 * Open X-Com Companion — tactical MOVE command validation (M1-19, testable core).
 */
#include "../Battlescape/Position.h"
#include <string>

namespace OpenXcom
{

namespace CompanionCommandCore
{

enum CommandLifecycleStatus
{
	CMD_REJECTED,
	CMD_ACCEPTED,
	CMD_EXECUTING,
	CMD_COMPLETED,
	CMD_INTERRUPTED,
	CMD_FAILED
};

struct MoveCommandParsed
{
	bool valid;
	std::string commandId;
	std::string commandType;
	int actorUnitId;
	int destX;
	int destY;
	int destZ;
	std::string snapshotSha256;
	std::string sessionId;
	int turn;
	int stateVersion;
	std::string rejectCode;
};

struct MoveValidationInput
{
	bool executorEnabled;
	bool playerTurn;
	bool battleBusy;
	bool battleInitialized;
	bool debugMode;
	bool unitsFalling;
	std::string sessionId;
	int turn;
	std::string lastSnapshotSha256;
	std::string commandSnapshotSha256;
	std::string commandSessionId;
	int commandTurn;
	int commandStateVersion;
	int lastExportedStateVersion;
	int actorUnitId;
	bool actorIsPlayerSoldier;
	bool actorAlive;
	bool actorAiControlled;
	int destX;
	int destY;
	int destZ;
	int mapSizeX;
	int mapSizeY;
	int mapSizeZ;
	bool duplicateCommandId;
	bool priorCommandNonTerminal;
	bool humanOverrideRevoked;
};

struct MoveValidationResult
{
	bool accepted;
	std::string rejectCode;
	std::string publicReason;
};

struct WalkCompletionInput
{
	Position requestedDest;
	Position startPos;
	Position currentPos;
	bool battleBusy;
	bool actorAlive;
	bool actorStillAiControlled;
	std::string actionResult;
};

MoveValidationResult validateMoveCommand(const MoveValidationInput &input);
CommandLifecycleStatus evaluateWalkTerminalStatus(const WalkCompletionInput &input);

bool parseMoveCommandJson(const std::string &json, MoveCommandParsed *out);
bool isValidCommandIdFormat(const std::string &commandId);
bool isValidSessionIdFormat(const std::string &sessionId);
bool isValidSnapshotSha256Format(const std::string &sha);

const char *lifecycleStatusToString(CommandLifecycleStatus status);

} // namespace CompanionCommandCore

} // namespace OpenXcom
