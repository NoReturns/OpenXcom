#pragma once
/*
 * Open X-Com Companion — ownership handoff safety (M1-WP7A).
 */
#include "CompanionBattlescapeOwnershipCore.h"

namespace OpenXcom
{

namespace CompanionOwnershipHandoffCore
{

CompanionBattlescapeOwnershipCore::OwnershipHandoffBlockReason evaluateHandoffBlockForActor(
	int actorUnitId,
	bool battleBusy,
	int inFlightCompanionActorUnitId,
	bool nativeActionInProgress);

} // namespace CompanionOwnershipHandoffCore

} // namespace OpenXcom
