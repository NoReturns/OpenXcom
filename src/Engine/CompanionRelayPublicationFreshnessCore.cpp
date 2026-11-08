/*
 * Open X-Com Companion — event-driven tactical SEND freshness (M1).
 */
#include "CompanionRelayPublicationFreshnessCore.h"
#include "CompanionBriefingTestSupport.h"
#include "CompanionJsonLite.h"
#include "CompanionSha256.h"
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace OpenXcom
{

namespace CompanionRelayPublicationFreshnessCore
{

namespace
{

const char *PUBLICATION_CONFIRM_FILENAME = "companion-tactical-publication-confirm.json";
const char *BRIEFING_ACK_FILENAME = "briefing-ack.json";
const char *BRIEFING_REQUEST_FILENAME = "briefing-request.json";

TacticalBindingSnapshot g_lastExportBinding = {};
ConfirmedPublicationView g_cachedConfirmed = {};
int g_tacticalMutationGeneration = 0;
int g_mutationAtLastConfirmed = 0;

bool readTextFile(const std::string &path, std::string *out)
{
	if (out == 0)
	{
		return false;
	}
	std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
	if (!in)
	{
		return false;
	}
	std::ostringstream buffer;
	buffer << in.rdbuf();
	*out = buffer.str();
	return true;
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
	if (dir[dir.size() - 1] == '/' || dir[dir.size() - 1] == '\\')
	{
		return dir + name;
	}
	return dir + sep + name;
}

bool isTerminalAckStatus(const std::string &status)
{
	return status == "published" || status == "skipped_unchanged" || status == "dry_run";
}

bool parsePublicationConfirmJson(const std::string &json, ConfirmedPublicationView *out)
{
	if (out == 0)
	{
		return false;
	}
	ConfirmedPublicationView view = {};
	std::string status;
	if (!CompanionJsonLite::extractJsonStringField(json, "status", &status))
	{
		int schemaVersion = 0;
		if (!CompanionJsonLite::extractJsonIntField(json, "schema_version", &schemaVersion) || schemaVersion != 1)
		{
			return false;
		}
		if (!CompanionJsonLite::extractJsonStringField(json, "snapshot_sha256", &view.snapshotSha256)
		    || !CompanionSha256::isValidHexDigest(view.snapshotSha256))
		{
			return false;
		}
		CompanionJsonLite::extractJsonStringField(json, "session_id", &view.sessionId);
		CompanionJsonLite::extractJsonIntField(json, "turn", &view.turn);
		CompanionJsonLite::extractJsonIntField(json, "state_version", &view.stateVersion);
		view.terminalSuccess = true;
		view.parsed = !view.sessionId.empty();
		*out = view;
		return view.parsed;
	}
	if (!isTerminalAckStatus(status))
	{
		return false;
	}
	if (!CompanionJsonLite::extractJsonStringField(json, "snapshot_sha256", &view.snapshotSha256)
	    || !CompanionSha256::isValidHexDigest(view.snapshotSha256))
	{
		return false;
	}
	CompanionJsonLite::extractJsonStringField(json, "session_id", &view.sessionId);
	CompanionJsonLite::extractJsonIntField(json, "turn", &view.turn);
	CompanionJsonLite::extractJsonIntField(json, "state_version", &view.stateVersion);
	view.terminalSuccess = true;
	view.parsed = true;
	*out = view;
	return true;
}

bool confirmMatchesLiveScope(const ConfirmedPublicationView &confirmed, const TacticalBindingSnapshot &live)
{
	if (!confirmed.parsed || !confirmed.terminalSuccess)
	{
		return false;
	}
	if (!confirmed.sessionId.empty() && !live.sessionId.empty() && confirmed.sessionId != live.sessionId)
	{
		return false;
	}
	if (confirmed.turn > 0 && live.turn > 0 && confirmed.turn != live.turn)
	{
		return false;
	}
	return true;
}

} // namespace

void resetConfirmedPublicationBinding()
{
	g_cachedConfirmed = ConfirmedPublicationView();
	g_lastExportBinding = TacticalBindingSnapshot();
	g_tacticalMutationGeneration = 0;
	g_mutationAtLastConfirmed = 0;
}

void onBattleSessionContextChanged()
{
	resetConfirmedPublicationBinding();
}

void noteTacticalBattleMutation()
{
	++g_tacticalMutationGeneration;
}

void notePlayerTacticalMutationIfSnapshotConfigured()
{
	const char *snapshotPath = std::getenv("OPENXCOM_COMPANION_SNAPSHOT_PATH");
	if (snapshotPath == 0 || snapshotPath[0] == '\0')
	{
		return;
	}
	noteTacticalBattleMutation();
}

int getTacticalMutationGeneration()
{
	return g_tacticalMutationGeneration;
}

int getMutationAtLastExport()
{
	return g_lastExportBinding.mutationGeneration;
}

int getMutationAtLastConfirmed()
{
	return g_mutationAtLastConfirmed;
}

void noteSnapshotExportBinding(
	int stateVersion,
	const std::string &snapshotSha256LowerHex,
	const std::string &sessionId,
	int turn)
{
	if (!CompanionSha256::isValidHexDigest(snapshotSha256LowerHex))
	{
		return;
	}
	g_lastExportBinding.stateVersion = stateVersion;
	g_lastExportBinding.exportSha256 = snapshotSha256LowerHex;
	g_lastExportBinding.sessionId = sessionId;
	g_lastExportBinding.turn = turn;
	g_lastExportBinding.mutationGeneration = g_tacticalMutationGeneration;
}

TacticalBindingSnapshot getLastExportBinding()
{
	return g_lastExportBinding;
}

bool readBriefingAckPublicationConfirm(const std::string &ackDir, ConfirmedPublicationView *out)
{
	if (out == 0 || ackDir.empty())
	{
		return false;
	}
	std::string json;
	if (!readTextFile(joinPath(ackDir, BRIEFING_ACK_FILENAME), &json))
	{
		return false;
	}
	return parsePublicationConfirmJson(json, out);
}

bool readTacticalPublicationConfirmFile(const std::string &ackDir, ConfirmedPublicationView *out)
{
	if (out == 0 || ackDir.empty())
	{
		return false;
	}
	std::string json;
	if (!readTextFile(joinPath(ackDir, PUBLICATION_CONFIRM_FILENAME), &json))
	{
		return false;
	}
	return parsePublicationConfirmJson(json, out);
}

ConfirmedPublicationView mergePublicationConfirms(
	const ConfirmedPublicationView &briefingAck,
	const ConfirmedPublicationView &publicationConfirm)
{
	if (!briefingAck.parsed)
	{
		return publicationConfirm;
	}
	if (!publicationConfirm.parsed)
	{
		return briefingAck;
	}
	if (publicationConfirm.stateVersion > briefingAck.stateVersion
	    || (publicationConfirm.stateVersion == briefingAck.stateVersion
	        && publicationConfirm.snapshotSha256 > briefingAck.snapshotSha256))
	{
		return publicationConfirm;
	}
	return briefingAck;
}

SendButtonAppearance evaluateSendButtonAppearance(const SendFreshnessEvaluateInput &input)
{
	if (!input.companionConfigured)
	{
		return SEND_APPEARANCE_NORMAL;
	}
	if (input.relayBusy)
	{
		return SEND_APPEARANCE_NORMAL;
	}
	const int liveMutation = input.live.mutationGeneration;
	const bool exportCoversMutation = !input.lastExport.exportSha256.empty()
		&& input.lastExport.mutationGeneration >= liveMutation
		&& input.lastExport.sessionId == input.live.sessionId
		&& input.lastExport.turn == input.live.turn;

	const bool publicationConfirmed = input.confirmed.parsed && input.confirmed.terminalSuccess
		&& confirmMatchesLiveScope(input.confirmed, input.live)
		&& input.confirmed.snapshotSha256 == input.lastExport.exportSha256
		&& exportCoversMutation
		&& input.lastExport.mutationGeneration <= g_mutationAtLastConfirmed
		&& liveMutation <= g_mutationAtLastConfirmed;

	if (exportCoversMutation && publicationConfirmed)
	{
		return SEND_APPEARANCE_NORMAL;
	}
	if (liveMutation > g_mutationAtLastConfirmed)
	{
		return SEND_APPEARANCE_NEEDS_PUBLICATION;
	}
	if (liveMutation > input.lastExport.mutationGeneration)
	{
		return SEND_APPEARANCE_NEEDS_PUBLICATION;
	}
	if (!input.lastExport.exportSha256.empty()
		&& (!input.confirmed.parsed || input.confirmed.snapshotSha256 != input.lastExport.exportSha256))
	{
		return SEND_APPEARANCE_NEEDS_PUBLICATION;
	}
	if (liveMutation > 0 && input.lastExport.exportSha256.empty())
	{
		return SEND_APPEARANCE_NEEDS_PUBLICATION;
	}
	return SEND_APPEARANCE_NORMAL;
}

bool loadConfirmedPublicationForLiveScope(
	const std::string &ackDir,
	const TacticalBindingSnapshot &live,
	const TacticalBindingSnapshot &lastExport,
	ConfirmedPublicationView *out)
{
	if (out == 0 || ackDir.empty())
	{
		return false;
	}
	ConfirmedPublicationView briefingAck;
	ConfirmedPublicationView publicationConfirm;
	const bool haveAck = readBriefingAckPublicationConfirm(ackDir, &briefingAck);
	const bool haveConfirm = readTacticalPublicationConfirmFile(ackDir, &publicationConfirm);
	if (!haveAck && !haveConfirm)
	{
		if (g_cachedConfirmed.parsed && confirmMatchesLiveScope(g_cachedConfirmed, live))
		{
			*out = g_cachedConfirmed;
			return true;
		}
		return false;
	}
	ConfirmedPublicationView merged = mergePublicationConfirms(briefingAck, publicationConfirm);
	if (!lastExport.exportSha256.empty()
	    && merged.parsed
	    && merged.snapshotSha256 != lastExport.exportSha256)
	{
		if (briefingAck.parsed && briefingAck.snapshotSha256 == lastExport.exportSha256)
		{
			merged = briefingAck;
		}
		else if (publicationConfirm.parsed
		    && publicationConfirm.snapshotSha256 == lastExport.exportSha256)
		{
			merged = publicationConfirm;
		}
	}
	if (merged.parsed && merged.sessionId.empty())
	{
		std::string requestJson;
		if (readTextFile(joinPath(ackDir, BRIEFING_REQUEST_FILENAME), &requestJson))
		{
			CompanionJsonLite::extractJsonStringField(requestJson, "session_id", &merged.sessionId);
			if (merged.turn <= 0)
			{
				CompanionJsonLite::extractJsonIntField(requestJson, "turn", &merged.turn);
			}
		}
	}
	if (merged.stateVersion <= 0 && lastExport.stateVersion > 0
	    && merged.snapshotSha256 == lastExport.exportSha256)
	{
		merged.stateVersion = lastExport.stateVersion;
	}
	if (!confirmMatchesLiveScope(merged, live))
	{
		*out = ConfirmedPublicationView();
		return false;
	}
	if (merged.snapshotSha256 == lastExport.exportSha256
	    && lastExport.mutationGeneration >= 0)
	{
		g_mutationAtLastConfirmed = lastExport.mutationGeneration;
	}
	g_cachedConfirmed = merged;
	*out = merged;
	return true;
}

} // namespace CompanionRelayPublicationFreshnessCore

} // namespace OpenXcom
