/*
 * Open X-Com Companion — bounded roster-casualty snapshot settlement deferral.
 */
#include "CompanionCommandCasualtySettlementCore.h"

namespace OpenXcom
{

namespace CompanionCommandCasualtySettlementCore
{

bool shouldDeferRosterCasualtySettlement(int64_t deferStartedAtMs, int64_t nowMs)
{
	if (deferStartedAtMs <= 0)
	{
		return true;
	}
	const int64_t elapsed = nowMs - deferStartedAtMs;
	return elapsed >= 0 && elapsed < ROSTER_CASUALTY_SETTLEMENT_DEFER_MAX_MS;
}

} // namespace CompanionCommandCasualtySettlementCore

} // namespace OpenXcom
