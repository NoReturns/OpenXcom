#pragma once
/*
 * Open X-Com Companion — tactical MOVE command validation (M1-19, testable core).
 */
#include "../Battlescape/Position.h"
#include <string>

namespace OpenXcom
{

namespace CompanionCommandCore
{

enum CommandLifecycleStatus
{
	CMD_REJECTED,
	CMD_ACCEPTED,
	CMD_EXECUTING,
	CMD_COMPLETED,
	CMD_INTERRUPTED,
	CMD_FAILED
};

struct MoveCommandParsed
{
	bool valid;
	std::string commandId;
	std::string commandType;
	int actorUnitId;
	int destX;
	int destY;
	int destZ;
	std::string snapshotSha256;
	std::string sessionId;
	int turn;
	int stateVersion;
	std::string rejectCode;
};

struct TacticalCommandBindingValidationInput
{
	bool executorEnabled;
	bool playerTurn;
	bool battleBusy;
	bool battleInitialized;
	bool debugMode;
	bool unitsFalling;
	std::string sessionId;
	int turn;
	std::string lastSnapshotSha256;
	std::string commandSnapshotSha256;
	std::string commandSessionId;
	int commandTurn;
	int commandStateVersion;
	int lastExportedStateVersion;
	int actorUnitId;
	bool actorIsPlayerSoldier;
	bool actorAlive;
	bool actorAiControlled;
	bool duplicateCommandId;
	bool priorCommandNonTerminal;
	bool humanOverrideRevoked;
};

struct MoveValidationInput : TacticalCommandBindingValidationInput
{
	int destX;
	int destY;
	int destZ;
	int mapSizeX;
	int mapSizeY;
	int mapSizeZ;
};

struct KneelCommandParsed
{
	bool valid;
	std::string commandId;
	std::string commandType;
	int actorUnitId;
	std::string snapshotSha256;
	std::string sessionId;
	int turn;
	int stateVersion;
	std::string rejectCode;
};

struct KneelValidationInput : TacticalCommandBindingValidationInput
{
	bool actorAlreadyKneeled;
	bool actorCanKneelPosture;
	bool insufficientTuForKneelDown;
};

typedef KneelCommandParsed StandCommandParsed;

struct StandValidationInput : TacticalCommandBindingValidationInput
{
	bool actorKneeling;
	bool actorCanKneelPosture;
	bool insufficientTuForStandUp;
};

struct SnapShotCommandParsed
{
	bool valid;
	std::string commandId;
	std::string commandType;
	int actorUnitId;
	int targetX;
	int targetY;
	int targetZ;
	std::string activeHand;
	std::string snapshotSha256;
	std::string sessionId;
	int turn;
	int stateVersion;
	std::string rejectCode;
};

struct SnapShotValidationInput : TacticalCommandBindingValidationInput
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

struct SnapShotCompletionInput
{
	bool battleBusy;
	bool actorAlive;
	bool actorStillAiControlled;
	bool projectileLaunched;
	std::string actionResult;
};

struct PrimeCommandParsed
{
	bool valid;
	std::string commandId;
	std::string commandType;
	int actorUnitId;
	std::string activeHand;
	int fuseTimer;
	std::string snapshotSha256;
	std::string sessionId;
	int turn;
	int stateVersion;
	std::string rejectCode;
};

struct ThrowCommandParsed
{
	bool valid;
	std::string commandId;
	std::string commandType;
	int actorUnitId;
	int targetX;
	int targetY;
	int targetZ;
	std::string activeHand;
	std::string snapshotSha256;
	std::string sessionId;
	int turn;
	int stateVersion;
	std::string rejectCode;
};

struct PrimeValidationInput : TacticalCommandBindingValidationInput
{
	std::string activeHand;
	bool activeHandValid;
	bool grenadePresentInHand;
	bool grenadeSupportsPrime;
	bool grenadeAlreadyPrimed;
	bool invalidFuseTimer;
	bool grenadeUsable;
	bool insufficientTuForPrime;
};

struct ThrowValidationInput : TacticalCommandBindingValidationInput
{
	int targetX;
	int targetY;
	int targetZ;
	int mapSizeX;
	int mapSizeY;
	int mapSizeZ;
	std::string activeHand;
	bool activeHandValid;
	bool grenadePresentInHand;
	bool grenadeSupportsThrow;
	bool grenadeUsable;
	bool targetOutOfThrowRange;
	bool insufficientTuForThrow;
};

struct ReloadCommandParsed
{
	bool valid;
	std::string commandId;
	std::string commandType;
	int actorUnitId;
	int weaponBattleItemId;
	int ammoBattleItemId;
	std::string snapshotSha256;
	std::string sessionId;
	int turn;
	int stateVersion;
	std::string rejectCode;
};

struct UnloadCommandParsed
{
	bool valid;
	std::string commandId;
	std::string commandType;
	int actorUnitId;
	int weaponBattleItemId;
	int loadedAmmoBattleItemId;
	std::string snapshotSha256;
	std::string sessionId;
	int turn;
	int stateVersion;
	std::string rejectCode;
};

struct ReloadValidationInput : TacticalCommandBindingValidationInput
{
	int weaponBattleItemId;
	int ammoBattleItemId;
	bool weaponResolved;
	bool ammoResolved;
	bool campaignResearchAvailable;
	bool weaponBelongsToActor;
	bool ammoBelongsToActor;
	bool weaponInEitherHand;
	bool weaponIsFirearm;
	bool weaponNeedsPhysicalMagazine;
	bool weaponChamberEmptyForReloadUi;
	bool ammoIsPhysicalClip;
	bool ammoCarriedOffGround;
	bool ammoNotAlreadyLoadedInWeapon;
	bool ammunitionCompatible;
	bool weaponUsableEnvironment;
	bool weaponResearchAuthorized;
	bool ammoResearchAuthorized;
	bool insufficientTuForReload;
};

struct UnloadValidationInput : TacticalCommandBindingValidationInput
{
	int weaponBattleItemId;
	int loadedAmmoBattleItemId;
	bool weaponResolved;
	bool campaignResearchAvailable;
	bool weaponBelongsToActor;
	bool weaponInEitherHand;
	bool weaponIsFirearm;
	bool weaponNeedsPhysicalMagazine;
	bool weaponHasLoadedMagazine;
	bool loadedAmmoIdentityMatches;
	bool loadedAmmoIsPhysicalClip;
	bool otherHandClearExceptWeapon;
	bool weaponUsableEnvironment;
	bool weaponResearchAuthorized;
	bool ammoResearchAuthorized;
	bool insufficientTuForUnload;
};

struct InventoryMoveCommandParsed
{
	bool valid;
	std::string commandId;
	std::string commandType;
	int actorUnitId;
	int itemBattleItemId;
	std::string sourceSlot;
	std::string destinationSlot;
	int destinationX;
	int destinationY;
	bool sourceCoordsGuard;
	int sourceX;
	int sourceY;
	std::string snapshotSha256;
	std::string sessionId;
	int turn;
	int stateVersion;
	std::string rejectCode;
};

struct InventoryMoveValidationInput : TacticalCommandBindingValidationInput
{
	int itemBattleItemId;
	std::string sourceSlotId;
	std::string destinationSlotId;
	int destinationX;
	int destinationY;
	bool sourceCoordsGuard;
	int sourceX;
	int sourceY;

