#pragma once
/*
 * Open X-Com Companion — DROP ground placement evidence (M1 P1-C).
 */
#include <sstream>
#include <string>

namespace OpenXcom
{

class BattleItem;
class BattleUnit;

namespace CompanionDropGroundPlacementCore
{

/** Evidence captured immediately after tryExecuteCompanionDrop (engine postcondition). */
struct GroundPlacementEvidence
{
	bool present;
	bool verified;
	int itemBattleItemId;
	int tileX;
	int tileY;
	int tileZ;
	int inventoryX;
	int inventoryY;
	std::string groundSlotId;
	std::string verificationStatus;
	std::string evidenceClass;
};

/**
 * Inspect item state after DROP; never infers placement from COMPLETED alone.
 * verified=true only when item is on actor tile ground inventory with stable id.
 */
bool captureAfterSuccessfulDrop(BattleItem *item, BattleUnit *actor, int expectedItemId, GroundPlacementEvidence *out);

/** Append optional ground_placement object (comma after prior field). */
void appendGroundPlacementJson(std::ostringstream &json, const GroundPlacementEvidence &evidence);

} // namespace CompanionDropGroundPlacementCore

} // namespace OpenXcom
