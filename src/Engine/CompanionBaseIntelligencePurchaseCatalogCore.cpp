/*
 * Open X-Com Companion — Base Intelligence purchase catalog (PurchaseState parity).
 * Catalog rows must match Basescape/PurchaseState.cpp constructor (lines ~148–210).
 * Visibility is UI availability only — not funds/hangar/quarters feasibility.
 */
#include "CompanionBaseIntelligencePurchaseCatalogCore.h"
#include "../Mod/Mod.h"
#include "../Mod/RuleCraft.h"
#include "../Mod/RuleItem.h"
#include "../Mod/RuleSoldier.h"
#include "../Savegame/SavedGame.h"
#include "../Savegame/Base.h"
#include "../Savegame/ItemContainer.h"

#include <iomanip>
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

int exportBaseSlotId(size_t baseIndex)
{
	return static_cast<int>(baseIndex + 1);
}

struct CatalogEntry
{
	std::string kind;
	std::string ruleId;
	int unitCost;
	int deliveryHours;
	int quantityAtBase;
};

void appendCatalogEntry(std::ostringstream &json, const CatalogEntry &e, bool *first)
{
	if (!*first)
	{
		json << ',';
	}
	*first = false;
	json << '{';
	appendEscapedJsonString(json, "kind");
	json << ':';
	appendEscapedJsonString(json, e.kind);
	json << ',';
	appendEscapedJsonString(json, "rule_id");
	json << ':';
	appendEscapedJsonString(json, e.ruleId);
	json << ',';
	appendEscapedJsonString(json, "unit_cost");
	json << ':';
	json << e.unitCost;
	json << ',';
	appendEscapedJsonString(json, "delivery_hours");
	json << ':';
	json << e.deliveryHours;
	json << ',';
	appendEscapedJsonString(json, "quantity_at_base");
	json << ':';
	json << e.quantityAtBase;
	json << '}';
}

void buildCatalogForBase(SavedGame *save, Mod *mod, Base *base, std::vector<CatalogEntry> *out)
{
	out->clear();
	if (save == 0 || mod == 0 || base == 0 || out == 0)
	{
		return;
	}

	const std::vector<std::string> &soldiers = mod->getSoldiersList();
	for (std::vector<std::string>::const_iterator i = soldiers.begin(); i != soldiers.end(); ++i)
	{
		RuleSoldier *rule = mod->getSoldier(*i);
		if (rule == 0)
		{
			continue;
		}
		if (rule->getBuyCost() != 0 && save->isResearched(rule->getRequirements()))
		{
			CatalogEntry e = {};
			e.kind = "soldier";
			e.ruleId = rule->getType();
			e.unitCost = rule->getBuyCost();
			int time = rule->getTransferTime();
			if (time == 0)
			{
				time = mod->getPersonnelTime();
			}
			e.deliveryHours = time;
			e.quantityAtBase = base->getSoldierCount(rule->getType());
			out->push_back(e);
		}
	}

	{
		CatalogEntry e = {};
		e.kind = "scientist";
		e.ruleId = "STR_SCIENTIST";
		e.unitCost = mod->getScientistCost() * 2;
		e.deliveryHours = mod->getPersonnelTime();
		e.quantityAtBase = base->getTotalScientists();
		out->push_back(e);
	}
	{
		CatalogEntry e = {};
		e.kind = "engineer";
		e.ruleId = "STR_ENGINEER";
		e.unitCost = mod->getEngineerCost() * 2;
		e.deliveryHours = mod->getPersonnelTime();
		e.quantityAtBase = base->getTotalEngineers();
		out->push_back(e);
	}

	const std::vector<std::string> &crafts = mod->getCraftsList();
	for (std::vector<std::string>::const_iterator i = crafts.begin(); i != crafts.end(); ++i)
	{
		RuleCraft *rule = mod->getCraft(*i);
		if (rule == 0)
		{
			continue;
		}
		if (rule->getBuyCost() != 0 && save->isResearched(rule->getRequirements()))
		{
			CatalogEntry e = {};
			e.kind = "craft";
			e.ruleId = rule->getType();
			e.unitCost = rule->getBuyCost();
			e.deliveryHours = rule->getTransferTime();
			e.quantityAtBase = base->getCraftCount(rule->getType());
			out->push_back(e);
		}
	}

	const std::vector<std::string> &items = mod->getItemsList();
	for (std::vector<std::string>::const_iterator i = items.begin(); i != items.end(); ++i)
	{
		RuleItem *rule = mod->getItem(*i);
		if (rule == 0)
		{
			continue;
		}
		if (rule->getBuyCost() != 0 && save->isResearched(rule->getRequirements()))
		{
			CatalogEntry e = {};
			e.kind = "item";
			e.ruleId = rule->getType();
			e.unitCost = rule->getBuyCost();
			e.deliveryHours = rule->getTransferTime();
			e.quantityAtBase = 0;
			if (base->getStorageItems() != 0)
			{
				e.quantityAtBase = base->getStorageItems()->getItem(rule->getType());
			}
			out->push_back(e);
		}
	}
}

} // namespace

