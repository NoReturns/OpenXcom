#pragma once
/*
 * Open X-Com Companion — battlescape AUTO RETRIEVE scheduling (M1).
 */
#include <string>
#include "CompanionOnDemandRelayCore.h"

namespace OpenXcom
{

namespace CompanionAutoRetrieveCore
{

const int AUTO_RETRIEVE_INTERVAL_MIN_SEC = 15;
const int AUTO_RETRIEVE_INTERVAL_MAX_SEC = 120;
const int AUTO_RETRIEVE_INTERVAL_DEFAULT_SEC = 30;
const int AUTO_RETRIEVE_INTERVAL_STEP_SEC = 15;

void resetForNewGameSession();
void enterBattlescape();
void leaveBattlescape();

bool isEnabled();
int intervalSec();
long long lastAttemptWallMs();
bool isBattlescapeActive();

void setEnabled(bool enabled);
void adjustIntervalSec(int deltaSteps);
int clampIntervalSec(int sec);

bool isRelayPhaseBlockingAutoRetrieve(CompanionOnDemandRelayCore::RelayUiPhase phase);
bool shouldAttemptAutoRetrieve(CompanionOnDemandRelayCore::RelayUiPhase phase, long long wallNowMs);
void noteAutoRetrieveAttempt(long long wallNowMs);

void formatToggleButtonText(bool enabled, std::string *out);
void formatIntervalLabel(int intervalSec, std::string *out);

} // namespace CompanionAutoRetrieveCore

} // namespace OpenXcom
