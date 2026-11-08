/*
 * Open X-Com Companion — manual equipment rules reference export (M1-WP5).
 */
#include "CompanionRulesReferenceExport.h"
#include "CompanionRulesReferenceExportCore.h"
#include "CompanionBriefingTestSupport.h"
#include "CompanionSnapshotExportCore.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/GameTime.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleItem.h"
#include "../Mod/RuleInventory.h"
#include "../Mod/RuleResearch.h"
#include "../Engine/Options.h"
#include "../Engine/Logger.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <map>

namespace OpenXcom
{

namespace
{

const char *SNAPSHOT_ENV = "OPENXCOM_COMPANION_SNAPSHOT_PATH";

std::string joinPath(const std::string &dir, const char *name)
{
	if (dir.empty())
	{
		return std::string(name);
	}
#ifdef _WIN32
	return dir + '\\' + name;
#else
	return dir + '/' + name;
#endif
}

bool shouldIncludeBattleType(BattleType battleType)
{
	switch (battleType)
	{
	case BT_CORPSE:
	case BT_NONE:
		return false;
	default:
		return true;
	}
}

void collectDiscoveredResearchNames(SavedGame *save, std::vector<std::string> *out)
{
	if (save == 0 || out == 0)
	{
		return;
	}
	for (std::vector<const RuleResearch *>::const_iterator it = save->getDiscoveredResearch().begin();
	     it != save->getDiscoveredResearch().end(); ++it)
	{
		if (*it != 0)
		{
			out->push_back((*it)->getName());
		}
	}
}

void collectActiveModLabels(std::vector<std::string> *out)
{
	if (out == 0)
	{
		return;
	}
	const std::vector<const ModInfo *> activeMods = Options::getActiveMods();
	for (std::vector<const ModInfo *>::const_iterator it = activeMods.begin(); it != activeMods.end(); ++it)
	{
		if (*it != 0)
		{
			out->push_back((*it)->getId() + " ver: " + (*it)->getVersion());
		}
	}
}

void buildItemRequirementsIndex(Mod *mod, std::map<std::string, std::vector<std::string> > *out)
{
	if (mod == 0 || out == 0)
	{
		return;
	}
	const std::vector<std::string> &itemTypes = mod->getItemsList();
	for (std::vector<std::string>::const_iterator it = itemTypes.begin(); it != itemTypes.end(); ++it)
	{
		RuleItem *rules = mod->getItem(*it, false);
		if (rules != 0)
		{
			(*out)[rules->getType()] = rules->getRequirements();
		}
	}
}

std::vector<std::string> filterEntitledCompatibleAmmo(
	RuleItem *weaponRules,
	const std::map<std::string, std::vector<std::string> > &requirementsByType,
	const std::vector<std::string> &discoveredResearchSorted)
{
	std::vector<std::string> entitled;
	static const std::vector<std::string> emptyRequirements;
	if (weaponRules == 0)
	{
		return entitled;
	}
	std::vector<std::string> *compatible = weaponRules->getCompatibleAmmo();
	if (compatible == 0)
	{
		return entitled;
	}
	for (std::vector<std::string>::const_iterator it = compatible->begin(); it != compatible->end(); ++it)
	{
		std::map<std::string, std::vector<std::string> >::const_iterator reqIt = requirementsByType.find(*it);
		const std::vector<std::string> &requirements =
			reqIt != requirementsByType.end() ? reqIt->second : emptyRequirements;
		if (CompanionRulesReferenceExportCore::isResearchRequirementsEntitled(
		        requirements, discoveredResearchSorted))
		{
			entitled.push_back(*it);
		}
	}
	std::sort(entitled.begin(), entitled.end());
	entitled.erase(std::unique(entitled.begin(), entitled.end()), entitled.end());
	return entitled;
}

CompanionRulesReferenceExportCore::ItemRulesInput buildItemRecord(
	RuleItem *rules,
	const std::map<std::string, std::vector<std::string> > &requirementsByType,
	const std::vector<std::string> &discoveredResearchSorted)
{
	CompanionRulesReferenceExportCore::ItemRulesInput item = {};
	item.type = rules->getType();
	item.battleType = CompanionRulesReferenceExportCore::battleTypeId(rules->getBattleType());
	item.weaponCategoryKnown = false;
	item.flatRate = rules->getFlatRate();
	item.clipSize = rules->getClipSize();
	item.waypoints = rules->getWaypoints();
	item.twoHanded = rules->isTwoHanded();
	item.arcingShot = rules->getArcingShot();
	item.strengthApplied = rules->isStrengthApplied();
	item.skillApplied = rules->isSkillApplied();
	item.autoShots = rules->getAutoShots();
	item.shotgunPellets = rules->getShotgunPellets();
	item.explosionRadius = rules->getExplosionRadius();

	if (rules->getBattleType() == BT_FIREARM)
	{
		item.projectileDamageSource = "loaded_ammo";
		item.compatibleAmmo = filterEntitledCompatibleAmmo(rules, requirementsByType, discoveredResearchSorted);
		if (rules->getAccuracySnap() != 0)
		{
			item.snap.present = true;
			item.snap.accuracy = rules->getAccuracySnap();
			item.snap.tuRule = rules->getTUSnap();
		}
		if (rules->getAccuracyAuto() != 0)
		{
			item.autoShot.present = true;
			item.autoShot.accuracy = rules->getAccuracyAuto();
			item.autoShot.tuRule = rules->getTUAuto();
		}
		if (rules->getAccuracyAimed() != 0)
		{
			item.aimed.present = true;
			item.aimed.accuracy = rules->getAccuracyAimed();
			item.aimed.tuRule = rules->getTUAimed();
		}
		if (rules->getTUMelee() > 0)
		{
			item.melee.present = true;
			item.melee.accuracy = rules->getAccuracyMelee();
			item.melee.tuRule = rules->getTUMelee();
			item.melee.power = rules->getMeleePower();
		}
	}
	else if (rules->getBattleType() == BT_AMMO)
	{
		item.includeDirectDamage = true;
		item.damagePower = rules->getPower();
		item.damageType = CompanionRulesReferenceExportCore::damageTypeId(rules->getDamageType());
	}
	else if (rules->getBattleType() == BT_MELEE)
	{
		item.includeDirectDamage = true;
		item.damagePower = rules->getPower() != 0 ? rules->getPower() : rules->getMeleePower();
		item.damageType = CompanionRulesReferenceExportCore::damageTypeId(rules->getDamageType());
		if (rules->getTUMelee() > 0)
		{
			item.melee.present = true;
			item.melee.accuracy = rules->getAccuracyMelee();
			item.melee.tuRule = rules->getTUMelee();
			item.melee.power = rules->getMeleePower();
		}
	}
	else if (rules->getBattleType() == BT_GRENADE || rules->getBattleType() == BT_PROXIMITYGRENADE)
	{
		item.includeDirectDamage = true;
		item.damagePower = rules->getPower();
		item.damageType = CompanionRulesReferenceExportCore::damageTypeId(rules->getDamageType());
	}
	else if (rules->getBattleType() == BT_MEDIKIT)
	{
		item.medikit.present = true;
		item.medikit.heal = rules->getHealQuantity();
		item.medikit.painKiller = rules->getPainKillerQuantity();
		item.medikit.stimulant = rules->getStimulantQuantity();
		item.medikit.tuUse = rules->getTUUse();
	}

	return item;
}

void collectInventorySections(Mod *mod, std::vector<CompanionRulesReferenceExportCore::InventorySectionInput> *out)
{
	if (mod == 0 || out == 0)
	{
		return;
	}
	std::map<std::string, RuleInventory *> *invs = mod->getInventories();
	if (invs == 0)
	{
		return;
	}
	for (std::map<std::string, RuleInventory *>::const_iterator it = invs->begin(); it != invs->end(); ++it)
	{
		RuleInventory *ri = it->second;
		if (ri == 0)
		{
			continue;
		}
		CompanionRulesReferenceExportCore::InventorySectionInput sec = {};
		sec.id = ri->getId();
		const InventoryType t = ri->getType();
		if (t == INV_SLOT)
		{
			sec.sectionType = "slot";
		}
		else if (t == INV_HAND)
		{
			sec.sectionType = "hand";
		}
		else if (t == INV_GROUND)
		{
			sec.sectionType = "ground";
		}
		else
		{
			sec.sectionType = "unknown";
		}
		const std::map<std::string, int> &costs = ri->getTransferCosts();
		for (std::map<std::string, int>::const_iterator c = costs.begin(); c != costs.end(); ++c)
		{
			CompanionRulesReferenceExportCore::InventorySectionTransferCostInput tc = {};
			tc.destinationSlotId = c->first;
			tc.tu = c->second;
			sec.transferCosts.push_back(tc);
		}
		std::sort(sec.transferCosts.begin(), sec.transferCosts.end(),
			[](const CompanionRulesReferenceExportCore::InventorySectionTransferCostInput &a,
			   const CompanionRulesReferenceExportCore::InventorySectionTransferCostInput &b) {
				return a.destinationSlotId < b.destinationSlotId;
			});
		out->push_back(sec);
	}
	std::sort(out->begin(), out->end(),
		[](const CompanionRulesReferenceExportCore::InventorySectionInput &a,
		   const CompanionRulesReferenceExportCore::InventorySectionInput &b) {
			return a.id < b.id;
		});
}

} // namespace

bool CompanionRulesReferenceExport::snapshotPathConfigured()
{
	const char *path = std::getenv(SNAPSHOT_ENV);
	return path != 0 && path[0] != '\0';
}

bool CompanionRulesReferenceExport::tryExport(SavedGame *save, Mod *mod)
{
	if (!snapshotPathConfigured())
	{
		Log(LOG_INFO) << "Companion rules reference: skipped (OPENXCOM_COMPANION_SNAPSHOT_PATH not set)";
		return false;
	}
	if (save == 0 || mod == 0)
	{
		Log(LOG_WARNING) << "Companion rules reference: export failed (missing save or mod)";
		return false;
	}
	save->ensureCompanionCampaignId();

	const char *snapshotPath = std::getenv(SNAPSHOT_ENV);
	const std::string outputDir = CompanionBriefingTestSupport::parentDirectoryOfSnapshotPath(snapshotPath);
	if (outputDir.empty())
	{
		Log(LOG_WARNING) << "Companion rules reference: invalid snapshot path directory";
		return false;
	}

	std::vector<std::string> discoveredResearch;
	collectDiscoveredResearchNames(save, &discoveredResearch);
	CompanionRulesReferenceExportCore::sortStringVector(&discoveredResearch);

	std::map<std::string, std::vector<std::string> > requirementsByType;
	buildItemRequirementsIndex(mod, &requirementsByType);

	std::vector<CompanionRulesReferenceExportCore::ItemRulesInput> items;
	const std::vector<std::string> &itemTypes = mod->getItemsList();
	for (std::vector<std::string>::const_iterator it = itemTypes.begin(); it != itemTypes.end(); ++it)
	{
		RuleItem *rules = mod->getItem(*it, false);
		if (rules == 0 || !shouldIncludeBattleType(rules->getBattleType()))
		{
			continue;
		}
		if (rules->isAlien())
		{
			continue;
		}
		const std::vector<std::string> &requirements = rules->getRequirements();
		if (!CompanionRulesReferenceExportCore::isResearchRequirementsEntitled(requirements, discoveredResearch))
		{
			continue;
		}
		items.push_back(buildItemRecord(rules, requirementsByType, discoveredResearch));
	}

	std::vector<CompanionRulesReferenceExportCore::InventorySectionInput> inventorySections;
	collectInventorySections(mod, &inventorySections);

	GameTime *time = save->getTime();
	CompanionRulesReferenceExportCore::ExportBuildInput input = {};
	input.campaignId = save->getCompanionCampaignId();
	input.saveName = save->getName();
	if (time != 0)
	{
		input.gameDate.year = time->getYear();
		input.gameDate.month = time->getMonth();
		input.gameDate.day = time->getDay();
		input.gameDate.hour = time->getHour();
		input.gameDate.minute = time->getMinute();
	}
	collectActiveModLabels(&input.activeModLabels);
	input.discoveredResearch = discoveredResearch;
	input.items = items;
	input.inventorySections = inventorySections;
	input.exportedAtMs = static_cast<long long>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count());

	const CompanionRulesReferenceExportCore::ExportBuildResult built =
		CompanionRulesReferenceExportCore::buildRulesReferenceJson(input);
	if (!built.ok)
	{
		Log(LOG_WARNING) << "Companion rules reference: build failed (" << built.publicReason << ")";
		return false;
	}

	const std::string destPath = joinPath(outputDir, CompanionRulesReferenceExportCore::RULES_REFERENCE_FILENAME);
	if (!CompanionSnapshotExportCore::writeUtf8FileAtomically(destPath, built.jsonUtf8))
	{
		Log(LOG_WARNING) << "Companion rules reference: atomic write failed: " << destPath;
		return false;
	}

	Log(LOG_INFO) << "Companion rules reference: wrote " << destPath
	              << " items=" << items.size()
	              << " revision=" << built.contentRevisionSha256.substr(0, 12);
	return true;
}

} // namespace OpenXcom
