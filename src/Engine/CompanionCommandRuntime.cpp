/*
 * Open X-Com Companion — local command file polling and results (M1-19).
 */
#include "CompanionCommandRuntime.h"
#include "CompanionCommandCore.h"
#include "CompanionCommandRuntimeSupport.h"
#include "CompanionCommandJournalCore.h"
#include "CompanionJsonLite.h"
#include "CompanionTacticalAuthorityCore.h"
#include "CompanionBriefingTestSupport.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/Soldier.h"
#include "../Battlescape/BattlescapeGame.h"
#include "Logger.h"
#include "CrossPlatform.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <set>
#include <string>
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
std::string g_boundSessionId;
std::string g_boundTacticalBattleId;

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

void logKneelStartedOnce(const std::string &commandId, int actorUnitId)
{
	logCommandDiagnostic("KNEEL started command_id=" + commandId.substr(0, 8) + "... actor=" + std::to_string(actorUnitId));
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

std::string buildResultJson(const std::string &commandType,
	const std::string &commandId,
	const std::string &sessionId,
	int turn,
	int actorUnitId,
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
	     << "  \"command_id\": \"" << commandId << "\",\n"
	     << "  \"command_type\": \"" << commandType << "\",\n"
	     << "  \"status\": \"" << CompanionCommandCore::lifecycleStatusToString(status) << "\",\n"
	     << "  \"session_id\": \"" << sessionId << "\",\n"
	     << "  \"turn\": " << turn << ",\n"
	     << "  \"actor_unit_id\": " << actorUnitId << ",\n";
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

std::string buildMoveResultJson(const CompanionCommandCore::MoveCommandParsed &cmd,
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
	return buildResultJson(cmd.commandType, cmd.commandId, cmd.sessionId, cmd.turn, cmd.actorUnitId,
		status, publicReason, startPos, finalPos, remainingTu, kneeled, destinationReached,
		snapshotSha256, inboxSha256);
}

std::string buildKneelResultJson(const CompanionCommandCore::KneelCommandParsed &cmd,
	CompanionCommandCore::CommandLifecycleStatus status,
	const std::string &publicReason,
	Position pos,
	int remainingTu,
	bool kneeled,
	const std::string &snapshotSha256,
	const std::string &inboxSha256)
{
	return buildResultJson(cmd.commandType, cmd.commandId, cmd.sessionId, cmd.turn, cmd.actorUnitId,
		status, publicReason, pos, pos, remainingTu, kneeled, false, snapshotSha256, inboxSha256);
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
	CompanionCommandJournalCore::CommandJournalDocument doc = {};
	if (CompanionCommandJournalCore::parseCommandJournalJson(journalJson, &doc))
	{
		return CompanionCommandJournalCore::journalDocumentContainsCommandId(doc, commandId);
	}
	return journalJson.find("\"" + commandId + "\"") != std::string::npos;
}

void appendJournalEntry(const std::string &journalPath, const std::string &commandId)
{
	CompanionCommandJournalCore::appendCommandJournalEntry(journalPath, commandId);
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
	if (cmd.commandType.empty())
	{
		cmd.commandType = "MOVE";
	}
	const std::string json = buildMoveResultJson(cmd, CompanionCommandCore::CMD_REJECTED,
		publicReason.empty() ? reasonCode : publicReason, Position(0, 0, 0), Position(0, 0, 0), 0, false, false, std::string(), inboxSha256);
	writeResultFile(resultPath, json);
}

void rejectKneelCommand(const std::string &resultPath, const CompanionCommandCore::KneelCommandParsed &parsed,
	const std::string &reasonCode, const std::string &publicReason, const std::string &inboxSha256)
{
	CompanionCommandCore::KneelCommandParsed cmd = parsed;
	if (cmd.commandId.empty())
	{
		cmd.commandId = "unknown";
		cmd.commandType = "KNEEL";
		cmd.sessionId = "00000000000000000000000000000000";
		cmd.turn = 1;
	}
	if (cmd.commandType.empty())
	{
		cmd.commandType = "KNEEL";
	}
	const std::string json = buildKneelResultJson(cmd, CompanionCommandCore::CMD_REJECTED,
		publicReason.empty() ? reasonCode : publicReason, Position(0, 0, 0), 0, false, std::string(), inboxSha256);
	writeResultFile(resultPath, json);
}

bool applyDuplicateInboxDecision(
	CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision,
	const std::string &commandId,
	const std::string &inboxDigest,
	const std::string &resultPath,
	const std::string &conflictPath,
	const std::string &existingResultJson,
	const CompanionCommandCore::MoveCommandParsed *moveRejectTemplate,
	const CompanionCommandCore::KneelCommandParsed *kneelRejectTemplate)
{
	switch (duplicateDecision)
	{
	case CompanionCommandRuntimeSupport::DUPLICATE_INBOX_NO_OP:
		return true;
	case CompanionCommandRuntimeSupport::DUPLICATE_INBOX_CONFLICT_PRESERVE:
	{
		std::string priorStatus;
		std::string storedInboxDigest;
		CompanionCommandRuntimeSupport::parseTacticalResultBinding(
			existingResultJson, 0, &priorStatus, &storedInboxDigest);
		recordDuplicatePayloadConflict(
			conflictPath, commandId, priorStatus, inboxDigest, storedInboxDigest);
		logDuplicatePayloadConflictOnce(commandId);
		logRejectOnce(commandId, "DUPLICATE_COMMAND_ID");
		return true;
	}
	case CompanionCommandRuntimeSupport::DUPLICATE_INBOX_IDENTITY_MISMATCH:
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
			commandId,
			conflictKind,
			priorCommandId,
			priorStatus,
			inboxDigest);
		logDuplicateResultIdentityConflictOnce(commandId);
		return true;
	}
	case CompanionCommandRuntimeSupport::DUPLICATE_INBOX_REJECT:
		if (kneelRejectTemplate != 0)
		{
			rejectKneelCommand(resultPath, *kneelRejectTemplate, "DUPLICATE_COMMAND_ID", "Command ID already used.", inboxDigest);
		}
		else if (moveRejectTemplate != 0)
		{
			rejectCommand(resultPath, *moveRejectTemplate, "DUPLICATE_COMMAND_ID", "Command ID already used.", inboxDigest);
		}
		logRejectOnce(commandId, "DUPLICATE_COMMAND_ID");
		return true;
	default:
		return false;
	}
}

