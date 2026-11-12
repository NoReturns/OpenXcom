/*
 * Open X-Com Companion — local command file polling and results (M1-19).
 */
#include "CompanionCommandRuntime.h"
#include "CompanionCommandCore.h"
#include "CompanionCommandRuntimeSupport.h"
#include "CompanionDropGroundPlacementCore.h"
#include "CompanionRelayPublicationFreshnessCore.h"
#include "CompanionCommandJournalCore.h"
#include "CompanionJsonLite.h"
#include "CompanionCampaignOwnershipCore.h"
#include "CompanionTacticalAuthorityCore.h"
#include "CompanionSnapshot.h"
#include "CompanionCommandCasualtySettlement.h"
#include "CompanionCommandCasualtySettlementCore.h"
#include "CompanionSoldierOwnershipCore.h"
#include "CompanionBriefingTestSupport.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/BattleUnit.h"
#include "../Savegame/BattleItem.h"
#include "../Savegame/Tile.h"
#include <vector>
#include "../Mod/RuleItem.h"
#include "../Mod/RuleInventory.h"
#include "../Savegame/Soldier.h"
#include "../Battlescape/BattlescapeGame.h"
#include "../Battlescape/ProjectileFlyBState.h"
#include "CompanionGroundInventoryCore.h"
#include "Logger.h"
#include <map>
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
		enum Kind
		{
			KIND_NONE,
			KIND_MOVE,
			KIND_SNAP_SHOT,
			KIND_AUTO_SHOT,
			KIND_AIMED_SHOT,
			KIND_THROW
		};

		bool inFlight;
		Kind kind;
	std::string commandId;
	std::string inboxDigest;
	CompanionCommandCore::MoveCommandParsed moveParsed;
	CompanionCommandCore::SnapShotCommandParsed snapParsed;
	CompanionCommandCore::ThrowCommandParsed throwParsed;
	Position startPos;
	bool snapProjectileLaunched;
	std::string snapActionResult;
	std::string walkActionResult;
	long long rosterCasualtyDeferStartedAtMs;
};

ActiveCommand g_active = {};
int g_commandTickDepth = 0;

static void clearRosterCasualtyDeferClock();
static void beginInFlightCommand();
static bool shouldDelayInFlightTerminalCompletion(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	CompanionCommandCore::CommandLifecycleStatus terminal);
std::string g_lastSnapshotSha256;

namespace
{

struct CommandTickDepthGuard
{
	CommandTickDepthGuard() { ++g_commandTickDepth; }
	~CommandTickDepthGuard() { --g_commandTickDepth; }
};

} // namespace

static bool isDirectedFireKind(ActiveCommand::Kind kind)
{
	return kind == ActiveCommand::KIND_SNAP_SHOT ||
	       kind == ActiveCommand::KIND_AUTO_SHOT ||
	       kind == ActiveCommand::KIND_AIMED_SHOT;
}

static bool isThrowKind(ActiveCommand::Kind kind)
{
	return kind == ActiveCommand::KIND_THROW;
}

static bool isAsyncProjectileKind(ActiveCommand::Kind kind)
{
	return isDirectedFireKind(kind) || isThrowKind(kind);
}

static BattleActionType battleActionTypeForDirectedFireKind(ActiveCommand::Kind kind)
{
	switch (kind)
	{
	case ActiveCommand::KIND_AUTO_SHOT: return BA_AUTOSHOT;
	case ActiveCommand::KIND_AIMED_SHOT: return BA_AIMEDSHOT;
	case ActiveCommand::KIND_SNAP_SHOT:
	default: return BA_SNAPSHOT;
	}
}

static ActiveCommand::Kind directedFireKindForCommandType(const std::string &commandType)
{
	if (commandType == "AUTO_SHOT")
	{
		return ActiveCommand::KIND_AUTO_SHOT;
	}
	if (commandType == "AIMED_SHOT")
	{
		return ActiveCommand::KIND_AIMED_SHOT;
	}
	return ActiveCommand::KIND_SNAP_SHOT;
}
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

static void writeResultFile(const std::string &resultPath, const std::string &json);

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

void logStandStartedOnce(const std::string &commandId, int actorUnitId)
{
	logCommandDiagnostic("STAND started command_id=" + commandId.substr(0, 8) + "... actor=" + std::to_string(actorUnitId));
}

void logKneelStartedOnce(const std::string &commandId, int actorUnitId)
{
	logCommandDiagnostic("KNEEL started command_id=" + commandId.substr(0, 8) + "... actor=" + std::to_string(actorUnitId));
}

