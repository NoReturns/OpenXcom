#pragma once
/*
 * Open X-Com Companion — testable command polling policy (M1-21C).
 */
#include "CompanionCommandCore.h"
#include <ctime>
#include <string>

namespace OpenXcom
{

namespace CompanionCommandRuntimeSupport
{

const int COMMAND_INBOX_MIN_POLL_INTERVAL_MS = 250;

enum DuplicateInboxDecision
{
	DUPLICATE_INBOX_PROCEED,
	DUPLICATE_INBOX_NO_OP,
	DUPLICATE_INBOX_CONFLICT_PRESERVE,
	DUPLICATE_INBOX_IDENTITY_MISMATCH,
	DUPLICATE_INBOX_REJECT
};

bool computeInboxCommandDigestHex(const std::string &commandJson, std::string *outHex64);

bool parseTacticalResultBinding(
	const std::string &resultJson,
	std::string *commandIdOut,
	std::string *statusOut,
	std::string *inboxSha256Out);

DuplicateInboxDecision evaluateDuplicateInboxObservation(
	bool duplicateCommandId,
	const std::string &incomingCommandId,
	const std::string &inboxCommandDigestHex,
	const std::string &existingResultJson,
	bool moveInFlight);

bool shouldOverwriteTacticalResultOnDuplicateDecision(DuplicateInboxDecision decision);

bool shouldWriteConflictSidecarOnDuplicateDecision(DuplicateInboxDecision decision);

std::string buildDuplicatePayloadConflictJson(
	const std::string &commandId,
	const std::string &priorResultStatus,
	const std::string &observedInboxDigestHex,
	const std::string &storedInboxDigestHex);

std::string buildDuplicateResultIdentityConflictJson(
	const std::string &incomingCommandId,
	const std::string &conflictKind,
	const std::string &priorResultCommandId,
	const std::string &priorResultStatus,
	const std::string &observedInboxDigestHex);

bool shouldRunCompanionCommandTickDuringThink(
	bool playerTurn,
	bool debugMode,
	bool unitsFalling,
	bool battleStatesEmpty,
	bool battleBusy,
	bool moveInFlight);

/** Battlescape popState can re-enter CompanionCommandRuntime::tick during command start. */
bool shouldDeferInFlightCommandCompletionWhileReentrant(int commandTickDepth);

/** Orphan rehydrate is only valid when command/result session_id still matches the live battle session. */
bool commandSessionBindingMatchesLiveBattle(
	const std::string &liveSessionId,
	const std::string &commandSessionId,
	const std::string &resultSessionId);

bool shouldRehydrateOrphanedDirectedFireForSession(
	const std::string &liveSessionId,
	const std::string &commandSessionId,
	const std::string &resultSessionId);

bool isOpenCompanionResultStatus(const std::string &status);

bool isCrossSessionPersistedOpenCommand(
	const std::string &liveSessionId,
	const std::string &inboxSessionId,
	const std::string &resultSessionId,
	const std::string &resultStatus);

bool shouldPreserveSameSessionInFlightExecution(
	const std::string &liveSessionId,
	const std::string &commandSessionId,
	const std::string &resultSessionId,
	const std::string &resultCommandId,
	bool activeInFlight,
	const std::string &activeCommandId);

/** Persisted EXECUTING/ACCEPTED from a prior battle session (disk orphan), not same-session in-flight work. */
bool shouldRecoverPersistedCrossSessionOrphan(
	const std::string &liveSessionId,
	const std::string &inboxSessionId,
	const std::string &resultSessionId,
	const std::string &resultStatus,
	const std::string &inboxCommandId,
	const std::string &resultCommandId,
	bool activeInFlight,
	const std::string &activeCommandId,
	const std::string &activeCommandSessionId);

bool parseTacticalResultSessionBinding(
	const std::string &resultJson,
	std::string *commandIdOut,
	std::string *statusOut,
	std::string *sessionIdOut,
	std::string *inboxSha256Out);

const char *sessionBoundaryInterruptedPublicReasonForCommandType(const std::string &commandType);

/** Persisted non-terminal result after launcher quarantine removed tactical-command.json. */
bool parseSnapShotParsedFromOpenResultJson(
	const std::string &resultJson,
	CompanionCommandCore::SnapShotCommandParsed *out);

bool parseMoveParsedFromOpenResultJson(
	const std::string &resultJson,
	CompanionCommandCore::MoveCommandParsed *out);

bool parseKneelParsedFromOpenResultJson(
	const std::string &resultJson,
	CompanionCommandCore::KneelCommandParsed *out);

enum HandleStateCompanionStage
{
	HANDLE_STATE_COMPANION_BRIEFING = 1,
	HANDLE_STATE_COMPANION_COMMAND_TICK = 2,
	HANDLE_STATE_COMPANION_IDLE_SNAPSHOT = 3
};

const HandleStateCompanionStage *handleStateCompanionStageOrder(size_t *outCount);

bool exportKneeledSnapshotField(bool unitIsKneeled);

bool shouldExportSnapshotBeforeTerminalCommandResult(CompanionCommandCore::CommandLifecycleStatus status);

bool shouldPollCommandInboxFile(
	bool moveInFlight,
	time_t fileMtime,
	time_t cachedMtime,
	long msSinceLastPoll,
	int minIntervalMs = COMMAND_INBOX_MIN_POLL_INTERVAL_MS);

struct InboxCommandBinding
{
	bool parsed;
	std::string commandId;
	std::string sessionId;
	std::string snapshotSha256;
	int turn;
	int stateVersion;
};

struct SnapshotExportBinding
{
	std::string sessionId;
	std::string snapshotSha256;
	int turn;
	int stateVersion;
};

enum StaleInboxArchiveReason
{
	STALE_INBOX_KEEP = 0,
	STALE_INBOX_BINDING_MISMATCH,
	STALE_INBOX_JOURNAL_DUPLICATE
};

bool parseInboxCommandBinding(const std::string &commandJson, InboxCommandBinding *out);

StaleInboxArchiveReason evaluateStaleInboxArchive(
	const InboxCommandBinding &inbox,
	const SnapshotExportBinding &currentExport,
	bool journalContainsCommandId,
	bool moveInFlight,
	const std::string &inFlightCommandId);

std::string buildArchivedInboxFileName(
	const std::string &reasonToken,
	const std::string &commandId,
	long long epochMillisUtc);

} // namespace CompanionCommandRuntimeSupport

} // namespace OpenXcom