void fillBindingValidationInput(
	CompanionCommandCore::TacticalCommandBindingValidationInput *input,
	BattlescapeGame *game,
	SavedBattleGame *battle,
	bool battleInitialized,
	const std::string &sessionId,
	const std::string &commandSnapshotSha256,
	const std::string &commandSessionId,
	int commandTurn,
	int commandStateVersion,
	int actorUnitId,
	bool actorIsPlayerSoldier,
	bool actorAlive,
	bool actorAiControlled,
	bool humanOverrideRevoked)
{
	input->executorEnabled = true;
	input->playerTurn = battle->getSide() == FACTION_PLAYER;
	input->battleBusy = game->isBusy();
	input->battleInitialized = battleInitialized;
	input->debugMode = battle->getDebugMode();
	input->unitsFalling = battle->getUnitsFalling();
	input->sessionId = sessionId;
	input->turn = battle->getTurn();
	input->lastSnapshotSha256 = g_lastSnapshotSha256;
	input->commandSnapshotSha256 = commandSnapshotSha256;
	input->commandSessionId = commandSessionId;
	input->commandTurn = commandTurn;
	input->commandStateVersion = commandStateVersion;
	input->lastExportedStateVersion = g_lastExportedStateVersion;
	input->actorUnitId = actorUnitId;
	input->actorIsPlayerSoldier = actorIsPlayerSoldier;
	input->actorAlive = actorAlive;
	input->actorAiControlled = actorAiControlled;
	input->duplicateCommandId = false;
	input->priorCommandNonTerminal = g_active.inFlight;
	input->humanOverrideRevoked = humanOverrideRevoked;
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

bool isCompanionMoveActor(BattleUnit *actor, SavedGame *campaign)
{
	return CompanionTacticalAuthorityCore::isLiveCampaignPlayerSoldierUnit(actor, campaign);
}

CompanionTacticalAuthorityCore::TacticalAuthorityDecision tacticalAuthorityForActor(
	SavedGame *campaign,
	SavedBattleGame *battle,
	BattleUnit *actor,
	const std::string &controlJson,
	const std::string &sessionId)
{
	if (actor == 0)
	{
		return CompanionTacticalAuthorityCore::TacticalAuthorityDecision { false, false };
	}
	const bool liveActor = isCompanionMoveActor(actor, campaign);
	const bool contextPresent = (campaign != 0);
	std::set<int> emptyAiIds;
	const std::set<int> &aiIds = (battle != 0)
		? battle->getCompanionTacticalAiSoldierIds()
		: (contextPresent ? campaign->getCompanionCampaignAiSoldierIds() : emptyAiIds);
	const std::string relayCampaignId = contextPresent ? campaign->getCompanionCampaignId() : std::string();
	const std::string relayTacticalBattleId = battle != 0 ? battle->getCompanionTacticalBattleId() : std::string();
	const CompanionTacticalAuthorityCore::TacticalAuthorityInput input =
		CompanionTacticalAuthorityCore::buildTacticalAuthorityInput(
			contextPresent,
			aiIds,
			liveActor,
			actor->getId(),
			controlJson,
			sessionId,
			relayCampaignId,
			relayTacticalBattleId);
	return CompanionTacticalAuthorityCore::evaluateTacticalAuthority(input);
}

long long currentEpochMillis()
{
	return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count());
}

