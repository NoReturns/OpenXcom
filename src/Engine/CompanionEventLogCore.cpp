/*
 * Open X-Com Companion — session id + eligibility core (linked by game and unit tests).
 */
#include "CompanionEventLog.h"
#include "CompanionEventLogTestSupport.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace OpenXcom
{

namespace
{

bool isRecordedWeaponFireAction(BattleActionType type)
{
	return type == BA_SNAPSHOT || type == BA_AIMEDSHOT || type == BA_AUTOSHOT || type == BA_LAUNCH;
}

std::string formatSessionId(uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3)
{
	char buf[33];
	std::sprintf(buf, "%08x%08x%08x%08x", w0, w1, w2, w3);
	return std::string(buf);
}

uint32_t mixSessionWord(uint32_t a, uint32_t b)
{
	uint64_t x = (static_cast<uint64_t>(a) << 32) ^ static_cast<uint64_t>(b);
	x ^= x >> 33;
	x *= 0xff51afd7ed558ccdULL;
	x ^= x >> 33;
	x *= 0xc4ceb9fe1a85ec53ULL;
	x ^= x >> 33;
	return static_cast<uint32_t>(x ^ (x >> 32));
}

} // namespace

namespace CompanionEventLogTestSupport
{

bool isValidSessionIdFormat(const std::string &sessionId)
{
	if (sessionId.size() != 32)
	{
		return false;
	}
	for (size_t i = 0; i < sessionId.size(); ++i)
	{
		const char c = sessionId[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
		{
			return false;
		}
	}
	return true;
}

std::string makeSessionId()
{
	static std::atomic<uint32_t> sessionNonce(0);
	const uint32_t nonce = ++sessionNonce;
	const uint64_t ticks = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
	const uint32_t w0 = mixSessionWord(nonce, static_cast<uint32_t>(ticks));
	const uint32_t w1 = mixSessionWord(static_cast<uint32_t>(ticks >> 32), static_cast<uint32_t>(ticks));
	const uint32_t w2 = mixSessionWord(w0, w1 ^ nonce);
	const uint32_t w3 = mixSessionWord(w2, nonce ^ static_cast<uint32_t>(ticks >> 16));
	return formatSessionId(w0, w1, w2, w3);
}

bool shouldRecordDeliberatePlayerWeaponFire(const EligibilityInput &input)
{
	if (input.debugMode)
	{
		return false;
	}
	if (input.battleSide != FACTION_PLAYER)
	{
		return false;
	}
	if (!input.playerOrdered)
	{
		return false;
	}
	if (!isRecordedWeaponFireAction(input.actionType))
	{
		return false;
	}
	if (input.actorFaction != FACTION_PLAYER || input.actorOriginalFaction != FACTION_PLAYER)
	{
		return false;
	}
	if (!input.actorHasGeoscapeSoldier)
	{
		return false;
	}
	return true;
}

} // namespace CompanionEventLogTestSupport

CompanionEventLog::CompanionEventLog() : _nextSeq(1), _droppedCount(0)
{
}

void CompanionEventLog::beginSession()
{
	_sessionId = CompanionEventLogTestSupport::makeSessionId();
	_events.clear();
	_nextSeq = 1;
	_droppedCount = 0;
}

int CompanionEventLog::getOldestSeq() const
{
	if (_events.empty())
	{
		return 0;
	}
	return _events.front().seq;
}

int CompanionEventLog::getLatestSeq() const
{
	if (_events.empty())
	{
		return 0;
	}
	return _events.back().seq;
}

}
