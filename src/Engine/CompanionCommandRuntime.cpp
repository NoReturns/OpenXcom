/*
 * Open X-Com Companion — local command file polling and results (M1-19).
 */
#include "CompanionCommandRuntime.h"
#include "CompanionCommandCore.h"
#include "CompanionCommandRuntimeSupport.h"
#include "CompanionSoldierOwnershipCore.h"
#include "CompanionBriefingTestSupport.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Battlescape/BattlescapeGame.h"
#include "Logger.h"
#include "CrossPlatform.h"
#include <cstdlib>
#include <chrono>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace OpenXcom
{

namespace CompanionCommandRuntime
{

namespace
{

const char *SNAPSHOT_ENV = "OPENXCOM_COMPANION_SNAPSHOT_PATH";

struct ActiveCommand
{
	bool inFlight;
	std::string commandId;
	std::string inboxDigest;
	CompanionCommandCore::MoveCommandParsed parsed;
	Position startPos;
};

ActiveCommand g_active = {};
std::string g_lastSnapshotSha256;
int g_lastExportedStateVersion = 0;

time_t g_inboxCachedMtime = 0;
std::chrono::steady_clock::time_point g_inboxLastPollTime = std::chrono::steady_clock::now();

bool g_loggedExecutorEnabled = false;
bool g_loggedExecutorDisabled = false;
bool g_loggedInboxWhileDisabled = false;
std::string g_lastLoggedRejectCommandId;
std::string g_lastLoggedRejectCode;
std::string g_lastLoggedDuplicateConflictCommandId;
std::string g_lastLoggedDuplicateIdentityConflictCommandId;
std::string g_lastLoggedTerminalCommandId;
CompanionCommandCore::CommandLifecycleStatus g_lastLoggedTerminalStatus =
	CompanionCommandCore::CMD_REJECTED;

bool readTextFile(const std::string &path, std::string *out)
{
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	if (!in)
	{
		return false;
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	*out = ss.str();
	return true;
}

bool writeTextFile(const std::string &path, const std::string &content)
{
	std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
	if (!out)
	{
		return false;
	}
	out << content;
	return static_cast<bool>(out);
}

bool commandFileMtime(const std::string &path, time_t *mtimeOut)
{
	if (mtimeOut == 0 || !CrossPlatform::fileExists(path))
	{
		return false;
	}
	struct stat info = {};
	if (stat(path.c_str(), &info) != 0)
	{
		return false;
	}
	*mtimeOut = info.st_mtime;
	return true;
}

bool commandFileExists(const std::string &path)
{
	return CrossPlatform::fileExists(path);
}

long msSince(const std::chrono::steady_clock::time_point &since)
{
	return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - since).count());
}

void logCommandDiagnostic(const std::string &message)
{
	Log(LOG_INFO) << "Companion command: " << message;
}

void logExecutorEnvironmentOnce()
{
	const bool enabled = CompanionSoldierOwnershipCore::isExecutorEnabledFromEnvironment();
	if (enabled && !g_loggedExecutorEnabled)
	{
		logCommandDiagnostic("executor enabled");
		g_loggedExecutorEnabled = true;
		g_loggedExecutorDisabled = false;
		g_loggedInboxWhileDisabled = false;
	}
	else if (!enabled && !g_loggedExecutorDisabled)
	{
		logCommandDiagnostic("executor disabled (OPENXCOM_COMPANION_COMMAND_EXECUTOR must be exactly 1)");
		g_loggedExecutorDisabled = true;
		g_loggedExecutorEnabled = false;
	}
}

void logInboxPresentWhileDisabledOnce(const std::string &commandPath)
{
	if (g_loggedInboxWhileDisabled || !commandFileExists(commandPath))
	{
		return;
	}
	logCommandDiagnostic("inbox file present while executor disabled");
	g_loggedInboxWhileDisabled = true;
}

void logRejectOnce(const std::string &commandId, const std::string &rejectCode)
{
	if (commandId == g_lastLoggedRejectCommandId && rejectCode == g_lastLoggedRejectCode)
	{
		return;
	}
	logCommandDiagnostic("rejected command_id=" + commandId.substr(0, 8) + "... code=" + rejectCode);
	g_lastLoggedRejectCommandId = commandId;
	g_lastLoggedRejectCode = rejectCode;
}

void logDuplicatePayloadConflictOnce(const std::string &commandId)
{
	if (commandId == g_lastLoggedDuplicateConflictCommandId)
	{
		return;
	}
	logCommandDiagnostic("duplicate inbox payload conflict command_id=" + commandId.substr(0, 8) +
		"... preserving prior tactical-result.json");
	g_lastLoggedDuplicateConflictCommandId = commandId;
}

void recordDuplicatePayloadConflict(
	const std::string &conflictPath,
	const std::string &commandId,
	const std::string &priorResultStatus,
	const std::string &observedInboxDigestHex,
	const std::string &storedInboxDigestHex)
{
	const std::string json = CompanionCommandRuntimeSupport::buildDuplicatePayloadConflictJson(
		commandId, priorResultStatus, observedInboxDigestHex, storedInboxDigestHex);
	writeTextFile(conflictPath, json);
}

void logDuplicateResultIdentityConflictOnce(const std::string &commandId)
{
	if (commandId == g_lastLoggedDuplicateIdentityConflictCommandId)
	{
		return;
	}
	logCommandDiagnostic("duplicate command result identity mismatch command_id=" + commandId.substr(0, 8) +
		"... preserving prior tactical-result.json");
	g_lastLoggedDuplicateIdentityConflictCommandId = commandId;
}

void recordDuplicateResultIdentityConflict(
	const std::string &conflictPath,
	const std::string &incomingCommandId,
	const std::string &conflictKind,
	const std::string &priorResultCommandId,
	const std::string &priorResultStatus,
	const std::string &observedInboxDigestHex)
{
	const std::string json = CompanionCommandRuntimeSupport::buildDuplicateResultIdentityConflictJson(
		incomingCommandId, conflictKind, priorResultCommandId, priorResultStatus, observedInboxDigestHex);
	writeTextFile(conflictPath, json);
}

void logMoveStartedOnce(const std::string &commandId, int actorUnitId)
{
	logCommandDiagnostic("MOVE started command_id=" + commandId.substr(0, 8) + "... actor=" + std::to_string(actorUnitId));
}

void logTerminalOnce(const std::string &commandId, CompanionCommandCore::CommandLifecycleStatus status)
{
	if (commandId == g_lastLoggedTerminalCommandId && status == g_lastLoggedTerminalStatus)
	{
		return;
	}
	logCommandDiagnostic("terminal command_id=" + commandId.substr(0, 8) + "... status=" +
		CompanionCommandCore::lifecycleStatusToString(status));
	g_lastLoggedTerminalCommandId = commandId;
	g_lastLoggedTerminalStatus = status;
}

void logInboxDetectedOnce(const std::string &commandId)
{
	static std::string lastInboxId;
	if (commandId == lastInboxId)
	{
		return;
	}
	logCommandDiagnostic("inbox command_id=" + commandId.substr(0, 8) + "...");
	lastInboxId = commandId;
}

std::string snapshotDirectory()
{
	const char *snapshotPath = std::getenv(SNAPSHOT_ENV);
	if (snapshotPath == 0 || snapshotPath[0] == '\0')
	{
		return std::string();
	}
	return CompanionBriefingTestSupport::parentDirectoryOfSnapshotPath(snapshotPath);
}

std::string joinPath(const std::string &dir, const char *name)
{
#ifdef _WIN32
	const char sep = '\\';
#else
	const char sep = '/';
#endif
	if (dir.empty())
	{
		return std::string(name);
	}
	return dir + sep + name;
}

std::string buildResultJson(const CompanionCommandCore::MoveCommandParsed &cmd,
	CompanionCommandCore::CommandLifecycleStatus status,
	const std::string &publicReason,
	Position startPos,
	Position finalPos,
	int remainingTu,
	bool kneeled,
	bool destinationReached,
	const std::string &snapshotSha256,
	const std::string &inboxSha256)
{
	std::ostringstream json;
	json << "{\n"
	     << "  \"schema_version\": 1,\n"
	     << "  \"command_id\": \"" << cmd.commandId << "\",\n"
	     << "  \"command_type\": \"MOVE\",\n"
	     << "  \"status\": \"" << CompanionCommandCore::lifecycleStatusToString(status) << "\",\n"
	     << "  \"session_id\": \"" << cmd.sessionId << "\",\n"
	     << "  \"turn\": " << cmd.turn << ",\n"
	     << "  \"actor_unit_id\": " << cmd.actorUnitId << ",\n";
	if (!publicReason.empty())
	{
		json << "  \"public_reason\": \"" << publicReason << "\",\n";
	}
	if (status == CompanionCommandCore::CMD_EXECUTING ||
	    status == CompanionCommandCore::CMD_COMPLETED ||
	    status == CompanionCommandCore::CMD_INTERRUPTED ||
	    status == CompanionCommandCore::CMD_FAILED)
	{
		json << "  \"starting_position\": { \"x\": " << startPos.x
		     << ", \"y\": " << startPos.y << ", \"z\": " << startPos.z << " },\n";
	}
	if (status == CompanionCommandCore::CMD_COMPLETED ||
	    status == CompanionCommandCore::CMD_INTERRUPTED ||
	    status == CompanionCommandCore::CMD_FAILED)
	{
		json << "  \"final_position\": { \"x\": " << finalPos.x
		     << ", \"y\": " << finalPos.y << ", \"z\": " << finalPos.z << " },\n"
	     << "  \"remaining_tu\": " << remainingTu << ",\n"
	     << "  \"kneeled\": " << (kneeled ? "true" : "false") << ",\n"
	     << "  \"destination_reached\": " << (destinationReached ? "true" : "false") << ",\n";
	}
	if (!snapshotSha256.empty())
	{
		json << "  \"snapshot_sha256\": \"" << snapshotSha256 << "\",\n";
	}
	if (!inboxSha256.empty())
	{
		json << "  \"inbox_sha256\": \"" << inboxSha256 << "\",\n";
	}
	json << "  \"completed_at\": \"engine-local\"\n"
	     << "}\n";
	return json.str();
}

BattleUnit *findUnitById(SavedBattleGame *battle, int unitId)
{
	if (battle == 0)
	{
		return 0;
	}
	for (std::vector<BattleUnit*>::iterator i = battle->getUnits()->begin(); i != battle->getUnits()->end(); ++i)
	{
		if ((*i)->getId() == unitId)
		{
			return *i;
		}
	}
	return 0;
}

bool journalContainsCommandId(const std::string &journalJson, const std::string &commandId)
{
	return journalJson.find("\"" + commandId + "\"") != std::string::npos;
}

void appendJournalEntry(const std::string &journalPath, const std::string &commandId)
{
	std::string existing;
	readTextFile(journalPath, &existing);
	if (journalContainsCommandId(existing, commandId))
	{
		return;
	}
	if (existing.empty())
	{
		existing = "{\n  \"schema_version\": 1,\n  \"command_ids\": [\n  ]\n}\n";
	}
	const std::string::size_type insertPos = existing.rfind(']');
	if (insertPos == std::string::npos)
	{
		return;
	}
	const std::string entry = existing.find("[\n  ]") != std::string::npos
		? "    \"" + commandId + "\"\n  "
		: "    \"" + commandId + "\",\n  ";
	existing.insert(insertPos, entry);
	writeTextFile(journalPath, existing);
}

void writeResultFile(const std::string &resultPath, const std::string &json)
{
	std::ofstream out(resultPath.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
	if (out)
	{
		out << json;
	}
}

void rejectCommand(const std::string &resultPath, const CompanionCommandCore::MoveCommandParsed &parsed,
	const std::string &reasonCode, const std::string &publicReason, const std::string &inboxSha256)
{
	CompanionCommandCore::MoveCommandParsed cmd = parsed;
	if (cmd.commandId.empty())
	{
		cmd.commandId = "unknown";
		cmd.commandType = "MOVE";
		cmd.sessionId = "00000000000000000000000000000000";
		cmd.turn = 1;
	}
	const std::string json = buildResultJson(cmd, CompanionCommandCore::CMD_REJECTED,
		publicReason.empty() ? reasonCode : publicReason, Position(0, 0, 0), Position(0, 0, 0), 0, false, false, std::string(), inboxSha256);
	writeResultFile(resultPath, json);
}

bool shouldReadCommandInboxNow(const std::string &commandPath)
{
	time_t mtime = 0;
	if (!commandFileMtime(commandPath, &mtime))
	{
		return false;
	}
	const bool poll = CompanionCommandRuntimeSupport::shouldPollCommandInboxFile(
		g_active.inFlight,
		mtime,
		g_inboxCachedMtime,
		msSince(g_inboxLastPollTime));
	if (poll)
	{
		g_inboxCachedMtime = mtime;
		g_inboxLastPollTime = std::chrono::steady_clock::now();
	}
	return poll;
}

} // namespace

void noteSnapshotExportSha256(const std::string &sha256LowerHex)
{
	g_lastSnapshotSha256 = sha256LowerHex;
}

void noteSnapshotExportStateVersion(int latestEventSeq)
{
	g_lastExportedStateVersion = latestEventSeq;
}

bool isMoveInFlight()
{
	return g_active.inFlight;
}

void tick(BattlescapeGame *game, SavedBattleGame *battle, bool battleInitialized)
{
	if (game == 0 || battle == 0)
	{
		return;
	}
	const std::string dir = snapshotDirectory();
	if (dir.empty())
	{
		return;
	}
	const std::string controlPath = joinPath(dir, "companion-soldier-control.json");
	const std::string commandPath = joinPath(dir, "tactical-command.json");
	const std::string resultPath = joinPath(dir, "tactical-result.json");
	const std::string conflictPath = joinPath(dir, "tactical-command-conflict.json");
	const std::string journalPath = joinPath(dir, "tactical-command-journal.json");

	std::string controlJson;
	readTextFile(controlPath, &controlJson);

	if (g_active.inFlight)
	{
		BattleUnit *unit = findUnitById(battle, g_active.parsed.actorUnitId);
		const bool aiControlled = unit != 0 &&
			CompanionSoldierOwnershipCore::isAiControlledUnit(controlJson, battle->getCompanionEventLog().getSessionId(), unit->getId());
		if (unit != 0 && !aiControlled)
		{
			game->cancelCurrentAction();
		}
		CompanionCommandCore::WalkCompletionInput done = {};
		done.requestedDest = Position(g_active.parsed.destX, g_active.parsed.destY, g_active.parsed.destZ);
		done.startPos = g_active.startPos;
		done.currentPos = unit != 0 ? unit->getPosition() : g_active.startPos;
		done.battleBusy = game->isBusy();
		done.actorAlive = unit != 0 && !unit->isOut();
		done.actorStillAiControlled = aiControlled;
		done.actionResult = std::string();

		const CompanionCommandCore::CommandLifecycleStatus terminal =
			CompanionCommandCore::evaluateWalkTerminalStatus(done);
		if (terminal == CompanionCommandCore::CMD_EXECUTING)
		{
			return;
		}

		const bool reached = unit != 0 && unit->getPosition() == done.requestedDest;
		const std::string json = buildResultJson(
			g_active.parsed,
			terminal,
			terminal == CompanionCommandCore::CMD_INTERRUPTED ? "Movement interrupted or incomplete." : std::string(),
			g_active.startPos,
			unit != 0 ? unit->getPosition() : done.startPos,
			unit != 0 ? unit->getTimeUnits() : 0,
			unit != 0 && unit->isKneeled(),
			reached,
			g_lastSnapshotSha256,
			g_active.inboxDigest);
		writeResultFile(resultPath, json);
		logTerminalOnce(g_active.commandId, terminal);
		g_active.inFlight = false;
		return;
	}

	logExecutorEnvironmentOnce();

	if (!CompanionSoldierOwnershipCore::isExecutorEnabledFromEnvironment())
	{
		logInboxPresentWhileDisabledOnce(commandPath);
		return;
	}

	if (!shouldReadCommandInboxNow(commandPath))
	{
		return;
	}

	std::string commandJson;
	if (!readTextFile(commandPath, &commandJson))
	{
		return;
	}

	CompanionCommandCore::MoveCommandParsed parsed = {};
	if (!CompanionCommandCore::parseMoveCommandJson(commandJson, &parsed))
	{
		std::string inboxDigest;
		CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
		rejectCommand(resultPath, parsed, parsed.rejectCode, parsed.rejectCode, inboxDigest);
		logRejectOnce(parsed.commandId.empty() ? "unknown" : parsed.commandId, parsed.rejectCode);
		return;
	}

	logInboxDetectedOnce(parsed.commandId);

	std::string inboxDigest;
	if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest))
	{
		return;
	}

	std::string journalJson;
	readTextFile(journalPath, &journalJson);
	const bool duplicate = journalContainsCommandId(journalJson, parsed.commandId);

	std::string existingResultJson;
	readTextFile(resultPath, &existingResultJson);
	const CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision =
		CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
			duplicate, parsed.commandId, inboxDigest, existingResultJson, g_active.inFlight);
	if (duplicateDecision == CompanionCommandRuntimeSupport::DUPLICATE_INBOX_NO_OP)
	{
		return;
	}
	if (duplicateDecision == CompanionCommandRuntimeSupport::DUPLICATE_INBOX_CONFLICT_PRESERVE)
	{
		std::string priorStatus;
		std::string storedInboxDigest;
		CompanionCommandRuntimeSupport::parseTacticalResultBinding(
			existingResultJson, 0, &priorStatus, &storedInboxDigest);
		recordDuplicatePayloadConflict(
			conflictPath, parsed.commandId, priorStatus, inboxDigest, storedInboxDigest);
		logDuplicatePayloadConflictOnce(parsed.commandId);
		logRejectOnce(parsed.commandId, "DUPLICATE_COMMAND_ID");
		return;
	}
	if (duplicateDecision == CompanionCommandRuntimeSupport::DUPLICATE_INBOX_IDENTITY_MISMATCH)
	{
		std::string priorCommandId;
		std::string priorStatus;
		const bool parsedPrior = CompanionCommandRuntimeSupport::parseTacticalResultBinding(
			existingResultJson, &priorCommandId, &priorStatus, 0);
		const std::string conflictKind = !parsedPrior
			? "result_binding_unparseable"
			: "result_command_id_mismatch";
		recordDuplicateResultIdentityConflict(
			conflictPath,
			parsed.commandId,
			conflictKind,
			priorCommandId,
			priorStatus,
			inboxDigest);
		logDuplicateResultIdentityConflictOnce(parsed.commandId);
		return;
	}
	if (duplicateDecision == CompanionCommandRuntimeSupport::DUPLICATE_INBOX_REJECT)
	{
		rejectCommand(resultPath, parsed, "DUPLICATE_COMMAND_ID", "Command ID already used.", inboxDigest);
		logRejectOnce(parsed.commandId, "DUPLICATE_COMMAND_ID");
		return;
	}

	BattleUnit *actor = findUnitById(battle, parsed.actorUnitId);
	CompanionCommandCore::MoveValidationInput input = {};
	input.executorEnabled = true;
	input.playerTurn = battle->getSide() == FACTION_PLAYER;
	input.battleBusy = game->isBusy();
	input.battleInitialized = battleInitialized;
	input.debugMode = battle->getDebugMode();
	input.unitsFalling = battle->getUnitsFalling();
	input.sessionId = battle->getCompanionEventLog().getSessionId();
	input.turn = battle->getTurn();
	input.lastSnapshotSha256 = g_lastSnapshotSha256;
	input.commandSnapshotSha256 = parsed.snapshotSha256;
	input.commandSessionId = parsed.sessionId;
	input.commandTurn = parsed.turn;
	input.commandStateVersion = parsed.stateVersion;
	input.lastExportedStateVersion = g_lastExportedStateVersion;
	input.actorUnitId = parsed.actorUnitId;
	input.actorIsPlayerSoldier = actor != 0 && actor->getOriginalFaction() == FACTION_PLAYER &&
		actor->getFaction() == FACTION_PLAYER && actor->getGeoscapeSoldier() != 0;
	input.actorAlive = actor != 0 && !actor->isOut();
	input.actorAiControlled = actor != 0 &&
		CompanionSoldierOwnershipCore::isAiControlledUnit(controlJson, input.sessionId, actor->getId());
	input.destX = parsed.destX;
	input.destY = parsed.destY;
	input.destZ = parsed.destZ;
	input.mapSizeX = battle->getMapSizeX();
	input.mapSizeY = battle->getMapSizeY();
	input.mapSizeZ = battle->getMapSizeZ();
	input.duplicateCommandId = false;
	input.priorCommandNonTerminal = g_active.inFlight;
	input.humanOverrideRevoked = false;

	const CompanionCommandCore::MoveValidationResult verdict = CompanionCommandCore::validateMoveCommand(input);
	if (!verdict.accepted)
	{
		rejectCommand(resultPath, parsed, verdict.rejectCode, verdict.publicReason, inboxDigest);
		appendJournalEntry(journalPath, parsed.commandId);
		logRejectOnce(parsed.commandId, verdict.rejectCode);
		return;
	}

	logCommandDiagnostic("accepted command_id=" + parsed.commandId.substr(0, 8) + "... actor=" + std::to_string(parsed.actorUnitId));

	g_active.inFlight = true;
	g_active.commandId = parsed.commandId;
	g_active.inboxDigest = inboxDigest;
	g_active.parsed = parsed;

	std::string rejectReason;
	const Position dest(parsed.destX, parsed.destY, parsed.destZ);
	if (!game->tryExecuteCompanionMove(actor, dest, &rejectReason))
	{
		g_active.inFlight = false;
		rejectCommand(resultPath, parsed, "MOVE_START_FAILED", rejectReason, inboxDigest);
		appendJournalEntry(journalPath, parsed.commandId);
		logRejectOnce(parsed.commandId, "MOVE_START_FAILED");
		return;
	}

	g_active.startPos = actor->getPosition();
	logMoveStartedOnce(parsed.commandId, parsed.actorUnitId);

	writeResultFile(resultPath, buildResultJson(parsed, CompanionCommandCore::CMD_EXECUTING, std::string(),
		actor->getPosition(), actor->getPosition(), actor->getTimeUnits(), actor->isKneeled(), false, g_lastSnapshotSha256, inboxDigest));
	appendJournalEntry(journalPath, parsed.commandId);
}

} // namespace CompanionCommandRuntime

} // namespace OpenXcom