bool ensureBackupsDirectory(const std::string &snapshotDir)
{
	return CrossPlatform::createFolder(joinPath(snapshotDir, "backups"));
}

bool tryRenameInboxToBackups(
	const std::string &commandPath,
	const std::string &snapshotDir,
	const std::string &reasonToken,
	const std::string &commandId)
{
	if (!CrossPlatform::fileExists(commandPath))
	{
		return false;
	}
	if (!ensureBackupsDirectory(snapshotDir))
	{
		return false;
	}
	const std::string destName = CompanionCommandRuntimeSupport::buildArchivedInboxFileName(
		reasonToken, commandId, currentEpochMillis());
	const std::string destPath = joinPath(joinPath(snapshotDir, "backups"), destName.c_str());
	return CrossPlatform::moveFile(commandPath, destPath);
}

void interruptInFlightForBindingReset(const std::string &resultPath)
{
	if (!g_active.inFlight)
	{
		return;
	}
	const std::string json = buildMoveResultJson(
		g_active.parsed,
		CompanionCommandCore::CMD_INTERRUPTED,
		"Battle binding reset.",
		g_active.startPos,
		g_active.startPos,
		0,
		false,
		false,
		g_lastSnapshotSha256,
		g_active.inboxDigest);
	writeResultFile(resultPath, json);
	logCommandDiagnostic("in-flight command interrupted command_id=" + g_active.commandId.substr(0, 8) +
		"... reason=binding_reset");
	g_active.inFlight = false;
}

bool tryArchiveStaleInboxForExport(
	SavedBattleGame *battle,
	const CompanionCommandRuntimeSupport::SnapshotExportBinding &exportBinding)
{
	const std::string dir = snapshotDirectory();
	if (dir.empty() || battle == 0 || exportBinding.snapshotSha256.empty())
	{
		return false;
	}
	const std::string commandPath = joinPath(dir, "tactical-command.json");
	const std::string journalPath = joinPath(dir, "tactical-command-journal.json");
	std::string commandJson;
	if (!readTextFile(commandPath, &commandJson))
	{
		return false;
	}
	CompanionCommandRuntimeSupport::InboxCommandBinding inbox = {};
	if (!CompanionCommandRuntimeSupport::parseInboxCommandBinding(commandJson, &inbox))
	{
		return false;
	}
	std::string journalJson;
	readTextFile(journalPath, &journalJson);
	const bool journaled = journalContainsCommandId(journalJson, inbox.commandId);
	const CompanionCommandRuntimeSupport::StaleInboxArchiveReason reason =
		CompanionCommandRuntimeSupport::evaluateStaleInboxArchive(
			inbox,
			exportBinding,
			journaled,
			g_active.inFlight,
			g_active.commandId);
	if (reason == CompanionCommandRuntimeSupport::STALE_INBOX_KEEP)
	{
		return false;
	}
	const char *reasonToken = reason == CompanionCommandRuntimeSupport::STALE_INBOX_JOURNAL_DUPLICATE
		? "journaled"
		: "stale-binding";
	if (tryRenameInboxToBackups(commandPath, dir, reasonToken, inbox.commandId))
	{
		g_inboxCachedMtime = 0;
		logCommandDiagnostic("archived stale inbox command_id=" + inbox.commandId.substr(0, 8) +
			"... reason=" + reasonToken);
		return true;
	}
	return false;
}

