#pragma once
/*
 * Open X-Com Companion — MC hostile equipment relay (M1 MC-04).
 */
#include "CompanionCommandCore.h"
#include "CompanionTacticalMcHostileAuthorityCore.h"
#include <string>

namespace OpenXcom
{

class BattlescapeGame;
class BattleUnit;
class SavedBattleGame;
class SavedGame;

namespace CompanionTacticalMcHostileEquipmentExecutionCore
{

bool mcEquipmentRelayExecutionSupported();

struct McPrimeValidationInput
{
	CompanionTacticalMcHostileAuthorityCore::McRemoteAuthorityInput authority;
	CompanionCommandCore::PrimeValidationInput contract;
};

struct McThrowValidationInput
{
	CompanionTacticalMcHostileAuthorityCore::McRemoteAuthorityInput authority;
	CompanionCommandCore::ThrowValidationInput contract;
};

struct McReloadValidationInput
{
	CompanionTacticalMcHostileAuthorityCore::McRemoteAuthorityInput authority;
	CompanionCommandCore::ReloadValidationInput contract;
};

struct McUnloadValidationInput
{
	CompanionTacticalMcHostileAuthorityCore::McRemoteAuthorityInput authority;
	CompanionCommandCore::UnloadValidationInput contract;
};

struct McInventoryMoveValidationInput
{
	CompanionTacticalMcHostileAuthorityCore::McRemoteAuthorityInput authority;
	CompanionCommandCore::InventoryMoveValidationInput contract;
};

struct McPickupValidationInput
{
	CompanionTacticalMcHostileAuthorityCore::McRemoteAuthorityInput authority;
	CompanionCommandCore::PickupValidationInput contract;
};

struct McDropValidationInput
{
	CompanionTacticalMcHostileAuthorityCore::McRemoteAuthorityInput authority;
	CompanionCommandCore::DropValidationInput contract;
};

CompanionCommandCore::MoveValidationResult validateMcPrimeExecutionPreflight(const McPrimeValidationInput &input);
CompanionCommandCore::MoveValidationResult validateMcThrowExecutionPreflight(const McThrowValidationInput &input);
CompanionCommandCore::MoveValidationResult validateMcReloadExecutionPreflight(const McReloadValidationInput &input);
CompanionCommandCore::MoveValidationResult validateMcUnloadExecutionPreflight(const McUnloadValidationInput &input);
CompanionCommandCore::MoveValidationResult validateMcInventoryMoveExecutionPreflight(const McInventoryMoveValidationInput &input);
CompanionCommandCore::MoveValidationResult validateMcPickupExecutionPreflight(const McPickupValidationInput &input);
CompanionCommandCore::MoveValidationResult validateMcDropExecutionPreflight(const McDropValidationInput &input);

CompanionCommandCore::PrimeValidationInput buildMcPrimeContractInput(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame *campaign,
	const CompanionCommandCore::PrimeCommandParsed &parsed,
	BattleUnit *actor);

CompanionCommandCore::ThrowValidationInput buildMcThrowContractInput(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame *campaign,
	const CompanionCommandCore::ThrowCommandParsed &parsed,
	BattleUnit *actor);

CompanionCommandCore::ReloadValidationInput buildMcReloadContractInput(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame *campaign,
	const CompanionCommandCore::ReloadCommandParsed &parsed,
	BattleUnit *actor);

CompanionCommandCore::UnloadValidationInput buildMcUnloadContractInput(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame *campaign,
	const CompanionCommandCore::UnloadCommandParsed &parsed,
	BattleUnit *actor);

CompanionCommandCore::InventoryMoveValidationInput buildMcInventoryMoveContractInput(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame *campaign,
	const CompanionCommandCore::InventoryMoveCommandParsed &parsed,
	BattleUnit *actor);

CompanionCommandCore::PickupValidationInput buildMcPickupContractInput(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame *campaign,
	const CompanionCommandCore::PickupCommandParsed &parsed,
	BattleUnit *actor);

CompanionCommandCore::DropValidationInput buildMcDropContractInput(
	BattlescapeGame *game,
	SavedBattleGame *battle,
	SavedGame *campaign,
	const CompanionCommandCore::DropCommandParsed &parsed,
	BattleUnit *actor);

} // namespace CompanionTacticalMcHostileEquipmentExecutionCore

} // namespace OpenXcom