void logSnapShotStartedOnce(const std::string &commandId, int actorUnitId)
{
	logCommandDiagnostic("SNAP_SHOT started command_id=" + commandId.substr(0, 8) + "... actor=" + std::to_string(actorUnitId));
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

static std::string insertMoveDiagnosticsFields(
	const std::string &baseJson,
	const CompanionCommandCore::WalkStopDiagnostics &diag)
{
	if (diag.movementStopReason.empty())
	{
		return baseJson;
	}
	const std::string::size_type insertAt = baseJson.rfind("  \"completed_at\"");
	if (insertAt == std::string::npos)
	{
		return baseJson;
	}
	std::ostringstream extra;
	extra << "  \"movement_stop_reason\": \"" << diag.movementStopReason << "\",\n";
	if (!diag.engineActionResult.empty())
	{
		extra << "  \"engine_action_result\": \"" << diag.engineActionResult << "\",\n";
	}
	return baseJson.substr(0, insertAt) + extra.str() + baseJson.substr(insertAt);
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
	const std::string &inboxSha256,
	const CompanionCommandCore::WalkStopDiagnostics *stopDiagnostics = 0)
{
	const std::string base = buildResultJson(cmd.commandType, cmd.commandId, cmd.sessionId, cmd.turn, cmd.actorUnitId,
		status, publicReason, startPos, finalPos, remainingTu, kneeled, destinationReached,
		snapshotSha256, inboxSha256);
	if (stopDiagnostics == 0)
	{
		return base;
	}
	return insertMoveDiagnosticsFields(base, *stopDiagnostics);
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

std::string buildSnapShotResultJson(const CompanionCommandCore::SnapShotCommandParsed &cmd,
	CompanionCommandCore::CommandLifecycleStatus status,
	const std::string &publicReason,
	Position startPos,
	Position finalPos,
	int remainingTu,
	bool kneeled,
	const std::string &snapshotSha256,
	const std::string &inboxSha256)
{
	return buildResultJson(cmd.commandType, cmd.commandId, cmd.sessionId, cmd.turn, cmd.actorUnitId,
		status, publicReason, startPos, finalPos, remainingTu, kneeled, false, snapshotSha256, inboxSha256);
}

std::string buildPrimeResultJson(const CompanionCommandCore::PrimeCommandParsed &cmd,
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

std::string buildThrowResultJson(const CompanionCommandCore::ThrowCommandParsed &cmd,
	CompanionCommandCore::CommandLifecycleStatus status,
	const std::string &publicReason,
	Position startPos,
	Position finalPos,
	int remainingTu,
	bool kneeled,
	const std::string &snapshotSha256,
	const std::string &inboxSha256)
{
	return buildResultJson(cmd.commandType, cmd.commandId, cmd.sessionId, cmd.turn, cmd.actorUnitId,
		status, publicReason, startPos, finalPos, remainingTu, kneeled, false, snapshotSha256, inboxSha256);
}

std::string buildReloadResultJson(const CompanionCommandCore::ReloadCommandParsed &cmd,
	CompanionCommandCore::CommandLifecycleStatus status,
	const std::string &publicReason,
	Position pos,
	int remainingTu,
	bool kneeled,
	const std::string &snapshotSha256,
	const std::string &inboxSha256,
	int tuSpent)
{
	std::ostringstream json;
	json << "{\n"
	     << "  \"schema_version\": 1,\n"
	     << "  \"command_id\": \"" << cmd.commandId << "\",\n"
	     << "  \"command_type\": \"" << cmd.commandType << "\",\n"
	     << "  \"status\": \"" << CompanionCommandCore::lifecycleStatusToString(status) << "\",\n"
	     << "  \"session_id\": \"" << cmd.sessionId << "\",\n"
	     << "  \"turn\": " << cmd.turn << ",\n"
	     << "  \"actor_unit_id\": " << cmd.actorUnitId << ",\n"
	     << "  \"weapon_battle_item_id\": " << cmd.weaponBattleItemId << ",\n"
	     << "  \"ammo_battle_item_id\": " << cmd.ammoBattleItemId << ",\n";
	if (!publicReason.empty())
	{
		json << "  \"public_reason\": \"" << publicReason << "\",\n";
	}
	if (status == CompanionCommandCore::CMD_EXECUTING ||
	    status == CompanionCommandCore::CMD_COMPLETED ||
	    status == CompanionCommandCore::CMD_INTERRUPTED ||
	    status == CompanionCommandCore::CMD_FAILED)
	{
		json << "  \"starting_position\": { \"x\": " << pos.x
		     << ", \"y\": " << pos.y << ", \"z\": " << pos.z << " },\n";
	}
	if (status == CompanionCommandCore::CMD_COMPLETED ||
	    status == CompanionCommandCore::CMD_INTERRUPTED ||
	    status == CompanionCommandCore::CMD_FAILED)
	{
		json << "  \"final_position\": { \"x\": " << pos.x
		     << ", \"y\": " << pos.y << ", \"z\": " << pos.z << " },\n"
		     << "  \"remaining_tu\": " << remainingTu << ",\n"
		     << "  \"kneeled\": " << (kneeled ? "true" : "false") << ",\n"
		     << "  \"destination_reached\": false,\n";
	}
	if (status == CompanionCommandCore::CMD_COMPLETED && tuSpent >= 0)
	{
		json << "  \"tu_spent\": " << tuSpent << ",\n";
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

std::string buildUnloadResultJson(const CompanionCommandCore::UnloadCommandParsed &cmd,
	CompanionCommandCore::CommandLifecycleStatus status,
	const std::string &publicReason,
	Position pos,
	int remainingTu,
	bool kneeled,
	const std::string &snapshotSha256,
	const std::string &inboxSha256,
	int tuSpent)
{
	std::ostringstream json;
	json << "{\n"
	     << "  \"schema_version\": 1,\n"
	     << "  \"command_id\": \"" << cmd.commandId << "\",\n"
	     << "  \"command_type\": \"" << cmd.commandType << "\",\n"
	     << "  \"status\": \"" << CompanionCommandCore::lifecycleStatusToString(status) << "\",\n"
	     << "  \"session_id\": \"" << cmd.sessionId << "\",\n"
	     << "  \"turn\": " << cmd.turn << ",\n"
	     << "  \"actor_unit_id\": " << cmd.actorUnitId << ",\n"
	     << "  \"weapon_battle_item_id\": " << cmd.weaponBattleItemId << ",\n"
	     << "  \"loaded_ammo_battle_item_id\": " << cmd.loadedAmmoBattleItemId << ",\n";
	if (!publicReason.empty())
	{
		json << "  \"public_reason\": \"" << publicReason << "\",\n";
	}
	if (status == CompanionCommandCore::CMD_EXECUTING ||
	    status == CompanionCommandCore::CMD_COMPLETED ||
	    status == CompanionCommandCore::CMD_INTERRUPTED ||
	    status == CompanionCommandCore::CMD_FAILED)
	{
		json << "  \"starting_position\": { \"x\": " << pos.x
		     << ", \"y\": " << pos.y << ", \"z\": " << pos.z << " },\n";
	}
	if (status == CompanionCommandCore::CMD_COMPLETED ||
	    status == CompanionCommandCore::CMD_INTERRUPTED ||
	    status == CompanionCommandCore::CMD_FAILED)
	{
		json << "  \"final_position\": { \"x\": " << pos.x
		     << ", \"y\": " << pos.y << ", \"z\": " << pos.z << " },\n"
		     << "  \"remaining_tu\": " << remainingTu << ",\n"
		     << "  \"kneeled\": " << (kneeled ? "true" : "false") << ",\n"
		     << "  \"destination_reached\": false,\n";
	}
	if (status == CompanionCommandCore::CMD_COMPLETED && tuSpent >= 0)
	{
		json << "  \"tu_spent\": " << tuSpent << ",\n";
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

std::string buildInventoryMoveResultJson(const CompanionCommandCore::InventoryMoveCommandParsed &cmd,
	CompanionCommandCore::CommandLifecycleStatus status,
	const std::string &publicReason,
	Position pos,
	int remainingTu,
	bool kneeled,
	const std::string &snapshotSha256,
	const std::string &inboxSha256,
	int tuSpent)
{
	std::ostringstream json;
	json << "{\n"
	     << "  \"schema_version\": 1,\n"
	     << "  \"command_id\": \"" << cmd.commandId << "\",\n"
	     << "  \"command_type\": \"" << cmd.commandType << "\",\n"
	     << "  \"status\": \"" << CompanionCommandCore::lifecycleStatusToString(status) << "\",\n"
	     << "  \"session_id\": \"" << cmd.sessionId << "\",\n"
	     << "  \"turn\": " << cmd.turn << ",\n"
	     << "  \"actor_unit_id\": " << cmd.actorUnitId << ",\n"
	     << "  \"item_battle_item_id\": " << cmd.itemBattleItemId << ",\n"
	     << "  \"source_slot\": \"" << cmd.sourceSlot << "\",\n"
	     << "  \"destination_slot\": \"" << cmd.destinationSlot << "\",\n"
	     << "  \"destination_x\": " << cmd.destinationX << ",\n"
	     << "  \"destination_y\": " << cmd.destinationY << ",\n";
	if (!publicReason.empty())
	{
		json << "  \"public_reason\": \"" << publicReason << "\",\n";
	}
	if (status == CompanionCommandCore::CMD_EXECUTING ||
	    status == CompanionCommandCore::CMD_COMPLETED ||
	    status == CompanionCommandCore::CMD_INTERRUPTED ||
	    status == CompanionCommandCore::CMD_FAILED)
	{
		json << "  \"starting_position\": { \"x\": " << pos.x
		     << ", \"y\": " << pos.y << ", \"z\": " << pos.z << " },\n";
	}
	if (status == CompanionCommandCore::CMD_COMPLETED ||
	    status == CompanionCommandCore::CMD_INTERRUPTED ||
	    status == CompanionCommandCore::CMD_FAILED)
	{
		json << "  \"final_position\": { \"x\": " << pos.x
		     << ", \"y\": " << pos.y << ", \"z\": " << pos.z << " },\n"
		     << "  \"remaining_tu\": " << remainingTu << ",\n"
		     << "  \"kneeled\": " << (kneeled ? "true" : "false") << ",\n"
		     << "  \"destination_reached\": false,\n";
	}
	if (status == CompanionCommandCore::CMD_COMPLETED && tuSpent >= 0)
	{
		json << "  \"tu_spent\": " << tuSpent << ",\n";
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

std::string buildPickupResultJson(const CompanionCommandCore::PickupCommandParsed &cmd,
	CompanionCommandCore::CommandLifecycleStatus status,
	const std::string &publicReason,
	Position pos,
	int remainingTu,
	bool kneeled,
	const std::string &snapshotSha256,
	const std::string &inboxSha256,
	int tuSpent)
{
	std::ostringstream json;
	json << "{\n"
	     << "  \"schema_version\": 1,\n"
	     << "  \"command_id\": \"" << cmd.commandId << "\",\n"
	     << "  \"command_type\": \"" << cmd.commandType << "\",\n"
	     << "  \"status\": \"" << CompanionCommandCore::lifecycleStatusToString(status) << "\",\n"
	     << "  \"session_id\": \"" << cmd.sessionId << "\",\n"
	     << "  \"turn\": " << cmd.turn << ",\n"
	     << "  \"actor_unit_id\": " << cmd.actorUnitId << ",\n"
	     << "  \"item_battle_item_id\": " << cmd.itemBattleItemId << ",\n"
	     << "  \"ground_position\": { \"x\": " << cmd.groundX
	     << ", \"y\": " << cmd.groundY << ", \"z\": " << cmd.groundZ << " },\n"
	     << "  \"destination_slot\": \"" << cmd.destinationSlot << "\",\n"
	     << "  \"destination_x\": " << cmd.destinationX << ",\n"
	     << "  \"destination_y\": " << cmd.destinationY << ",\n";
	if (!publicReason.empty())
	{
		json << "  \"public_reason\": \"" << publicReason << "\",\n";
	}
	if (status == CompanionCommandCore::CMD_EXECUTING ||
	    status == CompanionCommandCore::CMD_COMPLETED ||
	    status == CompanionCommandCore::CMD_INTERRUPTED ||
	    status == CompanionCommandCore::CMD_FAILED)
	{
		json << "  \"starting_position\": { \"x\": " << pos.x
		     << ", \"y\": " << pos.y << ", \"z\": " << pos.z << " },\n";
	}
	if (status == CompanionCommandCore::CMD_COMPLETED ||
	    status == CompanionCommandCore::CMD_INTERRUPTED ||
	    status == CompanionCommandCore::CMD_FAILED)
	{
		json << "  \"final_position\": { \"x\": " << pos.x
		     << ", \"y\": " << pos.y << ", \"z\": " << pos.z << " },\n"
		     << "  \"remaining_tu\": " << remainingTu << ",\n"
		     << "  \"kneeled\": " << (kneeled ? "true" : "false") << ",\n"
		     << "  \"destination_reached\": false,\n";
	}
	if (status == CompanionCommandCore::CMD_COMPLETED && tuSpent >= 0)
	{
		json << "  \"tu_spent\": " << tuSpent << ",\n";
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

std::string buildDropResultJson(const CompanionCommandCore::DropCommandParsed &cmd,
	CompanionCommandCore::CommandLifecycleStatus status,
	const std::string &publicReason,
	Position pos,
	int remainingTu,
	bool kneeled,
	const std::string &snapshotSha256,
	const std::string &inboxSha256,
	int tuSpent,
	const CompanionDropGroundPlacementCore::GroundPlacementEvidence *groundPlacement)
{
	std::ostringstream json;
	json << "{\n"
	     << "  \"schema_version\": 1,\n"
	     << "  \"command_id\": \"" << cmd.commandId << "\",\n"
	     << "  \"command_type\": \"" << cmd.commandType << "\",\n"
	     << "  \"status\": \"" << CompanionCommandCore::lifecycleStatusToString(status) << "\",\n"
	     << "  \"session_id\": \"" << cmd.sessionId << "\",\n"
	     << "  \"turn\": " << cmd.turn << ",\n"
	     << "  \"actor_unit_id\": " << cmd.actorUnitId << ",\n"
	     << "  \"item_battle_item_id\": " << cmd.itemBattleItemId << ",\n"
	     << "  \"source_slot\": \"" << cmd.sourceSlot << "\",\n";
	if (cmd.sourceCoordsGuard)
	{
		json << "  \"source_x\": " << cmd.sourceX << ",\n"
		     << "  \"source_y\": " << cmd.sourceY << ",\n";
	}
	if (!publicReason.empty())
	{
		json << "  \"public_reason\": \"" << publicReason << "\",\n";
	}
	if (status == CompanionCommandCore::CMD_EXECUTING ||
	    status == CompanionCommandCore::CMD_COMPLETED ||
	    status == CompanionCommandCore::CMD_INTERRUPTED ||
	    status == CompanionCommandCore::CMD_FAILED)
	{
		json << "  \"starting_position\": { \"x\": " << pos.x
		     << ", \"y\": " << pos.y << ", \"z\": " << pos.z << " },\n";
	}
	if (status == CompanionCommandCore::CMD_COMPLETED ||
	    status == CompanionCommandCore::CMD_INTERRUPTED ||
	    status == CompanionCommandCore::CMD_FAILED)
	{
		json << "  \"final_position\": { \"x\": " << pos.x
		     << ", \"y\": " << pos.y << ", \"z\": " << pos.z << " },\n"
		     << "  \"remaining_tu\": " << remainingTu << ",\n"
		     << "  \"kneeled\": " << (kneeled ? "true" : "false") << ",\n"
		     << "  \"destination_reached\": false,\n";
	}
	if (status == CompanionCommandCore::CMD_COMPLETED && tuSpent >= 0)
	{
		json << "  \"tu_spent\": " << tuSpent << ",\n";
	}
	if (status == CompanionCommandCore::CMD_COMPLETED
	    && groundPlacement != 0 && groundPlacement->present)
	{
		CompanionDropGroundPlacementCore::appendGroundPlacementJson(json, *groundPlacement);
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

void rejectReloadCommand(const std::string &resultPath, const CompanionCommandCore::ReloadCommandParsed &parsed,
	const std::string &reasonCode, const std::string &publicReason, const std::string &inboxSha256)
{
	CompanionCommandCore::ReloadCommandParsed cmd = parsed;
	if (cmd.commandId.empty())
	{
		cmd.commandId = "unknown";
		cmd.commandType = "RELOAD";
		cmd.sessionId = "00000000000000000000000000000000";
		cmd.turn = 1;
		cmd.weaponBattleItemId = 0;
		cmd.ammoBattleItemId = 0;
	}
	if (cmd.commandType.empty())
	{
		cmd.commandType = "RELOAD";
	}
	const std::string json = buildReloadResultJson(cmd, CompanionCommandCore::CMD_REJECTED,
		publicReason.empty() ? reasonCode : publicReason, Position(0, 0, 0), 0, false,
		std::string(), inboxSha256, -1);
	writeResultFile(resultPath, json);
}

void rejectUnloadCommand(const std::string &resultPath, const CompanionCommandCore::UnloadCommandParsed &parsed,
	const std::string &reasonCode, const std::string &publicReason, const std::string &inboxSha256)
{
	CompanionCommandCore::UnloadCommandParsed cmd = parsed;
	if (cmd.commandId.empty())
	{
		cmd.commandId = "unknown";
		cmd.commandType = "UNLOAD";
		cmd.sessionId = "00000000000000000000000000000000";
		cmd.turn = 1;
		cmd.weaponBattleItemId = 0;
		cmd.loadedAmmoBattleItemId = 0;
	}
	if (cmd.commandType.empty())
	{
		cmd.commandType = "UNLOAD";
	}
	const std::string json = buildUnloadResultJson(cmd, CompanionCommandCore::CMD_REJECTED,
		publicReason.empty() ? reasonCode : publicReason, Position(0, 0, 0), 0, false,
		std::string(), inboxSha256, -1);
	writeResultFile(resultPath, json);
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
	return CompanionCommandJournalCore::journalJsonContainsCommandId(journalJson, commandId);
}

void appendJournalEntry(const std::string &journalPath, const std::string &commandId)
{
	CompanionCommandJournalCore::appendCommandJournalEntry(journalPath, commandId);
}

static void writeResultFile(const std::string &resultPath, const std::string &json)
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

void rejectStandCommand(const std::string &resultPath, const CompanionCommandCore::StandCommandParsed &parsed,
	const std::string &reasonCode, const std::string &publicReason, const std::string &inboxSha256)
{
	CompanionCommandCore::StandCommandParsed cmd = parsed;
	if (cmd.commandId.empty())
	{
		cmd.commandId = "unknown";
		cmd.commandType = "STAND";
		cmd.sessionId = "00000000000000000000000000000000";
		cmd.turn = 1;
	}
	if (cmd.commandType.empty())
	{
		cmd.commandType = "STAND";
	}
	const std::string json = buildKneelResultJson(cmd, CompanionCommandCore::CMD_REJECTED,
		publicReason.empty() ? reasonCode : publicReason, Position(0, 0, 0), 0, false, std::string(), inboxSha256);
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

void rejectSnapShotCommand(const std::string &resultPath, const CompanionCommandCore::SnapShotCommandParsed &parsed,
	const std::string &reasonCode, const std::string &publicReason, const std::string &inboxSha256)
{
	CompanionCommandCore::SnapShotCommandParsed cmd = parsed;
	if (cmd.commandId.empty())
	{
		cmd.commandId = "unknown";
		cmd.commandType = "SNAP_SHOT";
		cmd.sessionId = "00000000000000000000000000000000";
		cmd.turn = 1;
	}
	if (cmd.commandType.empty())
	{
		cmd.commandType = "SNAP_SHOT";
	}
	const std::string json = buildSnapShotResultJson(cmd, CompanionCommandCore::CMD_REJECTED,
		publicReason.empty() ? reasonCode : publicReason, Position(0, 0, 0), Position(0, 0, 0), 0, false,
		std::string(), inboxSha256);
	writeResultFile(resultPath, json);
}

void rejectPrimeCommand(const std::string &resultPath, const CompanionCommandCore::PrimeCommandParsed &parsed,
	const std::string &reasonCode, const std::string &publicReason, const std::string &inboxSha256)
{
	CompanionCommandCore::PrimeCommandParsed cmd = parsed;
	if (cmd.commandId.empty())
	{
		cmd.commandId = "unknown";
		cmd.commandType = "PRIME";
		cmd.sessionId = "00000000000000000000000000000000";
		cmd.turn = 1;
	}
	if (cmd.commandType.empty())
	{
		cmd.commandType = "PRIME";
	}
	const std::string json = buildPrimeResultJson(cmd, CompanionCommandCore::CMD_REJECTED,
		publicReason.empty() ? reasonCode : publicReason, Position(0, 0, 0), 0, false,
		std::string(), inboxSha256);
	writeResultFile(resultPath, json);
}

void rejectThrowCommand(const std::string &resultPath, const CompanionCommandCore::ThrowCommandParsed &parsed,
	const std::string &reasonCode, const std::string &publicReason, const std::string &inboxSha256)
{
	CompanionCommandCore::ThrowCommandParsed cmd = parsed;
	if (cmd.commandId.empty())
	{
		cmd.commandId = "unknown";
		cmd.commandType = "THROW";
		cmd.sessionId = "00000000000000000000000000000000";
		cmd.turn = 1;
	}
	if (cmd.commandType.empty())
	{
		cmd.commandType = "THROW";
	}
	const std::string json = buildThrowResultJson(cmd, CompanionCommandCore::CMD_REJECTED,
		publicReason.empty() ? reasonCode : publicReason, Position(0, 0, 0), Position(0, 0, 0), 0, false,
		std::string(), inboxSha256);
	writeResultFile(resultPath, json);
}

void rejectInventoryMoveCommand(const std::string &resultPath,
	const CompanionCommandCore::InventoryMoveCommandParsed &parsed,
	const std::string &reasonCode, const std::string &publicReason, const std::string &inboxSha256)
{
	CompanionCommandCore::InventoryMoveCommandParsed cmd = parsed;
	if (cmd.commandId.empty())
	{
		cmd.commandId = "unknown";
		cmd.commandType = "INVENTORY_MOVE";
		cmd.sessionId = "00000000000000000000000000000000";
		cmd.turn = 1;
		cmd.itemBattleItemId = 0;
		cmd.sourceSlot.clear();
		cmd.destinationSlot.clear();
		cmd.destinationX = 0;
		cmd.destinationY = 0;
	}
	if (cmd.commandType.empty())
	{
		cmd.commandType = "INVENTORY_MOVE";
	}
	const std::string json = buildInventoryMoveResultJson(cmd, CompanionCommandCore::CMD_REJECTED,
		publicReason.empty() ? reasonCode : publicReason, Position(0, 0, 0), 0, false,
		std::string(), inboxSha256, -1);
	writeResultFile(resultPath, json);
}

void rejectPickupCommand(const std::string &resultPath,
	const CompanionCommandCore::PickupCommandParsed &parsed,
	const std::string &reasonCode, const std::string &publicReason, const std::string &inboxSha256)
{
	CompanionCommandCore::PickupCommandParsed cmd = parsed;
	if (cmd.commandId.empty())
	{
		cmd.commandId = "unknown";
		cmd.commandType = "PICKUP";
		cmd.sessionId = "00000000000000000000000000000000";
		cmd.turn = 1;
		cmd.itemBattleItemId = 0;
		cmd.destinationSlot.clear();
		cmd.destinationX = 0;
		cmd.destinationY = 0;
		cmd.groundX = 0;
		cmd.groundY = 0;
		cmd.groundZ = 0;
	}
	if (cmd.commandType.empty())
	{
		cmd.commandType = "PICKUP";
	}
	const std::string json = buildPickupResultJson(cmd, CompanionCommandCore::CMD_REJECTED,
		publicReason.empty() ? reasonCode : publicReason, Position(0, 0, 0), 0, false,
		std::string(), inboxSha256, -1);
	writeResultFile(resultPath, json);
}

void rejectDropCommand(const std::string &resultPath,
	const CompanionCommandCore::DropCommandParsed &parsed,
	const std::string &reasonCode, const std::string &publicReason, const std::string &inboxSha256)
{
	CompanionCommandCore::DropCommandParsed cmd = parsed;
	if (cmd.commandId.empty())
	{
		cmd.commandId = "unknown";
		cmd.commandType = "DROP";
		cmd.sessionId = "00000000000000000000000000000000";
		cmd.turn = 1;
		cmd.itemBattleItemId = 0;
		cmd.sourceSlot.clear();
	}
	if (cmd.commandType.empty())
	{
		cmd.commandType = "DROP";
	}
	const std::string json = buildDropResultJson(cmd, CompanionCommandCore::CMD_REJECTED,
		publicReason.empty() ? reasonCode : publicReason, Position(0, 0, 0), 0, false,
		std::string(), inboxSha256, -1, 0);
	writeResultFile(resultPath, json);
}

static bool rejectUnreadableCommandJournal(
	const std::string &resultPath,
	const std::string &journalJson,
	const std::string &inboxDigest,
	const CompanionCommandCore::MoveCommandParsed *moveRejectTemplate,
	const CompanionCommandCore::KneelCommandParsed *kneelRejectTemplate,
	const CompanionCommandCore::SnapShotCommandParsed *snapRejectTemplate,
	const CompanionCommandCore::PrimeCommandParsed *primeRejectTemplate,
	const CompanionCommandCore::ThrowCommandParsed *throwRejectTemplate,
	const CompanionCommandCore::ReloadCommandParsed *reloadRejectTemplate,
	const CompanionCommandCore::UnloadCommandParsed *unloadRejectTemplate,
	const CompanionCommandCore::InventoryMoveCommandParsed *inventoryMoveRejectTemplate,
	const CompanionCommandCore::PickupCommandParsed *pickupRejectTemplate,
	const CompanionCommandCore::DropCommandParsed *dropRejectTemplate)
{
	if (CompanionCommandJournalCore::classifyExistingJournalJson(journalJson) !=
		CompanionCommandJournalCore::JOURNAL_UNREADABLE)
	{
		return false;
	}
	if (reloadRejectTemplate != 0)
	{
		rejectReloadCommand(resultPath, *reloadRejectTemplate, "JOURNAL_MALFORMED",
			"Tactical command journal is unreadable.", inboxDigest);
	}
	else if (unloadRejectTemplate != 0)
	{
		rejectUnloadCommand(resultPath, *unloadRejectTemplate, "JOURNAL_MALFORMED",
			"Tactical command journal is unreadable.", inboxDigest);
	}
	else if (inventoryMoveRejectTemplate != 0)
	{
		rejectInventoryMoveCommand(resultPath, *inventoryMoveRejectTemplate, "JOURNAL_MALFORMED",
			"Tactical command journal is unreadable.", inboxDigest);
	}
	else if (pickupRejectTemplate != 0)
	{
		rejectPickupCommand(resultPath, *pickupRejectTemplate, "JOURNAL_MALFORMED",
			"Tactical command journal is unreadable.", inboxDigest);
	}
	else if (dropRejectTemplate != 0)
	{
		rejectDropCommand(resultPath, *dropRejectTemplate, "JOURNAL_MALFORMED",
			"Tactical command journal is unreadable.", inboxDigest);
	}
	else if (throwRejectTemplate != 0)
	{
		rejectThrowCommand(resultPath, *throwRejectTemplate, "JOURNAL_MALFORMED",
			"Tactical command journal is unreadable.", inboxDigest);
	}
	else if (primeRejectTemplate != 0)
	{
		rejectPrimeCommand(resultPath, *primeRejectTemplate, "JOURNAL_MALFORMED",
			"Tactical command journal is unreadable.", inboxDigest);
	}
	else if (snapRejectTemplate != 0)
	{
		rejectSnapShotCommand(resultPath, *snapRejectTemplate, "JOURNAL_MALFORMED",
			"Tactical command journal is unreadable.", inboxDigest);
	}
	else if (kneelRejectTemplate != 0)
	{
		rejectKneelCommand(resultPath, *kneelRejectTemplate, "JOURNAL_MALFORMED",
			"Tactical command journal is unreadable.", inboxDigest);
	}
	else if (moveRejectTemplate != 0)
	{
		rejectCommand(resultPath, *moveRejectTemplate, "JOURNAL_MALFORMED",
			"Tactical command journal is unreadable.", inboxDigest);
	}
	return true;
}

bool applyDuplicateInboxDecision(
	CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision,
	const std::string &commandId,
	const std::string &inboxDigest,
	const std::string &resultPath,
	const std::string &conflictPath,
	const std::string &existingResultJson,
	const CompanionCommandCore::MoveCommandParsed *moveRejectTemplate,
	const CompanionCommandCore::KneelCommandParsed *kneelRejectTemplate,
	const CompanionCommandCore::SnapShotCommandParsed *snapRejectTemplate,
	const CompanionCommandCore::PrimeCommandParsed *primeRejectTemplate,
	const CompanionCommandCore::ThrowCommandParsed *throwRejectTemplate,
	const CompanionCommandCore::ReloadCommandParsed *reloadRejectTemplate,
	const CompanionCommandCore::UnloadCommandParsed *unloadRejectTemplate,
	const CompanionCommandCore::InventoryMoveCommandParsed *inventoryMoveRejectTemplate,
	const CompanionCommandCore::PickupCommandParsed *pickupRejectTemplate,
	const CompanionCommandCore::DropCommandParsed *dropRejectTemplate)
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
		if (reloadRejectTemplate != 0)
		{
			rejectReloadCommand(resultPath, *reloadRejectTemplate, "DUPLICATE_COMMAND_ID", "Command ID already used.", inboxDigest);
		}
		else if (unloadRejectTemplate != 0)
		{
			rejectUnloadCommand(resultPath, *unloadRejectTemplate, "DUPLICATE_COMMAND_ID", "Command ID already used.", inboxDigest);
		}
		else if (inventoryMoveRejectTemplate != 0)
		{
			rejectInventoryMoveCommand(resultPath, *inventoryMoveRejectTemplate, "DUPLICATE_COMMAND_ID", "Command ID already used.", inboxDigest);
		}
		else if (pickupRejectTemplate != 0)
		{
			rejectPickupCommand(resultPath, *pickupRejectTemplate, "DUPLICATE_COMMAND_ID", "Command ID already used.", inboxDigest);
		}
		else if (dropRejectTemplate != 0)
		{
			rejectDropCommand(resultPath, *dropRejectTemplate, "DUPLICATE_COMMAND_ID", "Command ID already used.", inboxDigest);
		}
		else if (throwRejectTemplate != 0)
		{
			rejectThrowCommand(resultPath, *throwRejectTemplate, "DUPLICATE_COMMAND_ID", "Command ID already used.", inboxDigest);
		}
		else if (primeRejectTemplate != 0)
		{
			rejectPrimeCommand(resultPath, *primeRejectTemplate, "DUPLICATE_COMMAND_ID", "Command ID already used.", inboxDigest);
		}
		else if (snapRejectTemplate != 0)
		{
			rejectSnapShotCommand(resultPath, *snapRejectTemplate, "DUPLICATE_COMMAND_ID", "Command ID already used.", inboxDigest);
		}
		else if (kneelRejectTemplate != 0)
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

static bool snapWeaponSupportsSnapShot(BattleItem *weapon)
{
	if (weapon == 0)
	{
		return false;
	}
	RuleItem *rules = weapon->getRules();
	if (rules->getBattleType() != BT_FIREARM || rules->getAccuracySnap() == 0)
	{
		return false;
	}
	if (rules->getWaypoints() != 0)
	{
		return false;
	}
	BattleItem *ammo = weapon->getAmmoItem();
	if (ammo != 0 && ammo->getRules()->getWaypoints() != 0)
	{
		return false;
	}
	return true;
}

static bool snapWeaponHasAmmo(BattleItem *weapon)
{
	if (weapon == 0 || weapon->getRules()->getBattleType() != BT_FIREARM)
	{
		return false;
	}
	if (!weapon->needsAmmo())
	{
		return true;
	}
	BattleItem *ammo = weapon->getAmmoItem();
	return ammo != 0 && ammo->getAmmoQuantity() > 0;
}

static bool snapWeaponUsable(SavedBattleGame *battle, BattleItem *weapon)
{
	if (battle == 0 || weapon == 0)
	{
		return false;
	}
	if (!battle->getItemUsable(weapon).empty())
	{
		return false;
	}
	SavedGame *geoscape = battle->getGeoscapeSave();
	if (geoscape == 0)
	{
		return true;
	}
	return geoscape->isResearched(weapon->getRules()->getRequirements());
}

static bool snapWeaponSupportsAutoShot(BattleItem *weapon)
{
	if (weapon == 0)
	{
		return false;
	}
	RuleItem *rules = weapon->getRules();
	if (rules->getBattleType() != BT_FIREARM || rules->getAccuracyAuto() == 0)
	{
		return false;
	}
	if (rules->getWaypoints() != 0)
	{
		return false;
	}
	BattleItem *ammo = weapon->getAmmoItem();
	if (ammo != 0 && ammo->getRules()->getWaypoints() != 0)
	{
		return false;
	}
	return true;
}

static bool snapWeaponSupportsAimedShot(BattleItem *weapon)
{
	if (weapon == 0)
	{
		return false;
	}
	RuleItem *rules = weapon->getRules();
	if (rules->getBattleType() != BT_FIREARM || rules->getAccuracyAimed() == 0)
	{
		return false;
	}
	if (rules->getWaypoints() != 0)
	{
		return false;
	}
	BattleItem *ammo = weapon->getAmmoItem();
	if (ammo != 0 && ammo->getRules()->getWaypoints() != 0)
	{
		return false;
	}
	return true;
}

void fillSnapShotValidationInput(
	CompanionCommandCore::SnapShotValidationInput *input,
	BattlescapeGame *game,
	SavedBattleGame *battle,
	bool battleInitialized,
	const std::string &sessionId,
	const CompanionCommandCore::SnapShotCommandParsed &parsed,
	int actorUnitId,
	bool actorIsPlayerSoldier,
	bool actorAlive,
	bool actorAiControlled,
	bool humanOverrideRevoked,
	BattleUnit *actor)
{
	fillBindingValidationInput(
		input,
		game,
		battle,
		battleInitialized,
		sessionId,
		parsed.snapshotSha256,
		parsed.sessionId,
		parsed.turn,
		parsed.stateVersion,
		actorUnitId,
		actorIsPlayerSoldier,
		actorAlive,
		actorAiControlled,
		humanOverrideRevoked);
	input->targetX = parsed.targetX;
	input->targetY = parsed.targetY;
	input->targetZ = parsed.targetZ;
	input->mapSizeX = battle->getMapSizeX();
	input->mapSizeY = battle->getMapSizeY();
	input->mapSizeZ = battle->getMapSizeZ();
	input->activeHandValid = true;
	BattleItem *weapon = actor != 0 ? actor->getItem(parsed.activeHand) : 0;
	input->weaponPresentInHand = weapon != 0;
	BattleActionType actionType = BA_SNAPSHOT;
	if (parsed.commandType == "AUTO_SHOT")
	{
		actionType = BA_AUTOSHOT;
		input->weaponSupportsFireMode = snapWeaponSupportsAutoShot(weapon);
	}
	else if (parsed.commandType == "AIMED_SHOT")
	{
		actionType = BA_AIMEDSHOT;
		input->weaponSupportsFireMode = snapWeaponSupportsAimedShot(weapon);
	}
	else
	{
		input->weaponSupportsFireMode = snapWeaponSupportsSnapShot(weapon);
	}
	input->weaponHasAmmo = snapWeaponHasAmmo(weapon);
	input->weaponUsable = snapWeaponUsable(battle, weapon);
	const int fireTu = actor != 0 && weapon != 0 ? actor->getActionTUs(actionType, weapon) : 0;
	input->insufficientTuForFireMode = actor == 0 || weapon == 0 || fireTu <= 0 ||
		!game->checkReservedTU(actor, fireTu, true);
	input->visibleUnitTargetDefinitelyBlocked = false;
	if (actor != 0 && weapon != 0 && battle != 0 && game != 0)
	{
		const Position target(parsed.targetX, parsed.targetY, parsed.targetZ);
		if (target.x >= 0 && target.y >= 0 && target.z >= 0 &&
		    target.x < battle->getMapSizeX() && target.y < battle->getMapSizeY() && target.z < battle->getMapSizeZ())
		{
			BattleAction probe = {};
			probe.type = actionType;
			probe.actor = actor;
			probe.weapon = weapon;
			probe.target = target;
			probe.autoShotCounter = 0;
			input->visibleUnitTargetDefinitelyBlocked =
				ProjectileFlyBState::companionPreflightVisibleUnitTargetBlocked(
					probe, battle, game, actor->getPosition());
		}
	}
}

static bool companionHandGrenadeSupportsPrime(BattleItem *weapon)
{
	if (weapon == 0 || weapon->getRules()->isFixed())
	{
		return false;
	}
	const BattleType bt = weapon->getRules()->getBattleType();
	return bt == BT_GRENADE || bt == BT_PROXIMITYGRENADE;
}

static bool companionHandGrenadeSupportsThrow(BattleItem *weapon)
{
	return companionHandGrenadeSupportsPrime(weapon);
}

static bool companionGrenadeUsable(SavedBattleGame *battle, BattleItem *weapon)
{
	if (battle == 0 || weapon == 0)
	{
		return false;
	}
	if (!battle->getItemUsable(weapon).empty())
	{
		return false;
	}
	return snapWeaponUsable(battle, weapon);
}

void fillPrimeValidationInput(
	CompanionCommandCore::PrimeValidationInput *input,
	BattlescapeGame *game,
	SavedBattleGame *battle,
	bool battleInitialized,
	const std::string &sessionId,
	const CompanionCommandCore::PrimeCommandParsed &parsed,
	int actorUnitId,
	bool actorIsPlayerSoldier,
	bool actorAlive,
	bool actorAiControlled,
	bool humanOverrideRevoked,
	BattleUnit *actor)
{
	fillBindingValidationInput(
		input,
		game,
		battle,
		battleInitialized,
		sessionId,
		parsed.snapshotSha256,
		parsed.sessionId,
		parsed.turn,
		parsed.stateVersion,
		actorUnitId,
		actorIsPlayerSoldier,
		actorAlive,
		actorAiControlled,
		humanOverrideRevoked);
	input->activeHand = parsed.activeHand;
	input->activeHandValid = true;
	BattleItem *weapon = actor != 0 ? actor->getItem(parsed.activeHand) : 0;
	input->grenadePresentInHand = weapon != 0;
	input->grenadeSupportsPrime = companionHandGrenadeSupportsPrime(weapon);
	input->grenadeAlreadyPrimed = weapon != 0 && weapon->getFuseTimer() >= 0;
	const int battleType = weapon != 0 ? static_cast<int>(weapon->getRules()->getBattleType()) : BT_NONE;
	input->invalidFuseTimer = weapon == 0 ||
		!CompanionCommandCore::isValidPrimeFuseTimerForBattleType(parsed.fuseTimer, battleType);
	input->grenadeUsable = companionGrenadeUsable(battle, weapon);
	const int primeTu = actor != 0 && weapon != 0 ? actor->getActionTUs(BA_PRIME, weapon) : 0;
	input->insufficientTuForPrime = actor == 0 || weapon == 0 || primeTu <= 0 ||
		!game->checkReservedTU(actor, primeTu, true);
}

void fillThrowValidationInput(
	CompanionCommandCore::ThrowValidationInput *input,
	BattlescapeGame *game,
	SavedBattleGame *battle,
	bool battleInitialized,
	const std::string &sessionId,
	const CompanionCommandCore::ThrowCommandParsed &parsed,
	int actorUnitId,
	bool actorIsPlayerSoldier,
	bool actorAlive,
	bool actorAiControlled,
	bool humanOverrideRevoked,
	BattleUnit *actor)
{
	fillBindingValidationInput(
		input,
		game,
		battle,
		battleInitialized,
		sessionId,
		parsed.snapshotSha256,
		parsed.sessionId,
		parsed.turn,
		parsed.stateVersion,
		actorUnitId,
		actorIsPlayerSoldier,
		actorAlive,
		actorAiControlled,
		humanOverrideRevoked);
	input->targetX = parsed.targetX;
	input->targetY = parsed.targetY;
	input->targetZ = parsed.targetZ;
	input->mapSizeX = battle->getMapSizeX();
	input->mapSizeY = battle->getMapSizeY();
	input->mapSizeZ = battle->getMapSizeZ();
	input->activeHand = parsed.activeHand;
	input->activeHandValid = true;
	BattleItem *weapon = actor != 0 ? actor->getItem(parsed.activeHand) : 0;
	input->grenadePresentInHand = weapon != 0;
	input->grenadeSupportsThrow = companionHandGrenadeSupportsThrow(weapon);
	input->grenadeUsable = companionGrenadeUsable(battle, weapon);
	input->targetOutOfThrowRange = false;
	if (actor != 0 && weapon != 0)
	{
		const Position target(parsed.targetX, parsed.targetY, parsed.targetZ);
		if (target.x >= 0 && target.y >= 0 && target.z >= 0 &&
		    target.x < battle->getMapSizeX() && target.y < battle->getMapSizeY() && target.z < battle->getMapSizeZ())
		{
			Tile *targetTile = battle->getTile(target);
			if (targetTile != 0)
			{
				BattleAction probe = {};
				probe.type = BA_THROW;
				probe.actor = actor;
				probe.weapon = weapon;
				probe.target = target;
				input->targetOutOfThrowRange = !ProjectileFlyBState::validThrowRange(&probe, actor->getPosition(), targetTile);
			}
		}
	}
	const int throwTu = actor != 0 && weapon != 0 ? actor->getActionTUs(BA_THROW, weapon) : 0;
	input->insufficientTuForThrow = actor == 0 || weapon == 0 || throwTu <= 0 ||
		!game->checkReservedTU(actor, throwTu, true);
}

static BattleItem *findBattleItemByIdOnUnit(BattleUnit *actor, int battleItemId)
{
	if (actor == 0 || battleItemId <= 0)
	{
		return 0;
	}
	for (std::vector<BattleItem*>::iterator i = actor->getInventory()->begin();
	     i != actor->getInventory()->end(); ++i)
	{
		if (*i != 0 && (*i)->getId() == battleItemId)
		{
			return *i;
		}
	}
	return 0;
}

static bool companionReloadWeaponHeldInLeftOrRightHand(BattleUnit *actor, BattleItem *weapon)
{
	if (actor == 0 || weapon == 0)
	{
		return false;
	}
	return actor->getItem("STR_RIGHT_HAND") == weapon || actor->getItem("STR_LEFT_HAND") == weapon;
}

static bool companionUnloadOtherHandClear(BattleUnit *actor, BattleItem *weapon)
{
	if (actor == 0 || weapon == 0 || weapon->getSlot() == 0)
	{
		return false;
	}
	const std::string &sid = weapon->getSlot()->getId();
	if (sid == "STR_RIGHT_HAND")
	{
		return actor->getItem("STR_LEFT_HAND") == 0;
	}
	if (sid == "STR_LEFT_HAND")
	{
		return actor->getItem("STR_RIGHT_HAND") == 0;
	}
	return false;
}

static bool companionReloadAmmoCompatible(BattleItem *weapon, BattleItem *ammo)
{
	if (weapon == 0 || ammo == 0)
	{
		return false;
	}
	RuleItem *wRules = weapon->getRules();
	RuleItem *aRules = ammo->getRules();
	if (wRules == 0 || aRules == 0)
	{
		return false;
	}
	for (std::vector<std::string>::const_iterator c = wRules->getCompatibleAmmo()->begin();
	     c != wRules->getCompatibleAmmo()->end(); ++c)
	{
		if (*c == aRules->getType())
		{
			return true;
		}
	}
	return false;
}

void fillReloadValidationInput(
	CompanionCommandCore::ReloadValidationInput *input,
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame *campaign,
	bool battleInitialized,
	const std::string &sessionId,
	const CompanionCommandCore::ReloadCommandParsed &parsed,
	int actorUnitId,
	bool actorIsPlayerSoldier,
	bool actorAlive,
	bool actorAiControlled,
	bool humanOverrideRevoked,
	BattleUnit *actor)
{
	fillBindingValidationInput(
		input,
		game,
		battle,
		battleInitialized,
		sessionId,
		parsed.snapshotSha256,
		parsed.sessionId,
		parsed.turn,
		parsed.stateVersion,
		actorUnitId,
		actorIsPlayerSoldier,
		actorAlive,
		actorAiControlled,
		humanOverrideRevoked);
	input->weaponBattleItemId = parsed.weaponBattleItemId;
	input->ammoBattleItemId = parsed.ammoBattleItemId;

	BattleItem *weapon = findBattleItemByIdOnUnit(actor, parsed.weaponBattleItemId);
	BattleItem *ammo = findBattleItemByIdOnUnit(actor, parsed.ammoBattleItemId);

	input->weaponResolved = weapon != 0;
	input->ammoResolved = ammo != 0;
	input->campaignResearchAvailable = campaign != 0;

	input->weaponBelongsToActor = weapon != 0 && weapon->getOwner() == actor;
	input->ammoBelongsToActor = ammo != 0 && ammo->getOwner() == actor;

	input->weaponInEitherHand =
		weapon != 0 &&
		weapon->getSlot() != 0 &&
		weapon->getSlot()->getType() == INV_HAND &&
		companionReloadWeaponHeldInLeftOrRightHand(actor, weapon);

	input->weaponIsFirearm = weapon != 0 && weapon->getRules() != 0 &&
		weapon->getRules()->getBattleType() == BT_FIREARM;
	input->weaponNeedsPhysicalMagazine = weapon != 0 && weapon->needsAmmo() &&
		weapon->getRules() != 0 && !weapon->getRules()->getCompatibleAmmo()->empty();
	input->weaponChamberEmptyForReloadUi = weapon != 0 && weapon->getAmmoItem() == 0;

	input->ammoIsPhysicalClip = ammo != 0 && ammo->getRules() != 0 &&
		ammo->getRules()->getBattleType() == BT_AMMO;
	input->ammoCarriedOffGround = ammo != 0 && ammo->getSlot() != 0 &&
		ammo->getSlot()->getType() != INV_GROUND;
	input->ammoNotAlreadyLoadedInWeapon = !(weapon != 0 && ammo != 0 && weapon->getAmmoItem() == ammo);
	input->ammunitionCompatible = companionReloadAmmoCompatible(weapon, ammo);

	input->weaponUsableEnvironment = battle != 0 && weapon != 0 && ammo != 0 &&
		battle->getItemUsable(weapon).empty() && battle->getItemUsable(ammo).empty();

	const bool resOk = campaign != 0 && weapon != 0 && ammo != 0 && weapon->getRules() != 0 && ammo->getRules() != 0;
	input->weaponResearchAuthorized = resOk && campaign->isResearched(weapon->getRules()->getRequirements());
	input->ammoResearchAuthorized = resOk && campaign->isResearched(ammo->getRules()->getRequirements());

	const int reloadTu = 15;
	input->insufficientTuForReload = actor == 0 || weapon == 0 || ammo == 0 ||
		!game->checkReservedTU(actor, reloadTu, true);
}

void fillUnloadValidationInput(
	CompanionCommandCore::UnloadValidationInput *input,
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame *campaign,
	bool battleInitialized,
	const std::string &sessionId,
	const CompanionCommandCore::UnloadCommandParsed &parsed,
	int actorUnitId,
	bool actorIsPlayerSoldier,
	bool actorAlive,
	bool actorAiControlled,
	bool humanOverrideRevoked,
	BattleUnit *actor)
{
	fillBindingValidationInput(
		input,
		game,
		battle,
		battleInitialized,
		sessionId,
		parsed.snapshotSha256,
		parsed.sessionId,
		parsed.turn,
		parsed.stateVersion,
		actorUnitId,
		actorIsPlayerSoldier,
		actorAlive,
		actorAiControlled,
		humanOverrideRevoked);
	input->weaponBattleItemId = parsed.weaponBattleItemId;
	input->loadedAmmoBattleItemId = parsed.loadedAmmoBattleItemId;

	BattleItem *weapon = findBattleItemByIdOnUnit(actor, parsed.weaponBattleItemId);
	BattleItem *loadedAmmo = weapon != 0 ? weapon->getAmmoItem() : 0;

	input->weaponResolved = weapon != 0;
	input->campaignResearchAvailable = campaign != 0;

	input->weaponBelongsToActor = weapon != 0 && weapon->getOwner() == actor;

	input->weaponInEitherHand =
		weapon != 0 &&
		weapon->getSlot() != 0 &&
		weapon->getSlot()->getType() == INV_HAND &&
		companionReloadWeaponHeldInLeftOrRightHand(actor, weapon);

	input->weaponIsFirearm = weapon != 0 && weapon->getRules() != 0 &&
		weapon->getRules()->getBattleType() == BT_FIREARM;
	input->weaponNeedsPhysicalMagazine = weapon != 0 && weapon->needsAmmo() &&
		weapon->getRules() != 0 && !weapon->getRules()->getCompatibleAmmo()->empty();

	input->weaponHasLoadedMagazine = loadedAmmo != 0;
	input->loadedAmmoIdentityMatches =
		loadedAmmo != 0 && loadedAmmo->getId() == parsed.loadedAmmoBattleItemId;
	input->loadedAmmoIsPhysicalClip =
		loadedAmmo != 0 && loadedAmmo->getRules() != 0 &&
		loadedAmmo->getRules()->getBattleType() == BT_AMMO;

	input->otherHandClearExceptWeapon = companionUnloadOtherHandClear(actor, weapon);

	input->weaponUsableEnvironment = battle != 0 && weapon != 0 && loadedAmmo != 0 &&
		battle->getItemUsable(weapon).empty() && battle->getItemUsable(loadedAmmo).empty();

	const bool resOk =
		campaign != 0 && weapon != 0 && loadedAmmo != 0 &&
		weapon->getRules() != 0 && loadedAmmo->getRules() != 0;
	input->weaponResearchAuthorized = resOk && campaign->isResearched(weapon->getRules()->getRequirements());
	input->ammoResearchAuthorized = resOk && campaign->isResearched(loadedAmmo->getRules()->getRequirements());

	const int unloadTu = 8;
	input->insufficientTuForUnload = actor == 0 || weapon == 0 || loadedAmmo == 0 ||
		!game->checkReservedTU(actor, unloadTu, true);
}

namespace
{

static bool companionRuntimeInventoryOverlap(BattleUnit *unit, BattleItem *movingItem, RuleInventory *destSlot, int destX, int destY)
{
	if (unit == 0 || movingItem == 0 || destSlot == 0)
	{
		return true;
	}
	if (destSlot->getType() == INV_GROUND)
	{
		return true;
	}
	for (std::vector<BattleItem*>::const_iterator i = unit->getInventory()->begin();
	     i != unit->getInventory()->end(); ++i)
	{
		if (*i == 0 || *i == movingItem)
		{
			continue;
		}
		if ((*i)->getSlot() == 0)
		{
			continue;
		}
		if ((*i)->getSlot() == destSlot && (*i)->occupiesSlot(destX, destY, movingItem))
		{
			return true;
		}
	}
	return false;
}

static bool isCarriedInventorySectionString(const std::string &id)
{
	return id == "STR_RIGHT_HAND" || id == "STR_LEFT_HAND" || id == "STR_BELT" ||
	       id == "STR_BACK_PACK" || id == "STR_RIGHT_SHOULDER" || id == "STR_LEFT_SHOULDER" ||
	       id == "STR_RIGHT_LEG" || id == "STR_LEFT_LEG";
}

static BattleItem *findGroundItemFromActorExport(BattleUnit *actor, int battleItemId)
{
	if (actor == 0 || battleItemId <= 0)
	{
		return 0;
	}
	Tile *tile = actor->getTile();
	if (tile == 0)
	{
		return 0;
	}
	for (std::vector<BattleItem*>::const_iterator i = tile->getInventory()->begin();
	     i != tile->getInventory()->end(); ++i)
	{
		BattleItem *item = *i;
		if (item != 0 && item->getId() == battleItemId &&
		    item->getSlot() != 0 && item->getSlot()->getType() == INV_GROUND)
		{
			return item;
		}
	}
	return 0;
}

} // namespace

void fillInventoryMoveValidationInput(
	CompanionCommandCore::InventoryMoveValidationInput *input,
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame * /*campaign*/,
	bool battleInitialized,
	const std::string &sessionId,
	const CompanionCommandCore::InventoryMoveCommandParsed &parsed,
	int actorUnitId,
	bool actorIsPlayerSoldier,
	bool actorAlive,
	bool actorAiControlled,
	bool humanOverrideRevoked,
	BattleUnit *actor)
{
	fillBindingValidationInput(
		input,
		game,
		battle,
		battleInitialized,
		sessionId,
		parsed.snapshotSha256,
		parsed.sessionId,
		parsed.turn,
		parsed.stateVersion,
		actorUnitId,
		actorIsPlayerSoldier,
		actorAlive,
		actorAiControlled,
		humanOverrideRevoked);
	input->itemBattleItemId = parsed.itemBattleItemId;
	input->sourceSlotId = parsed.sourceSlot;
	input->destinationSlotId = parsed.destinationSlot;
	input->destinationX = parsed.destinationX;
	input->destinationY = parsed.destinationY;
	input->sourceCoordsGuard = parsed.sourceCoordsGuard;
	input->sourceX = parsed.sourceX;
	input->sourceY = parsed.sourceY;

	BattleItem *item = findBattleItemByIdOnUnit(actor, parsed.itemBattleItemId);
	Mod *mod = game != 0 ? game->getMod() : 0;
	RuleInventory *destRule = mod != 0 ? mod->getInventory(parsed.destinationSlot, false) : 0;

	input->itemResolved = item != 0;
	input->itemBelongsToActor = item != 0 && item->getOwner() == actor;
	input->itemCarriedOffGround = item != 0 && item->getSlot() != 0 && item->getSlot()->getType() != INV_GROUND;

	RuleInventory *srcSlot = item != 0 ? item->getSlot() : 0;
	input->itemSlotMatchesCommand =
		item != 0 && srcSlot != 0 && srcSlot->getId() == parsed.sourceSlot;

	input->sourceCoordsMatch =
		!parsed.sourceCoordsGuard ||
		(item != 0 &&
		 item->getSlotX() == parsed.sourceX &&
		 item->getSlotY() == parsed.sourceY);

	RuleItem *itemRules = item != 0 ? item->getRules() : 0;
	input->itemIsFixedWeapon = itemRules != 0 && itemRules->isFixed();
	input->itemNestedInFirearm = item != 0 && item->isAmmo() && item->getSlot() == 0;

	input->primedExplosiveBlocked = false;
	if (item != 0 && itemRules != 0 &&
	    item->getFuseTimer() >= 0 &&
	    (itemRules->getBattleType() == BT_GRENADE || itemRules->getBattleType() == BT_PROXIMITYGRENADE))
	{
		input->primedExplosiveBlocked = true;
	}

	input->destinationSlotResolved = destRule != 0;
	input->destinationIsGround = parsed.destinationSlot == "STR_GROUND";
	input->destinationAllowedCarriedType = isCarriedInventorySectionString(parsed.destinationSlot);
	input->destinationSameSectionAsCommand =
		destRule != 0 && destRule->getId() == parsed.destinationSlot;

	input->itemFitsDestinationGeometry = false;
	input->destinationOccupied = false;
	input->transferCostResolved = false;
	input->transferCostTu = -1;
	input->insufficientTuForInventoryMove = false;

	if (item == 0 || srcSlot == 0 || mod == 0 || destRule == 0 || input->primedExplosiveBlocked)
	{
		return;
	}

	input->itemFitsDestinationGeometry = destRule->fitItemInSlot(itemRules, parsed.destinationX, parsed.destinationY);
	input->destinationOccupied =
		input->itemFitsDestinationGeometry &&
		companionRuntimeInventoryOverlap(actor, item, destRule, parsed.destinationX, parsed.destinationY);

	int moveTu = 0;
	if (srcSlot->getId() == destRule->getId())
	{
		moveTu = 0;
		input->transferCostResolved = true;
	}
	else
	{
		const std::map<std::string, int> &costs = srcSlot->getTransferCosts();
		std::map<std::string, int>::const_iterator cit = costs.find(destRule->getId());
		if (cit != costs.end())
		{
			moveTu = cit->second;
			input->transferCostResolved = true;
		}
	}
	input->transferCostTu = moveTu;

	if (!input->transferCostResolved || input->destinationOccupied || !input->itemFitsDestinationGeometry ||
	    input->primedExplosiveBlocked)
	{
		return;
	}

	input->insufficientTuForInventoryMove =
		moveTu > 0 && (actor == 0 || !game->checkReservedTU(actor, moveTu, true));
}

void fillPickupValidationInput(
	CompanionCommandCore::PickupValidationInput *input,
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame *campaign,
	bool battleInitialized,
	const std::string &sessionId,
	const CompanionCommandCore::PickupCommandParsed &parsed,
	int actorUnitId,
	bool actorIsPlayerSoldier,
	bool actorAlive,
	bool actorAiControlled,
	bool humanOverrideRevoked,
	BattleUnit *actor)
{
	fillBindingValidationInput(
		input,
		game,
		battle,
		battleInitialized,
		sessionId,
		parsed.snapshotSha256,
		parsed.sessionId,
		parsed.turn,
		parsed.stateVersion,
		actorUnitId,
		actorIsPlayerSoldier,
		actorAlive,
		actorAiControlled,
		humanOverrideRevoked);
	input->itemBattleItemId = parsed.itemBattleItemId;
	input->groundX = parsed.groundX;
	input->groundY = parsed.groundY;
	input->groundZ = parsed.groundZ;
	input->destinationSlotId = parsed.destinationSlot;
	input->destinationX = parsed.destinationX;
	input->destinationY = parsed.destinationY;

	input->campaignResearchAvailable = campaign != 0;
	input->groundPositionMatchesActor =
		actor != 0 &&
		actor->getPosition().x == parsed.groundX &&
		actor->getPosition().y == parsed.groundY &&
		actor->getPosition().z == parsed.groundZ;

	BattleItem *groundItem = findGroundItemFromActorExport(actor, parsed.itemBattleItemId);
	input->itemResolvedOnActorGround = groundItem != 0;

	RuleItem *itemRules = groundItem != 0 ? groundItem->getRules() : 0;
	input->itemIsFixedWeapon = itemRules != 0 && itemRules->isFixed();
	input->itemResearchAuthorized =
		itemRules != 0 && campaign != 0 && campaign->isResearched(itemRules->getRequirements());

	Mod *mod = game != 0 ? game->getMod() : 0;
	RuleInventory *groundRule = mod != 0 ? mod->getInventory("STR_GROUND", false) : 0;
	RuleInventory *destRule = mod != 0 ? mod->getInventory(parsed.destinationSlot, false) : 0;

	input->destinationSlotResolved = destRule != 0;
	input->destinationAllowedCarriedType = isCarriedInventorySectionString(parsed.destinationSlot);
	input->destinationSameSectionAsCommand =
		destRule != 0 && destRule->getId() == parsed.destinationSlot;

	input->itemFitsDestinationGeometry = false;
	input->destinationOccupied = false;
	input->transferCostResolved = false;
	input->transferCostTu = -1;
	input->insufficientTuForPickup = false;

	if (campaign == 0 || groundItem == 0 || itemRules == 0 || mod == 0 || groundRule == 0 || destRule == 0)
	{
		return;
	}

	input->itemFitsDestinationGeometry = destRule->fitItemInSlot(itemRules, parsed.destinationX, parsed.destinationY);
	input->destinationOccupied =
		input->itemFitsDestinationGeometry &&
		companionRuntimeInventoryOverlap(actor, groundItem, destRule, parsed.destinationX, parsed.destinationY);

	const std::map<std::string, int> &fromGround = groundRule->getTransferCosts();
	std::map<std::string, int>::const_iterator cit = fromGround.find(destRule->getId());
	if (cit != fromGround.end() && cit->second >= 0)
	{
		input->transferCostResolved = true;
		input->transferCostTu = cit->second;
	}

	if (!input->transferCostResolved || input->destinationOccupied || !input->itemFitsDestinationGeometry)
	{
		return;
	}

	const int moveTu = input->transferCostTu;
	input->insufficientTuForPickup =
		moveTu > 0 && (actor == 0 || !game->checkReservedTU(actor, moveTu, true));
}

void fillDropValidationInput(
	CompanionCommandCore::DropValidationInput *input,
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame *campaign,
	bool battleInitialized,
	const std::string &sessionId,
	const CompanionCommandCore::DropCommandParsed &parsed,
	int actorUnitId,
	bool actorIsPlayerSoldier,
	bool actorAlive,
	bool actorAiControlled,
	bool humanOverrideRevoked,
	BattleUnit *actor)
{
	fillBindingValidationInput(
		input,
		game,
		battle,
		battleInitialized,
		sessionId,
		parsed.snapshotSha256,
		parsed.sessionId,
		parsed.turn,
		parsed.stateVersion,
		actorUnitId,
		actorIsPlayerSoldier,
		actorAlive,
		actorAiControlled,
		humanOverrideRevoked);
	input->itemBattleItemId = parsed.itemBattleItemId;
	input->sourceSlotId = parsed.sourceSlot;
	input->sourceCoordsGuard = parsed.sourceCoordsGuard;
	input->sourceX = parsed.sourceX;
	input->sourceY = parsed.sourceY;

	input->campaignResearchAvailable = campaign != 0;

	BattleItem *item = findBattleItemByIdOnUnit(actor, parsed.itemBattleItemId);
	Mod *mod = game != 0 ? game->getMod() : 0;
	RuleInventory *groundRule = mod != 0 ? mod->getInventory("STR_GROUND", false) : 0;

	input->itemResolved = item != 0;
	input->itemBelongsToActor = item != 0 && item->getOwner() == actor;
	input->itemCarriedOffGround = item != 0 && item->getSlot() != 0 && item->getSlot()->getType() != INV_GROUND;

	RuleInventory *srcSlot = item != 0 ? item->getSlot() : 0;
	input->itemSlotMatchesCommand =
		item != 0 && srcSlot != 0 && srcSlot->getId() == parsed.sourceSlot;

	input->sourceCoordsMatch =
		!parsed.sourceCoordsGuard ||
		(item != 0 &&
		 item->getSlotX() == parsed.sourceX &&
		 item->getSlotY() == parsed.sourceY);

	RuleItem *itemRules = item != 0 ? item->getRules() : 0;
	input->itemIsFixedWeapon = itemRules != 0 && itemRules->isFixed();
	input->itemNestedInFirearm = item != 0 && item->isAmmo() && item->getSlot() == 0;
	input->itemResearchAuthorized =
		itemRules != 0 && campaign != 0 && campaign->isResearched(itemRules->getRequirements());

	input->groundRuleResolved = groundRule != 0;
	input->groundHasRoom = false;
	input->transferCostResolved = false;
	input->transferCostTu = -1;
	input->insufficientTuForDrop = false;

	if (campaign == 0 || item == 0 || itemRules == 0 || mod == 0 || groundRule == 0 || srcSlot == 0 ||
	    srcSlot->getType() == INV_GROUND || input->itemNestedInFirearm)
	{
		return;
	}

	int gx = 0;
	int gy = 0;
	input->groundHasRoom = companionFindFirstGroundPlacement(actor, groundRule, item, &gx, &gy);

	const std::map<std::string, int> &costs = srcSlot->getTransferCosts();
	std::map<std::string, int>::const_iterator cit = costs.find(groundRule->getId());
	if (cit != costs.end() && cit->second >= 0)
	{
		input->transferCostResolved = true;
		input->transferCostTu = cit->second;
	}

	if (!input->transferCostResolved || !input->groundHasRoom)
	{
		return;
	}

	const int moveTu = input->transferCostTu;
	input->insufficientTuForDrop =
		moveTu > 0 && (actor == 0 || !game->checkReservedTU(actor, moveTu, true));
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

void logNotAiControlledAuthorityOnce(
	const std::string &commandId,
	const CompanionSoldierOwnershipCore::RelayControlDiagnostics &d,
	int unitId)
{
	static std::string s_lastCommandId;
	static std::string s_lastDiagSignature;
	std::ostringstream sig;
	sig << "u=" << unitId
	    << " read=" << (d.controlFileReadOk ? 1 : 0)
	    << " nonempty=" << (d.controlFileNonEmpty ? 1 : 0)
	    << " parse=" << (d.controlJsonParseOk ? 1 : 0)
	    << " tactAi=" << (d.soldierInTacticalAiSet ? 1 : 0)
	    << " campMode=" << static_cast<int>(d.campaignMode)
	    << " bind=" << (d.relayBindingMatch ? 1 : 0)
	    << " asn=" << (d.assignmentPresent ? 1 : 0)
	    << " asnAi=" << (d.assignmentIsAi ? 1 : 0)
	    << " durDoc=" << (d.durableIdsInDocument ? 1 : 0)
	    << " rcamp=" << (d.relayCampaignIdNonEmpty ? 1 : 0)
	    << " rtac=" << (d.relayTacticalBattleIdNonEmpty ? 1 : 0)
	    << " cid=" << (d.campaignIdMatch ? 1 : 0)
	    << " tbid=" << (d.tacticalBattleIdMatch ? 1 : 0)
	    << " sess=" << (d.sessionBindingMatch ? 1 : 0)
	    << " relayAi=" << (d.relayAiControlled ? 1 : 0)
	    << " permit=" << (d.aiCommandPermitted ? 1 : 0);
	const std::string signature = sig.str();
	if (commandId == s_lastCommandId && signature == s_lastDiagSignature)
	{
		return;
	}
	s_lastCommandId = commandId;
	s_lastDiagSignature = signature;
	logCommandDiagnostic(
		"authority NOT_AI_CONTROLLED command_id=" + commandId.substr(0, 8) + "... unit=" + std::to_string(unitId) +
			" control_read=" + (d.controlFileReadOk ? "1" : "0") +
			" nonempty=" + (d.controlFileNonEmpty ? "1" : "0") +
			" parse=" + (d.controlJsonParseOk ? "1" : "0") +
			" soldier_in_tactical_ai_set=" + (d.soldierInTacticalAiSet ? "1" : "0") +
			" campaign_mode=" + std::string(d.campaignMode == CompanionSoldierOwnershipCore::CONTROL_AI ? "AI" : "HUMAN") +
			" relay_binding=" + (d.relayBindingMatch ? "1" : "0") +
			" assignment_present=" + (d.assignmentPresent ? "1" : "0") +
			" assignment_ai=" + (d.assignmentIsAi ? "1" : "0") +
			" durable_ids_in_doc=" + (d.durableIdsInDocument ? "1" : "0") +
			" relay_campaign_nonempty=" + (d.relayCampaignIdNonEmpty ? "1" : "0") +
			" relay_tactical_nonempty=" + (d.relayTacticalBattleIdNonEmpty ? "1" : "0") +
			" campaign_id_match=" + (d.campaignIdMatch ? "1" : "0") +
			" tactical_battle_id_match=" + (d.tacticalBattleIdMatch ? "1" : "0") +
			" session_bind_match=" + (d.sessionBindingMatch ? "1" : "0") +
			" relay_ai_controlled=" + (d.relayAiControlled ? "1" : "0") +
			" ai_command_permitted=" + (d.aiCommandPermitted ? "1" : "0"));
}

void maybeLogNotAiControlledReject(
	const std::string &rejectCode,
	const std::string &commandId,
	int actorUnitId,
	SavedGame *campaign,
	SavedBattleGame *battle,
	const std::string &controlJson,
	bool controlFileReadOk,
	const std::string &sessionId)
{
	if (rejectCode != "NOT_AI_CONTROLLED" || actorUnitId <= 0 || battle == 0)
	{
		return;
	}
	BattleUnit *actor = findUnitById(battle, actorUnitId);
	const bool liveActor = isCompanionMoveActor(actor, campaign);
	CompanionSoldierOwnershipCore::RelayControlBindingContext ctx = {};
	ctx.battleSessionId = sessionId;
	if (campaign != 0)
	{
		ctx.campaignId = campaign->getCompanionCampaignId();
	}
	ctx.tacticalBattleId = battle->getCompanionTacticalBattleId();

	const std::set<int> &tacticalAiIds = battle->getCompanionTacticalAiSoldierIds();
	const bool soldierInTacticalAi = tacticalAiIds.find(actorUnitId) != tacticalAiIds.end();

	const CompanionSoldierOwnershipCore::SoldierControlMode campaignMode =
		CompanionCampaignOwnershipCore::resolveCampaignSoldierControlMode(tacticalAiIds, actorUnitId, liveActor);

	const CompanionSoldierOwnershipCore::RelayControlDiagnostics d =
		CompanionSoldierOwnershipCore::computeRelayControlDiagnostics(
			controlJson, controlFileReadOk, ctx, actorUnitId, campaignMode, soldierInTacticalAi);
	const std::string cid = commandId.empty() ? "unknown" : commandId;
	logNotAiControlledAuthorityOnce(cid, d, actorUnitId);
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
	std::string json;
	if (isThrowKind(g_active.kind))
	{
		json = buildThrowResultJson(
			g_active.throwParsed,
			CompanionCommandCore::CMD_INTERRUPTED,
			"Battle binding reset.",
			g_active.startPos,
			g_active.startPos,
			0,
			false,
			g_lastSnapshotSha256,
			g_active.inboxDigest);
	}
	else if (isDirectedFireKind(g_active.kind))
	{
		json = buildSnapShotResultJson(
			g_active.snapParsed,
			CompanionCommandCore::CMD_INTERRUPTED,
			"Battle binding reset.",
			g_active.startPos,
			g_active.startPos,
			0,
			false,
			g_lastSnapshotSha256,
			g_active.inboxDigest);
	}
	else
	{
		json = buildMoveResultJson(
			g_active.moveParsed,
			CompanionCommandCore::CMD_INTERRUPTED,
			"Battle binding reset.",
			g_active.startPos,
			g_active.startPos,
			0,
			false,
			false,
			g_lastSnapshotSha256,
			g_active.inboxDigest);
	}
	writeResultFile(resultPath, json);
	logCommandDiagnostic("in-flight command interrupted command_id=" + g_active.commandId.substr(0, 8) +
		"... reason=binding_reset");
	g_active.inFlight = false;
	g_active.kind = ActiveCommand::KIND_NONE;
	clearRosterCasualtyDeferClock();
}

std::string snapshotSha256ForTerminalCommandResult(
	SavedBattleGame *battle,
	CompanionCommandCore::CommandLifecycleStatus terminalStatus)
{
	std::string snapshotSha256 = g_lastSnapshotSha256;
	if (battle != 0 &&
	    CompanionCommandRuntimeSupport::shouldExportSnapshotBeforeTerminalCommandResult(terminalStatus))
	{
		CompanionSnapshot::tryExport(battle, &snapshotSha256);
	}
	return snapshotSha256;
}

static void clearRosterCasualtyDeferClock()
{
	g_active.rosterCasualtyDeferStartedAtMs = 0;
}

static void beginInFlightCommand()
{
	g_active.inFlight = true;
	clearRosterCasualtyDeferClock();
}

static bool shouldDelayInFlightTerminalCompletion(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	CompanionCommandCore::CommandLifecycleStatus terminal)
{
	if (terminal == CompanionCommandCore::CMD_EXECUTING)
	{
		return true;
	}
	if (!CompanionCommandRuntimeSupport::shouldExportSnapshotBeforeTerminalCommandResult(terminal))
	{
		clearRosterCasualtyDeferClock();
		return false;
	}
	if (!CompanionCommandCasualtySettlement::isRosterCasualtySnapshotWorkUnsettled(game, battle))
	{
		clearRosterCasualtyDeferClock();
		return false;
	}
	const long long nowMs = currentEpochMillis();
	if (g_active.rosterCasualtyDeferStartedAtMs == 0)
	{
		g_active.rosterCasualtyDeferStartedAtMs = nowMs;
		logCommandDiagnostic("deferring terminal snapshot for roster casualty settlement command_id="
			+ g_active.commandId.substr(0, 8) + "...");
	}
	if (CompanionCommandCasualtySettlementCore::shouldDeferRosterCasualtySettlement(
		    g_active.rosterCasualtyDeferStartedAtMs, nowMs))
	{
		return true;
	}
	logCommandDiagnostic("roster casualty settlement defer timeout; finalizing terminal snapshot command_id="
		+ g_active.commandId.substr(0, 8) + "...");
	clearRosterCasualtyDeferClock();
	return false;
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

static std::string activeInFlightCommandSessionId()
{
	if (!g_active.inFlight)
	{
		return std::string();
	}
	switch (g_active.kind)
	{
	case ActiveCommand::KIND_MOVE:
		return g_active.moveParsed.sessionId;
	case ActiveCommand::KIND_SNAP_SHOT:
	case ActiveCommand::KIND_AUTO_SHOT:
	case ActiveCommand::KIND_AIMED_SHOT:
		return g_active.snapParsed.sessionId;
	case ActiveCommand::KIND_THROW:
		return g_active.throwParsed.sessionId;
	default:
		return std::string();
	}
}

static bool tryTerminalizePersistedCrossSessionOpenResult(
	SavedBattleGame *battle,
	const std::string &liveSessionId,
	const std::string &commandPath,
	const std::string &resultPath,
	const std::string &journalPath)
{
	if (battle == 0 || liveSessionId.empty())
	{
		return false;
	}

	std::string resultJson;
	if (!readTextFile(resultPath, &resultJson))
	{
		return false;
	}
	std::string resultCommandId;
	std::string resultStatus;
	std::string resultSessionId;
	std::string resultInboxDigest;
	if (!CompanionCommandRuntimeSupport::parseTacticalResultSessionBinding(
		    resultJson, &resultCommandId, &resultStatus, &resultSessionId, &resultInboxDigest))
	{
		return false;
	}
	if (resultCommandId.empty())
	{
		return false;
	}

	std::string journalJson;
	readTextFile(journalPath, &journalJson);
	if (!journalContainsCommandId(journalJson, resultCommandId))
	{
		return false;
	}

	std::string commandJson;
	const bool inboxPresent = readTextFile(commandPath, &commandJson);
	std::string inboxCommandId;
	std::string inboxSessionId;
	std::string inboxCommandType;
	if (inboxPresent)
	{
		CompanionJsonLite::extractJsonStringField(commandJson, "command_id", &inboxCommandId);
		CompanionJsonLite::extractJsonStringField(commandJson, "session_id", &inboxSessionId);
		CompanionJsonLite::extractJsonStringField(commandJson, "command_type", &inboxCommandType);
	}
	else
	{
		CompanionJsonLite::extractJsonStringField(resultJson, "command_type", &inboxCommandType);
	}

	if (!CompanionCommandRuntimeSupport::shouldRecoverPersistedCrossSessionOrphan(
		    liveSessionId,
		    inboxSessionId,
		    resultSessionId,
		    resultStatus,
		    inboxCommandId,
		    resultCommandId,
		    g_active.inFlight,
		    g_active.inFlight ? g_active.commandId : std::string(),
		    activeInFlightCommandSessionId()))
	{
		return false;
	}

	if (inboxPresent)
	{
		if (inboxCommandId != resultCommandId)
		{
			return false;
		}
		if (!resultInboxDigest.empty())
		{
			std::string inboxDigest;
			if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest) ||
			    inboxDigest != resultInboxDigest)
			{
				return false;
			}
		}
	}
	else
	{
		if (inboxCommandType.empty())
		{
			CompanionJsonLite::extractJsonStringField(resultJson, "command_type", &inboxCommandType);
		}
		if (inboxSessionId.empty())
		{
			inboxSessionId = resultSessionId;
		}
		inboxCommandId = resultCommandId;
		if (inboxCommandType.empty() || resultCommandId.empty() || resultInboxDigest.empty())
		{
			return false;
		}
	}

	const char *publicReason =
		CompanionCommandRuntimeSupport::sessionBoundaryInterruptedPublicReasonForCommandType(inboxCommandType);
	const std::string resultSnapshotSha256 =
		snapshotSha256ForTerminalCommandResult(battle, CompanionCommandCore::CMD_INTERRUPTED);
	std::string inboxDigestHex;
	if (inboxPresent)
	{
		if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigestHex))
		{
			return false;
		}
	}
	else
	{
		inboxDigestHex = resultInboxDigest;
	}

	std::string json;
	if (inboxCommandType == "MOVE")
	{
		CompanionCommandCore::MoveCommandParsed parsed = {};
		if (!(inboxPresent
		          ? CompanionCommandCore::parseMoveCommandJson(commandJson, &parsed)
		          : CompanionCommandRuntimeSupport::parseMoveParsedFromOpenResultJson(resultJson, &parsed)))
		{
			return false;
		}
		BattleUnit *actor = findUnitById(battle, parsed.actorUnitId);
		const Position pos = actor != 0 ? actor->getPosition() : Position(0, 0, 0);
		json = buildMoveResultJson(
			parsed,
			CompanionCommandCore::CMD_INTERRUPTED,
			publicReason,
			pos,
			pos,
			actor != 0 ? actor->getTimeUnits() : 0,
			actor != 0 && actor->isKneeled(),
			false,
			resultSnapshotSha256,
			inboxDigestHex);
	}
	else if (inboxCommandType == "SNAP_SHOT" || inboxCommandType == "AUTO_SHOT" || inboxCommandType == "AIMED_SHOT")
	{
		CompanionCommandCore::SnapShotCommandParsed snapParsed = {};
		if (!(inboxPresent
		          ? CompanionCommandCore::parseSnapShotCommandJson(commandJson, &snapParsed)
		          : CompanionCommandRuntimeSupport::parseSnapShotParsedFromOpenResultJson(resultJson, &snapParsed)))
		{
			return false;
		}
		BattleUnit *actor = findUnitById(battle, snapParsed.actorUnitId);
		const Position pos = actor != 0 ? actor->getPosition() : Position(0, 0, 0);
		json = buildSnapShotResultJson(
			snapParsed,
			CompanionCommandCore::CMD_INTERRUPTED,
			publicReason,
			pos,
			pos,
			actor != 0 ? actor->getTimeUnits() : 0,
			actor != 0 && actor->isKneeled(),
			resultSnapshotSha256,
			inboxDigestHex);
	}
	else if (inboxCommandType == "KNEEL")
	{
		CompanionCommandCore::KneelCommandParsed kneelParsed = {};
		if (!(inboxPresent
		          ? CompanionCommandCore::parseKneelCommandJson(commandJson, &kneelParsed)
		          : CompanionCommandRuntimeSupport::parseKneelParsedFromOpenResultJson(resultJson, &kneelParsed)))
		{
			return false;
		}
		BattleUnit *actor = findUnitById(battle, kneelParsed.actorUnitId);
		const Position pos = actor != 0 ? actor->getPosition() : Position(0, 0, 0);
		json = buildKneelResultJson(
			kneelParsed,
			CompanionCommandCore::CMD_INTERRUPTED,
			publicReason,
			pos,
			actor != 0 ? actor->getTimeUnits() : 0,
			actor != 0 && actor->isKneeled(),
			resultSnapshotSha256,
			inboxDigestHex);
	}
	else if (inboxCommandType == "STAND")
	{
		CompanionCommandCore::StandCommandParsed standParsed = {};
		if (inboxPresent)
		{
			if (!CompanionCommandCore::parseStandCommandJson(commandJson, &standParsed))
			{
				return false;
			}
		}
		else if (!CompanionCommandRuntimeSupport::parseKneelParsedFromOpenResultJson(resultJson, &standParsed))
		{
			return false;
		}
		BattleUnit *actor = findUnitById(battle, standParsed.actorUnitId);
		const Position pos = actor != 0 ? actor->getPosition() : Position(0, 0, 0);
		json = buildKneelResultJson(
			standParsed,
			CompanionCommandCore::CMD_INTERRUPTED,
			publicReason,
			pos,
			actor != 0 ? actor->getTimeUnits() : 0,
			actor != 0 && actor->isKneeled(),
			resultSnapshotSha256,
			inboxDigestHex);
	}
	else
	{
		return false;
	}

	writeResultFile(resultPath, json);
	logTerminalOnce(resultCommandId, CompanionCommandCore::CMD_INTERRUPTED);
	logCommandDiagnostic(
		"persisted cross-session orphan command_id=" + resultCommandId.substr(0, 8) + "...");
	if (g_active.inFlight && g_active.commandId == resultCommandId)
	{
		g_active.inFlight = false;
		g_active.kind = ActiveCommand::KIND_NONE;
		clearRosterCasualtyDeferClock();
	}
	tryArchiveStaleInboxForSessionChange(battle);
	g_inboxCachedMtime = 0;
	return true;
}

static void tryRecoverPersistedCrossSessionOrphanOnBinding(SavedBattleGame *battle)
{
	const std::string dir = snapshotDirectory();
	if (dir.empty() || battle == 0)
	{
		return;
	}
	const std::string liveSessionId = battle->getCompanionEventLog().getSessionId();
	tryTerminalizePersistedCrossSessionOpenResult(
		battle,
		liveSessionId,
		joinPath(dir, "tactical-command.json"),
		joinPath(dir, "tactical-result.json"),
		joinPath(dir, "tactical-command-journal.json"));
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
	tryRecoverPersistedCrossSessionOrphanOnBinding(battle);
	const bool hadPriorContext = !g_boundSessionId.empty() || !g_boundTacticalBattleId.empty();
	const bool contextChanged = hadPriorContext &&
		(sessionId != g_boundSessionId || tacticalId != g_boundTacticalBattleId);
	if (contextChanged)
	{
		CompanionRelayPublicationFreshnessCore::onBattleSessionContextChanged();
		const std::string dir = snapshotDirectory();
		if (!dir.empty())
		{
			interruptInFlightForBindingReset(joinPath(dir, "tactical-result.json"));
		}
		g_active.inFlight = false;
		g_active.kind = ActiveCommand::KIND_NONE;
		clearRosterCasualtyDeferClock();
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

int getInFlightActorUnitId()
{
	if (!g_active.inFlight)
	{
		return -1;
	}
	switch (g_active.kind)
	{
	case ActiveCommand::KIND_MOVE:
		return g_active.moveParsed.actorUnitId;
	case ActiveCommand::KIND_SNAP_SHOT:
	case ActiveCommand::KIND_AUTO_SHOT:
	case ActiveCommand::KIND_AIMED_SHOT:
		return g_active.snapParsed.actorUnitId;
	case ActiveCommand::KIND_THROW:
		return g_active.throwParsed.actorUnitId;
	default:
		return -1;
	}
}

void notifySnapShotProjectileLaunched(int actorUnitId)
{
	if (!g_active.inFlight || !isDirectedFireKind(g_active.kind))
	{
		return;
	}
	if (actorUnitId != g_active.snapParsed.actorUnitId)
	{
		return;
	}
	g_active.snapProjectileLaunched = true;
}

void notifyThrowProjectileLaunched(int actorUnitId)
{
	if (!g_active.inFlight || !isThrowKind(g_active.kind))
	{
		return;
	}
	if (actorUnitId != g_active.throwParsed.actorUnitId)
	{
		return;
	}
	g_active.snapProjectileLaunched = true;
}

void noteSnapShotBattleActionPop(int actorUnitId, const std::string &actionResult)
{
	if (!g_active.inFlight || !isDirectedFireKind(g_active.kind))
	{
		return;
	}
	if (actorUnitId != g_active.snapParsed.actorUnitId || g_active.snapProjectileLaunched)
	{
		return;
	}
	if (!actionResult.empty())
	{
		g_active.snapActionResult = actionResult;
	}
}

void noteThrowBattleActionPop(int actorUnitId, const std::string &actionResult)
{
	if (!g_active.inFlight || !isThrowKind(g_active.kind))
	{
		return;
	}
	if (actorUnitId != g_active.throwParsed.actorUnitId || g_active.snapProjectileLaunched)
	{
		return;
	}
	if (!actionResult.empty())
	{
		g_active.snapActionResult = actionResult;
	}
}

void noteWalkBattleActionPop(int actorUnitId, const std::string &actionResult)
{
	if (!g_active.inFlight || g_active.kind != ActiveCommand::KIND_MOVE)
	{
		return;
	}
	if (actorUnitId != g_active.moveParsed.actorUnitId)
	{
		return;
	}
	if (!actionResult.empty())
	{
		g_active.walkActionResult = actionResult;
	}
}

static bool tryResolveOrphanedDirectedFireSessionBoundary(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	const std::string &liveSessionId,
	const std::string &commandPath,
	const std::string &resultPath,
	const std::string &journalPath)
{
	if (g_active.inFlight || battle == 0)
	{
		return false;
	}
	return tryTerminalizePersistedCrossSessionOpenResult(
		battle, liveSessionId, commandPath, resultPath, journalPath);
}

static bool tryRehydrateOrphanedDirectedFireActive(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	const std::string &liveSessionId,
	const std::string &commandPath,
	const std::string &resultPath,
	const std::string &journalPath)
{
	if (g_active.inFlight || game == 0 || battle == 0)
	{
		return false;
	}
	std::string resultJson;
	if (!readTextFile(resultPath, &resultJson))
	{
		return false;
	}
	std::string status;
	std::string resultCommandId;
	std::string resultInboxDigest;
	CompanionJsonLite::extractJsonStringField(resultJson, "status", &status);
	if (status != "EXECUTING")
	{
		return false;
	}
	CompanionJsonLite::extractJsonStringField(resultJson, "command_id", &resultCommandId);
	std::string resultSessionId;
	CompanionJsonLite::extractJsonStringField(resultJson, "session_id", &resultSessionId);
	if (resultCommandId.empty())
	{
		return false;
	}

	std::string journalJson;
	readTextFile(journalPath, &journalJson);
	if (!journalContainsCommandId(journalJson, resultCommandId))
	{
		return false;
	}

	std::string commandJson;
	if (!readTextFile(commandPath, &commandJson))
	{
		return false;
	}
	std::string inboxCommandId;
	std::string inboxCommandType;
	CompanionJsonLite::extractJsonStringField(commandJson, "command_id", &inboxCommandId);
	CompanionJsonLite::extractJsonStringField(commandJson, "command_type", &inboxCommandType);
	if (inboxCommandId != resultCommandId)
	{
		return false;
	}
	if (inboxCommandType != "SNAP_SHOT" && inboxCommandType != "AUTO_SHOT" && inboxCommandType != "AIMED_SHOT")
	{
		return false;
	}

	CompanionCommandCore::SnapShotCommandParsed snapParsed = {};
	if (!CompanionCommandCore::parseSnapShotCommandJson(commandJson, &snapParsed))
	{
		return false;
	}

	if (!CompanionCommandRuntimeSupport::shouldRehydrateOrphanedDirectedFireForSession(
		    liveSessionId, snapParsed.sessionId, resultSessionId))
	{
		return false;
	}

	std::string inboxDigest;
	if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest))
	{
		return false;
	}
	CompanionJsonLite::extractJsonStringField(resultJson, "inbox_sha256", &resultInboxDigest);
	if (!resultInboxDigest.empty() && resultInboxDigest != inboxDigest)
	{
		return false;
	}

	BattleUnit *actor = findUnitById(battle, snapParsed.actorUnitId);
	beginInFlightCommand();
	g_active.kind = directedFireKindForCommandType(inboxCommandType);
	g_active.commandId = resultCommandId;
	g_active.inboxDigest = inboxDigest;
	g_active.snapParsed = snapParsed;
	g_active.startPos = actor != 0 ? actor->getPosition() : Position(0, 0, 0);
	g_active.snapProjectileLaunched = false;
	g_active.snapActionResult.clear();

	logCommandDiagnostic("rehydrated orphaned in-flight command_id=" + resultCommandId.substr(0, 8) + "...");
	return true;
}

void tick(BattlescapeGame *game, SavedBattleGame *battle, SavedGame *campaign, bool battleInitialized)
{
	const CommandTickDepthGuard depthGuard;
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
	const bool controlReadOk = readTextFile(controlPath, &controlJson);
	const std::string &sessionId = battle->getCompanionEventLog().getSessionId();

	if (!tryRehydrateOrphanedDirectedFireActive(game, battle, sessionId, commandPath, resultPath, journalPath))
	{
		tryResolveOrphanedDirectedFireSessionBoundary(
			game, battle, sessionId, commandPath, resultPath, journalPath);
	}

	if (g_active.inFlight)
	{
		if (CompanionCommandRuntimeSupport::shouldDeferInFlightCommandCompletionWhileReentrant(g_commandTickDepth))
		{
			return;
		}
		if (isThrowKind(g_active.kind))
		{
			BattleUnit *unit = findUnitById(battle, g_active.throwParsed.actorUnitId);
			const CompanionTacticalAuthorityCore::TacticalAuthorityDecision authority =
				tacticalAuthorityForActor(campaign, battle, unit, controlJson, sessionId);
			const bool aiCommandPermitted = authority.aiCommandPermitted;
			if (unit != 0 && !aiCommandPermitted)
			{
				game->cancelCurrentAction();
			}
			CompanionCommandCore::SnapShotCompletionInput done = {};
			done.battleBusy = game->isBusy();
			done.actorAlive = unit != 0 && !unit->isOut();
			done.actorStillAiControlled = aiCommandPermitted;
			done.projectileLaunched = g_active.snapProjectileLaunched;
			done.actionResult = g_active.snapActionResult;

			const CompanionCommandCore::CommandLifecycleStatus terminal =
				CompanionCommandCore::evaluateThrowTerminalStatus(done);
			if (shouldDelayInFlightTerminalCompletion(game, battle, terminal))
			{
				return;
			}

			std::string publicReason;
			if (terminal == CompanionCommandCore::CMD_INTERRUPTED)
			{
				publicReason = "Grenade throw interrupted or incomplete.";
			}
			else if (terminal == CompanionCommandCore::CMD_FAILED)
			{
				publicReason = !done.actionResult.empty()
					? done.actionResult
					: "Grenade throw did not resolve.";
			}

			const std::string resultSnapshotSha256 =
				snapshotSha256ForTerminalCommandResult(battle, terminal);
			const Position finalPos = unit != 0 ? unit->getPosition() : g_active.startPos;
			const std::string json = buildThrowResultJson(
				g_active.throwParsed,
				terminal,
				publicReason,
				g_active.startPos,
				finalPos,
				unit != 0 ? unit->getTimeUnits() : 0,
				unit != 0 && unit->isKneeled(),
				resultSnapshotSha256,
				g_active.inboxDigest);
			writeResultFile(resultPath, json);
			logTerminalOnce(g_active.commandId, terminal);
			g_active.inFlight = false;
			g_active.kind = ActiveCommand::KIND_NONE;
			clearRosterCasualtyDeferClock();
			return;
		}

		if (isDirectedFireKind(g_active.kind))
		{
			BattleUnit *unit = findUnitById(battle, g_active.snapParsed.actorUnitId);
			const CompanionTacticalAuthorityCore::TacticalAuthorityDecision authority =
				tacticalAuthorityForActor(campaign, battle, unit, controlJson, sessionId);
			const bool aiCommandPermitted = authority.aiCommandPermitted;
			if (unit != 0 && !aiCommandPermitted)
			{
				game->cancelCurrentAction();
			}
			CompanionCommandCore::SnapShotCompletionInput done = {};
			done.battleBusy = game->isBusy();
			done.actorAlive = unit != 0 && !unit->isOut();
			done.actorStillAiControlled = aiCommandPermitted;
			done.projectileLaunched = g_active.snapProjectileLaunched;
			done.actionResult = g_active.snapActionResult;

			const CompanionCommandCore::CommandLifecycleStatus terminal =
				CompanionCommandCore::evaluateSnapShotTerminalStatus(done);
			if (shouldDelayInFlightTerminalCompletion(game, battle, terminal))
			{
				return;
			}

			std::string publicReason;
			if (terminal == CompanionCommandCore::CMD_INTERRUPTED)
			{
				publicReason = "Weapon fire interrupted or incomplete.";
			}
			else if (terminal == CompanionCommandCore::CMD_FAILED)
			{
				publicReason = !done.actionResult.empty()
					? done.actionResult
					: "Weapon fire did not resolve.";
			}

			const std::string resultSnapshotSha256 =
				snapshotSha256ForTerminalCommandResult(battle, terminal);
			const Position finalPos = unit != 0 ? unit->getPosition() : g_active.startPos;
			const std::string json = buildSnapShotResultJson(
				g_active.snapParsed,
				terminal,
				publicReason,
				g_active.startPos,
				finalPos,
				unit != 0 ? unit->getTimeUnits() : 0,
				unit != 0 && unit->isKneeled(),
				resultSnapshotSha256,
				g_active.inboxDigest);
			writeResultFile(resultPath, json);
			logTerminalOnce(g_active.commandId, terminal);
			g_active.inFlight = false;
			g_active.kind = ActiveCommand::KIND_NONE;
			clearRosterCasualtyDeferClock();
			return;
		}

		BattleUnit *unit = findUnitById(battle, g_active.moveParsed.actorUnitId);
		const CompanionTacticalAuthorityCore::TacticalAuthorityDecision authority =
			tacticalAuthorityForActor(campaign, battle, unit, controlJson, sessionId);
		const bool aiCommandPermitted = authority.aiCommandPermitted;
		if (unit != 0 && !aiCommandPermitted)
		{
			game->cancelCurrentAction();
		}
		CompanionCommandCore::WalkCompletionInput done = {};
		done.requestedDest = Position(g_active.moveParsed.destX, g_active.moveParsed.destY, g_active.moveParsed.destZ);
		done.startPos = g_active.startPos;
		done.currentPos = unit != 0 ? unit->getPosition() : g_active.startPos;
		done.battleBusy = game->isBusy();
		done.actorAlive = unit != 0 && !unit->isOut();
		done.actorStillAiControlled = aiCommandPermitted;
		done.actionResult = g_active.walkActionResult;

		const CompanionCommandCore::CommandLifecycleStatus terminal =
			CompanionCommandCore::evaluateWalkTerminalStatus(done);
		if (shouldDelayInFlightTerminalCompletion(game, battle, terminal))
		{
			return;
		}

		const bool reached = unit != 0 && unit->getPosition() == done.requestedDest;
		const std::string resultSnapshotSha256 =
			snapshotSha256ForTerminalCommandResult(battle, terminal);
		const CompanionCommandCore::WalkStopDiagnostics stopDiagnostics =
			CompanionCommandCore::resolveWalkStopDiagnostics(terminal, done);
		std::string publicReason = stopDiagnostics.publicReason;
		if (publicReason.empty()
		    && terminal == CompanionCommandCore::CMD_INTERRUPTED)
		{
			publicReason = "Movement interrupted or incomplete.";
		}
		const std::string json = buildMoveResultJson(
			g_active.moveParsed,
			terminal,
			publicReason,
			g_active.startPos,
			unit != 0 ? unit->getPosition() : done.startPos,
			unit != 0 ? unit->getTimeUnits() : 0,
			unit != 0 && unit->isKneeled(),
			reached,
			resultSnapshotSha256,
			g_active.inboxDigest,
			stopDiagnostics.movementStopReason.empty() ? 0 : &stopDiagnostics);
		writeResultFile(resultPath, json);
		if (terminal == CompanionCommandCore::CMD_COMPLETED
		    || terminal == CompanionCommandCore::CMD_INTERRUPTED
		    || terminal == CompanionCommandCore::CMD_FAILED)
		{
			CompanionRelayPublicationFreshnessCore::noteTacticalBattleMutation();
		}
		logTerminalOnce(g_active.commandId, terminal);
		g_active.inFlight = false;
		g_active.kind = ActiveCommand::KIND_NONE;
		clearRosterCasualtyDeferClock();
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
	if (!inboxCommandType.empty() && inboxCommandType != "MOVE" && inboxCommandType != "KNEEL" &&
	    inboxCommandType != "STAND" &&
	    inboxCommandType != "SNAP_SHOT" && inboxCommandType != "AUTO_SHOT" && inboxCommandType != "AIMED_SHOT" &&
	    inboxCommandType != "PRIME" && inboxCommandType != "THROW" && inboxCommandType != "RELOAD" &&
	    inboxCommandType != "UNLOAD" && inboxCommandType != "INVENTORY_MOVE" &&
	    inboxCommandType != "PICKUP" && inboxCommandType != "DROP")
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

	if (inboxCommandType == "PRIME")
	{
		CompanionCommandCore::PrimeCommandParsed primeParsed = {};
		if (!CompanionCommandCore::parsePrimeCommandJson(commandJson, &primeParsed))
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			rejectPrimeCommand(resultPath, primeParsed, primeParsed.rejectCode, primeParsed.rejectCode, inboxDigest);
			logRejectOnce(primeParsed.commandId.empty() ? "unknown" : primeParsed.commandId, primeParsed.rejectCode);
			return;
		}

		logInboxDetectedOnce(primeParsed.commandId);

		std::string inboxDigest;
		if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest))
		{
			return;
		}

		std::string journalJson;
		readTextFile(journalPath, &journalJson);
		if (rejectUnreadableCommandJournal(resultPath, journalJson, inboxDigest, 0, 0, 0, &primeParsed, 0, 0, 0, 0, 0, 0))
		{
			logRejectOnce(primeParsed.commandId, "JOURNAL_MALFORMED");
			return;
		}
		const bool duplicate = journalContainsCommandId(journalJson, primeParsed.commandId);

		std::string existingResultJson;
		readTextFile(resultPath, &existingResultJson);
		const CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision =
			CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
				duplicate, primeParsed.commandId, inboxDigest, existingResultJson, g_active.inFlight);
		if (applyDuplicateInboxDecision(
			duplicateDecision, primeParsed.commandId, inboxDigest, resultPath, conflictPath,
			existingResultJson, 0, 0, 0, &primeParsed, 0, 0, 0, 0, 0, 0))
		{
			return;
		}

		BattleUnit *actor = findUnitById(battle, primeParsed.actorUnitId);
		const CompanionTacticalAuthorityCore::TacticalAuthorityDecision authority =
			tacticalAuthorityForActor(campaign, battle, actor, controlJson, sessionId);
		const bool liveActor = isCompanionMoveActor(actor, campaign);
		const std::set<int> &tacticalAiIds = battle->getCompanionTacticalAiSoldierIds();
		const CompanionTacticalAuthorityCore::TacticalAuthorityInput authorityInput =
			CompanionTacticalAuthorityCore::buildTacticalAuthorityInput(
				campaign != 0,
				tacticalAiIds,
				liveActor,
				primeParsed.actorUnitId,
				controlJson,
				sessionId,
				campaign != 0 ? campaign->getCompanionCampaignId() : std::string(),
				battle->getCompanionTacticalBattleId());

		CompanionCommandCore::PrimeValidationInput primeInput = {};
		fillPrimeValidationInput(
			&primeInput,
			game,
			battle,
			battleInitialized,
			sessionId,
			primeParsed,
			primeParsed.actorUnitId,
			liveActor,
			actor != 0 && !actor->isOut(),
			authority.aiCommandPermitted,
			CompanionTacticalAuthorityCore::isStaleRelayAiAfterCampaignHumanAssignment(
				authorityInput, controlJson, sessionId),
			actor);

		const CompanionCommandCore::MoveValidationResult primeVerdict =
			CompanionCommandCore::validatePrimeCommand(primeInput);
		if (!primeVerdict.accepted)
		{
			rejectPrimeCommand(resultPath, primeParsed, primeVerdict.rejectCode, primeVerdict.publicReason, inboxDigest);
			appendJournalEntry(journalPath, primeParsed.commandId);
			logRejectOnce(primeParsed.commandId, primeVerdict.rejectCode);
			maybeLogNotAiControlledReject(
				primeVerdict.rejectCode,
				primeParsed.commandId,
				primeParsed.actorUnitId,
				campaign,
				battle,
				controlJson,
				controlReadOk,
				sessionId);
			return;
		}

		logCommandDiagnostic("accepted command_id=" + primeParsed.commandId.substr(0, 8) +
			"... actor=" + std::to_string(primeParsed.actorUnitId));

		BattleItem *weapon = actor != 0 ? actor->getItem(primeParsed.activeHand) : 0;
		if (actor != 0 && actor->getActiveHand() != primeParsed.activeHand)
		{
			actor->setActiveHand(primeParsed.activeHand);
		}
		std::string rejectReason;
		if (!game->tryExecuteCompanionPrime(actor, weapon, primeParsed.fuseTimer, &rejectReason))
		{
			rejectPrimeCommand(resultPath, primeParsed, "PRIME_START_FAILED", rejectReason, inboxDigest);
			appendJournalEntry(journalPath, primeParsed.commandId);
			logRejectOnce(primeParsed.commandId, "PRIME_START_FAILED");
			return;
		}

		const std::string resultSnapshotSha256 =
			snapshotSha256ForTerminalCommandResult(battle, CompanionCommandCore::CMD_COMPLETED);
		writeResultFile(resultPath, buildPrimeResultJson(primeParsed, CompanionCommandCore::CMD_COMPLETED, std::string(),
			actor != 0 ? actor->getPosition() : Position(0, 0, 0),
			actor != 0 ? actor->getTimeUnits() : 0,
			actor != 0 && actor->isKneeled(),
			resultSnapshotSha256, inboxDigest));
		logTerminalOnce(primeParsed.commandId, CompanionCommandCore::CMD_COMPLETED);
		appendJournalEntry(journalPath, primeParsed.commandId);
		return;
	}

	if (inboxCommandType == "RELOAD")
	{
		CompanionCommandCore::ReloadCommandParsed reloadParsed = {};
		if (!CompanionCommandCore::parseReloadCommandJson(commandJson, &reloadParsed))
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			rejectReloadCommand(resultPath, reloadParsed, reloadParsed.rejectCode, reloadParsed.rejectCode, inboxDigest);
			logRejectOnce(reloadParsed.commandId.empty() ? "unknown" : reloadParsed.commandId, reloadParsed.rejectCode);
			return;
		}

		logInboxDetectedOnce(reloadParsed.commandId);

		std::string inboxDigest;
		if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest))
		{
			return;
		}

		std::string journalJson;
		readTextFile(journalPath, &journalJson);
		if (rejectUnreadableCommandJournal(resultPath, journalJson, inboxDigest, 0, 0, 0, 0, 0, &reloadParsed, 0, 0, 0, 0))
		{
			logRejectOnce(reloadParsed.commandId, "JOURNAL_MALFORMED");
			return;
		}
		const bool duplicate = journalContainsCommandId(journalJson, reloadParsed.commandId);

		std::string existingResultJson;
		readTextFile(resultPath, &existingResultJson);
		const CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision =
			CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
				duplicate, reloadParsed.commandId, inboxDigest, existingResultJson, g_active.inFlight);
		if (applyDuplicateInboxDecision(
			duplicateDecision, reloadParsed.commandId, inboxDigest, resultPath, conflictPath,
			existingResultJson, 0, 0, 0, 0, 0, &reloadParsed, 0, 0, 0, 0))
		{
			return;
		}

		BattleUnit *actor = findUnitById(battle, reloadParsed.actorUnitId);
		const CompanionTacticalAuthorityCore::TacticalAuthorityDecision authority =
			tacticalAuthorityForActor(campaign, battle, actor, controlJson, sessionId);
		const bool liveActor = isCompanionMoveActor(actor, campaign);
		const std::set<int> &tacticalAiIds = battle->getCompanionTacticalAiSoldierIds();
		const CompanionTacticalAuthorityCore::TacticalAuthorityInput authorityInput =
			CompanionTacticalAuthorityCore::buildTacticalAuthorityInput(
				campaign != 0,
				tacticalAiIds,
				liveActor,
				reloadParsed.actorUnitId,
				controlJson,
				sessionId,
				campaign != 0 ? campaign->getCompanionCampaignId() : std::string(),
				battle->getCompanionTacticalBattleId());

		CompanionCommandCore::ReloadValidationInput reloadInput = {};
		fillReloadValidationInput(
			&reloadInput,
			game,
			battle,
			campaign,
			battleInitialized,
			sessionId,
			reloadParsed,
			reloadParsed.actorUnitId,
			liveActor,
			actor != 0 && !actor->isOut(),
			authority.aiCommandPermitted,
			CompanionTacticalAuthorityCore::isStaleRelayAiAfterCampaignHumanAssignment(
				authorityInput, controlJson, sessionId),
			actor);

		const CompanionCommandCore::MoveValidationResult reloadVerdict =
			CompanionCommandCore::validateReloadCommand(reloadInput);
		if (!reloadVerdict.accepted)
		{
			rejectReloadCommand(resultPath, reloadParsed, reloadVerdict.rejectCode, reloadVerdict.publicReason, inboxDigest);
			appendJournalEntry(journalPath, reloadParsed.commandId);
			logRejectOnce(reloadParsed.commandId, reloadVerdict.rejectCode);
			maybeLogNotAiControlledReject(
				reloadVerdict.rejectCode,
				reloadParsed.commandId,
				reloadParsed.actorUnitId,
				campaign,
				battle,
				controlJson,
				controlReadOk,
				sessionId);
			return;
		}

		logCommandDiagnostic("accepted command_id=" + reloadParsed.commandId.substr(0, 8) +
			"... actor=" + std::to_string(reloadParsed.actorUnitId));

		BattleItem *weapon = findBattleItemByIdOnUnit(actor, reloadParsed.weaponBattleItemId);
		BattleItem *ammo = findBattleItemByIdOnUnit(actor, reloadParsed.ammoBattleItemId);
		const Position pos = actor != 0 ? actor->getPosition() : Position(0, 0, 0);
		std::string rejectReason;
		if (!game->tryExecuteCompanionReload(actor, weapon, ammo, &rejectReason))
		{
			rejectReloadCommand(resultPath, reloadParsed, "RELOAD_START_FAILED", rejectReason, inboxDigest);
			appendJournalEntry(journalPath, reloadParsed.commandId);
			logRejectOnce(reloadParsed.commandId, "RELOAD_START_FAILED");
			return;
		}

		const std::string resultSnapshotSha256 =
			snapshotSha256ForTerminalCommandResult(battle, CompanionCommandCore::CMD_COMPLETED);
		writeResultFile(resultPath, buildReloadResultJson(reloadParsed, CompanionCommandCore::CMD_COMPLETED, std::string(),
			pos,
			actor != 0 ? actor->getTimeUnits() : 0,
			actor != 0 && actor->isKneeled(),
			resultSnapshotSha256, inboxDigest, 15));
		logTerminalOnce(reloadParsed.commandId, CompanionCommandCore::CMD_COMPLETED);
		appendJournalEntry(journalPath, reloadParsed.commandId);
		return;
	}

	if (inboxCommandType == "UNLOAD")
	{
		CompanionCommandCore::UnloadCommandParsed unloadParsed = {};
		if (!CompanionCommandCore::parseUnloadCommandJson(commandJson, &unloadParsed))
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			rejectUnloadCommand(resultPath, unloadParsed, unloadParsed.rejectCode, unloadParsed.rejectCode, inboxDigest);
			logRejectOnce(unloadParsed.commandId.empty() ? "unknown" : unloadParsed.commandId, unloadParsed.rejectCode);
			return;
		}

		logInboxDetectedOnce(unloadParsed.commandId);

		std::string inboxDigest;
		if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest))
		{
			return;
		}

		std::string journalJson;
		readTextFile(journalPath, &journalJson);
		if (rejectUnreadableCommandJournal(resultPath, journalJson, inboxDigest, 0, 0, 0, 0, 0, 0, &unloadParsed, 0, 0, 0))
		{
			logRejectOnce(unloadParsed.commandId, "JOURNAL_MALFORMED");
			return;
		}
		const bool duplicate = journalContainsCommandId(journalJson, unloadParsed.commandId);

		std::string existingResultJson;
		readTextFile(resultPath, &existingResultJson);
		const CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision =
			CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
				duplicate, unloadParsed.commandId, inboxDigest, existingResultJson, g_active.inFlight);
		if (applyDuplicateInboxDecision(
			duplicateDecision, unloadParsed.commandId, inboxDigest, resultPath, conflictPath,
			existingResultJson, 0, 0, 0, 0, 0, 0, &unloadParsed, 0, 0, 0))
		{
			return;
		}

		BattleUnit *actor = findUnitById(battle, unloadParsed.actorUnitId);
		const CompanionTacticalAuthorityCore::TacticalAuthorityDecision authority =
			tacticalAuthorityForActor(campaign, battle, actor, controlJson, sessionId);
		const bool liveActor = isCompanionMoveActor(actor, campaign);
		const std::set<int> &tacticalAiIds = battle->getCompanionTacticalAiSoldierIds();
		const CompanionTacticalAuthorityCore::TacticalAuthorityInput authorityInput =
			CompanionTacticalAuthorityCore::buildTacticalAuthorityInput(
				campaign != 0,
				tacticalAiIds,
				liveActor,
				unloadParsed.actorUnitId,
				controlJson,
				sessionId,
				campaign != 0 ? campaign->getCompanionCampaignId() : std::string(),
				battle->getCompanionTacticalBattleId());

		CompanionCommandCore::UnloadValidationInput unloadInput = {};
		fillUnloadValidationInput(
			&unloadInput,
			game,
			battle,
			campaign,
			battleInitialized,
			sessionId,
			unloadParsed,
			unloadParsed.actorUnitId,
			liveActor,
			actor != 0 && !actor->isOut(),
			authority.aiCommandPermitted,
			CompanionTacticalAuthorityCore::isStaleRelayAiAfterCampaignHumanAssignment(
				authorityInput, controlJson, sessionId),
			actor);

		const CompanionCommandCore::MoveValidationResult unloadVerdict =
			CompanionCommandCore::validateUnloadCommand(unloadInput);
		if (!unloadVerdict.accepted)
		{
			rejectUnloadCommand(resultPath, unloadParsed, unloadVerdict.rejectCode, unloadVerdict.publicReason, inboxDigest);
			appendJournalEntry(journalPath, unloadParsed.commandId);
			logRejectOnce(unloadParsed.commandId, unloadVerdict.rejectCode);
			maybeLogNotAiControlledReject(
				unloadVerdict.rejectCode,
				unloadParsed.commandId,
				unloadParsed.actorUnitId,
				campaign,
				battle,
				controlJson,
				controlReadOk,
				sessionId);
			return;
		}

		logCommandDiagnostic("accepted command_id=" + unloadParsed.commandId.substr(0, 8) +
			"... actor=" + std::to_string(unloadParsed.actorUnitId));

		BattleItem *weapon = findBattleItemByIdOnUnit(actor, unloadParsed.weaponBattleItemId);
		const Position pos = actor != 0 ? actor->getPosition() : Position(0, 0, 0);
		std::string rejectReason;
		if (!game->tryExecuteCompanionUnload(actor, weapon, unloadParsed.loadedAmmoBattleItemId, &rejectReason))
		{
			rejectUnloadCommand(resultPath, unloadParsed, "UNLOAD_START_FAILED", rejectReason, inboxDigest);
			appendJournalEntry(journalPath, unloadParsed.commandId);
			logRejectOnce(unloadParsed.commandId, "UNLOAD_START_FAILED");
			return;
		}

		const std::string resultSnapshotSha256 =
			snapshotSha256ForTerminalCommandResult(battle, CompanionCommandCore::CMD_COMPLETED);
		writeResultFile(resultPath, buildUnloadResultJson(unloadParsed, CompanionCommandCore::CMD_COMPLETED, std::string(),
			pos,
			actor != 0 ? actor->getTimeUnits() : 0,
			actor != 0 && actor->isKneeled(),
			resultSnapshotSha256, inboxDigest, 8));
		logTerminalOnce(unloadParsed.commandId, CompanionCommandCore::CMD_COMPLETED);
		appendJournalEntry(journalPath, unloadParsed.commandId);
		return;
	}

	if (inboxCommandType == "INVENTORY_MOVE")
	{
		CompanionCommandCore::InventoryMoveCommandParsed invParsed = {};
		if (!CompanionCommandCore::parseInventoryMoveCommandJson(commandJson, &invParsed))
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			rejectInventoryMoveCommand(resultPath, invParsed, invParsed.rejectCode, invParsed.rejectCode, inboxDigest);
			logRejectOnce(invParsed.commandId.empty() ? "unknown" : invParsed.commandId, invParsed.rejectCode);
			return;
		}

		logInboxDetectedOnce(invParsed.commandId);

		std::string inboxDigest;
		if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest))
		{
			return;
		}

		std::string journalJson;
		readTextFile(journalPath, &journalJson);
		if (rejectUnreadableCommandJournal(resultPath, journalJson, inboxDigest, 0, 0, 0, 0, 0, 0, 0, &invParsed, 0, 0))
		{
			logRejectOnce(invParsed.commandId, "JOURNAL_MALFORMED");
			return;
		}
		const bool duplicate = journalContainsCommandId(journalJson, invParsed.commandId);

		std::string existingResultJson;
		readTextFile(resultPath, &existingResultJson);
		const CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision =
			CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
				duplicate, invParsed.commandId, inboxDigest, existingResultJson, g_active.inFlight);
		if (applyDuplicateInboxDecision(
			duplicateDecision, invParsed.commandId, inboxDigest, resultPath, conflictPath,
			existingResultJson, 0, 0, 0, 0, 0, 0, 0, &invParsed, 0, 0))
		{
			return;
		}

		BattleUnit *actor = findUnitById(battle, invParsed.actorUnitId);
		const CompanionTacticalAuthorityCore::TacticalAuthorityDecision authority =
			tacticalAuthorityForActor(campaign, battle, actor, controlJson, sessionId);
		const bool liveActor = isCompanionMoveActor(actor, campaign);
		const std::set<int> &tacticalAiIds = battle->getCompanionTacticalAiSoldierIds();
		const CompanionTacticalAuthorityCore::TacticalAuthorityInput authorityInput =
			CompanionTacticalAuthorityCore::buildTacticalAuthorityInput(
				campaign != 0,
				tacticalAiIds,
				liveActor,
				invParsed.actorUnitId,
				controlJson,
				sessionId,
				campaign != 0 ? campaign->getCompanionCampaignId() : std::string(),
				battle->getCompanionTacticalBattleId());

		CompanionCommandCore::InventoryMoveValidationInput invInput = {};
		fillInventoryMoveValidationInput(
			&invInput,
			game,
			battle,
			campaign,
			battleInitialized,
			sessionId,
			invParsed,
			invParsed.actorUnitId,
			liveActor,
			actor != 0 && !actor->isOut(),
			authority.aiCommandPermitted,
			CompanionTacticalAuthorityCore::isStaleRelayAiAfterCampaignHumanAssignment(
				authorityInput, controlJson, sessionId),
			actor);

		const CompanionCommandCore::MoveValidationResult invVerdict =
			CompanionCommandCore::validateInventoryMoveCommand(invInput);
		if (!invVerdict.accepted)
		{
			rejectInventoryMoveCommand(resultPath, invParsed, invVerdict.rejectCode, invVerdict.publicReason, inboxDigest);
			appendJournalEntry(journalPath, invParsed.commandId);
			logRejectOnce(invParsed.commandId, invVerdict.rejectCode);
			maybeLogNotAiControlledReject(
				invVerdict.rejectCode,
				invParsed.commandId,
				invParsed.actorUnitId,
				campaign,
				battle,
				controlJson,
				controlReadOk,
				sessionId);
			return;
		}

		logCommandDiagnostic("accepted command_id=" + invParsed.commandId.substr(0, 8) +
			"... actor=" + std::to_string(invParsed.actorUnitId));

		BattleItem *item = findBattleItemByIdOnUnit(actor, invParsed.itemBattleItemId);
		RuleInventory *destRule = game->getMod()->getInventory(invParsed.destinationSlot, false);
		const Position pos = actor != 0 ? actor->getPosition() : Position(0, 0, 0);

		const int tuBefore = actor != 0 ? actor->getTimeUnits() : 0;
		std::string rejectReason;
		if (!game->tryExecuteCompanionInventoryMove(actor, item, destRule, invParsed.destinationX, invParsed.destinationY, &rejectReason))
		{
			rejectInventoryMoveCommand(resultPath, invParsed, "INVENTORY_MOVE_START_FAILED", rejectReason, inboxDigest);
			appendJournalEntry(journalPath, invParsed.commandId);
			logRejectOnce(invParsed.commandId, "INVENTORY_MOVE_START_FAILED");
			return;
		}

		const int tuAfter = actor != 0 ? actor->getTimeUnits() : 0;
		const int tuSpent = tuBefore - tuAfter;
		if (tuSpent < 0)
		{
			rejectInventoryMoveCommand(resultPath, invParsed, "INVENTORY_MOVE_START_FAILED",
				"Inconsistent TU accounting after inventory move.", inboxDigest);
			appendJournalEntry(journalPath, invParsed.commandId);
			logRejectOnce(invParsed.commandId, "INVENTORY_MOVE_START_FAILED");
			return;
		}

		const std::string resultSnapshotSha256 =
			snapshotSha256ForTerminalCommandResult(battle, CompanionCommandCore::CMD_COMPLETED);
		writeResultFile(resultPath, buildInventoryMoveResultJson(invParsed, CompanionCommandCore::CMD_COMPLETED, std::string(),
			pos,
			actor != 0 ? actor->getTimeUnits() : 0,
			actor != 0 && actor->isKneeled(),
			resultSnapshotSha256, inboxDigest, tuSpent));
		logTerminalOnce(invParsed.commandId, CompanionCommandCore::CMD_COMPLETED);
		appendJournalEntry(journalPath, invParsed.commandId);
		return;
	}

	if (inboxCommandType == "PICKUP")
	{
		CompanionCommandCore::PickupCommandParsed pickupParsed = {};
		if (!CompanionCommandCore::parsePickupCommandJson(commandJson, &pickupParsed))
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			rejectPickupCommand(resultPath, pickupParsed, pickupParsed.rejectCode, pickupParsed.rejectCode, inboxDigest);
			logRejectOnce(pickupParsed.commandId.empty() ? "unknown" : pickupParsed.commandId, pickupParsed.rejectCode);
			return;
		}

		logInboxDetectedOnce(pickupParsed.commandId);

		std::string inboxDigest;
		if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest))
		{
			return;
		}

		std::string journalJson;
		readTextFile(journalPath, &journalJson);
		if (rejectUnreadableCommandJournal(resultPath, journalJson, inboxDigest, 0, 0, 0, 0, 0, 0, 0, 0, &pickupParsed, 0))
		{
			logRejectOnce(pickupParsed.commandId, "JOURNAL_MALFORMED");
			return;
		}
		const bool duplicate = journalContainsCommandId(journalJson, pickupParsed.commandId);

		std::string existingResultJson;
		readTextFile(resultPath, &existingResultJson);
		const CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision =
			CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
				duplicate, pickupParsed.commandId, inboxDigest, existingResultJson, g_active.inFlight);
		if (applyDuplicateInboxDecision(
			duplicateDecision, pickupParsed.commandId, inboxDigest, resultPath, conflictPath,
			existingResultJson, 0, 0, 0, 0, 0, 0, 0, 0, &pickupParsed, 0))
		{
			return;
		}

		BattleUnit *actor = findUnitById(battle, pickupParsed.actorUnitId);
		const CompanionTacticalAuthorityCore::TacticalAuthorityDecision authority =
			tacticalAuthorityForActor(campaign, battle, actor, controlJson, sessionId);
		const bool liveActor = isCompanionMoveActor(actor, campaign);
		const std::set<int> &tacticalAiIds = battle->getCompanionTacticalAiSoldierIds();
		const CompanionTacticalAuthorityCore::TacticalAuthorityInput authorityInput =
			CompanionTacticalAuthorityCore::buildTacticalAuthorityInput(
				campaign != 0,
				tacticalAiIds,
				liveActor,
				pickupParsed.actorUnitId,
				controlJson,
				sessionId,
				campaign != 0 ? campaign->getCompanionCampaignId() : std::string(),
				battle->getCompanionTacticalBattleId());

		CompanionCommandCore::PickupValidationInput pickupInput = {};
		fillPickupValidationInput(
			&pickupInput,
			game,
			battle,
			campaign,
			battleInitialized,
			sessionId,
			pickupParsed,
			pickupParsed.actorUnitId,
			liveActor,
			actor != 0 && !actor->isOut(),
			authority.aiCommandPermitted,
			CompanionTacticalAuthorityCore::isStaleRelayAiAfterCampaignHumanAssignment(
				authorityInput, controlJson, sessionId),
			actor);

		const CompanionCommandCore::MoveValidationResult pickupVerdict =
			CompanionCommandCore::validatePickupCommand(pickupInput);
		if (!pickupVerdict.accepted)
		{
			rejectPickupCommand(resultPath, pickupParsed, pickupVerdict.rejectCode, pickupVerdict.publicReason, inboxDigest);
			appendJournalEntry(journalPath, pickupParsed.commandId);
			logRejectOnce(pickupParsed.commandId, pickupVerdict.rejectCode);
			maybeLogNotAiControlledReject(
				pickupVerdict.rejectCode,
				pickupParsed.commandId,
				pickupParsed.actorUnitId,
				campaign,
				battle,
				controlJson,
				controlReadOk,
				sessionId);
			return;
		}

		logCommandDiagnostic("accepted command_id=" + pickupParsed.commandId.substr(0, 8) +
			"... actor=" + std::to_string(pickupParsed.actorUnitId));

		BattleItem *groundItem = findGroundItemFromActorExport(actor, pickupParsed.itemBattleItemId);
		RuleInventory *destRule = game->getMod()->getInventory(pickupParsed.destinationSlot, false);
		const Position groundPos(pickupParsed.groundX, pickupParsed.groundY, pickupParsed.groundZ);
		const Position pos = actor != 0 ? actor->getPosition() : Position(0, 0, 0);

		const int tuBefore = actor != 0 ? actor->getTimeUnits() : 0;
		std::string rejectReason;
		if (!game->tryExecuteCompanionPickup(
			actor,
			groundItem,
			groundPos,
			destRule,
			pickupParsed.destinationX,
			pickupParsed.destinationY,
			&rejectReason))
		{
			rejectPickupCommand(resultPath, pickupParsed, "PICKUP_START_FAILED", rejectReason, inboxDigest);
			appendJournalEntry(journalPath, pickupParsed.commandId);
			logRejectOnce(pickupParsed.commandId, "PICKUP_START_FAILED");
			return;
		}

		const int tuAfter = actor != 0 ? actor->getTimeUnits() : 0;
		const int tuSpent = tuBefore - tuAfter;
		if (tuSpent < 0)
		{
			rejectPickupCommand(resultPath, pickupParsed, "PICKUP_START_FAILED",
				"Inconsistent TU accounting after pickup.", inboxDigest);
			appendJournalEntry(journalPath, pickupParsed.commandId);
			logRejectOnce(pickupParsed.commandId, "PICKUP_START_FAILED");
			return;
		}

		const std::string resultSnapshotSha256 =
			snapshotSha256ForTerminalCommandResult(battle, CompanionCommandCore::CMD_COMPLETED);
		writeResultFile(resultPath, buildPickupResultJson(pickupParsed, CompanionCommandCore::CMD_COMPLETED, std::string(),
			pos,
			actor != 0 ? actor->getTimeUnits() : 0,
			actor != 0 && actor->isKneeled(),
			resultSnapshotSha256, inboxDigest, tuSpent));
		logTerminalOnce(pickupParsed.commandId, CompanionCommandCore::CMD_COMPLETED);
		appendJournalEntry(journalPath, pickupParsed.commandId);
		return;
	}

	if (inboxCommandType == "DROP")
	{
		CompanionCommandCore::DropCommandParsed dropParsed = {};
		if (!CompanionCommandCore::parseDropCommandJson(commandJson, &dropParsed))
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			rejectDropCommand(resultPath, dropParsed, dropParsed.rejectCode, dropParsed.rejectCode, inboxDigest);
			logRejectOnce(dropParsed.commandId.empty() ? "unknown" : dropParsed.commandId, dropParsed.rejectCode);
			return;
		}

		logInboxDetectedOnce(dropParsed.commandId);

		std::string inboxDigest;
		if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest))
		{
			return;
		}

		std::string journalJson;
		readTextFile(journalPath, &journalJson);
		if (rejectUnreadableCommandJournal(resultPath, journalJson, inboxDigest, 0, 0, 0, 0, 0, 0, 0, 0, 0, &dropParsed))
		{
			logRejectOnce(dropParsed.commandId, "JOURNAL_MALFORMED");
			return;
		}
		const bool duplicate = journalContainsCommandId(journalJson, dropParsed.commandId);

		std::string existingResultJson;
		readTextFile(resultPath, &existingResultJson);
		const CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision =
			CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
				duplicate, dropParsed.commandId, inboxDigest, existingResultJson, g_active.inFlight);
		if (applyDuplicateInboxDecision(
			duplicateDecision, dropParsed.commandId, inboxDigest, resultPath, conflictPath,
			existingResultJson, 0, 0, 0, 0, 0, 0, 0, 0, 0, &dropParsed))
		{
			return;
		}

		BattleUnit *actor = findUnitById(battle, dropParsed.actorUnitId);
		const CompanionTacticalAuthorityCore::TacticalAuthorityDecision authority =
			tacticalAuthorityForActor(campaign, battle, actor, controlJson, sessionId);
		const bool liveActor = isCompanionMoveActor(actor, campaign);
		const std::set<int> &tacticalAiIds = battle->getCompanionTacticalAiSoldierIds();
		const CompanionTacticalAuthorityCore::TacticalAuthorityInput authorityInput =
			CompanionTacticalAuthorityCore::buildTacticalAuthorityInput(
				campaign != 0,
				tacticalAiIds,
				liveActor,
				dropParsed.actorUnitId,
				controlJson,
				sessionId,
				campaign != 0 ? campaign->getCompanionCampaignId() : std::string(),
				battle->getCompanionTacticalBattleId());

		CompanionCommandCore::DropValidationInput dropInput = {};
		fillDropValidationInput(
			&dropInput,
			game,
			battle,
			campaign,
			battleInitialized,
			sessionId,
			dropParsed,
			dropParsed.actorUnitId,
			liveActor,
			actor != 0 && !actor->isOut(),
			authority.aiCommandPermitted,
			CompanionTacticalAuthorityCore::isStaleRelayAiAfterCampaignHumanAssignment(
				authorityInput, controlJson, sessionId),
			actor);

		const CompanionCommandCore::MoveValidationResult dropVerdict =
			CompanionCommandCore::validateDropCommand(dropInput);
		if (!dropVerdict.accepted)
		{
			rejectDropCommand(resultPath, dropParsed, dropVerdict.rejectCode, dropVerdict.publicReason, inboxDigest);
			appendJournalEntry(journalPath, dropParsed.commandId);
			logRejectOnce(dropParsed.commandId, dropVerdict.rejectCode);
			maybeLogNotAiControlledReject(
				dropVerdict.rejectCode,
				dropParsed.commandId,
				dropParsed.actorUnitId,
				campaign,
				battle,
				controlJson,
				controlReadOk,
				sessionId);
			return;
		}

		logCommandDiagnostic("accepted command_id=" + dropParsed.commandId.substr(0, 8) +
			"... actor=" + std::to_string(dropParsed.actorUnitId));

		BattleItem *item = findBattleItemByIdOnUnit(actor, dropParsed.itemBattleItemId);
		const Position pos = actor != 0 ? actor->getPosition() : Position(0, 0, 0);

		const int tuBefore = actor != 0 ? actor->getTimeUnits() : 0;
		std::string rejectReason;
		if (!game->tryExecuteCompanionDrop(actor, item, &rejectReason))
		{
			rejectDropCommand(resultPath, dropParsed, "DROP_START_FAILED", rejectReason, inboxDigest);
			appendJournalEntry(journalPath, dropParsed.commandId);
			logRejectOnce(dropParsed.commandId, "DROP_START_FAILED");
			return;
		}

		const int tuAfter = actor != 0 ? actor->getTimeUnits() : 0;
		const int tuSpent = tuBefore - tuAfter;
		if (tuSpent < 0)
		{
			rejectDropCommand(resultPath, dropParsed, "DROP_START_FAILED",
				"Inconsistent TU accounting after drop.", inboxDigest);
			appendJournalEntry(journalPath, dropParsed.commandId);
			logRejectOnce(dropParsed.commandId, "DROP_START_FAILED");
			return;
		}

		CompanionDropGroundPlacementCore::GroundPlacementEvidence dropPlacement = {};
		CompanionDropGroundPlacementCore::captureAfterSuccessfulDrop(
			item, actor, dropParsed.itemBattleItemId, &dropPlacement);
		const std::string resultSnapshotSha256 =
			snapshotSha256ForTerminalCommandResult(battle, CompanionCommandCore::CMD_COMPLETED);
		writeResultFile(resultPath, buildDropResultJson(dropParsed, CompanionCommandCore::CMD_COMPLETED, std::string(),
			pos,
			actor != 0 ? actor->getTimeUnits() : 0,
			actor != 0 && actor->isKneeled(),
			resultSnapshotSha256, inboxDigest, tuSpent,
			dropPlacement.present ? &dropPlacement : 0));
		logTerminalOnce(dropParsed.commandId, CompanionCommandCore::CMD_COMPLETED);
		appendJournalEntry(journalPath, dropParsed.commandId);
		return;
	}

	if (inboxCommandType == "THROW")
	{
		CompanionCommandCore::ThrowCommandParsed throwParsed = {};
		if (!CompanionCommandCore::parseThrowCommandJson(commandJson, &throwParsed))
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			rejectThrowCommand(resultPath, throwParsed, throwParsed.rejectCode, throwParsed.rejectCode, inboxDigest);
			logRejectOnce(throwParsed.commandId.empty() ? "unknown" : throwParsed.commandId, throwParsed.rejectCode);
			return;
		}

		logInboxDetectedOnce(throwParsed.commandId);

		std::string inboxDigest;
		if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest))
		{
			return;
		}

		std::string journalJson;
		readTextFile(journalPath, &journalJson);
		if (rejectUnreadableCommandJournal(resultPath, journalJson, inboxDigest, 0, 0, 0, 0, &throwParsed, 0, 0, 0, 0, 0))
		{
			logRejectOnce(throwParsed.commandId, "JOURNAL_MALFORMED");
			return;
		}
		const bool duplicate = journalContainsCommandId(journalJson, throwParsed.commandId);

		std::string existingResultJson;
		readTextFile(resultPath, &existingResultJson);
		const CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision =
			CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
				duplicate, throwParsed.commandId, inboxDigest, existingResultJson, g_active.inFlight);
		if (applyDuplicateInboxDecision(
			duplicateDecision, throwParsed.commandId, inboxDigest, resultPath, conflictPath,
			existingResultJson, 0, 0, 0, 0, &throwParsed, 0, 0, 0, 0, 0))
		{
			return;
		}

		BattleUnit *actor = findUnitById(battle, throwParsed.actorUnitId);
		const CompanionTacticalAuthorityCore::TacticalAuthorityDecision authority =
			tacticalAuthorityForActor(campaign, battle, actor, controlJson, sessionId);
		const bool liveActor = isCompanionMoveActor(actor, campaign);
		const std::set<int> &tacticalAiIds = battle->getCompanionTacticalAiSoldierIds();
		const CompanionTacticalAuthorityCore::TacticalAuthorityInput authorityInput =
			CompanionTacticalAuthorityCore::buildTacticalAuthorityInput(
				campaign != 0,
				tacticalAiIds,
				liveActor,
				throwParsed.actorUnitId,
				controlJson,
				sessionId,
				campaign != 0 ? campaign->getCompanionCampaignId() : std::string(),
				battle->getCompanionTacticalBattleId());

		CompanionCommandCore::ThrowValidationInput throwInput = {};
		fillThrowValidationInput(
			&throwInput,
			game,
			battle,
			battleInitialized,
			sessionId,
			throwParsed,
			throwParsed.actorUnitId,
			liveActor,
			actor != 0 && !actor->isOut(),
			authority.aiCommandPermitted,
			CompanionTacticalAuthorityCore::isStaleRelayAiAfterCampaignHumanAssignment(
				authorityInput, controlJson, sessionId),
			actor);

		const CompanionCommandCore::MoveValidationResult throwVerdict =
			CompanionCommandCore::validateThrowCommand(throwInput);
		if (!throwVerdict.accepted)
		{
			rejectThrowCommand(resultPath, throwParsed, throwVerdict.rejectCode, throwVerdict.publicReason, inboxDigest);
			appendJournalEntry(journalPath, throwParsed.commandId);
			logRejectOnce(throwParsed.commandId, throwVerdict.rejectCode);
			maybeLogNotAiControlledReject(
				throwVerdict.rejectCode,
				throwParsed.commandId,
				throwParsed.actorUnitId,
				campaign,
				battle,
				controlJson,
				controlReadOk,
				sessionId);
			return;
		}

		logCommandDiagnostic("accepted command_id=" + throwParsed.commandId.substr(0, 8) +
			"... actor=" + std::to_string(throwParsed.actorUnitId));

		beginInFlightCommand();
		g_active.kind = ActiveCommand::KIND_THROW;
		g_active.commandId = throwParsed.commandId;
		g_active.inboxDigest = inboxDigest;
		g_active.throwParsed = throwParsed;
		g_active.startPos = actor != 0 ? actor->getPosition() : Position(0, 0, 0);
		g_active.snapProjectileLaunched = false;
		g_active.snapActionResult.clear();

		BattleItem *weapon = actor->getItem(throwParsed.activeHand);
		const Position target(throwParsed.targetX, throwParsed.targetY, throwParsed.targetZ);
		if (actor != 0 && actor->getActiveHand() != throwParsed.activeHand)
		{
			actor->setActiveHand(throwParsed.activeHand);
		}
		std::string rejectReason;
		if (!game->tryExecuteCompanionThrow(actor, target, weapon, &rejectReason))
		{
			g_active.inFlight = false;
			g_active.kind = ActiveCommand::KIND_NONE;
			rejectThrowCommand(resultPath, throwParsed, "THROW_START_FAILED", rejectReason, inboxDigest);
			appendJournalEntry(journalPath, throwParsed.commandId);
			logRejectOnce(throwParsed.commandId, "THROW_START_FAILED");
			return;
		}

		beginInFlightCommand();
		g_active.kind = ActiveCommand::KIND_THROW;

		logSnapShotStartedOnce(throwParsed.commandId, throwParsed.actorUnitId);
		writeResultFile(resultPath, buildThrowResultJson(throwParsed, CompanionCommandCore::CMD_EXECUTING, std::string(),
			g_active.startPos, g_active.startPos, actor->getTimeUnits(), actor->isKneeled(), g_lastSnapshotSha256, inboxDigest));
		appendJournalEntry(journalPath, throwParsed.commandId);
		return;
	}

	if (inboxCommandType == "SNAP_SHOT" || inboxCommandType == "AUTO_SHOT" || inboxCommandType == "AIMED_SHOT")
	{
		CompanionCommandCore::SnapShotCommandParsed snapParsed = {};
		const bool parsed = inboxCommandType == "AUTO_SHOT"
			? CompanionCommandCore::parseAutoShotCommandJson(commandJson, &snapParsed)
			: inboxCommandType == "AIMED_SHOT"
				? CompanionCommandCore::parseAimedShotCommandJson(commandJson, &snapParsed)
				: CompanionCommandCore::parseSnapShotCommandJson(commandJson, &snapParsed);
		if (!parsed)
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			rejectSnapShotCommand(resultPath, snapParsed, snapParsed.rejectCode, snapParsed.rejectCode, inboxDigest);
			logRejectOnce(snapParsed.commandId.empty() ? "unknown" : snapParsed.commandId, snapParsed.rejectCode);
			return;
		}

		logInboxDetectedOnce(snapParsed.commandId);

		std::string inboxDigest;
		if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest))
		{
			return;
		}

		std::string journalJson;
		readTextFile(journalPath, &journalJson);
		if (rejectUnreadableCommandJournal(resultPath, journalJson, inboxDigest, 0, 0, &snapParsed, 0, 0, 0, 0, 0, 0, 0))
		{
			logRejectOnce(snapParsed.commandId, "JOURNAL_MALFORMED");
			return;
		}
		const bool duplicate = journalContainsCommandId(journalJson, snapParsed.commandId);

		std::string existingResultJson;
		readTextFile(resultPath, &existingResultJson);
		const CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision =
			CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
				duplicate, snapParsed.commandId, inboxDigest, existingResultJson, g_active.inFlight);
		if (applyDuplicateInboxDecision(
			duplicateDecision, snapParsed.commandId, inboxDigest, resultPath, conflictPath,
			existingResultJson, 0, 0, &snapParsed, 0, 0, 0, 0, 0, 0, 0))
		{
			return;
		}

		BattleUnit *actor = findUnitById(battle, snapParsed.actorUnitId);
		const CompanionTacticalAuthorityCore::TacticalAuthorityDecision authority =
			tacticalAuthorityForActor(campaign, battle, actor, controlJson, sessionId);
		const bool liveActor = isCompanionMoveActor(actor, campaign);
		const std::set<int> &tacticalAiIds = battle->getCompanionTacticalAiSoldierIds();
		const CompanionTacticalAuthorityCore::TacticalAuthorityInput authorityInput =
			CompanionTacticalAuthorityCore::buildTacticalAuthorityInput(
				campaign != 0,
				tacticalAiIds,
				liveActor,
				snapParsed.actorUnitId,
				controlJson,
				sessionId,
				campaign != 0 ? campaign->getCompanionCampaignId() : std::string(),
				battle->getCompanionTacticalBattleId());

		CompanionCommandCore::SnapShotValidationInput snapInput = {};
		fillSnapShotValidationInput(
			&snapInput,
			game,
			battle,
			battleInitialized,
			sessionId,
			snapParsed,
			snapParsed.actorUnitId,
			liveActor,
			actor != 0 && !actor->isOut(),
			authority.aiCommandPermitted,
			CompanionTacticalAuthorityCore::isStaleRelayAiAfterCampaignHumanAssignment(
				authorityInput, controlJson, sessionId),
			actor);

		CompanionCommandCore::MoveValidationResult snapVerdict = {};
		if (inboxCommandType == "AUTO_SHOT")
		{
			snapVerdict = CompanionCommandCore::validateAutoShotCommand(snapInput);
		}
		else if (inboxCommandType == "AIMED_SHOT")
		{
			snapVerdict = CompanionCommandCore::validateAimedShotCommand(snapInput);
		}
		else
		{
			snapVerdict = CompanionCommandCore::validateSnapShotCommand(snapInput);
		}
		if (!snapVerdict.accepted)
		{
			rejectSnapShotCommand(resultPath, snapParsed, snapVerdict.rejectCode, snapVerdict.publicReason, inboxDigest);
			appendJournalEntry(journalPath, snapParsed.commandId);
			logRejectOnce(snapParsed.commandId, snapVerdict.rejectCode);
			maybeLogNotAiControlledReject(
				snapVerdict.rejectCode,
				snapParsed.commandId,
				snapParsed.actorUnitId,
				campaign,
				battle,
				controlJson,
				controlReadOk,
				sessionId);
			return;
		}

		logCommandDiagnostic("accepted command_id=" + snapParsed.commandId.substr(0, 8) +
			"... actor=" + std::to_string(snapParsed.actorUnitId));

		beginInFlightCommand();
		g_active.kind = directedFireKindForCommandType(inboxCommandType);
		g_active.commandId = snapParsed.commandId;
		g_active.inboxDigest = inboxDigest;
		g_active.snapParsed = snapParsed;
		g_active.startPos = actor != 0 ? actor->getPosition() : Position(0, 0, 0);
		g_active.snapProjectileLaunched = false;
		g_active.snapActionResult.clear();

		BattleItem *weapon = actor->getItem(snapParsed.activeHand);
		const Position target(snapParsed.targetX, snapParsed.targetY, snapParsed.targetZ);
		std::string rejectReason;
		if (actor != 0 && actor->getActiveHand() != snapParsed.activeHand)
		{
			actor->setActiveHand(snapParsed.activeHand);
		}
		const BattleActionType actionType = battleActionTypeForDirectedFireKind(g_active.kind);
		if (!game->tryExecuteCompanionDirectedFire(actor, target, weapon, actionType, &rejectReason))
		{
			g_active.inFlight = false;
			g_active.kind = ActiveCommand::KIND_NONE;
			const std::string startFailedCode = inboxCommandType + "_START_FAILED";
			rejectSnapShotCommand(resultPath, snapParsed, startFailedCode, rejectReason, inboxDigest);
			appendJournalEntry(journalPath, snapParsed.commandId);
			logRejectOnce(snapParsed.commandId, startFailedCode);
			return;
		}

		beginInFlightCommand();
		g_active.kind = directedFireKindForCommandType(inboxCommandType);

		logSnapShotStartedOnce(snapParsed.commandId, snapParsed.actorUnitId);
		writeResultFile(resultPath, buildSnapShotResultJson(snapParsed, CompanionCommandCore::CMD_EXECUTING, std::string(),
			g_active.startPos, g_active.startPos, actor->getTimeUnits(), actor->isKneeled(), g_lastSnapshotSha256, inboxDigest));
		appendJournalEntry(journalPath, snapParsed.commandId);
		return;
	}

	if (inboxCommandType == "STAND")
	{
		CompanionCommandCore::StandCommandParsed standParsed = {};
		if (!CompanionCommandCore::parseStandCommandJson(commandJson, &standParsed))
		{
			std::string inboxDigest;
			CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest);
			rejectStandCommand(resultPath, standParsed, standParsed.rejectCode, standParsed.rejectCode, inboxDigest);
			logRejectOnce(standParsed.commandId.empty() ? "unknown" : standParsed.commandId, standParsed.rejectCode);
			return;
		}

		logInboxDetectedOnce(standParsed.commandId);

		std::string inboxDigest;
		if (!CompanionCommandRuntimeSupport::computeInboxCommandDigestHex(commandJson, &inboxDigest))
		{
			return;
		}

		std::string journalJson;
		readTextFile(journalPath, &journalJson);
		if (rejectUnreadableCommandJournal(resultPath, journalJson, inboxDigest, 0, &standParsed, 0, 0, 0, 0, 0, 0, 0, 0))
		{
			logRejectOnce(standParsed.commandId, "JOURNAL_MALFORMED");
			return;
		}
		const bool duplicate = journalContainsCommandId(journalJson, standParsed.commandId);

		std::string existingResultJson;
		readTextFile(resultPath, &existingResultJson);
		const CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision =
			CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
				duplicate, standParsed.commandId, inboxDigest, existingResultJson, g_active.inFlight);
		if (applyDuplicateInboxDecision(
			duplicateDecision, standParsed.commandId, inboxDigest, resultPath, conflictPath,
			existingResultJson, 0, &standParsed, 0, 0, 0, 0, 0, 0, 0, 0))
		{
			return;
		}

		BattleUnit *actor = findUnitById(battle, standParsed.actorUnitId);
		const CompanionTacticalAuthorityCore::TacticalAuthorityDecision authority =
			tacticalAuthorityForActor(campaign, battle, actor, controlJson, sessionId);
		const bool liveActor = isCompanionMoveActor(actor, campaign);
		const std::set<int> &tacticalAiIds = battle->getCompanionTacticalAiSoldierIds();
		const CompanionTacticalAuthorityCore::TacticalAuthorityInput authorityInput =
			CompanionTacticalAuthorityCore::buildTacticalAuthorityInput(
				campaign != 0,
				tacticalAiIds,
				liveActor,
				standParsed.actorUnitId,
				controlJson,
				sessionId,
				campaign != 0 ? campaign->getCompanionCampaignId() : std::string(),
				battle->getCompanionTacticalBattleId());

		CompanionCommandCore::StandValidationInput standInput = {};
		fillBindingValidationInput(
			&standInput,
			game,
			battle,
			battleInitialized,
			sessionId,
			standParsed.snapshotSha256,
			standParsed.sessionId,
			standParsed.turn,
			standParsed.stateVersion,
			standParsed.actorUnitId,
			liveActor,
			actor != 0 && !actor->isOut(),
			authority.aiCommandPermitted,
			CompanionTacticalAuthorityCore::isStaleRelayAiAfterCampaignHumanAssignment(
				authorityInput, controlJson, sessionId));
		standInput.actorKneeling = actor != 0 && actor->isKneeled();
		standInput.actorCanKneelPosture = actor != 0 && actor->getType() == "SOLDIER" && !actor->isFloating();
		standInput.insufficientTuForStandUp = actor != 0 && standInput.actorCanKneelPosture &&
			standInput.actorKneeling && !game->checkReservedTU(actor, 8, true);

		const CompanionCommandCore::MoveValidationResult standVerdict =
			CompanionCommandCore::validateStandCommand(standInput);
		if (!standVerdict.accepted)
		{
			rejectStandCommand(resultPath, standParsed, standVerdict.rejectCode, standVerdict.publicReason, inboxDigest);
			appendJournalEntry(journalPath, standParsed.commandId);
			logRejectOnce(standParsed.commandId, standVerdict.rejectCode);
			maybeLogNotAiControlledReject(
				standVerdict.rejectCode,
				standParsed.commandId,
				standParsed.actorUnitId,
				campaign,
				battle,
				controlJson,
				controlReadOk,
				sessionId);
			return;
		}

		logCommandDiagnostic("accepted command_id=" + standParsed.commandId.substr(0, 8) +
			"... actor=" + std::to_string(standParsed.actorUnitId));

		const Position startPos = actor != 0 ? actor->getPosition() : Position(0, 0, 0);
		const int tuBefore = actor != 0 ? actor->getTimeUnits() : 0;
		std::string rejectReason;
		if (!game->tryExecuteCompanionStandUp(actor, &rejectReason))
		{
			rejectStandCommand(resultPath, standParsed, "STAND_START_FAILED", rejectReason, inboxDigest);
			appendJournalEntry(journalPath, standParsed.commandId);
			logRejectOnce(standParsed.commandId, "STAND_START_FAILED");
			return;
		}

		logStandStartedOnce(standParsed.commandId, standParsed.actorUnitId);
		const int tuAfter = actor != 0 ? actor->getTimeUnits() : 0;
		if (tuBefore - tuAfter != 8)
		{
			logCommandDiagnostic("unexpected stand TU delta command_id=" + standParsed.commandId.substr(0, 8) +
				"... before=" + std::to_string(tuBefore) + " after=" + std::to_string(tuAfter));
		}
		std::string resultSnapshotSha256 =
			snapshotSha256ForTerminalCommandResult(battle, CompanionCommandCore::CMD_COMPLETED);

		writeResultFile(resultPath, buildKneelResultJson(standParsed, CompanionCommandCore::CMD_COMPLETED, std::string(),
			startPos, tuAfter, actor != 0 && actor->isKneeled(), resultSnapshotSha256, inboxDigest));
		CompanionRelayPublicationFreshnessCore::noteTacticalBattleMutation();
		logTerminalOnce(standParsed.commandId, CompanionCommandCore::CMD_COMPLETED);
		appendJournalEntry(journalPath, standParsed.commandId);
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
		if (rejectUnreadableCommandJournal(resultPath, journalJson, inboxDigest, 0, &kneelParsed, 0, 0, 0, 0, 0, 0, 0, 0))
		{
			logRejectOnce(kneelParsed.commandId, "JOURNAL_MALFORMED");
			return;
		}
		const bool duplicate = journalContainsCommandId(journalJson, kneelParsed.commandId);

		std::string existingResultJson;
		readTextFile(resultPath, &existingResultJson);
		const CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision =
			CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
				duplicate, kneelParsed.commandId, inboxDigest, existingResultJson, g_active.inFlight);
		if (applyDuplicateInboxDecision(
			duplicateDecision, kneelParsed.commandId, inboxDigest, resultPath, conflictPath,
			existingResultJson, 0, &kneelParsed, 0, 0, 0, 0, 0, 0, 0, 0))
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
			maybeLogNotAiControlledReject(
				kneelVerdict.rejectCode,
				kneelParsed.commandId,
				kneelParsed.actorUnitId,
				campaign,
				battle,
				controlJson,
				controlReadOk,
				sessionId);
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

		std::string resultSnapshotSha256 =
			snapshotSha256ForTerminalCommandResult(battle, CompanionCommandCore::CMD_COMPLETED);

		writeResultFile(resultPath, buildKneelResultJson(kneelParsed, CompanionCommandCore::CMD_COMPLETED, std::string(),
			startPos, tuAfter, actor != 0 && actor->isKneeled(), resultSnapshotSha256, inboxDigest));
		CompanionRelayPublicationFreshnessCore::noteTacticalBattleMutation();
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
	if (rejectUnreadableCommandJournal(resultPath, journalJson, inboxDigest, &parsed, 0, 0, 0, 0, 0, 0, 0, 0, 0))
	{
		logRejectOnce(parsed.commandId, "JOURNAL_MALFORMED");
		return;
	}
	const bool duplicate = journalContainsCommandId(journalJson, parsed.commandId);

	std::string existingResultJson;
	readTextFile(resultPath, &existingResultJson);
	const CompanionCommandRuntimeSupport::DuplicateInboxDecision duplicateDecision =
		CompanionCommandRuntimeSupport::evaluateDuplicateInboxObservation(
			duplicate, parsed.commandId, inboxDigest, existingResultJson, g_active.inFlight);
	if (applyDuplicateInboxDecision(
			duplicateDecision, parsed.commandId, inboxDigest, resultPath, conflictPath,
			existingResultJson, &parsed, 0, 0, 0, 0, 0, 0, 0, 0, 0))
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
		maybeLogNotAiControlledReject(
			verdict.rejectCode,
			parsed.commandId,
			parsed.actorUnitId,
			campaign,
			battle,
			controlJson,
			controlReadOk,
			sessionId);
		return;
	}

	logCommandDiagnostic("accepted command_id=" + parsed.commandId.substr(0, 8) + "... actor=" + std::to_string(parsed.actorUnitId));

	beginInFlightCommand();
	g_active.kind = ActiveCommand::KIND_MOVE;
	g_active.commandId = parsed.commandId;
	g_active.inboxDigest = inboxDigest;
	g_active.moveParsed = parsed;
	g_active.walkActionResult.clear();

	std::string rejectReason;
	const Position dest(parsed.destX, parsed.destY, parsed.destZ);
	if (!game->tryExecuteCompanionMove(actor, dest, &rejectReason))
	{
		g_active.inFlight = false;
		g_active.kind = ActiveCommand::KIND_NONE;
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
