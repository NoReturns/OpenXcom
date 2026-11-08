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

/**
 * WP8 diagnostic: how relay JSON + binding combined with in-memory tactical AI set.
 * Does not include raw JSON or full document contents.
 */
struct RelayControlDiagnostics
{
	bool controlFileReadOk;
	bool controlFileNonEmpty;
	bool controlJsonParseOk;
	bool soldierInTacticalAiSet;
	SoldierControlMode campaignMode;
	bool relayBindingMatch;
	bool assignmentPresent;
	bool assignmentIsAi;
	bool durableIdsInDocument;
	bool relayCampaignIdNonEmpty;
	bool relayTacticalBattleIdNonEmpty;
	bool campaignIdMatch;
	bool tacticalBattleIdMatch;
	bool sessionBindingMatch;
	bool relayAiControlled;
	bool aiCommandPermitted;
};

RelayControlDiagnostics computeRelayControlDiagnostics(
	const std::string &controlFileJson,
	bool controlFileReadOk,
	const RelayControlBindingContext &bindingContext,
	int unitId,
	SoldierControlMode campaignMode,
	bool soldierInTacticalAiSet);

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
