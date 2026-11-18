/*
 * Open X-Com Companion — native melee attack (M1 melee S2a).
 */
#pragma once

#include <string>

namespace OpenXcom
{

class BattlescapeGame;
class BattleUnit;
class BattleItem;

namespace CompanionMeleeExecutionCore
{

struct ExecutionOutcome
{
	bool meleeHit;
	bool targetUnconscious;
	bool targetKilled;
	int tuSpentMelee;
	int tuSpentTurning;
};

int estimateMeleeTurnSteps(const BattleUnit *actor, const BattleUnit *target);

bool executeMelee(
	BattlescapeGame *game,
	BattleUnit *actor,
	BattleItem *meleeWeapon,
	BattleUnit *target,
	std::string *rejectReason,
	ExecutionOutcome *outcome);

} // namespace CompanionMeleeExecutionCore

} // namespace OpenXcom