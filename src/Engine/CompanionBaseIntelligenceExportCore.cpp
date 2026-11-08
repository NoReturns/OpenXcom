/*
 * Open X-Com Companion — manual Geoscape base intelligence JSON (M1-BI1).
 */
#include "CompanionBaseIntelligenceExportCore.h"
#include "../Mod/Mod.h"
#include "../Mod/Armor.h"
#include "../Mod/RuleCountry.h"
#include "../Mod/RuleBaseFacility.h"
#include "../Mod/RuleCraftWeapon.h"
#include "../Mod/RuleResearch.h"
#include "../Mod/RuleManufacture.h"
#include "../Mod/RuleUfo.h"
#include "../Mod/RuleAlienMission.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/Base.h"
#include "../Savegame/BaseFacility.h"
#include "../Savegame/Soldier.h"
#include "../Savegame/Craft.h"
#include "../Savegame/CraftWeapon.h"
#include "../Savegame/ResearchProject.h"
#include "../Savegame/Production.h"
#include "../Savegame/ItemContainer.h"
#include "../Savegame/Transfer.h"
#include "../Savegame/Country.h"
#include "../Savegame/Ufo.h"
#include "../Savegame/Waypoint.h"
#include "../Savegame/MissionSite.h"
#include "../Savegame/AlienBase.h"
#include "../Savegame/GameTime.h"
#include "../Engine/Options.h"
#include "../version.h"

