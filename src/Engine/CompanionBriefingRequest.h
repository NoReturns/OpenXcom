#pragma once
/*
 * Open X-Com Companion — F6 tactical briefing request (local signal only).
 */
#include <string>

namespace OpenXcom
{

class SavedBattleGame;

class CompanionBriefingRequest
{
public:
	static bool snapshotPathConfigured();
	/// Player-legitimate idle export gates (includes configured snapshot path).
	static bool canFulfill(SavedBattleGame *battle, bool battleBusy, bool battleInitialized);
	/// Writes snapshot then briefing-request.json; returns false without leaving a ready request.
	static bool fulfill(SavedBattleGame *battle, std::string *outBriefingRequestId = 0);
};

} // namespace OpenXcom
