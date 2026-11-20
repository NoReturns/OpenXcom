/*
 * Open X-Com Companion — MC posture / turn-face validation (M1 MC-02B).
 */
#include "CompanionTacticalMcHostilePostureExecutionCore.h"

namespace OpenXcom
{

namespace CompanionTacticalMcHostilePostureExecutionCore
{

namespace
{

using CompanionCommandCore::MoveValidationResult;

MoveValidationResult reject(const char *code, const char *reason)
{
	MoveValidationResult result = {};
	result.accepted = false;
	result.rejectCode = code;
	result.publicReason = reason;
	return result;
}

} // namespace

MoveValidationResult validateMcKneelExecutionPreflight(const McKneelValidationInput &input)
{
	MoveValidationResult authority =
		CompanionTacticalMcHostileAuthorityCore::evaluateMcRemoteCommandAuthority(input.authority);
	if (!authority.accepted)
	{
		return authority;
	}
	if (!CompanionTacticalMcHostileAuthorityCore::mcKneelRelayExecutionSupported())
	{
		return reject("MC_EXECUTION_NOT_SUPPORTED", "MC kneel relay execution is not enabled.");
	}
	authority.accepted = false;
	if (input.actorAlreadyKneeled)
	{
		return reject("ALREADY_KNEELING", "Unit is already kneeling.");
	}
	if (!input.actorCanChangePosture)
	{
		return reject("INVALID_ACTOR", "Actor cannot kneel.");
	}
	if (input.insufficientTuForKneelDown)
	{
		return reject("INSUFFICIENT_TU", "Not enough time units to kneel.");
	}
	MoveValidationResult ok = {};
	ok.accepted = true;
	return ok;
}

MoveValidationResult validateMcStandExecutionPreflight(const McStandValidationInput &input)
{
	MoveValidationResult authority =
		CompanionTacticalMcHostileAuthorityCore::evaluateMcRemoteCommandAuthority(input.authority);
	if (!authority.accepted)
	{
		return authority;
	}
	if (!CompanionTacticalMcHostileAuthorityCore::mcKneelRelayExecutionSupported())
	{
		return reject("MC_EXECUTION_NOT_SUPPORTED", "MC stand relay execution is not enabled.");
	}
	authority.accepted = false;
	if (!input.actorKneeling)
	{
		return reject("ALREADY_STANDING", "Unit is already standing.");
	}
	if (!input.actorCanChangePosture)
	{
		return reject("INVALID_ACTOR", "Actor cannot change posture.");
	}
	if (input.insufficientTuForStandUp)
	{
		return reject("INSUFFICIENT_TU", "Not enough time units to stand.");
	}
	authority.accepted = true;
	return authority;
}

MoveValidationResult validateMcTurnFaceExecutionPreflight(const McTurnFaceValidationInput &input)
{
	MoveValidationResult authority =
		CompanionTacticalMcHostileAuthorityCore::evaluateMcRemoteCommandAuthority(input.authority);
	if (!authority.accepted)
	{
		return authority;
	}
	if (!CompanionTacticalMcHostileAuthorityCore::mcTurnFaceRelayExecutionSupported())
	{
		return reject("MC_EXECUTION_NOT_SUPPORTED", "MC_TURN_FACE relay execution is not enabled.");
	}
	authority.accepted = false;
	if (input.targetX < 0 || input.targetY < 0 || input.targetZ < 0 ||
	    input.targetX >= input.mapSizeX || input.targetY >= input.mapSizeY || input.targetZ >= input.mapSizeZ)
	{
		return reject("INVALID_TARGET", "Target out of map bounds.");
	}
	if (input.targetSameHorizontalAsActor)
	{
		return reject("INVALID_TARGET", "Target must differ from actor position on the horizontal plane.");
	}
	if (input.insufficientTuForTurn)
	{
		return reject("INSUFFICIENT_TU", "Not enough time units to turn.");
	}
	authority.accepted = true;
	return authority;
}

} // namespace CompanionTacticalMcHostilePostureExecutionCore

} // namespace OpenXcom