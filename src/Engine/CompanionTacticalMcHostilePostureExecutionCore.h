#pragma once
/*
 * Open X-Com Companion — MC posture / turn-face validation (M1 MC-02B).
 */
#include "CompanionCommandCore.h"
#include "CompanionTacticalMcHostileAuthorityCore.h"

namespace OpenXcom
{

namespace CompanionTacticalMcHostilePostureExecutionCore
{

struct McKneelValidationInput
{
	CompanionTacticalMcHostileAuthorityCore::McRemoteAuthorityInput authority;
	bool actorAlreadyKneeled;
	bool actorCanChangePosture;
	bool insufficientTuForKneelDown;
};

struct McStandValidationInput
{
	CompanionTacticalMcHostileAuthorityCore::McRemoteAuthorityInput authority;
	bool actorKneeling;
	bool actorCanChangePosture;
	bool insufficientTuForStandUp;
};

struct McTurnFaceValidationInput
{
	CompanionTacticalMcHostileAuthorityCore::McRemoteAuthorityInput authority;
	int targetX;
	int targetY;
	int targetZ;
	int mapSizeX;
	int mapSizeY;
	int mapSizeZ;
	bool targetSameHorizontalAsActor;
	bool insufficientTuForTurn;
};

CompanionCommandCore::MoveValidationResult validateMcKneelExecutionPreflight(const McKneelValidationInput &input);
CompanionCommandCore::MoveValidationResult validateMcStandExecutionPreflight(const McStandValidationInput &input);
CompanionCommandCore::MoveValidationResult validateMcTurnFaceExecutionPreflight(const McTurnFaceValidationInput &input);

} // namespace CompanionTacticalMcHostilePostureExecutionCore

} // namespace OpenXcom