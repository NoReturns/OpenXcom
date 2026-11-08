#pragma once
/*
 * Open X-Com Companion — in-battlescape soldier ownership UI/policy (M1-WP7A).
 *
 * Single source of truth: SavedGame + SavedBattleGame tactical assignments;
 * companion-soldier-control.json is republished when snapshot path is configured.
 */
#include "CompanionSoldierOwnershipCore.h"
#include <SDL_stdinc.h>
#include <string>
#include <vector>

namespace OpenXcom
{

class BattleUnit;
class BattlescapeState;
class BattlescapeGame;
class Language;
class Mod;
class SavedBattleGame;
class SavedGame;

namespace CompanionBattlescapeOwnershipCore
{

enum OwnershipHandoffBlockReason
{
	HANDOFF_OK = 0,
	HANDOFF_COMPANION_COMMAND_IN_FLIGHT,
	HANDOFF_NATIVE_ACTION_IN_PROGRESS,
	HANDOFF_NOT_ASSIGNABLE
};

enum OwnershipChangeError
{
	CHANGE_OK = 0,
	CHANGE_NOT_ASSIGNABLE,
	CHANGE_HANDOFF_BLOCKED,
	CHANGE_ASSIGN_FAILED,
	CHANGE_PUBLISH_FAILED
};

struct SquadOwnershipRow
{
	int soldierId;
	std::string displayName;
	CompanionSoldierOwnershipCore::SoldierControlMode effectiveOwner;
	std::string statusNote;
	bool unsavedVsGeoscapeSave;
	bool canAssignToAster;
	bool canReclaimForHuman;
	OwnershipHandoffBlockReason handoffBlock;
};

struct OwnershipChangeOutcome
{
	bool ok;
	OwnershipChangeError error;
	OwnershipHandoffBlockReason handoffBlock;
	CompanionSoldierOwnershipCore::SoldierControlMode effectiveOwnerAfter;
};

bool isDeployedRosterSoldierUnit(const BattleUnit *unit, const SavedGame *campaign);

CompanionSoldierOwnershipCore::SoldierControlMode effectiveTacticalOwner(
	const SavedBattleGame *battle,
	int soldierId,
	bool liveRoster);

OwnershipHandoffBlockReason evaluateHandoffBlock(
	const BattleUnit *unit,
	bool battleBusy,
	int inFlightCompanionActorUnitId);

bool soldierEligibleForOwnershipToggle(const BattleUnit *unit, const SavedGame *campaign);

/// Squad ownership UI in InventoryState: only after combat has begun (not craft / pre-mission deployment).
inline bool shouldShowCompanionSquadInventoryButton(
	BattlescapeState *inventoryParent,
	const SavedGame *campaign,
	bool battleBeforeGame)
{
	return inventoryParent != 0 && campaign != 0 && !battleBeforeGame;
}

std::vector<SquadOwnershipRow> buildSquadOwnershipRows(
	SavedGame *campaign,
	SavedBattleGame *battle,
	BattlescapeGame *game,
	Language *lang,
	int inFlightCompanionActorUnitId);

OwnershipChangeOutcome tryApplyOwnershipToggle(
	SavedGame *campaign,
	SavedBattleGame *battle,
	BattlescapeGame *game,
	int soldierId,
	bool assignToAster,
	int inFlightCompanionActorUnitId);

std::string handoffBlockMessageKey(OwnershipHandoffBlockReason reason);
std::string changeErrorMessageKey(OwnershipChangeError error);

/// Default battlescape HUD name colour (interfaces.rul `textName`, typically orange/brown index 128).
Uint8 defaultBattlescapeSoldierNameColor(const Mod *mod);

/// Readable green for ASTER-owned roster names (palette index 133, minty green in interfaces.rul).
const Uint8 BATTLESCAPE_ASTER_SOLDIER_NAME_COLOR = 133;

Uint8 soldierNameColorForEffectiveOwner(
	const Mod *mod,
	CompanionSoldierOwnershipCore::SoldierControlMode effectiveOwner);

} // namespace CompanionBattlescapeOwnershipCore

} // namespace OpenXcom
