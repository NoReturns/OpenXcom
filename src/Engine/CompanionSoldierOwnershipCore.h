#pragma once
/*
 * Open X-Com Companion — local soldier AI/human ownership (M1-19).
 */
#include <map>
#include <string>

namespace OpenXcom
{

namespace CompanionSoldierOwnershipCore
{

enum SoldierControlMode
{
	CONTROL_HUMAN,
	CONTROL_AI
};

struct SoldierControlDocument
{
	bool valid;
	int schemaVersion;
	std::string sessionId;
	std::string campaignId;
	std::string tacticalBattleId;
	std::map<int, std::string> assignments;
};

/// Relay-side binding for companion-soldier-control.json (M1-25B durable battle scope).
struct RelayControlBindingContext
{
	std::string battleSessionId;
	std::string campaignId;
	std::string tacticalBattleId;
};

bool isExecutorEnabledFromEnvironment();
bool parseSoldierControlDocument(const std::string &controlFileJson, SoldierControlDocument *out);
SoldierControlMode controlModeForUnit(
	const std::string &controlFileJson,
	const RelayControlBindingContext &bindingContext,
	int unitId);
bool isAiControlledUnit(
	const std::string &controlFileJson,
	const RelayControlBindingContext &bindingContext,
	int unitId);

/// Legacy session-only binding (tests and pre-M1-25B JSON).
inline bool isAiControlledUnit(const std::string &controlFileJson, const std::string &battleSessionId, int unitId)
{
	RelayControlBindingContext ctx = {};
	ctx.battleSessionId = battleSessionId;
	return isAiControlledUnit(controlFileJson, ctx, unitId);
}

inline SoldierControlMode controlModeForUnit(
	const std::string &controlFileJson,
	const std::string &battleSessionId,
	int unitId)
{
	RelayControlBindingContext ctx = {};
	ctx.battleSessionId = battleSessionId;
	return controlModeForUnit(controlFileJson, ctx, unitId);
}

} // namespace CompanionSoldierOwnershipCore

} // namespace OpenXcom