void tryArchiveStaleInboxForSessionChange(SavedBattleGame *battle)
{
	const std::string dir = snapshotDirectory();
	if (dir.empty() || battle == 0)
	{
		return;
	}
	const std::string commandPath = joinPath(dir, "tactical-command.json");
	std::string commandJson;
	if (!readTextFile(commandPath, &commandJson))
	{
		return;
	}
	CompanionCommandRuntimeSupport::InboxCommandBinding inbox = {};
	if (!CompanionCommandRuntimeSupport::parseInboxCommandBinding(commandJson, &inbox))
	{
		return;
	}
	if (inbox.sessionId == battle->getCompanionEventLog().getSessionId())
	{
		return;
	}
	if (g_active.inFlight && g_active.commandId == inbox.commandId)
	{
		return;
	}
	if (tryRenameInboxToBackups(commandPath, dir, "stale-session", inbox.commandId))
	{
		g_inboxCachedMtime = 0;
		logCommandDiagnostic("archived inbox with stale session command_id=" + inbox.commandId.substr(0, 8) + "...");
	}
}

} // namespace

void onBattleCompanionBindingContext(SavedBattleGame *battle)
{
	if (battle == 0)
	{
		return;
	}
	const std::string sessionId = battle->getCompanionEventLog().getSessionId();
	const std::string tacticalId = battle->getCompanionTacticalBattleId();
	const bool hadPriorContext = !g_boundSessionId.empty() || !g_boundTacticalBattleId.empty();
	const bool contextChanged = hadPriorContext &&
		(sessionId != g_boundSessionId || tacticalId != g_boundTacticalBattleId);
	if (contextChanged)
	{
		const std::string dir = snapshotDirectory();
		if (!dir.empty())
		{
			interruptInFlightForBindingReset(joinPath(dir, "tactical-result.json"));
		}
		g_active.inFlight = false;
		g_inboxCachedMtime = 0;
		tryArchiveStaleInboxForSessionChange(battle);
	}
	if (!sessionId.empty())
	{
		g_boundSessionId = sessionId;
	}
	if (!tacticalId.empty())
	{
		g_boundTacticalBattleId = tacticalId;
	}
}

void archiveStaleInboxAfterSnapshotExport(SavedBattleGame *battle, const std::string &snapshotSha256LowerHex)
{
	if (battle == 0 || snapshotSha256LowerHex.empty())
	{
		return;
	}
	CompanionCommandRuntimeSupport::SnapshotExportBinding exportBinding = {};
	exportBinding.sessionId = battle->getCompanionEventLog().getSessionId();
	exportBinding.snapshotSha256 = snapshotSha256LowerHex;
	exportBinding.turn = battle->getTurn();
	exportBinding.stateVersion = battle->getCompanionEventLog().getLatestSeq();
	tryArchiveStaleInboxForExport(battle, exportBinding);
}

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

