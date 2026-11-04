/*
 * Open X-Com Companion — local soldier AI/human ownership (M1-19).
 */
#include "CompanionSoldierOwnershipCore.h"
#include "CompanionCommandCore.h"
#include "CompanionJsonLite.h"
#include <cstdlib>

namespace OpenXcom
{

namespace CompanionSoldierOwnershipCore
{

namespace
{

bool parseAssignmentsObject(const std::string &json, std::map<int, std::string> *assignmentsOut)
{
	if (assignmentsOut == 0)
	{
		return false;
	}
	assignmentsOut->clear();
	std::string::size_type valueStart = 0;
	if (!CompanionJsonLite::findJsonKeyValueStart(json, "assignments", &valueStart))
	{
		return false;
	}
	if (valueStart >= json.size() || json[valueStart] != '{')
	{
		return false;
	}
	std::string::size_type pos = valueStart + 1;
	while (pos < json.size())
	{
		if (!CompanionJsonLite::skipWhitespace(json, &pos))
		{
			return false;
		}
		if (json[pos] == '}')
		{
			return true;
		}
		if (json[pos] != '"')
		{
			return false;
		}
		++pos;
		const std::string::size_type keyStart = pos;
		while (pos < json.size() && json[pos] != '"')
		{
			++pos;
		}
		if (pos >= json.size())
		{
			return false;
		}
		const std::string keyStr = json.substr(keyStart, pos - keyStart);
		++pos;
		if (!CompanionJsonLite::skipWhitespace(json, &pos))
		{
			return false;
		}
		if (json[pos] != ':')
		{
			return false;
		}
		++pos;
		if (!CompanionJsonLite::skipWhitespace(json, &pos))
		{
			return false;
		}
		std::string mode;
		if (!CompanionJsonLite::parseJsonStringValue(json, pos, &mode))
		{
			return false;
		}
		const std::string::size_type closeQuote = json.find('"', pos + 1);
		if (closeQuote == std::string::npos)
		{
			return false;
		}
		pos = closeQuote + 1;
		char *endPtr = 0;
		const long unitId = std::strtol(keyStr.c_str(), &endPtr, 10);
		if (endPtr == keyStr.c_str() || unitId <= 0)
		{
			return false;
		}
		if (mode != "ai" && mode != "human")
		{
			return false;
		}
		(*assignmentsOut)[static_cast<int>(unitId)] = mode;
		if (!CompanionJsonLite::skipWhitespace(json, &pos))
		{
			return false;
		}
		if (pos < json.size() && json[pos] == ',')
		{
			++pos;
			continue;
		}
		if (pos < json.size() && json[pos] == '}')
		{
			return true;
		}
		return false;
	}
	return false;
}

} // namespace

bool isExecutorEnabledFromEnvironment()
{
	const char *flag = std::getenv("OPENXCOM_COMPANION_COMMAND_EXECUTOR");
	return flag != 0 && flag[0] == '1' && flag[1] == '\0';
}

bool parseSoldierControlDocument(const std::string &controlFileJson, SoldierControlDocument *out)
{
	if (out == 0)
	{
		return false;
	}
	*out = SoldierControlDocument();
	if (controlFileJson.empty())
	{
		return false;
	}
	std::string::size_type pos = 0;
	if (!CompanionJsonLite::skipWhitespace(controlFileJson, &pos) || controlFileJson[pos] != '{')
	{
		return false;
	}
	if (!CompanionJsonLite::extractJsonIntField(controlFileJson, "schema_version", &out->schemaVersion) ||
	    out->schemaVersion != 1)
	{
		return false;
	}
	if (!CompanionJsonLite::extractJsonStringField(controlFileJson, "session_id", &out->sessionId) ||
	    !CompanionCommandCore::isValidSessionIdFormat(out->sessionId))
	{
		return false;
	}
	if (!parseAssignmentsObject(controlFileJson, &out->assignments))
	{
		return false;
	}
	CompanionJsonLite::extractJsonStringField(controlFileJson, "campaign_id", &out->campaignId);
	CompanionJsonLite::extractJsonStringField(controlFileJson, "tactical_battle_id", &out->tacticalBattleId);
	out->valid = true;
	return true;
}

namespace
{

bool relayBindingMatchesDocument(
	const SoldierControlDocument &doc,
	const RelayControlBindingContext &bindingContext)
{
	if (!doc.tacticalBattleId.empty() && !doc.campaignId.empty())
	{
		if (bindingContext.tacticalBattleId.empty() || bindingContext.campaignId.empty())
		{
			return false;
		}
		return doc.tacticalBattleId == bindingContext.tacticalBattleId
		    && doc.campaignId == bindingContext.campaignId;
	}
	if (bindingContext.battleSessionId.empty())
	{
		return false;
	}
	return doc.sessionId == bindingContext.battleSessionId;
}

} // namespace

SoldierControlMode controlModeForUnit(
	const std::string &controlFileJson,
	const RelayControlBindingContext &bindingContext,
	int unitId)
{
	if (controlFileJson.empty())
	{
		return CONTROL_HUMAN;
	}
	SoldierControlDocument doc = {};
	if (!parseSoldierControlDocument(controlFileJson, &doc))
	{
		return CONTROL_HUMAN;
	}
	if (!relayBindingMatchesDocument(doc, bindingContext))
	{
		return CONTROL_HUMAN;
	}
	std::map<int, std::string>::const_iterator found = doc.assignments.find(unitId);
	if (found == doc.assignments.end())
	{
		return CONTROL_HUMAN;
	}
	if (found->second == "ai")
	{
		return CONTROL_AI;
	}
	return CONTROL_HUMAN;
}

bool isAiControlledUnit(
	const std::string &controlFileJson,
	const RelayControlBindingContext &bindingContext,
	int unitId)
{
	return controlModeForUnit(controlFileJson, bindingContext, unitId) == CONTROL_AI;
}

} // namespace CompanionSoldierOwnershipCore

} // namespace OpenXcom
