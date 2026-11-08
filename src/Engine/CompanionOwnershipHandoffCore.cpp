/*
 * Open X-Com Companion — ownership handoff safety (M1-WP7A).
 */
#include "CompanionOwnershipHandoffCore.h"

namespace OpenXcom
{

namespace CompanionOwnershipHandoffCore
{

CompanionBattlescapeOwnershipCore::OwnershipHandoffBlockReason evaluateHandoffBlockForActor(
	int actorUnitId,
	bool /*battleBusy*/,
	int inFlightCompanionActorUnitId,
	bool nativeActionInProgress)
{
	if (actorUnitId <= 0)
	{
		return CompanionBattlescapeOwnershipCore::HANDOFF_NOT_ASSIGNABLE;
	}
	if (inFlightCompanionActorUnitId > 0 && actorUnitId == inFlightCompanionActorUnitId)
	{
		return CompanionBattlescapeOwnershipCore::HANDOFF_COMPANION_COMMAND_IN_FLIGHT;
	}
	if (nativeActionInProgress)
	{
		return CompanionBattlescapeOwnershipCore::HANDOFF_NATIVE_ACTION_IN_PROGRESS;
	}
	return CompanionBattlescapeOwnershipCore::HANDOFF_OK;
}

} // namespace CompanionOwnershipHandoffCore

} // namespace OpenXcom
