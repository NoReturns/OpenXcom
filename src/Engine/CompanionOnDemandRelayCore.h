#pragma once
/*
 * Open X-Com Companion — on-demand send/retrieve relay queue (M1-25E).
 */
#include <string>

namespace OpenXcom
{

namespace CompanionOnDemandRelayCore
{

const int RELAY_BUTTON_WIDTH = 88;
const int RELAY_BUTTON_HEIGHT = 14;
const int RELAY_STATUS_WIDTH = 200;
const int RELAY_STATUS_HEIGHT = 10;
const int RELAY_CONTROLS_X = 228;
const int RELAY_SEND_Y = 4;
const int RELAY_RETRIEVE_Y = 20;
const int RELAY_STATUS_Y = 36;

enum RelayUiPhase
{
	RELAY_UI_IDLE,
	RELAY_UI_SENDING,
	RELAY_UI_SENT,
	RELAY_UI_SEND_FAILED,
	RELAY_UI_NOT_READY,
	RELAY_UI_RETRIEVING,
	RELAY_UI_COMPLETED,
	RELAY_UI_NO_NEW_ORDERS,
	RELAY_UI_RETRIEVE_FAILED
};

long long companionWallClockMs();
bool companionSnapshotPathConfigured();
bool resolveRelayDir(std::string *outDir);
bool resolveRequestPath(std::string *outPath);
bool resolveStatusPath(std::string *outPath);
bool readStatusPhase(const std::string &statusJson, std::string *outPhase, bool *outTerminal);
RelayUiPhase mapPhaseString(const std::string &phase, const std::string &action);
bool isRelayBusyPhase(RelayUiPhase phase);
bool tryEnqueueAction(const char *action, std::string *outRequestId, std::string *outError);
bool tryEnqueueSend(const char *sendScope, const char *briefingRequestId,
	std::string *outRequestId, std::string *outError);
void formatStatusLabel(RelayUiPhase phase, std::string *out);
bool relayControlRectHitTest(int absX, int absY, int rectX, int rectY, bool visible);
bool reconcilePendingOnDemandRelay(long long wallNowMs);

} // namespace CompanionOnDemandRelayCore

} // namespace OpenXcom
