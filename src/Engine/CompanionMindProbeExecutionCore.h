#pragma once
/*
 * Open X-Com Companion — headless Mind Probe BA_USE (M1 MIND-PROBE-18).
 */
#include <string>

namespace OpenXcom
{

class BattlescapeGame;
class BattleUnit;
class BattleItem;
class SavedBattleGame;
class SavedGame;

namespace CompanionMindProbeExecutionCore
{

enum MindProbeUseResult
{
	MIND_PROBE_APPLIED,
	MIND_PROBE_INSUFFICIENT_TU,
	MIND_PROBE_TARGET_INELIGIBLE
};

MindProbeUseResult applyMindProbeUse(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame *campaign,
	BattleUnit *actor,
	BattleItem *mindProbe,
	BattleUnit *target,
	int *tuSpentOut,
	std::string *rejectReason);

} // namespace CompanionMindProbeExecutionCore

} // namespace OpenXcom