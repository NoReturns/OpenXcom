#pragma once
/*
 * Open X-Com Companion — test hooks for briefing request logic (M1-15B).
 */
#include <string>

namespace OpenXcom
{

namespace CompanionBriefingTestSupport
{

struct BriefingGateInput
{
	bool snapshotPathConfigured;
	bool debugMode;
	bool playerTurn;
	bool unitsFalling;
	bool battleBusy;
	bool battleInitialized;
};

struct BriefingPressState
{
	bool pending;
};

enum BriefingPressOutcome
{
	BRIEFING_PRESS_IGNORED,
	BRIEFING_PRESS_REJECTED,
	BRIEFING_PRESS_DEFERRED,
	BRIEFING_PRESS_FULFILL_NOW
};

BriefingPressOutcome evaluateBriefingKeyPress(const BriefingGateInput &input, BriefingPressState *state);
bool canFulfillBriefing(const BriefingGateInput &input);
bool shouldFulfillPendingBriefing(const BriefingGateInput &input, bool pending);

std::string parentDirectoryOfSnapshotPath(const std::string &snapshotPath);
std::string makeRequestId();
bool isValidRequestIdFormat(const std::string &requestId);
std::string buildBriefingRequestJson(const std::string &requestId, const std::string &sessionId, int turn,
	const std::string &snapshotSha256);
bool writeBriefingRequestFile(const std::string &requestDir, const std::string &jsonContent);
bool shouldWriteBriefingRequestAfterExport(bool exportSucceeded, const std::string &snapshotSha256);

} // namespace CompanionBriefingTestSupport

} // namespace OpenXcom
