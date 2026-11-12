#pragma once
/*
 * Open X-Com Companion — Base Intelligence purchase catalog (PurchaseState parity).
 */
#include <sstream>

namespace OpenXcom
{

class Mod;
class SavedGame;

namespace CompanionBaseIntelligencePurchaseCatalogCore
{

/// Appends JSON object value for key "purchasing" (leading comma included).
void appendPurchasingJson(std::ostringstream &json, SavedGame *save, Mod *mod);

} // namespace CompanionBaseIntelligencePurchaseCatalogCore

} // namespace OpenXcom
