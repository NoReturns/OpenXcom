#pragma once
/*
 * Open X-Com Companion — automatic local battle AAR capture at debrief (M1).
 */
#include "CompanionBattleAarCaptureCore.h"

namespace OpenXcom
{

class DebriefingState;
class SavedGame;

/// Friend accessor for DebriefingState private debrief fields.
class CompanionBattleAarDebriefAccess
{
public:
	static bool tryFinalizeAndWrite(DebriefingState *debrief, SavedGame *save);
};

namespace CompanionBattleAarCapture
{

/// Phase A: first statement of DebriefingState::prepareDebriefing().
void capturePhaseA(SavedGame *save, SavedBattleGame *battle);

/// Phase B: after debrief scoring and handlePromotions(), before setBattleGame(0).
bool tryFinalizeAndWrite(DebriefingState *debrief, SavedGame *save);

/// Test hook: last in-memory phase A (not for production relay).
const CompanionBattleAarCaptureCore::PhaseASnapshot *testLastPhaseA();

void testResetPhaseA();

} // namespace CompanionBattleAarCapture

} // namespace OpenXcom