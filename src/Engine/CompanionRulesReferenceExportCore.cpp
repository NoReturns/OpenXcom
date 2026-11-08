/*
 * Open X-Com Companion — research-gated equipment rules reference (M1-WP5).
 */
#include "CompanionRulesReferenceExportCore.h"
#include "CompanionCampaignIdentityCore.h"
#include "CompanionSha256.h"
#include "CompanionJsonLite.h"
#include "../Mod/RuleItem.h"
#include <algorithm>
#include <sstream>

namespace OpenXcom
{

namespace CompanionRulesReferenceExportCore
{

namespace
{

std::string jsonEscapeString(const std::string &value)
{
	std::string out;
	out.reserve(value.size() + 8);
	for (std::string::size_type i = 0; i < value.size(); ++i)
	{
		const char c = value[i];
		if (c == '\\' || c == '"')
		{
			out.push_back('\\');
			out.push_back(c);
		}
		else if (c == '\n')
		{
			out += "\\n";
		}
		else if (c == '\r')
		{
			out += "\\r";
		}
		else if (static_cast<unsigned char>(c) < 0x20)
		{
			continue;
		}
		else
		{
			out.push_back(c);
		}
	}
	return out;
}

void appendQuotedString(std::ostringstream &json, const std::string &value)
{
	json << '"' << jsonEscapeString(value) << '"';
}

std::string joinLinesForDigest(const std::vector<std::string> &lines)
{
	std::ostringstream digestInput;
	for (std::vector<std::string>::const_iterator it = lines.begin(); it != lines.end(); ++it)
	{
		if (it != lines.begin())
		{
			digestInput << '\n';
		}
		digestInput << *it;
	}
	return digestInput.str();
}

bool digestLinesSha256(const std::vector<std::string> &lines, std::string *outHex64)
{
	if (outHex64 == 0)
	{
		return false;
	}
	return CompanionSha256::digestHexLowerUtf8(joinLinesForDigest(lines), outHex64);
}

std::string tuUnitLabel(bool flatRate)
{
	return flatRate ? "absolute" : "percent";
}

void appendInventorySectionsJson(std::ostringstream &json, const std::vector<InventorySectionInput> &sections)
{
	json << "  \"inventorySections\": [";
	for (std::vector<InventorySectionInput>::size_type i = 0; i < sections.size(); ++i)
	{
		if (i > 0)
		{
			json << ",";
		}
		const InventorySectionInput &sec = sections[i];
		json << "\n    {\n"
		     << "      \"id\": ";
		appendQuotedString(json, sec.id);
		json << ",\n"
		     << "      \"type\": \"" << jsonEscapeString(sec.sectionType) << "\",\n"
		     << "      \"transferCosts\": [";
		for (std::vector<InventorySectionTransferCostInput>::size_type j = 0; j < sec.transferCosts.size(); ++j)
		{
			if (j > 0)
			{
				json << ", ";
			}
			json << "\n        { \"destinationSlotId\": ";
			appendQuotedString(json, sec.transferCosts[j].destinationSlotId);
			json << ", \"tu\": " << sec.transferCosts[j].tu << " }";
		}
		json << "\n      ]\n"
		     << "    }";
	}
	json << "\n  ],\n";
}

void appendFiringModeObject(
	std::ostringstream &json,
	const char *modeName,
	const FiringModeInput &mode,
	bool flatRate)
{
	if (!mode.present)
	{
		return;
	}
	json << "        \"" << modeName << "\": {\n"
	     << "          \"accuracy\": " << mode.accuracy << ",\n"
	     << "          \"tuRule\": " << mode.tuRule << ",\n"
	     << "          \"tuUnit\": \"" << tuUnitLabel(flatRate) << "\"\n"
	     << "        }";
}

} // namespace

bool isResearchRequirementsEntitled(
	const std::vector<std::string> &requirements,
	const std::vector<std::string> &discoveredResearchSorted)
{
	if (requirements.empty())
	{
		return true;
	}
	for (std::vector<std::string>::const_iterator req = requirements.begin(); req != requirements.end(); ++req)
	{
		if (!std::binary_search(discoveredResearchSorted.begin(), discoveredResearchSorted.end(), *req))
		{
			return false;
		}
	}
	return true;
}

std::string battleTypeId(int battleTypeEnum)
{
	switch (battleTypeEnum)
	{
	case BT_NONE: return "none";
	case BT_FIREARM: return "firearm";
	case BT_AMMO: return "ammo";
	case BT_MELEE: return "melee";
	case BT_GRENADE: return "grenade";
	case BT_PROXIMITYGRENADE: return "proximity_grenade";
	case BT_MEDIKIT: return "medikit";
	case BT_SCANNER: return "scanner";
	case BT_MINDPROBE: return "mindprobe";
	case BT_PSIAMP: return "psiamp";
	case BT_FLARE: return "flare";
	case BT_CORPSE: return "corpse";
	default: return "unknown";
	}
}

std::string damageTypeId(int damageTypeEnum)
{
	switch (damageTypeEnum)
	{
	case DT_NONE: return "none";
	case DT_AP: return "ap";
	case DT_IN: return "incendiary";
	case DT_HE: return "he";
	case DT_LASER: return "laser";
	case DT_PLASMA: return "plasma";
	case DT_STUN: return "stun";
	case DT_MELEE: return "melee";
	case DT_ACID: return "acid";
	case DT_SMOKE: return "smoke";
	default: return "unknown";
	}
}

std::string weaponCategoryLabel(const ItemRulesInput &item)
{
	if (item.battleType == "firearm")
	{
		if (item.waypoints > 0)
		{
			return "launcher";
		}
		if (item.twoHanded)
		{
			return "rifle";
		}
		return "pistol";
	}
	if (item.battleType == "melee")
	{
		return "melee";
	}
	if (item.battleType == "grenade" || item.battleType == "proximity_grenade")
	{
		return "grenade";
	}
	if (item.battleType == "ammo")
	{
		return "ammunition";
	}
	return item.battleType;
}

std::string computeResearchRevisionSha256(const std::vector<std::string> &discoveredResearchSorted)
{
	std::string digest;
	if (!digestLinesSha256(discoveredResearchSorted, &digest))
	{
		return std::string();
	}
	return digest;
}

std::string computeRulesetRevisionSha256(const std::vector<std::string> &activeModLabelsSorted)
{
	std::string digest;
	if (!digestLinesSha256(activeModLabelsSorted, &digest))
	{
		return std::string();
	}
	return digest;
}

std::string computeContentRevisionSha256(
	const std::string &campaignId,
	const std::string &researchRevisionSha256,
	const std::string &rulesetRevisionSha256)
{
	const std::string payload = campaignId + "\n" + researchRevisionSha256 + "\n" + rulesetRevisionSha256;
	std::string digest;
	if (!CompanionSha256::digestHexLowerUtf8(payload, &digest))
	{
		return std::string();
	}
	return digest;
}

void sortItemsByType(std::vector<ItemRulesInput> *items)
{
	if (items == 0)
	{
		return;
	}
	std::sort(items->begin(), items->end(), [](const ItemRulesInput &a, const ItemRulesInput &b) {
		return a.type < b.type;
	});
}

void sortStringVector(std::vector<std::string> *values)
{
	if (values == 0)
	{
		return;
	}
	std::sort(values->begin(), values->end());
}

ExportBuildResult buildRulesReferenceJson(const ExportBuildInput &input)
{
	ExportBuildResult result = {};
	result.ok = false;

	if (!CompanionCampaignIdentityCore::isValidCampaignIdFormat(input.campaignId))
	{
		result.publicReason = "invalid_campaign_id";
		return result;
	}

	std::vector<std::string> mods = input.activeModLabels;
	std::vector<std::string> research = input.discoveredResearch;
	sortStringVector(&mods);
	sortStringVector(&research);

	const std::string researchRevision = computeResearchRevisionSha256(research);
	const std::string rulesetRevision = computeRulesetRevisionSha256(mods);
	const std::string contentRevision = computeContentRevisionSha256(
		input.campaignId, researchRevision, rulesetRevision);
	if (researchRevision.empty() || rulesetRevision.empty() || contentRevision.empty())
	{
		result.publicReason = "identity_digest_failed";
		return result;
	}

	std::vector<ItemRulesInput> items = input.items;
	sortItemsByType(&items);

	std::ostringstream json;
	json << "{\n"
	     << "  \"schema_version\": " << RULES_REFERENCE_SCHEMA_VERSION << ",\n"
	     << "  \"artifact\": \"" << RULES_REFERENCE_ARTIFACT_ID << "\",\n"
	     << "  \"exported_at_ms\": " << input.exportedAtMs << ",\n"
	     << "  \"identity\": {\n"
	     << "    \"campaign_id\": \"" << jsonEscapeString(input.campaignId) << "\",\n"
	     << "    \"save_name\": ";
	appendQuotedString(json, input.saveName);
	json << ",\n"
	     << "    \"game_date\": {\n"
	     << "      \"year\": " << input.gameDate.year << ",\n"
	     << "      \"month\": " << input.gameDate.month << ",\n"
	     << "      \"day\": " << input.gameDate.day << ",\n"
	     << "      \"hour\": " << input.gameDate.hour << ",\n"
	     << "      \"minute\": " << input.gameDate.minute << "\n"
	     << "    },\n"
	     << "    \"research_revision_sha256\": \"" << researchRevision << "\",\n"
	     << "    \"ruleset_revision_sha256\": \"" << rulesetRevision << "\",\n"
	     << "    \"content_revision_sha256\": \"" << contentRevision << "\"\n"
	     << "  },\n"
	     << "  \"ruleset\": {\n"
	     << "    \"mods\": [";

	for (std::vector<std::string>::size_type i = 0; i < mods.size(); ++i)
	{
		if (i > 0)
		{
			json << ", ";
		}
		appendQuotedString(json, mods[i]);
	}

	json << "]\n"
	     << "  },\n"
	     << "  \"limitations\": {\n"
	     << "    \"staticActionCosts\": \"RuleItem tuSnap/tuAuto/tuAimed/tuMelee/tuUse are percent of soldier base TU unless flatRate is true; BA_THROW (25) and BA_PRIME (50) are engine constants not stored on RuleItem.\",\n"
	     << "    \"tacticalActionCostsAuthoritative\": \"F6 tactical snapshot actionCosts reflect current soldier TU, posture, ammo, and environment.\",\n"
	     << "    \"firearmHitDamage\": \"Projectile hits use loaded ammunition RuleItem power and damageType, not the launcher item.\",\n"
	     << "    \"inventoryTransferCosts\": \"Per-section move TU costs are from each RuleInventory costs map (same source as Inventory drag); see inventorySections[].transferCosts. Reload (15 TU), unload (8 TU), and ground pickup (6 TU) are engine constants, not part of this map.\"\n"
	     << "  },\n";
	appendInventorySectionsJson(json, input.inventorySections);
	json << "  \"items\": [";

	for (std::vector<ItemRulesInput>::size_type itemIndex = 0; itemIndex < items.size(); ++itemIndex)
	{
		const ItemRulesInput &item = items[itemIndex];
		if (itemIndex > 0)
		{
			json << ",";
		}
		json << "\n    {\n"
		     << "      \"type\": ";
		appendQuotedString(json, item.type);
		json << ",\n"
		     << "      \"battleType\": \"" << jsonEscapeString(item.battleType) << "\",\n"
		     << "      \"weaponCategory\": \"" << jsonEscapeString(
		            item.weaponCategoryKnown ? item.weaponCategory : weaponCategoryLabel(item))
		     << "\"";

		if (!item.compatibleAmmo.empty())
		{
			json << ",\n      \"compatibleAmmo\": [";
			for (std::vector<std::string>::size_type ammoIndex = 0; ammoIndex < item.compatibleAmmo.size(); ++ammoIndex)
			{
				if (ammoIndex > 0)
				{
					json << ", ";
				}
				appendQuotedString(json, item.compatibleAmmo[ammoIndex]);
			}
			json << "]";
		}

		if (item.battleType == "firearm" && !item.projectileDamageSource.empty())
		{
			json << ",\n      \"projectileDamageSource\": \"" << jsonEscapeString(item.projectileDamageSource) << "\"";
		}

		if (item.battleType == "firearm" || item.battleType == "melee" || item.battleType == "psiamp")
		{
			json << ",\n      \"flatRate\": " << (item.flatRate ? "true" : "false");
		}

		if (item.battleType == "firearm")
		{
			json << ",\n      \"clipSize\": " << item.clipSize;
			if (item.waypoints > 0)
			{
				json << ",\n      \"waypoints\": " << item.waypoints;
			}
		}

		const bool anyFiringMode = item.snap.present || item.autoShot.present || item.aimed.present;
		if (anyFiringMode)
		{
			json << ",\n      \"firingModes\": {\n";
			bool firstMode = true;
			if (item.snap.present)
			{
				appendFiringModeObject(json, "snap", item.snap, item.flatRate);
				firstMode = false;
			}
			if (item.autoShot.present)
			{
				if (!firstMode)
				{
					json << ",\n";
				}
				appendFiringModeObject(json, "auto", item.autoShot, item.flatRate);
				firstMode = false;
			}
			if (item.aimed.present)
			{
				if (!firstMode)
				{
					json << ",\n";
				}
				appendFiringModeObject(json, "aimed", item.aimed, item.flatRate);
			}
			json << "\n      }";
			if (item.autoShots > 0)
			{
				json << ",\n      \"autoShots\": " << item.autoShots;
			}
		}

		if (item.melee.present)
		{
			json << ",\n      \"melee\": {\n"
			     << "        \"accuracy\": " << item.melee.accuracy << ",\n"
			     << "        \"tuRule\": " << item.melee.tuRule << ",\n"
			     << "        \"tuUnit\": \"" << tuUnitLabel(item.flatRate) << "\",\n"
			     << "        \"power\": " << item.melee.power << "\n"
			     << "      }";
		}

		if (item.includeDirectDamage)
		{
			json << ",\n      \"damagePower\": " << item.damagePower << ",\n"
			     << "      \"damageType\": \"" << jsonEscapeString(item.damageType) << "\"";
			if (item.strengthApplied)
			{
				json << ",\n      \"strengthApplied\": true";
			}
			if (item.shotgunPellets > 1)
		 {
				json << ",\n      \"shotgunPellets\": " << item.shotgunPellets;
			}
		}

		if (item.explosionRadius > 0)
		{
			json << ",\n      \"explosionRadius\": " << item.explosionRadius;
		}
		if (item.arcingShot)
		{
			json << ",\n      \"arcingShot\": true";
		}
		if (item.skillApplied && item.battleType == "firearm")
		{
			json << ",\n      \"skillApplied\": true";
		}

		if (item.medikit.present)
		{
			json << ",\n      \"medikit\": {\n"
			     << "        \"heal\": " << item.medikit.heal << ",\n"
			     << "        \"painKiller\": " << item.medikit.painKiller << ",\n"
			     << "        \"stimulant\": " << item.medikit.stimulant << ",\n"
			     << "        \"tuUse\": " << item.medikit.tuUse << ",\n"
			     << "        \"tuUnit\": \"" << tuUnitLabel(item.flatRate) << "\"\n"
			     << "      }";
		}

		json << "\n    }";
	}

	json << "\n  ]\n"
	     << "}\n";

	if (json.str().size() > RULES_REFERENCE_MAX_BYTES)
	{
		result.publicReason = "size_cap_exceeded";
		return result;
	}

	if (!isValidBuiltRulesReferenceJson(json.str()))
	{
		result.publicReason = "built_json_invalid";
		return result;
	}

	result.ok = true;
	result.jsonUtf8 = json.str();
	result.contentRevisionSha256 = contentRevision;
	return result;
}

bool isValidBuiltRulesReferenceJson(const std::string &jsonUtf8)
{
	int schemaVersion = 0;
	if (!CompanionJsonLite::extractJsonIntField(jsonUtf8, "schema_version", &schemaVersion)
	    || (schemaVersion != 1 && schemaVersion != RULES_REFERENCE_SCHEMA_VERSION))
	{
		return false;
	}
	std::string artifact;
	if (!CompanionJsonLite::extractJsonStringField(jsonUtf8, "artifact", &artifact)
	    || artifact != RULES_REFERENCE_ARTIFACT_ID)
	{
		return false;
	}
	std::string campaignId;
	if (!CompanionJsonLite::extractJsonStringField(jsonUtf8, "campaign_id", &campaignId)
	    || !CompanionCampaignIdentityCore::isValidCampaignIdFormat(campaignId))
	{
		return false;
	}
	std::string contentRevision;
	if (!CompanionJsonLite::extractJsonStringField(jsonUtf8, "content_revision_sha256", &contentRevision)
	    || !CompanionSha256::isValidHexDigest(contentRevision))
	{
		return false;
	}
	return true;
}

} // namespace CompanionRulesReferenceExportCore

} // namespace OpenXcom
