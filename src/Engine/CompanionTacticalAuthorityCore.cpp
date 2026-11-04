/*
 * Open X-Com Companion — canonical tactical authority (M1-23F1).
 */
#include "CompanionTacticalAuthorityCore.h"
#include "CompanionCampaignOwnershipCore.h"

namespace OpenXcom
{

namespace CompanionTacticalAuthorityCore
{

namespace
{

CompanionSoldierOwnershipCore::RelayControlBindingContext relayBindingFromInput(
	const TacticalAuthorityInput &input)
{
	CompanionSoldierOwnershipCore::RelayControlBindingContext ctx = {};
	ctx.battleSessionId = input.battleSessionId;
	ctx.campaignId = input.relayCampaignId;
	ctx.tacticalBattleId = input.relayTacticalBattleId;
	return ctx;
}

} // namespace

TacticalAuthorityDecision evaluateTacticalAuthority(const TacticalAuthorityInput &input)
{
	TacticalAuthorityDecision decision = { false, false };

	if (!input.campaignContextPresent)
	{
		return decision;
	}
	if (input.unitId <= 0 || !input.isLivePlayerSoldier)
	{
		return decision;
	}

	const CompanionSoldierOwnershipCore::RelayControlBindingContext relayBinding =
		relayBindingFromInput(input);
	const bool relayAiControlled = CompanionSoldierOwnershipCore::isAiControlledUnit(
		input.controlFileJson, relayBinding, input.unitId);

	if (input.campaignMode == CompanionSoldierOwnershipCore::CONTROL_AI)
	{
		decision.aiCommandPermitted = relayAiControlled;
		decision.humanOrderPermitted = false;
	}
	else
	{
		decision.aiCommandPermitted = false;
		decision.humanOrderPermitted = true;
	}

	return decision;
}

TacticalAuthorityInput buildTacticalAuthorityInput(
	bool campaignContextPresent,
	const std::set<int> &currentCampaignAiSoldierIds,
	bool isLivePlayerSoldier,
	int unitId,
	const std::string &controlFileJson,
	const std::string &battleSessionId,
	const std::string &relayCampaignId,
	const std::string &relayTacticalBattleId)
{
	TacticalAuthorityInput input = {};
	input.campaignContextPresent = campaignContextPresent;
	input.isLivePlayerSoldier = isLivePlayerSoldier;
	input.unitId = unitId;
	input.controlFileJson = controlFileJson;
	input.battleSessionId = battleSessionId;
	input.relayCampaignId = relayCampaignId;
	input.relayTacticalBattleId = relayTacticalBattleId;
	input.campaignMode = CompanionCampaignOwnershipCore::resolveCampaignSoldierControlMode(
		currentCampaignAiSoldierIds, unitId, isLivePlayerSoldier);
	return input;
}

bool isStaleRelayAiAfterCampaignHumanAssignment(
	const TacticalAuthorityInput &input,
	const std::string &controlFileJson,
	const std::string &battleSessionId)
{
	if (!input.campaignContextPresent || !input.isLivePlayerSoldier || input.unitId <= 0)
	{
		return false;
	}
	if (input.campaignMode != CompanionSoldierOwnershipCore::CONTROL_HUMAN)
	{
		return false;
	}
	CompanionSoldierOwnershipCore::RelayControlBindingContext ctx = relayBindingFromInput(input);
	ctx.battleSessionId = battleSessionId;
	return CompanionSoldierOwnershipCore::isAiControlledUnit(controlFileJson, ctx, input.unitId);
}

} // namespace CompanionTacticalAuthorityCore

} // namespace OpenXcom
