/*
 * Open X-Com Companion — F6 tactical briefing request (local signal only).
 */
#include "CompanionBriefingRequest.h"
#include "CompanionBriefingTestSupport.h"
#include "CompanionSnapshot.h"
#include "../Savegame/SavedBattleGame.h"
#include "Logger.h"
#include <chrono>
#include <cstdlib>

namespace OpenXcom
{

namespace
{

const char *SNAPSHOT_ENV = "OPENXCOM_COMPANION_SNAPSHOT_PATH";

} // namespace

bool CompanionBriefingRequest::snapshotPathConfigured()
{
	const char *path = std::getenv(SNAPSHOT_ENV);
	return path != 0 && path[0] != '\0';
}

bool CompanionBriefingRequest::canFulfill(SavedBattleGame *battle, bool battleBusy, bool battleInitialized)
{
	if (battle == 0)
	{
		return false;
	}

	CompanionBriefingTestSupport::BriefingGateInput input = {};
	input.snapshotPathConfigured = snapshotPathConfigured();
	input.debugMode = battle->getDebugMode();
	input.playerTurn = battle->getSide() == FACTION_PLAYER;
	input.unitsFalling = battle->getUnitsFalling();
	input.battleBusy = battleBusy;
	input.battleInitialized = battleInitialized;
	return CompanionBriefingTestSupport::canFulfillBriefing(input);
}

bool CompanionBriefingRequest::fulfill(SavedBattleGame *battle, std::string *outBriefingRequestId)
{
	if (battle == 0 || !snapshotPathConfigured())
	{
		return false;
	}

	const char *snapshotPath = std::getenv(SNAPSHOT_ENV);
	std::string snapshotSha256;
	if (!CompanionSnapshot::tryExport(battle, &snapshotSha256))
	{
		Log(LOG_INFO) << "Companion briefing: snapshot export failed";
		return false;
	}

	const std::string requestDir = CompanionBriefingTestSupport::parentDirectoryOfSnapshotPath(snapshotPath);
	const std::string requestId = CompanionBriefingTestSupport::makeRequestId();
	const std::string sessionId = battle->getCompanionEventLog().getSessionId();
	const std::string json = CompanionBriefingTestSupport::buildBriefingRequestJson(
		requestId, sessionId, battle->getTurn(), snapshotSha256);

	if (!CompanionBriefingTestSupport::writeBriefingRequestFile(requestDir, json))
	{
		Log(LOG_INFO) << "Companion briefing: failed to write request file";
		return false;
	}

	const long long exportedAtMs = static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count());
	if (!CompanionBriefingTestSupport::writeTacticalSendMarkerFile(
		requestDir, requestId, sessionId, battle->getTurn(), snapshotSha256, exportedAtMs))
	{
		Log(LOG_INFO) << "Companion briefing: failed to write tactical send marker";
		return false;
	}

	if (outBriefingRequestId != 0)
	{
		*outBriefingRequestId = requestId;
	}

	Log(LOG_INFO) << "Companion briefing request " << requestId.substr(0, 8) << " turn " << battle->getTurn();
	return true;
}

} // namespace OpenXcom