	bool itemResolved;
	bool itemBelongsToActor;
	bool itemCarriedOffGround;
	bool itemSlotMatchesCommand;
	bool sourceCoordsMatch;
	bool itemIsFixedWeapon;
	bool itemNestedInFirearm;
	bool destinationSlotResolved;
	bool destinationIsGround;
	bool destinationAllowedCarriedType;
	bool destinationSameSectionAsCommand;
	bool itemFitsDestinationGeometry;
	bool destinationOccupied;
	bool transferCostResolved;
	int transferCostTu;
	bool primedExplosiveBlocked;
	bool insufficientTuForInventoryMove;
};

struct PickupCommandParsed
{
	bool valid;
	std::string commandId;
	std::string commandType;
	int actorUnitId;
	int itemBattleItemId;
	int groundX;
	int groundY;
	int groundZ;
	std::string destinationSlot;
	int destinationX;
	int destinationY;
	std::string snapshotSha256;
	std::string sessionId;
	int turn;
	int stateVersion;
	std::string rejectCode;
};

struct DropCommandParsed
{
	bool valid;
	std::string commandId;
	std::string commandType;
	int actorUnitId;
	int itemBattleItemId;
	std::string sourceSlot;
	bool sourceCoordsGuard;
	int sourceX;
	int sourceY;
	std::string snapshotSha256;
	std::string sessionId;
	int turn;
	int stateVersion;
	std::string rejectCode;
};

struct PickupValidationInput : TacticalCommandBindingValidationInput
{
	int itemBattleItemId;
	int groundX;
	int groundY;
	int groundZ;
	std::string destinationSlotId;
	int destinationX;
	int destinationY;

