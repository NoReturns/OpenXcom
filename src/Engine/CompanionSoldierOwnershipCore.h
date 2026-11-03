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
	std::map<int, std::string> assignments;
};

bool isExecutorEnabledFromEnvironment();
bool parseSoldierControlDocument(const std::string &controlFileJson, SoldierControlDocument *out);
bool isAiControlledUnit(const std::string &controlFileJson, const std::string &battleSessionId, int unitId);
SoldierControlMode controlModeForUnit(const std::string &controlFileJson, const std::string &battleSessionId, int unitId);

} // namespace CompanionSoldierOwnershipCore

} // namespace OpenXcom
