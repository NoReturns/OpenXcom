#pragma once
/*
 * Open X-Com Companion — bounded deferral of terminal command snapshot export
 * while roster-soldier death/collapse processing is still in flight (M1).
 */
#include <cstdint>

namespace OpenXcom
{

namespace CompanionCommandCasualtySettlementCore
{

/** Stay below cooperative relay local engine wait (8000 ms). */
const int ROSTER_CASUALTY_SETTLEMENT_DEFER_MAX_MS = 7500;

bool shouldDeferRosterCasualtySettlement(int64_t deferStartedAtMs, int64_t nowMs);

} // namespace CompanionCommandCasualtySettlementCore

} // namespace OpenXcom