	bool campaignResearchAvailable;
	bool groundPositionMatchesActor;
	bool itemResolvedOnActorGround;
	bool itemResearchAuthorized;
	bool destinationSlotResolved;
	bool destinationAllowedCarriedType;
	bool destinationSameSectionAsCommand;
	bool itemFitsDestinationGeometry;
	bool destinationOccupied;
	bool transferCostResolved;
	int transferCostTu;
	bool insufficientTuForPickup;
	bool itemIsFixedWeapon;
};

struct DropValidationInput : TacticalCommandBindingValidationInput
{
	int itemBattleItemId;
	std::string sourceSlotId;
	bool sourceCoordsGuard;
	int sourceX;
	int sourceY;

	bool campaignResearchAvailable;
	bool itemResolved;
	bool itemBelongsToActor;
	bool itemCarriedOffGround;
	bool itemSlotMatchesCommand;
	bool sourceCoordsMatch;
	bool itemIsFixedWeapon;
	bool itemNestedInFirearm;
	bool groundRuleResolved;
	bool transferCostResolved;
	int transferCostTu;
	bool groundHasRoom;
	bool insufficientTuForDrop;
	bool itemResearchAuthorized;
};

struct MoveValidationResult
{
	bool accepted;
	std::string rejectCode;
	std::string publicReason;
};

struct WalkCompletionInput
{
	Position requestedDest;
	Position startPos;
	Position currentPos;
	bool battleBusy;
	bool actorAlive;
	bool actorStillAiControlled;
	std::string actionResult;
};

/// Optional MOVE terminal diagnostics derived from engine evidence (not coordinate heuristics).
struct WalkStopDiagnostics
{
	std::string movementStopReason;
	std::string publicReason;
	/// Set only when actionResult should appear on the wire as engine_action_result.
	std::string engineActionResult;
};

MoveValidationResult validateTacticalCommandBinding(const TacticalCommandBindingValidationInput &input);
MoveValidationResult validateMoveCommand(const MoveValidationInput &input);
MoveValidationResult validateKneelCommand(const KneelValidationInput &input);
MoveValidationResult validateStandCommand(const StandValidationInput &input);
MoveValidationResult validateSnapShotCommand(const SnapShotValidationInput &input);
MoveValidationResult validateAutoShotCommand(const SnapShotValidationInput &input);
MoveValidationResult validateAimedShotCommand(const SnapShotValidationInput &input);
MoveValidationResult validatePrimeCommand(const PrimeValidationInput &input);
MoveValidationResult validateThrowCommand(const ThrowValidationInput &input);
MoveValidationResult validateReloadCommand(const ReloadValidationInput &input);
MoveValidationResult validateUnloadCommand(const UnloadValidationInput &input);
MoveValidationResult validateInventoryMoveCommand(const InventoryMoveValidationInput &input);
MoveValidationResult validatePickupCommand(const PickupValidationInput &input);
MoveValidationResult validateDropCommand(const DropValidationInput &input);
CommandLifecycleStatus evaluateWalkTerminalStatus(const WalkCompletionInput &input);
WalkStopDiagnostics resolveWalkStopDiagnostics(
	CommandLifecycleStatus terminal,
	const WalkCompletionInput &input);
CommandLifecycleStatus evaluateSnapShotTerminalStatus(const SnapShotCompletionInput &input);
CommandLifecycleStatus evaluateThrowTerminalStatus(const SnapShotCompletionInput &input);

bool parseMoveCommandJson(const std::string &json, MoveCommandParsed *out);
bool parseKneelCommandJson(const std::string &json, KneelCommandParsed *out);
bool parseStandCommandJson(const std::string &json, StandCommandParsed *out);
bool parseSnapShotCommandJson(const std::string &json, SnapShotCommandParsed *out);
bool parseAutoShotCommandJson(const std::string &json, SnapShotCommandParsed *out);
bool parseAimedShotCommandJson(const std::string &json, SnapShotCommandParsed *out);
bool parsePrimeCommandJson(const std::string &json, PrimeCommandParsed *out);
bool parseThrowCommandJson(const std::string &json, ThrowCommandParsed *out);
bool parseReloadCommandJson(const std::string &json, ReloadCommandParsed *out);
bool parseUnloadCommandJson(const std::string &json, UnloadCommandParsed *out);
bool parseInventoryMoveCommandJson(const std::string &json, InventoryMoveCommandParsed *out);
bool parsePickupCommandJson(const std::string &json, PickupCommandParsed *out);
bool parseDropCommandJson(const std::string &json, DropCommandParsed *out);
bool isValidPrimeFuseTimerForBattleType(int fuseTimer, int battleType);
bool isValidCommandIdFormat(const std::string &commandId);
bool isValidSessionIdFormat(const std::string &sessionId);
bool isValidSnapshotSha256Format(const std::string &sha);

const char *lifecycleStatusToString(CommandLifecycleStatus status);

} // namespace CompanionCommandCore

} // namespace OpenXcom
