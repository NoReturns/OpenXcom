#pragma once
/*
 * Open X-Com Companion — HWP guided launch (M1 HWP-S6-GUIDED).
 */
#include "CompanionCommandCore.h"
#include "../Battlescape/Position.h"
#include <string>
#include <vector>

namespace OpenXcom
{

class BattleItem;
class BattleUnit;
class BattlescapeGame;

namespace CompanionHwpGuidedLaunchExecutionCore
{

struct HwpGuidedLaunchStartOutcome
{
	bool started;
	std::string rejectReason;
	int tuAtAcceptance;
};

bool fixedWeaponSupportsGuidedLaunch(BattleItem *weapon);

int guidedLaunchWaypointLimit(BattleItem *weapon);

bool preflightHwpGuidedLaunch(
	BattlescapeGame *game,
	BattleUnit *actor,
	const std::vector<Position> &waypoints,
	int weaponBattleItemId,
	std::string *rejectReason);

bool tryStartHwpGuidedLaunch(
	BattlescapeGame *game,
	BattleUnit *actor,
	const std::vector<Position> &waypoints,
	int weaponBattleItemId,
	std::string *rejectReason,
	HwpGuidedLaunchStartOutcome *outcome);

CompanionCommandCore::MoveValidationResult buildHwpGuidedLaunchContractValidation(
	BattlescapeGame *game,
	BattleUnit *actor,
	const std::vector<Position> &waypoints,
	int weaponBattleItemId);

} // namespace CompanionHwpGuidedLaunchExecutionCore

} // namespace OpenXcom