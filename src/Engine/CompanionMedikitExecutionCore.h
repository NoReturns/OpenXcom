#pragma once
/*
 * Open X-Com Companion — native medikit target resolution and headless application (M1-WP9 S2).
 * Mirrors ActionMenuState + MedikitState; no UI state stack.
 */
#include <string>

namespace OpenXcom
{

class BattleUnit;
class BattleItem;
class SavedBattleGame;

namespace CompanionMedikitExecutionCore
{

/// Same selection order as ActionMenuState BA_USE + BT_MEDIKIT (unconscious co-tile, else melee-range woundable).
BattleUnit *resolveNativeMedikitTarget(SavedBattleGame *battle, BattleUnit *actor);

enum MedikitApplicationResult
{
	MEDIKIT_APPLIED,
	MEDIKIT_NO_CHARGE,
	MEDIKIT_INSUFFICIENT_TU
};

MedikitApplicationResult applyPainKiller(BattleUnit *actor, BattleItem *medikit, BattleUnit *target, int tu, int *tuSpentOut);
MedikitApplicationResult applyStimulant(
	BattleUnit *actor,
	BattleItem *medikit,
	BattleUnit *target,
	int tu,
	int *tuSpentOut,
	bool *targetRevivedOut);
MedikitApplicationResult applyHeal(
	BattleUnit *actor,
	BattleItem *medikit,
	BattleUnit *target,
	int bodyPart,
	int tu,
	int *tuSpentOut,
	bool *targetRevivedOut);

} // namespace CompanionMedikitExecutionCore

} // namespace OpenXcom