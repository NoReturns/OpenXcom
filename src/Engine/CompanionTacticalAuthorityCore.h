#pragma once
/*
 * Open X-Com Companion — canonical tactical authority (M1-23F1).
 *
 * Pure decision layer: combines in-memory campaign ownership with relay control JSON.
 * Callers supply resolved campaign inputs; Battlescape supplies battle/campaign context (F4A).
 */
#include "CompanionSoldierOwnershipCore.h"
#include <set>
#include <string>

namespace OpenXcom
{

class BattleUnit;
class SavedBattleGame;
class SavedGame;

namespace CompanionTacticalAuthorityCore
{

struct TacticalAuthorityDecision
{
	bool aiCommandPermitted;
	bool humanOrderPermitted;
};

struct TacticalAuthorityInput
{
	bool campaignContextPresent;
	bool isLivePlayerSoldier;
	CompanionSoldierOwnershipCore::SoldierControlMode campaignMode;
	std::string controlFileJson;
	std::string battleSessionId;
	std::string relayCampaignId;
	std::string relayTacticalBattleId;
	int unitId;
};

/// Campaign is canonical; relay JSON alone never grants AI or blocks human when campaign is HUMAN.
TacticalAuthorityDecision evaluateTacticalAuthority(const TacticalAuthorityInput &input);

/// Builds input using current in-memory campaign AI ids (UNSAVED changes included via caller's set).
TacticalAuthorityInput buildTacticalAuthorityInput(
	bool campaignContextPresent,
	const std::set<int> &currentCampaignAiSoldierIds,
	bool isLivePlayerSoldier,
	int unitId,
	const std::string &controlFileJson,
	const std::string &battleSessionId,
	const std::string &relayCampaignId = std::string(),
	const std::string &relayTacticalBattleId = std::string());

bool isStaleRelayAiAfterCampaignHumanAssignment(
	const TacticalAuthorityInput &input,
	const std::string &controlFileJson,
	const std::string &battleSessionId);

/// Living geoscape-linked player soldier on the active campaign roster (companion policy scope).
bool isLiveCampaignPlayerSoldierUnit(const BattleUnit *unit, const SavedGame *campaign);

/// True when companion human/AI order policy applies (campaign roster identity only; relay optional).
bool shouldApplyCompanionHumanOrderPolicy(const BattleUnit *unit, const SavedGame *campaign);

/// Reads relay control JSON beside OPENXCOM_COMPANION_SNAPSHOT_PATH (empty if unavailable).
bool tryReadRelayControlJsonFromSnapshotEnvironment(std::string *controlJsonOut);

/**
 * F4A/F4B1 human order admission. Returns true when the order is allowed.
 * Outside companion policy scope (no campaign roster soldier), returns true (legacy gameplay).
 * Campaign ASTER denies human orders regardless of relay JSON availability.
 */
bool humanPlayerOrderPermitted(
	SavedGame *campaign,
	SavedBattleGame *battle,
	BattleUnit *unit,
	const std::string &controlFileJson);

inline bool humanPlayerMovementPermitted(
	SavedGame *campaign,
	SavedBattleGame *battle,
	BattleUnit *unit,
	const std::string &controlFileJson)
{
	return humanPlayerOrderPermitted(campaign, battle, unit, controlFileJson);
}

/*
 * Intentionally not gated (global battle preferences, not per-unit human orders):
 * - BattlescapeState — TU shot reserve (SavedBattleGame::setTUReserved), kneel-reserve toggle
 * - BattlescapeState — personal lighting, camera/map UI, end turn, selection, stats (read-only)
 *
 * Not player human orders:
 * - TileEngine::tryReaction — reaction fire
 * - UnitWalkBState / BattlescapeGame::kneel during path execution — automatic kneel along route
 */

} // namespace CompanionTacticalAuthorityCore

} // namespace OpenXcom