#include <algorithm>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace OpenXcom
{

namespace
{

void appendEscapedJsonString(std::ostringstream &out, const std::string &s)
{
	out << '"';
	for (size_t i = 0; i < s.size(); ++i)
	{
		const unsigned char c = static_cast<unsigned char>(s[i]);
		switch (c)
		{
		case '"': out << "\\\""; break;
		case '\\': out << "\\\\"; break;
		case '\b': out << "\\b"; break;
		case '\f': out << "\\f"; break;
		case '\n': out << "\\n"; break;
		case '\r': out << "\\r"; break;
		case '\t': out << "\\t"; break;
		default:
			if (c < 0x20)
			{
				out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec << std::setfill(' ');
			}
			else
			{
				out << static_cast<char>(c);
			}
		}
	}
	out << '"';
}

struct TruncationPolicy
{
	bool omitResearchAvailableByBase;
	bool omitManufacturingAvailable;
	bool omitGeoscapeWaypoints;
	bool omitCountryFundingHistory;
	size_t maxSoldiers;
	size_t maxStorageTypesPerBase;
	size_t maxCompletedResearch;
	size_t maxUfos;
	size_t maxMissionSites;
	size_t maxAlienBases;
	size_t maxWaypoints;
};

const TruncationPolicy FULL_POLICY = {
	false, false, false, false,
	(size_t)-1, (size_t)-1, (size_t)-1,
	(size_t)-1, (size_t)-1, (size_t)-1, (size_t)-1,
};

TruncationPolicy compactPolicy()
{
	TruncationPolicy p = {};
	p.omitResearchAvailableByBase = false;
	p.omitManufacturingAvailable = false;
	p.omitGeoscapeWaypoints = false;
	p.omitCountryFundingHistory = false;
	p.maxSoldiers = 200;
	p.maxStorageTypesPerBase = 350;
	p.maxCompletedResearch = 450;
	p.maxUfos = 80;
	p.maxMissionSites = 60;
	p.maxAlienBases = 40;
	p.maxWaypoints = 40;
	return p;
}

TruncationPolicy aggressivePolicy()
{
	TruncationPolicy p = compactPolicy();
	p.omitResearchAvailableByBase = true;
	p.omitManufacturingAvailable = true;
	p.omitGeoscapeWaypoints = true;
	p.omitCountryFundingHistory = true;
	p.maxSoldiers = 120;
	p.maxStorageTypesPerBase = 200;
	p.maxCompletedResearch = 250;
	p.maxUfos = 50;
	p.maxMissionSites = 35;
	p.maxAlienBases = 25;
	p.maxWaypoints = 0;
	return p;
}

/**
 * Sum of current monthly country funding; safe if a Country has no funding history yet.
 */
int computeCountryFundingTotal(SavedGame *save)
{
	if (save == 0)
	{
		return 0;
	}
	int total = 0;
	const std::vector<Country *> *cc = save->getCountries();
	for (size_t i = 0; i < cc->size(); ++i)
	{
		const std::vector<int> &f = cc->at(i)->getFunding();
		if (!f.empty())
		{
			total += f.back();
		}
	}
	return total;
}

std::string difficultyLabel(GameDifficulty d)
{
	switch (d)
	{
	case DIFF_BEGINNER: return "beginner";
	case DIFF_EXPERIENCED: return "experienced";
	case DIFF_VETERAN: return "veteran";
	case DIFF_GENIUS: return "genius";
	case DIFF_SUPERHUMAN: return "superhuman";
	default: return "unknown";
	}
}

bool psiStatsVisibleForExport(SavedGame *save, Mod *mod)
{
	if (!Options::psiStrengthEval)
	{
		return false;
	}
	return save->isResearched(mod->getPsiRequirements(), false);
}

/**
 * Stable BI1 base key: 1-based index into SavedGame::getBases() order.
 * OpenXcom does not assign Target::_id for bases placed in normal geoscape flow
 * (often all bases keep id 0), so export must not use Target::getId() for bases.
 */
int exportBaseSlotId(size_t baseIndex)
{
	return static_cast<int>(baseIndex + 1);
}

void appendItemQtyObject(std::ostringstream &json, const std::map<std::string, int> &sorted, size_t maxKeys, bool *truncated)
{
	json << '{';
	bool first = true;
	size_t n = 0;
	for (std::map<std::string, int>::const_iterator i = sorted.begin(); i != sorted.end(); ++i)
	{
		if (maxKeys != (size_t)-1 && n >= maxKeys)
		{
			if (truncated != 0)
			{
				*truncated = true;
			}
			break;
		}
		if (!first)
		{
			json << ',';
		}
		first = false;
		appendEscapedJsonString(json, i->first);
		json << ':';
		json << i->second;
		++n;
	}
	json << '}';
}

/**
 * Patches export_meta fields written as stubs (truncated=false, truncated_sections=[], utf8_bytes=0).
 * Scope all finds to the single export_meta object — the prior implementation re-scanned the full
 * document each utf8_bytes iteration (O(n²), effectively hangs on multi-hundred-KiB JSON).
 */
bool patchExportMeta(std::string *json, bool truncated, const std::vector<std::string> &sections,
	size_t /*utf8BytesIgnored*/, const std::string &/*engineVersionLabelIgnored*/)
{
	if (json == 0)
	{
		return false;
	}
	std::string &s = *json;
	const size_t em = s.find("\"export_meta\"");
	if (em == std::string::npos)
	{
		return false;
	}

	static const char secEmpty[] = "\"truncated_sections\":[]";
	const size_t secPos = s.find(secEmpty, em);
	if (secPos == std::string::npos)
	{
		return false;
	}
	std::ostringstream secOut;
	secOut << "\"truncated_sections\":[";
	for (size_t i = 0; i < sections.size(); ++i)
	{
		if (i > 0)
		{
			secOut << ',';
		}
		appendEscapedJsonString(secOut, sections[i]);
	}
	secOut << ']';
	s.replace(secPos, sizeof(secEmpty) - 1, secOut.str());

	static const char truncFalse[] = "\"truncated\":false";
	static const char truncTrue[] = "\"truncated\":true";
	if (truncated)
	{
		const size_t tfPos = s.find(truncFalse, em);
		if (tfPos == std::string::npos)
		{
			return false;
		}
		s.replace(tfPos, sizeof(truncFalse) - 1, truncTrue);
	}

	static const char utf8Key[] = "\"utf8_bytes\":";
	const size_t uPos = s.find(utf8Key, em);
	if (uPos == std::string::npos)
	{
		return false;
	}
	size_t digitStart = uPos + (sizeof(utf8Key) - 1);
	while (digitStart < s.size() && s[digitStart] == ' ')
	{
		++digitStart;
	}
	size_t digitEnd = digitStart;
	while (digitEnd < s.size() && s[digitEnd] >= '0' && s[digitEnd] <= '9')
	{
		++digitEnd;
	}
	const size_t oldWidth = digitEnd - digitStart;
	std::string num = std::to_string(s.size());
	for (int iter = 0; iter < 24; ++iter)
	{
		const size_t L = s.size() - oldWidth + num.size();
		const std::string next = std::to_string(L);
		if (next == num)
		{
			s.replace(digitStart, oldWidth, num);
			return true;
		}
		num = next;
	}
	return false;
}

struct BuildInternalResult
{
	bool ok;
	std::string jsonUtf8;
	bool anyTruncation;
	std::vector<std::string> truncatedSections;
	std::string skip_reason;
};

BuildInternalResult buildJsonUtf8Impl(SavedGame *save, Mod *mod, long long exportedAtMs,
	const std::string &engineVersionLabel, const TruncationPolicy &pol)
{
	BuildInternalResult r = {};
	r.ok = false;
	if (save == 0 || mod == 0)
	{
		r.skip_reason = "internal_error_missing_inputs";
		return r;
	}
	if (save->getDebugMode())
	{
		r.skip_reason = "debug_mode";
		return r;
	}

	std::ostringstream json;
	const GameTime *gt = save->getTime();
	const bool psiVisible = psiStatsVisibleForExport(save, mod);
	std::vector<std::string> truncSecs;

	json << std::fixed;
	json << '{';
	json << "\"schema_version\":2";

	// identity
	json << ",\"identity\":{";
	appendEscapedJsonString(json, "campaign_id");
	json << ':';
	appendEscapedJsonString(json, save->getCompanionCampaignId());
	json << ',';
	appendEscapedJsonString(json, "save_display_name");
	json << ':';
	appendEscapedJsonString(json, save->getName());
	json << ',';
	appendEscapedJsonString(json, "save_file_path");
	json << ":null";
	json << ',';
	appendEscapedJsonString(json, "difficulty");
	json << ':';
	appendEscapedJsonString(json, difficultyLabel(save->getDifficulty()));
	json << ',';
	appendEscapedJsonString(json, "ironman");
	json << ':';
	json << (save->isIronman() ? "true" : "false");
	json << ',';
	appendEscapedJsonString(json, "months_passed");
	json << ':';
	json << save->getMonthsPassed();
	json << ',';
	appendEscapedJsonString(json, "exported_at_ms");
	json << ':';
	json << exportedAtMs;
	json << ',';
	json << "\"game_date\":{";
	appendEscapedJsonString(json, "year");
	json << ':';
	json << (gt != 0 ? gt->getYear() : 0);
	json << ',';
	appendEscapedJsonString(json, "month");
	json << ':';
	json << (gt != 0 ? gt->getMonth() : 0);
	json << ',';
	appendEscapedJsonString(json, "day");
	json << ':';
	json << (gt != 0 ? gt->getDay() : 0);
	json << ',';
	appendEscapedJsonString(json, "hour");
	json << ':';
	json << (gt != 0 ? gt->getHour() : 0);
	json << ',';
	appendEscapedJsonString(json, "minute");
	json << ':';
	json << (gt != 0 ? gt->getMinute() : 0);
	json << '}';
	json << '}';

	json << ",\"schema_notes\":{";
	appendEscapedJsonString(json, "base_id");
	json << ':';
	appendEscapedJsonString(json,
		"All JSON fields named base_id, and bases[].id, use exportBaseSlotId: 1-based index into "
		"SavedGame::getBases() vector order (first base is 1). Stable for a given save while base list "
		"order is unchanged; base rename does not change these ids. "
		"bases[].target_id is the engine Target id (often 0 for geoscape-placed bases; not used as a unique key).");
	json << ',';
	appendEscapedJsonString(json, "bases.scientists_and_engineers");
	json << ':';
	appendEscapedJsonString(json,
		"Same as Base::_scientists / _engineers: unassigned staff pool in living quarters only. "
		"Scientists assigned to active research are not included; see bases[].research_active[].assigned_scientists "
		"and bases[].scientists_total_in_base (= pool + assignments + incoming scientist transfers). "
		"Engineers assigned to manufacturing are excluded from engineers; see manufacturing_active and engineers_total_in_base.");
	json << '}';

	const std::vector<Base *> *bases = save->getBases();
	std::map<const Base *, int> exportIdForBase;
	for (size_t bi = 0; bi < bases->size(); ++bi)
	{
		exportIdForBase[bases->at(bi)] = exportBaseSlotId(bi);
	}

	// roster counts for overview
	int totalSoldiers = 0;
	int availableSoldiers = 0;
	std::vector<Soldier *> roster;
	for (size_t bi = 0; bi < bases->size(); ++bi)
	{
		Base *b = bases->at(bi);
		for (size_t si = 0; si < b->getSoldiers()->size(); ++si)
		{
			roster.push_back(b->getSoldiers()->at(si));
			++totalSoldiers;
		}
		availableSoldiers += b->getAvailableSoldiers(false);
	}
	std::sort(roster.begin(), roster.end(),
		[](const Soldier *a, const Soldier *b) { return a->getId() < b->getId(); });
	bool rosterTrunc = false;
	if (pol.maxSoldiers != (size_t)-1 && roster.size() > pol.maxSoldiers)
	{
		roster.resize(pol.maxSoldiers);
		rosterTrunc = true;
		r.anyTruncation = true;
		truncSecs.push_back("personnel.soldiers");
	}

	// overview (short decision block)
	json << ",\"overview\":{";
	appendEscapedJsonString(json, "funds");
	json << ':';
	json << save->getFunds();
	json << ',';
	appendEscapedJsonString(json, "bases");
	json << ':';
	json << static_cast<int>(bases->size());
	json << ',';
	appendEscapedJsonString(json, "soldiers_total");
	json << ':';
	json << totalSoldiers;
	json << ',';
	appendEscapedJsonString(json, "soldiers_available_geoscape_count");
	json << ':';
	json << availableSoldiers;
	json << '}';

	// finances
	json << ",\"finances\":{";
	appendEscapedJsonString(json, "funds");
	json << ':';
	json << save->getFunds();
	json << ',';
	appendEscapedJsonString(json, "base_maintenance_total");
	json << ':';
	json << save->getBaseMaintenance();
	json << ',';
	appendEscapedJsonString(json, "country_funding_total");
	json << ':';
	json << computeCountryFundingTotal(save);
	json << ',';
	appendEscapedJsonString(json, "funds_monthly_history");
	json << ':';
	json << '[';
	for (size_t i = 0; i < save->getFundsList().size(); ++i)
	{
		if (i > 0)
		{
			json << ',';
		}
		json << save->getFundsList()[i];
	}
	json << ']';
	json << ',';
	appendEscapedJsonString(json, "maintenance_monthly_history");
	json << ':';
	json << '[';
	for (size_t i = 0; i < save->getMaintenances().size(); ++i)
	{
		if (i > 0)
		{
			json << ',';
		}
		json << save->getMaintenances()[i];
	}
	json << ']';
	json << ',';
	appendEscapedJsonString(json, "income_monthly_history");
	json << ':';
	json << '[';
	for (size_t i = 0; i < save->getIncomes().size(); ++i)
	{
		if (i > 0)
		{
			json << ',';
		}
		json << save->getIncomes()[i];
	}
	json << ']';
	json << ',';
	appendEscapedJsonString(json, "expenditure_monthly_history");
	json << ':';
	json << '[';
	for (size_t i = 0; i < save->getExpenditures().size(); ++i)
	{
		if (i > 0)
		{
			json << ',';
		}
		json << save->getExpenditures()[i];
	}
	json << ']';
	json << ',';
	appendEscapedJsonString(json, "research_score_monthly_history");
	json << ':';
	json << '[';
	for (size_t i = 0; i < save->getResearchScores().size(); ++i)
	{
		if (i > 0)
		{
			json << ',';
		}
		json << save->getResearchScores()[i];
	}
	json << ']';

	if (!pol.omitCountryFundingHistory)
	{
		json << ',';
		appendEscapedJsonString(json, "countries");
		json << ':';
		json << '[';
		for (size_t ci = 0; ci < save->getCountries()->size(); ++ci)
		{
			Country *c = save->getCountries()->at(ci);
			if (ci > 0)
			{
				json << ',';
			}
			json << '{';
			appendEscapedJsonString(json, "type");
			json << ':';
			appendEscapedJsonString(json, c->getRules()->getType());
			json << ',';
			appendEscapedJsonString(json, "pact");
			json << ':';
			json << (c->getPact() ? "true" : "false");
			json << ',';
			appendEscapedJsonString(json, "funding_history");
			json << ':';
			json << '[';
			const std::vector<int> &fund = c->getFunding();
			for (size_t fi = 0; fi < fund.size(); ++fi)
			{
				if (fi > 0)
				{
					json << ',';
				}
				json << fund[fi];
			}
			json << ']';
			json << '}';
		}
		json << ']';
	}
	else
	{
		r.anyTruncation = true;
		truncSecs.push_back("finances.countries");
	}

	json << '}';

	// bases
	json << ",\"bases\":[";
	for (size_t bi = 0; bi < bases->size(); ++bi)
	{
		Base *b = bases->at(bi);
		if (bi > 0)
		{
			json << ',';
		}
		json << '{';
		appendEscapedJsonString(json, "id");
		json << ':';
		json << exportBaseSlotId(bi);
		json << ',';
		appendEscapedJsonString(json, "target_id");
		json << ':';
		json << b->getId();
		json << ',';
		appendEscapedJsonString(json, "name");
		json << ':';
		appendEscapedJsonString(json, b->getDisplayLabelForExport());
		json << ',';
		appendEscapedJsonString(json, "lon");
		json << ':';
		json << b->getLongitude();
		json << ',';
		appendEscapedJsonString(json, "lat");
		json << ':';
		json << b->getLatitude();
		json << ',';
		appendEscapedJsonString(json, "scientists");
		json << ':';
		json << b->getScientists();
		json << ',';
		appendEscapedJsonString(json, "engineers");
		json << ':';
		json << b->getEngineers();
		json << ',';
		appendEscapedJsonString(json, "scientists_total_in_base");
		json << ':';
		json << b->getTotalScientists();
		json << ',';
		appendEscapedJsonString(json, "engineers_total_in_base");
		json << ':';
		json << b->getTotalEngineers();
		json << ',';
		appendEscapedJsonString(json, "storage");
		json << ':';
		{
			std::map<std::string, int> order;
			if (b->getStorageItems() != 0 && b->getStorageItems()->getContents() != 0)
			{
				const std::map<std::string, int> &raw = *b->getStorageItems()->getContents();
				order.insert(raw.begin(), raw.end());
			}
			bool st = false;
			appendItemQtyObject(json, order, pol.maxStorageTypesPerBase, &st);
			if (st)
			{
				r.anyTruncation = true;
				truncSecs.push_back("bases[" + std::to_string(static_cast<long long>(bi)) + "].storage");
			}
		}
		json << ',';
		appendEscapedJsonString(json, "storage_used_size");
		json << ':';
		json << b->getUsedStores();
		json << ',';
		appendEscapedJsonString(json, "storage_capacity");
		json << ':';
		json << b->getAvailableStores();
		json << ',';
		appendEscapedJsonString(json, "facilities");
		json << ':';
		json << '[';
		const std::vector<BaseFacility *> *fac = b->getFacilities();
		for (size_t fi = 0; fi < fac->size(); ++fi)
		{
			BaseFacility *bf = fac->at(fi);
			if (fi > 0)
			{
				json << ',';
			}
			json << '{';
			appendEscapedJsonString(json, "type");
			json << ':';
			appendEscapedJsonString(json, bf->getRules()->getType());
			json << ',';
			appendEscapedJsonString(json, "x");
			json << ':';
			json << bf->getX();
			json << ',';
			appendEscapedJsonString(json, "y");
			json << ':';
			json << bf->getY();
			json << ',';
			appendEscapedJsonString(json, "build_time_remaining_days");
			json << ':';
			json << bf->getBuildTime();
			json << ',';
			appendEscapedJsonString(json, "operational");
			json << ':';
			json << (bf->getBuildTime() <= 0 ? "true" : "false");
			json << '}';
		}
		json << ']';
		json << ',';
		appendEscapedJsonString(json, "research_active");
		json << ':';
		json << '[';
		const std::vector<ResearchProject *> &br = b->getResearch();
		for (size_t ri = 0; ri < br.size(); ++ri)
		{
			if (ri > 0)
			{
				json << ',';
			}
			const ResearchProject *rp = br[ri];
			json << '{';
			appendEscapedJsonString(json, "topic");
			json << ':';
			appendEscapedJsonString(json, rp->getRules()->getName());
			json << ',';
			appendEscapedJsonString(json, "assigned_scientists");
			json << ':';
			json << rp->getAssigned();
			json << ',';
			appendEscapedJsonString(json, "spent_man_days");
			json << ':';
			json << rp->getSpent();
			json << ',';
			appendEscapedJsonString(json, "cost_man_days");
			json << ':';
			json << rp->getCost();
			json << '}';
		}
		json << ']';
		json << ',';
		appendEscapedJsonString(json, "manufacturing_active");
		json << ':';
		json << '[';
		const std::vector<Production *> &bp = b->getProductions();
		for (size_t pi = 0; pi < bp.size(); ++pi)
		{
			if (pi > 0)
			{
				json << ',';
			}
			const Production *pr = bp[pi];
			json << '{';
			appendEscapedJsonString(json, "project");
			json << ':';
			appendEscapedJsonString(json, pr->getRules()->getName());
			json << ',';
			appendEscapedJsonString(json, "assigned_engineers");
			json << ':';
			json << pr->getAssignedEngineers();
			json << ',';
			appendEscapedJsonString(json, "amount_total");
			json << ':';
			json << pr->getAmountTotal();
			json << ',';
			appendEscapedJsonString(json, "amount_produced");
			json << ':';
			json << pr->getAmountProduced();
			json << ',';
			appendEscapedJsonString(json, "time_spent_hours");
			json << ':';
			json << pr->getTimeSpent();
			json << '}';
		}
		json << ']';
		json << ',';
		appendEscapedJsonString(json, "transfers_incoming");
		json << ':';
		json << '[';
		{
			const std::vector<Transfer *> *tx = b->getTransfers();
			bool firstTx = true;
			for (size_t ti = 0; ti < tx->size(); ++ti)
			{
				Transfer *t = tx->at(ti);
				if (t->getHours() <= 0)
				{
					continue;
				}
				if (!firstTx)
				{
					json << ',';
				}
				firstTx = false;
				json << '{';
				appendEscapedJsonString(json, "hours_remaining");
				json << ':';
				json << t->getHours();
				json << ',';
				appendEscapedJsonString(json, "type");
				json << ':';
				switch (t->getType())
				{
				case TRANSFER_ITEM: appendEscapedJsonString(json, "item"); break;
				case TRANSFER_CRAFT: appendEscapedJsonString(json, "craft"); break;
				case TRANSFER_SOLDIER: appendEscapedJsonString(json, "soldier"); break;
				case TRANSFER_SCIENTIST: appendEscapedJsonString(json, "scientists"); break;
				case TRANSFER_ENGINEER: appendEscapedJsonString(json, "engineers"); break;
				default: appendEscapedJsonString(json, "unknown"); break;
				}
				json << ',';
				appendEscapedJsonString(json, "quantity");
				json << ':';
				json << t->getQuantity();
				json << ',';
				appendEscapedJsonString(json, "item_id");
				json << ':';
				if (t->getType() == TRANSFER_ITEM)
				{
					appendEscapedJsonString(json, t->getItems());
				}
				else
				{
					json << "null";
				}
				json << '}';
			}
		}
		json << ']';
		json << '}';
	}
	json << ']';

	// personnel
	json << ",\"personnel\":{";
	appendEscapedJsonString(json, "total_soldiers");
	json << ':';
	json << totalSoldiers;
	json << ',';
	appendEscapedJsonString(json, "available_soldiers_geoscape_count");
	json << ':';
	json << availableSoldiers;
	json << ',';
	appendEscapedJsonString(json, "soldiers");
	json << ':';
	json << '[';
	for (size_t i = 0; i < roster.size(); ++i)
	{
		Soldier *s = roster[i];
		if (i > 0)
		{
			json << ',';
		}
		Base *home = 0;
		for (size_t bi = 0; bi < bases->size(); ++bi)
		{
			Base *b = bases->at(bi);
			const std::vector<Soldier *> *sv = b->getSoldiers();
			for (size_t j = 0; j < sv->size(); ++j)
			{
				if (sv->at(j)->getId() == s->getId())
				{
					home = b;
					break;
				}
			}
			if (home != 0)
			{
				break;
			}
		}
		json << '{';
		appendEscapedJsonString(json, "id");
		json << ':';
		json << s->getId();
		json << ',';
		appendEscapedJsonString(json, "name");
		json << ':';
		appendEscapedJsonString(json, s->getName(false, 999));
		json << ',';
		appendEscapedJsonString(json, "rank");
		json << ':';
		json << static_cast<int>(s->getRank());
		json << ',';
		appendEscapedJsonString(json, "base_id");
		json << ':';
		if (home != 0)
		{
			std::map<const Base *, int>::const_iterator it = exportIdForBase.find(home);
			json << (it != exportIdForBase.end() ? it->second : 0);
		}
		else
		{
			json << 0;
		}
		json << ',';
		appendEscapedJsonString(json, "base_name");
		json << ':';
		if (home != 0)
		{
			appendEscapedJsonString(json, home->getDisplayLabelForExport());
		}
		else
		{
			json << "null";
		}
		json << ',';
		appendEscapedJsonString(json, "assigned_craft");
		json << ':';
		if (s->getCraft() != 0)
		{
			json << '{';
			appendEscapedJsonString(json, "name");
			json << ':';
			appendEscapedJsonString(json, s->getCraft()->getDisplayLabelForExport());
			json << ',';
			appendEscapedJsonString(json, "type");
			json << ':';
			appendEscapedJsonString(json, s->getCraft()->getType());
			json << '}';
		}
		else
		{
			json << "null";
		}
		const UnitStats *st = s->getCurrentStats();
		json << ',';
		appendEscapedJsonString(json, "stats");
		json << ':';
		json << '{';
		json << "\"tu\":" << st->tu
		     << ",\"stamina\":" << st->stamina
		     << ",\"health\":" << st->health
		     << ",\"bravery\":" << st->bravery
		     << ",\"reactions\":" << st->reactions
		     << ",\"firing\":" << st->firing
		     << ",\"throwing\":" << st->throwing
		     << ",\"strength\":" << st->strength
		     << ",\"melee\":" << st->melee;
		if (psiVisible)
		{
			json << ",\"psi_strength\":" << st->psiStrength
			     << ",\"psi_skill\":" << st->psiSkill;
		}
		json << '}';
		json << ',';
		appendEscapedJsonString(json, "missions");
		json << ':';
		json << s->getMissions();
		json << ',';
		appendEscapedJsonString(json, "kills");
		json << ':';
		json << s->getKills();
		json << ',';
		appendEscapedJsonString(json, "wound_recovery_days");
		json << ':';
		json << s->getWoundRecovery();
		json << ',';
		appendEscapedJsonString(json, "in_psi_training");
		json << ':';
		json << (s->isInPsiTraining() ? "true" : "false");
		json << '}';
	}
	json << ']';
	if (rosterTrunc)
	{
		json << ',';
		appendEscapedJsonString(json, "soldiers_omitted_count");
		json << ':';
		json << (totalSoldiers - static_cast<int>(roster.size()));
	}
	json << '}';

	// aircraft
	json << ",\"aircraft\":[";
	{
		bool firstCraft = true;
		for (size_t bi = 0; bi < bases->size(); ++bi)
		{
			Base *b = bases->at(bi);
			const std::vector<Craft *> *cv = b->getCrafts();
			for (size_t ci = 0; ci < cv->size(); ++ci)
			{
				Craft *c = cv->at(ci);
				if (!firstCraft)
				{
					json << ',';
				}
				firstCraft = false;
				json << '{';
				appendEscapedJsonString(json, "type");
				json << ':';
				appendEscapedJsonString(json, c->getType());
				json << ',';
				appendEscapedJsonString(json, "name");
				json << ':';
				appendEscapedJsonString(json, c->getDisplayLabelForExport());
				json << ',';
				appendEscapedJsonString(json, "base_id");
				json << ':';
				json << exportBaseSlotId(bi);
				json << ',';
				appendEscapedJsonString(json, "base_name");
				json << ':';
				appendEscapedJsonString(json, b->getDisplayLabelForExport());
				json << ',';
				appendEscapedJsonString(json, "id");
				json << ':';
				json << c->getId();
				json << ',';
				appendEscapedJsonString(json, "status");
				json << ':';
				appendEscapedJsonString(json, c->getStatus());
				json << ',';
				appendEscapedJsonString(json, "fuel");
				json << ':';
				json << c->getFuel();
				json << ',';
				appendEscapedJsonString(json, "damage");
				json << ':';
				json << c->getDamage();
				json << ',';
				appendEscapedJsonString(json, "low_fuel");
				json << ':';
				json << (c->getLowFuel() ? "true" : "false");
				json << ',';
				appendEscapedJsonString(json, "on_mission");
				json << ':';
				json << (c->getMissionComplete() ? "true" : "false");
				json << ',';
				appendEscapedJsonString(json, "weapons");
				json << ':';
				json << '[';
				std::vector<CraftWeapon *> *w = c->getWeapons();
				for (size_t wi = 0; wi < w->size(); ++wi)
				{
					CraftWeapon *cw = w->at(wi);
					if (wi > 0)
					{
						json << ',';
					}
					json << '{';
					appendEscapedJsonString(json, "type");
					json << ':';
					appendEscapedJsonString(json, cw->getRules()->getType());
					json << ',';
					appendEscapedJsonString(json, "ammo");
					json << ':';
					json << cw->getAmmo();
					json << '}';
				}
				json << ']';
				json << ',';
				appendEscapedJsonString(json, "equipment");
				json << ':';
				{
					std::map<std::string, int> order;
					if (c->getItems() != 0 && c->getItems()->getContents() != 0)
					{
						const std::map<std::string, int> &raw = *c->getItems()->getContents();
						order.insert(raw.begin(), raw.end());
					}
					appendItemQtyObject(json, order, (size_t)-1, 0);
				}
				json << ',';
				appendEscapedJsonString(json, "soldiers_aboard");
				json << ':';
				json << c->getNumSoldiers();
				json << '}';
			}
		}
	}
	json << ']';

	// research
	json << ",\"research\":{";
	appendEscapedJsonString(json, "completed");
	json << ':';
	json << '[';
	{
		std::vector<std::string> done;
		const std::vector<const RuleResearch *> &disc = save->getDiscoveredResearch();
		for (size_t i = 0; i < disc.size(); ++i)
		{
			done.push_back(disc[i]->getName());
		}
		std::sort(done.begin(), done.end());
		bool rt = false;
		for (size_t i = 0; i < done.size(); ++i)
		{
			if (pol.maxCompletedResearch != (size_t)-1 && i >= pol.maxCompletedResearch)
			{
				rt = true;
				break;
			}
			if (i > 0)
			{
				json << ',';
			}
			appendEscapedJsonString(json, done[i]);
		}
		if (rt)
		{
			r.anyTruncation = true;
			truncSecs.push_back("research.completed");
		}
	}
	json << ']';
	if (!pol.omitResearchAvailableByBase)
	{
		json << ',';
		appendEscapedJsonString(json, "available_by_base");
		json << ':';
		json << '[';
		for (size_t bi = 0; bi < bases->size(); ++bi)
		{
			Base *b = bases->at(bi);
			if (bi > 0)
			{
				json << ',';
			}
			std::vector<RuleResearch *> avail;
			save->getAvailableResearchProjects(avail, mod, b, false);
			std::sort(avail.begin(), avail.end(),
				[](const RuleResearch *a, const RuleResearch *b) { return a->getName() < b->getName(); });
			json << '{';
			appendEscapedJsonString(json, "base_id");
			json << ':';
			json << exportBaseSlotId(bi);
			json << ',';
			appendEscapedJsonString(json, "topics");
			json << ':';
			json << '[';
			for (size_t ti = 0; ti < avail.size(); ++ti)
			{
				if (ti > 0)
				{
					json << ',';
				}
				appendEscapedJsonString(json, avail[ti]->getName());
			}
			json << ']';
			json << '}';
		}
		json << ']';
	}
	else
	{
		r.anyTruncation = true;
		truncSecs.push_back("research.available_by_base");
	}
	json << '}';

	// manufacturing
	json << ",\"manufacturing\":{";
	appendEscapedJsonString(json, "active");
	json << ':';
	json << '[';
	{
		bool first = true;
		for (size_t bi = 0; bi < bases->size(); ++bi)
		{
			Base *b = bases->at(bi);
			const std::vector<Production *> &bp = b->getProductions();
			for (size_t pi = 0; pi < bp.size(); ++pi)
			{
				if (!first)
				{
					json << ',';
				}
				first = false;
				const Production *pr = bp[pi];
				json << '{';
				appendEscapedJsonString(json, "base_id");
				json << ':';
				json << exportBaseSlotId(bi);
				json << ',';
				appendEscapedJsonString(json, "project");
				json << ':';
				appendEscapedJsonString(json, pr->getRules()->getName());
				json << ',';
				appendEscapedJsonString(json, "engineers");
				json << ':';
				json << pr->getAssignedEngineers();
				json << '}';
			}
		}
	}
	json << ']';
	if (!pol.omitManufacturingAvailable)
	{
		json << ',';
		appendEscapedJsonString(json, "available_options");
		json << ':';
		json << '[';
		for (size_t bi = 0; bi < bases->size(); ++bi)
		{
			Base *b = bases->at(bi);
			if (bi > 0)
			{
				json << ',';
			}
			std::vector<RuleManufacture *> mans;
			save->getAvailableProductions(mans, mod, b);
			std::sort(mans.begin(), mans.end(),
				[](const RuleManufacture *a, const RuleManufacture *b) { return a->getName() < b->getName(); });
			json << '{';
			appendEscapedJsonString(json, "base_id");
			json << ':';
			json << exportBaseSlotId(bi);
			json << ',';
			appendEscapedJsonString(json, "projects");
			json << ':';
			json << '[';
			for (size_t mi = 0; mi < mans.size(); ++mi)
			{
				if (mi > 0)
				{
					json << ',';
				}
				appendEscapedJsonString(json, mans[mi]->getName());
			}
			json << ']';
			json << '}';
		}
		json << ']';
	}
	else
	{
		r.anyTruncation = true;
		truncSecs.push_back("manufacturing.available_options");
	}
	json << "}";

	// purchasing / buy catalog — deferred (not implemented in engine exporter)
	json << ",\"purchasing\":{";
	appendEscapedJsonString(json, "note");
	json << ':';
	appendEscapedJsonString(json, "Purchase catalog not exported (would require PurchaseState / market parity audit).");
	json << '}';

	// geoscape
	json << ",\"geoscape\":{";
	appendEscapedJsonString(json, "ufos");
	json << ':';
	json << '[';
	{
		size_t count = 0;
		const std::vector<Ufo *> *us = save->getUfos();
		for (size_t i = 0; i < us->size(); ++i)
		{
			Ufo *u = us->at(i);
			if (!u->getDetected() && !u->getHyperDetected())
			{
				continue;
			}
			if (pol.maxUfos != (size_t)-1 && count >= pol.maxUfos)
			{
				r.anyTruncation = true;
				truncSecs.push_back("geoscape.ufos");
				break;
			}
			if (count > 0)
			{
				json << ',';
			}
			++count;
			json << '{';
			appendEscapedJsonString(json, "id");
			json << ':';
			json << u->getId();
			json << ',';
			appendEscapedJsonString(json, "type");
			json << ':';
			appendEscapedJsonString(json, u->getRules()->getType());
			json << ',';
			appendEscapedJsonString(json, "detected");
			json << ':';
			json << (u->getDetected() ? "true" : "false");
			json << ',';
			appendEscapedJsonString(json, "hyperwave_detected");
			json << ':';
			json << (u->getHyperDetected() ? "true" : "false");
			json << ',';
			appendEscapedJsonString(json, "lon");
			json << ':';
			json << u->getLongitude();
			json << ',';
			appendEscapedJsonString(json, "lat");
			json << ':';
			json << u->getLatitude();
			json << '}';
		}
	}
	json << ']';
	json << ',';
	appendEscapedJsonString(json, "mission_sites");
	json << ':';
	json << '[';
	{
		size_t count = 0;
		const std::vector<MissionSite *> *ms = save->getMissionSites();
		for (size_t i = 0; i < ms->size(); ++i)
		{
			MissionSite *m = ms->at(i);
			if (!m->getDetected())
			{
				continue;
			}
			if (pol.maxMissionSites != (size_t)-1 && count >= pol.maxMissionSites)
			{
				r.anyTruncation = true;
				truncSecs.push_back("geoscape.mission_sites");
				break;
			}
			if (count > 0)
			{
				json << ',';
			}
			++count;
			json << '{';
			appendEscapedJsonString(json, "type");
			json << ':';
			appendEscapedJsonString(json, m->getType());
			json << ',';
			appendEscapedJsonString(json, "race");
			json << ':';
			appendEscapedJsonString(json, m->getAlienRace());
			json << ',';
			appendEscapedJsonString(json, "lon");
			json << ':';
			json << m->getLongitude();
			json << ',';
			appendEscapedJsonString(json, "lat");
			json << ':';
			json << m->getLatitude();
			json << '}';
		}
	}
	json << ']';
	json << ',';
	appendEscapedJsonString(json, "alien_bases");
	json << ':';
	json << '[';
	{
		size_t count = 0;
		const std::vector<AlienBase *> *ab = save->getAlienBases();
		for (size_t i = 0; i < ab->size(); ++i)
		{
			AlienBase *a = ab->at(i);
			if (!a->isDiscovered())
			{
				continue;
			}
			if (pol.maxAlienBases != (size_t)-1 && count >= pol.maxAlienBases)
			{
				r.anyTruncation = true;
				truncSecs.push_back("geoscape.alien_bases");
				break;
			}
			if (count > 0)
			{
				json << ',';
			}
			++count;
			json << '{';
			appendEscapedJsonString(json, "type");
			json << ':';
			appendEscapedJsonString(json, a->getType());
			json << ',';
			appendEscapedJsonString(json, "race");
			json << ':';
			appendEscapedJsonString(json, a->getAlienRace());
			json << ',';
			appendEscapedJsonString(json, "lon");
			json << ':';
			json << a->getLongitude();
			json << ',';
			appendEscapedJsonString(json, "lat");
			json << ':';
			json << a->getLatitude();
			json << '}';
		}
	}
	json << ']';
	if (!pol.omitGeoscapeWaypoints)
	{
		json << ',';
		appendEscapedJsonString(json, "waypoints");
		json << ':';
		json << '[';
		const std::vector<Waypoint *> *wp = save->getWaypoints();
		size_t wcount = 0;
		for (size_t i = 0; i < wp->size(); ++i)
		{
			Waypoint *w = wp->at(i);
			if (pol.maxWaypoints != (size_t)-1 && wcount >= pol.maxWaypoints)
			{
				r.anyTruncation = true;
				truncSecs.push_back("geoscape.waypoints");
				break;
			}
			if (wcount > 0)
			{
				json << ',';
			}
			++wcount;
			json << '{';
			appendEscapedJsonString(json, "id");
			json << ':';
			json << w->getId();
			json << ',';
			appendEscapedJsonString(json, "lon");
			json << ':';
			json << w->getLongitude();
			json << ',';
			appendEscapedJsonString(json, "lat");
			json << ':';
			json << w->getLatitude();
			json << '}';
		}
		json << ']';
	}
	else
	{
		r.anyTruncation = true;
		truncSecs.push_back("geoscape.waypoints");
	}
	json << "}";

	// export_meta stub — patched after measuring UTF-8 size
	json << ",\"export_meta\":{";
	appendEscapedJsonString(json, "engine_build");
	json << ':';
	appendEscapedJsonString(json, engineVersionLabel);
	json << ',';
	appendEscapedJsonString(json, "utf8_bytes");
	json << ':';
	json << '0';
	json << ',';
	appendEscapedJsonString(json, "truncated");
	json << ':';
	json << "false";
	json << ',';
	appendEscapedJsonString(json, "truncated_sections");
	json << ':';
	json << "[]";
	json << '}';

	json << '}';

	r.jsonUtf8 = json.str();
	std::vector<std::string> dedup;
	std::set<std::string> seen;
	for (size_t i = 0; i < truncSecs.size(); ++i)
	{
		if (seen.insert(truncSecs[i]).second)
		{
			dedup.push_back(truncSecs[i]);
		}
	}
	r.truncatedSections = dedup;
	const bool truncFlag = r.anyTruncation;
	if (!patchExportMeta(&r.jsonUtf8, truncFlag, r.truncatedSections, r.jsonUtf8.size(), engineVersionLabel))
	{
		r.ok = false;
		r.skip_reason = "export_meta_patch_failed";
		return r;
	}
	r.ok = true;
	return r;
}

} // namespace

namespace CompanionBaseIntelligenceExportCore
{

BuildResult buildJsonUtf8(SavedGame *save, Mod *mod,
	long long exportedAtMs, const std::string &engineVersionLabel)
{
	BuildResult out = {};
	const BuildInternalResult a = buildJsonUtf8Impl(save, mod, exportedAtMs, engineVersionLabel, FULL_POLICY);
	if (!a.ok)
	{
		out.ok = false;
		out.skip_reason = a.skip_reason;
		return out;
	}
	if (a.jsonUtf8.size() <= EXPORT_JSON_MAX_BYTES)
	{
		out.ok = true;
		out.jsonUtf8 = a.jsonUtf8;
		out.truncated = a.anyTruncation;
		out.truncated_sections = a.truncatedSections;
		return out;
	}

	const BuildInternalResult b = buildJsonUtf8Impl(save, mod, exportedAtMs, engineVersionLabel, compactPolicy());
	if (b.ok && b.jsonUtf8.size() <= EXPORT_JSON_MAX_BYTES)
	{
		out.ok = true;
		out.jsonUtf8 = b.jsonUtf8;
		out.truncated = true;
		out.truncated_sections = b.truncatedSections;
		return out;
	}

	const BuildInternalResult c = buildJsonUtf8Impl(save, mod, exportedAtMs, engineVersionLabel, aggressivePolicy());
	if (!c.ok || c.jsonUtf8.size() > EXPORT_JSON_MAX_BYTES)
	{
		out.ok = false;
		out.skip_reason = "size_cap_exceeded";
		return out;
	}
	out.ok = true;
	out.jsonUtf8 = c.jsonUtf8;
	out.truncated = true;
	out.truncated_sections = c.truncatedSections;
	return out;
}

} // namespace CompanionBaseIntelligenceExportCore

} // namespace OpenXcom