namespace CompanionBaseIntelligencePurchaseCatalogCore
{

void appendPurchasingJson(std::ostringstream &json, SavedGame *save, Mod *mod)
{
	json << ",\"purchasing\":{";
	appendEscapedJsonString(json, "notes");
	json << ':';
	appendEscapedJsonString(json,
		"Catalog mirrors Purchase/Hire rows (PurchaseState). Rows appear even when funds or capacity "
		"would block checkout. quantity_at_base matches the UI quantity column: soldiers by type count, "
		"scientists/engineers total at base, craft by type count, items in base stores only (excludes in-flight transfers).");
	json << ',';
	appendEscapedJsonString(json, "available_by_base");
	json << ':';
	json << '[';

	const std::vector<Base *> *bases = save != 0 ? save->getBases() : 0;
	if (bases != 0)
	{
		std::vector<CatalogEntry> catalog;
		for (size_t bi = 0; bi < bases->size(); ++bi)
		{
			Base *b = bases->at(bi);
			if (bi > 0)
			{
				json << ',';
			}
			json << '{';
			appendEscapedJsonString(json, "base_id");
			json << ':';
			json << exportBaseSlotId(bi);
			json << ',';
			appendEscapedJsonString(json, "capacity_for_purchases");
			json << ':';
			json << '{';
			const int qUsed = b->getUsedQuarters();
			const int qCap = b->getAvailableQuarters();
			appendEscapedJsonString(json, "living_quarters_used");
			json << ':';
			json << qUsed;
			json << ',';
			appendEscapedJsonString(json, "living_quarters_capacity");
			json << ':';
			json << qCap;
			json << ',';
			appendEscapedJsonString(json, "living_quarters_free");
			json << ':';
			json << (qCap - qUsed);
			json << ',';
			const int hUsed = b->getUsedHangars();
			const int hCap = b->getAvailableHangars();
			appendEscapedJsonString(json, "hangars_used");
			json << ':';
			json << hUsed;
			json << ',';
			appendEscapedJsonString(json, "hangars_capacity");
			json << ':';
			json << hCap;
			json << ',';
			appendEscapedJsonString(json, "hangars_free");
			json << ':';
			json << (hCap - hUsed);
			json << ',';
			const double storesUsed = b->getUsedStores();
			const int storesCap = b->getAvailableStores();
			appendEscapedJsonString(json, "storage_used_size");
			json << ':';
			json << storesUsed;
			json << ',';
			appendEscapedJsonString(json, "storage_capacity");
			json << ':';
			json << storesCap;
			json << ',';
			appendEscapedJsonString(json, "storage_free_size");
			json << ':';
			json << (static_cast<double>(storesCap) - storesUsed);
			json << '}';
			json << ',';
			appendEscapedJsonString(json, "catalog");
			json << ':';
			json << '[';

			buildCatalogForBase(save, mod, b, &catalog);
			bool firstCat = true;
			for (size_t ci = 0; ci < catalog.size(); ++ci)
			{
				appendCatalogEntry(json, catalog[ci], &firstCat);
			}
			json << ']';
			json << '}';
		}
	}
	json << ']';
	json << '}';
}

} // namespace CompanionBaseIntelligencePurchaseCatalogCore

} // namespace OpenXcom
