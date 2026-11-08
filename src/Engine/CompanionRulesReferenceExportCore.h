#pragma once
/*
 * Open X-Com Companion — research-gated equipment rules reference (M1-WP5).
 */
#include <string>
#include <vector>

namespace OpenXcom
{

namespace CompanionRulesReferenceExportCore
{

static const int RULES_REFERENCE_SCHEMA_VERSION = 2;
static const char *RULES_REFERENCE_ARTIFACT_ID = "companion-rules-reference";
static const char *RULES_REFERENCE_FILENAME = "companion-rules-reference.json";
static const size_t RULES_REFERENCE_MAX_BYTES = 256 * 1024;

struct GameDateInput
{
	int year;
	int month;
	int day;
	int hour;
	int minute;
};

struct FiringModeInput
{
	bool present;
	int accuracy;
	int tuRule;
};

struct MeleeRulesInput
{
	bool present;
	int accuracy;
	int tuRule;
	int power;
};

struct MedikitRulesInput
{
	bool present;
	int heal;
	int painKiller;
	int stimulant;
	int tuUse;
};

struct InventorySectionTransferCostInput
{
	std::string destinationSlotId;
	int tu;
};

struct InventorySectionInput
{
	std::string id;
	std::string sectionType;
	std::vector<InventorySectionTransferCostInput> transferCosts;
};

struct ItemRulesInput
{
	std::string type;
	std::string battleType;
	std::string weaponCategory;
	bool weaponCategoryKnown;
	std::vector<std::string> compatibleAmmo;
	bool flatRate;
	int clipSize;
	int waypoints;
	bool twoHanded;
	bool arcingShot;
	bool strengthApplied;
	bool skillApplied;
	FiringModeInput snap;
	FiringModeInput autoShot;
	FiringModeInput aimed;
	MeleeRulesInput melee;
	bool includeDirectDamage;
	int damagePower;
	std::string damageType;
	int explosionRadius;
	int autoShots;
	int shotgunPellets;
	MedikitRulesInput medikit;
	std::string projectileDamageSource;
};

struct ExportBuildInput
{
	std::string campaignId;
	std::string saveName;
	GameDateInput gameDate;
	std::vector<std::string> activeModLabels;
	std::vector<std::string> discoveredResearch;
	std::vector<ItemRulesInput> items;
	std::vector<InventorySectionInput> inventorySections;
	long long exportedAtMs;
};

struct ExportBuildResult
{
	bool ok;
	std::string publicReason;
	std::string jsonUtf8;
	std::string contentRevisionSha256;
};

bool isResearchRequirementsEntitled(
	const std::vector<std::string> &requirements,
	const std::vector<std::string> &discoveredResearchSorted);

std::string battleTypeId(int battleTypeEnum);
std::string damageTypeId(int damageTypeEnum);
std::string weaponCategoryLabel(const ItemRulesInput &item);

std::string computeResearchRevisionSha256(const std::vector<std::string> &discoveredResearchSorted);
std::string computeRulesetRevisionSha256(const std::vector<std::string> &activeModLabelsSorted);
std::string computeContentRevisionSha256(
	const std::string &campaignId,
	const std::string &researchRevisionSha256,
	const std::string &rulesetRevisionSha256);

void sortItemsByType(std::vector<ItemRulesInput> *items);
void sortStringVector(std::vector<std::string> *values);

ExportBuildResult buildRulesReferenceJson(const ExportBuildInput &input);

bool isValidBuiltRulesReferenceJson(const std::string &jsonUtf8);

} // namespace CompanionRulesReferenceExportCore

} // namespace OpenXcom