void tick(BattlescapeGame *game, SavedBattleGame *battle, SavedGame *campaign, bool battleInitialized)
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
	const std::string &sessionId = battle->getCompanionEventLog().getSessionId();

	if (g_active.inFlight)
	{
		BattleUnit *unit = findUnitById(battle, g_active.parsed.actorUnitId);
		const CompanionTacticalAuthorityCore::TacticalAuthorityDecision authority =
			tacticalAuthorityForActor(campaign, battle, unit, controlJson, sessionId);
		const bool aiCommandPermitted = authority.aiCommandPermitted;
		if (unit != 0 && !aiCommandPermitted)
		{
			game->cancelCurrentAction();
		}
		CompanionCommandCore::WalkCompletionInput done = {};
		done.requestedDest = Position(g_active.parsed.destX, g_active.parsed.destY, g_active.parsed.destZ);
		done.startPos = g_active.startPos;
		done.currentPos = unit != 0 ? unit->getPosition() : g_active.startPos;
		done.battleBusy = game->isBusy();
		done.actorAlive = unit != 0 && !unit->isOut();
		done.actorStillAiControlled = aiCommandPermitted;
		done.actionResult = std::string();

		const CompanionCommandCore::CommandLifecycleStatus terminal =
			CompanionCommandCore::evaluateWalkTerminalStatus(done);
		if (terminal == CompanionCommandCore::CMD_EXECUTING)
		{
			return;
		}

		const bool reached = unit != 0 && unit->getPosition() == done.requestedDest;
		const std::string json = buildMoveResultJson(
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

	std::string inboxCommandType;
	CompanionJsonLite::extractJsonStringField(commandJson, "command_type", &inboxCommandType);
	if (!inboxCommandType.empty() && inboxCommandType != "MOVE" && inboxCommandType != "KNEEL")
	{
		std::string inboxDigest;
		CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
		CompanionCommandCore::MoveCommandParsed unsupported = {};
		unsupported.rejectCode = "UNSUPPORTED_COMMAND_TYPE";
		CompanionJsonLite::extractJsonStringField(commandJson, "command_id", &unsupported.commandId);
		unsupported.commandType = inboxCommandType;
		rejectCommand(resultPath, unsupported, "UNSUPPORTED_COMMAND_TYPE", "Unsupported command type.", inboxDigest);
		logRejectOnce(unsupported.commandId.empty() ? "unknown" : unsupported.commandId, "UNSUPPORTED_COMMAND_TYPE");
		return;
	}

	if (inboxCommandType == "KNEEL")
	{
		CompanionCommandCore::KneelCommandParsed kneelParsed = {};
		if (!CompanionCommandCore::parseKneelCommandJson(commandJson, &kneelParsed))
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			rejectKneelCommand(resultPath, kneelParsed, kneelParsed.rejectCode, kneelParsed.rejectCode, inboxDigest);
			logRejectOnce(kneelParsed.commandId.empty() ? "unknown" : kneelParsed.commandId, kneelParsed.rejectCode);
			return;
		}

		logInboxDetectedOnce(kneelParsed.commandId);

		std::string inboxDigest;
		if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest))
		{
			return;
		}

		std::string journalJson;
		readTextFile(journalPath, &journalJson);
		const bool duplicate = journalContainsCommandId(journalJson, kneelParsed.commandId);

		std::string existingResultJson;
		readTextFile(resultPath, &existingResultJson);
		const CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision =
			CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
				duplicate, kneelParsed.commandId, inboxDigest, existingResultJson, g_active.inFlight);
		if (applyDuplicateInboxDecision(
			duplicateDecision, kneelParsed.commandId, inboxDigest, resultPath, conflictPath,
			existingResultJson, 0, &kneelParsed))
		{
			return;
		}

		BattleUnit *actor = findUnitById(battle, kneelParsed.actorUnitId);
		const CompanionTacticalAuthorityCore::TacticalAuthorityDecision authority =
			tacticalAuthorityForActor(campaign, battle, actor, controlJson, sessionId);
		const bool liveActor = isCompanionMoveActor(actor, campaign);
		const std::set<int> &tacticalAiIds = battle->getCompanionTacticalAiSoldierIds();
		const CompanionTacticalAuthorityCore::TacticalAuthorityInput authorityInput =
			CompanionTacticalAuthorityCore::buildTacticalAuthorityInput(
				campaign != 0,
				tacticalAiIds,
				liveActor,
				kneelParsed.actorUnitId,
				controlJson,
				sessionId,
				campaign != 0 ? campaign->getCompanionCampaignId() : std::string(),
				battle->getCompanionTacticalBattleId());

		CompanionCommandCore::KneelValidationInput kneelInput = {};
		fillBindingValidationInput(
			&kneelInput,
			game,
			battle,
			battleInitialized,
			sessionId,
			kneelParsed.snapshotSha256,
			kneelParsed.sessionId,
			kneelParsed.turn,
			kneelParsed.stateVersion,
			kneelParsed.actorUnitId,
			liveActor,
			actor != 0 && !actor->isOut(),
			authority.aiCommandPermitted,
			CompanionTacticalAuthorityCore::isStaleRelayAiAfterCampaignHumanAssignment(
				authorityInput, controlJson, sessionId));
		kneelInput.actorAlreadyKneeled = actor != 0 && actor->isKneeled();
		kneelInput.actorCanKneelPosture = actor != 0 && actor->getType() == "SOLDIER" && !actor->isFloating();
		kneelInput.insufficientTuForKneelDown = actor != 0 && kneelInput.actorCanKneelPosture &&
			!kneelInput.actorAlreadyKneeled && !game->checkReservedTU(actor, 4, true);

		const CompanionCommandCore::MoveValidationResult kneelVerdict =
			CompanionCommandCore::validateKneelCommand(kneelInput);
		if (!kneelVerdict.accepted)
		{
			rejectKneelCommand(resultPath, kneelParsed, kneelVerdict.rejectCode, kneelVerdict.publicReason, inboxDigest);
			appendJournalEntry(journalPath, kneelParsed.commandId);
			logRejectOnce(kneelParsed.commandId, kneelVerdict.rejectCode);
			return;
		}

		logCommandDiagnostic("accepted command_id=" + kneelParsed.commandId.substr(0, 8) +
			"... actor=" + std::to_string(kneelParsed.actorUnitId));

		const Position startPos = actor != 0 ? actor->getPosition() : Position(0, 0, 0);
		const int tuBefore = actor != 0 ? actor->getTimeUnits() : 0;
		std::string rejectReason;
		if (!game->tryExecuteCompanionKneelDown(actor, &rejectReason))
		{
			rejectKneelCommand(resultPath, kneelParsed, "KNEEL_START_FAILED", rejectReason, inboxDigest);
			appendJournalEntry(journalPath, kneelParsed.commandId);
			logRejectOnce(kneelParsed.commandId, "KNEEL_START_FAILED");
			return;
		}

		logKneelStartedOnce(kneelParsed.commandId, kneelParsed.actorUnitId);
		const int tuAfter = actor != 0 ? actor->getTimeUnits() : 0;
		if (tuBefore - tuAfter != 4)
		{
			logCommandDiagnostic("unexpected kneel TU delta command_id=" + kneelParsed.commandId.substr(0, 8) +
				"... before=" + std::to_string(tuBefore) + " after=" + std::to_string(tuAfter));
		}

		writeResultFile(resultPath, buildKneelResultJson(kneelParsed, CompanionCommandCore::CMD_COMPLETED, std::string(),
			startPos, tuAfter, actor != 0 && actor->isKneeled(), g_lastSnapshotSha256, inboxDigest));
		logTerminalOnce(kneelParsed.commandId, CompanionCommandCore::CMD_COMPLETED);
		appendJournalEntry(journalPath, kneelParsed.commandId);
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
	if (applyDuplicateInboxDecision(
		duplicateDecision, parsed.commandId, inboxDigest, resultPath, conflictPath,
		existingResultJson, &parsed, 0))
	{
		return;
	}

	BattleUnit *actor = findUnitById(battle, parsed.actorUnitId);
	const CompanionTacticalAuthorityCore::TacticalAuthorityDecision authority =
		tacticalAuthorityForActor(campaign, battle, actor, controlJson, sessionId);
	const bool liveActor = isCompanionMoveActor(actor, campaign);
	const std::set<int> &tacticalAiIds = battle->getCompanionTacticalAiSoldierIds();
	const CompanionTacticalAuthorityCore::TacticalAuthorityInput authorityInput =
		CompanionTacticalAuthorityCore::buildTacticalAuthorityInput(
			campaign != 0,
			tacticalAiIds,
			liveActor,
			parsed.actorUnitId,
			controlJson,
			sessionId,
			campaign != 0 ? campaign->getCompanionCampaignId() : std::string(),
			battle->getCompanionTacticalBattleId());

	CompanionCommandCore::MoveValidationInput input = {};
	fillBindingValidationInput(
		&input,
		game,
		battle,
		battleInitialized,
		sessionId,
		parsed.snapshotSha256,
		parsed.sessionId,
		parsed.turn,
		parsed.stateVersion,
		parsed.actorUnitId,
		liveActor,
		actor != 0 && !actor->isOut(),
		authority.aiCommandPermitted,
		CompanionTacticalAuthorityCore::isStaleRelayAiAfterCampaignHumanAssignment(
			authorityInput, controlJson, sessionId));
	input.destX = parsed.destX;
	input.destY = parsed.destY;
	input.destZ = parsed.destZ;
	input.mapSizeX = battle->getMapSizeX();
	input.mapSizeY = battle->getMapSizeY();
	input.mapSizeZ = battle->getMapSizeZ();

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

	writeResultFile(resultPath, buildMoveResultJson(parsed, CompanionCommandCore::CMD_EXECUTING, std::string(),
		actor->getPosition(), actor->getPosition(), actor->getTimeUnits(), actor->isKneeled(), false, g_lastSnapshotSha256, inboxDigest));
	appendJournalEntry(journalPath, parsed.commandId);
}

} // namespace CompanionCommandRuntime

} // namespace OpenXcom
