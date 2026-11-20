#pragma once
/*
 * Open X-Com Companion — MC hostile directed fire (M1 MC-03A).
 *
 * Reuses BattlescapeGame::tryExecuteCompanionDirectedFire; soldier hand/target schema with MC authority.
 */
#include "CompanionCommandCore.h"
#include "CompanionTacticalMcHostileAuthorityCore.h"
#include "../Battlescape/Position.h"
#include <string>

namespace OpenXcom
{

enum BattleActionType;

class BattleItem;
class BattleUnit;
class BattlescapeGame;

namespace CompanionTacticalMcHostileFireExecutionCore
{

bool mcFireRelayExecutionSupported();

struct McDirectedFireContractInput
{
	int targetX;
	int targetY;
	int targetZ;
	int mapSizeX;
	int mapSizeY;
	int mapSizeZ;
	bool activeHandValid;
	bool weaponPresentInHand;
	bool weaponSupportsFireMode;
	bool weaponHasAmmo;
	bool weaponUsable;
	bool insufficientTuForFireMode;
	bool visibleUnitTargetDefinitelyBlocked;
};

struct McDirectedFireValidationInput
{
	CompanionTacticalMcHostileAuthorityCore::McRemoteAuthorityInput authority;
	McDirectedFireContractInput contract;
};

CompanionCommandCore::MoveValidationResult validateMcDirectedFireContract(
	const McDirectedFireContractInput &input,
	const char *unsupportedModeReason,
	const char *insufficientTuReason);

CompanionCommandCore::MoveValidationResult validateMcDirectedFireExecutionPreflight(
	const McDirectedFireValidationInput &input,
	const char *unsupportedModeReason,
	const char *insufficientTuReason);

McDirectedFireContractInput buildMcDirectedFireContractInput(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	BattleUnit *actor,
	const CompanionCommandCore::SnapShotCommandParsed &parsed);

/// Queues UnitTurnBState + ProjectileFlyBState when preflight passes.
bool tryStartMcDirectedFire(
	BattlescapeGame *game,
	BattleUnit *actor,
	const Position &target,
	BattleItem *weapon,
	BattleActionType actionType,
	std::string *rejectReason);

int estimateFacingStepsForMcFire(BattleUnit *actor, const Position &target);

} // namespace CompanionTacticalMcHostileFireExecutionCore

} // namespace OpenXcom