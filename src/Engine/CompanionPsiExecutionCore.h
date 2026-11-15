#pragma once
/*
 * Open X-Com Companion — native Psi-Amp panic / mind control execution (M1-WP10).
 * Engine-authoritative psychic resolution; no Node RNG.
 */
#include <string>

namespace OpenXcom
{

class BattlescapeGame;
class BattleUnit;
class BattleItem;

namespace CompanionPsiExecutionCore
{

/// Validates target visibility/faction and runs native panic attack. Spends TU on completed attempt (success or fail).
bool executePanic(
	BattlescapeGame *game,
	BattleUnit *actor,
	BattleItem *psiAmp,
	BattleUnit *target,
	std::string *rejectReason,
	int *tuSpentOut,
	bool *psychicSucceededOut);

bool executeMindControl(
	BattlescapeGame *game,
	BattleUnit *actor,
	BattleItem *psiAmp,
	BattleUnit *target,
	std::string *rejectReason,
	int *tuSpentOut,
	bool *psychicSucceededOut);

} // namespace CompanionPsiExecutionCore

} // namespace OpenXcom