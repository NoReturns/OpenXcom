#pragma once
/*
 * Open X-Com Companion — on-demand send/retrieve relay queue (M1-25E).
 */
#include <string>

namespace OpenXcom
{

class SavedGame;
class Mod;

namespace CompanionOnDemandRelayCore
{

const int RELAY_BUTTON_WIDTH = 88;
const int RELAY_BUTTON_HEIGHT = 14;

enum RelayControlHit
{
	RELAY_CONTROL_HIT_NONE = 0,
	RELAY_CONTROL_HIT_SEND,
	RELAY_CONTROL_HIT_RETRIEVE
};
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
	RELAY_UI_RETRIEVE_FAILED,
	RELAY_UI_MANUAL_UPLOAD_REQUIRED,
	RELAY_UI_DEFERRED,
	RELAY_UI_UNKNOWN
};

struct RelayStatusView
{
	std::string request_id;
	std::string action;
	std::string phase;
	std::string outcome;
	std::string error_code;
	std::string last_command_id;
	std::string last_command_status;
	std::string retrieve_outcome;
	bool terminal;
	bool parsed;
	long long updated_at_ms;

	RelayStatusView();
};

/** Strategic send only: marker is written inside tryEnqueueSend after in-flight guards. */
struct StrategicSendMarkerPrep
{
	std::string content_sha256;
	std::string campaign_id;
	long long exported_at_ms;
};

/** Live SEND uses save/mod (export after guards). Tests may set fixture_marker_prep only. */
struct StrategicSendEnqueueContext
{
	SavedGame *save;
	Mod *mod;
	const StrategicSendMarkerPrep *fixture_marker_prep;
};

long long companionWallClockMs();
bool companionSnapshotPathConfigured();
bool resolveRelayDir(std::string *outDir);
bool resolveRequestPath(std::string *outPath);
bool resolveStatusPath(std::string *outPath);
bool readStatusPhase(const std::string &statusJson, std::string *outPhase, bool *outTerminal);
bool parseRelayStatusJson(const std::string &statusJson, RelayStatusView *out);
bool shouldAcceptRelayStatusUpdate(const RelayStatusView &existing, const RelayStatusView &incoming, bool hasExisting);
RelayUiPhase mapPhaseString(const std::string &phase, const std::string &action);
RelayUiPhase mapStatusViewToUiPhase(const RelayStatusView &status);
bool isRelayBusyPhase(RelayUiPhase phase);
/** True while an in-flight relay should suppress SEND “needs publication” highlight (not terminal sent). */
bool suppressSendPublicationHighlight(RelayUiPhase phase);
bool tryEnqueueAction(const char *action, std::string *outRequestId, std::string *outError);
bool tryEnqueueSend(const char *sendScope, const char *briefingRequestId,
	std::string *outRequestId, std::string *outError,
	const std::string *predeterminedRequestId = 0,
	const StrategicSendEnqueueContext *strategicEnqueue = 0);
bool reportTerminalSendFailure(const char *message, const char *errorCode);
void formatStatusLabel(RelayUiPhase phase, std::string *out);
void formatRelayStatusLabel(const RelayStatusView &status, std::string *out);
bool relayControlRectHitTest(int absX, int absY, int rectX, int rectY, int rectW, int rectH, bool visible);
RelayControlHit relayControlHitTarget(int absX, int absY,
	int sendX, int sendY, int sendW, int sendH, bool sendVisible,
	int retrieveX, int retrieveY, int retrieveW, int retrieveH, bool retrieveVisible);
bool reconcilePendingOnDemandRelay(long long wallNowMs);

} // namespace CompanionOnDemandRelayCore

} // namespace OpenXcom
