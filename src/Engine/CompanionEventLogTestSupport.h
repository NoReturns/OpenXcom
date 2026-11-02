#pragma once
/*
 * Open X-Com Companion — test hooks for companion_event_log_test (not linked in release game).
 */
#include "../Battlescape/BattlescapeGame.h"
#include "../Savegame/BattleUnit.h"

namespace OpenXcom
{

namespace CompanionEventLogTestSupport
{

struct EligibilityInput
{
	bool debugMode;
	UnitFaction battleSide;
	BattleActionType actionType;
	bool playerOrdered;
	UnitFaction actorFaction;
	UnitFaction actorOriginalFaction;
	bool actorHasGeoscapeSoldier;
};

bool shouldRecordDeliberatePlayerWeaponFire(const EligibilityInput &input);
std::string makeSessionId();
bool isValidSessionIdFormat(const std::string &sessionId);

} // namespace CompanionEventLogTestSupport

} // namespace OpenXcom
