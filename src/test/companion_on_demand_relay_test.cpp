/*
 * Open X-Com Companion — on-demand relay queue (M1-25E).
 */
#include "Engine/CompanionOnDemandRelayCore.h"
#include "Engine/CompanionOnDemandRelayLayout.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <direct.h>
#define COMPANION_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define COMPANION_MKDIR(path) mkdir(path, 0755)
#endif

using namespace OpenXcom;
using namespace OpenXcom::CompanionOnDemandRelayCore;
using namespace OpenXcom::CompanionOnDemandRelayLayout;

static int g_failures = 0;

static void expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << std::endl;
		++g_failures;
	}
}

static void writeFile(const std::string &path, const std::string &body)
{
	std::ofstream out(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
	out << body;
}

static void testEnqueueAndDuplicateGuard(const std::string &root)
{
	const std::string snapshotPath = root + "/tactical-live.yaml";
	writeFile(snapshotPath, "tactical: {}\n");
#ifdef _WIN32
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str(), 1);
#endif

	std::string requestId;
	std::string error;
	expect(tryEnqueueSend("tactical", "0123456789abcdef0123456789abcdef", &requestId, &error), "tactical send enqueue succeeds");
	expect(!requestId.empty(), "request id assigned");

	std::string requestId2;
	expect(!tryEnqueueSend("tactical", "fedcba9876543210fedcba9876543210", &requestId2, &error), "duplicate enqueue blocked while pending");
	expect(error == "request_in_flight", "duplicate error reason");

	std::string requestPath;
	std::string statusPath;
	resolveRequestPath(&requestPath);
	resolveStatusPath(&statusPath);
	std::remove(requestPath.c_str());
	writeFile(statusPath, "{\n  \"action\": \"send\",\n  \"phase\": \"sending\",\n  \"terminal\": false\n}\n");
	expect(!tryEnqueueAction("retrieve", &requestId2, &error), "non-terminal status blocks enqueue");
	std::remove(statusPath.c_str());
}

static void testStatusPhaseMapping()
{
	expect(mapPhaseString("no_new_orders", "retrieve") == RELAY_UI_NO_NEW_ORDERS, "no_new_orders maps");
	expect(isRelayBusyPhase(RELAY_UI_SENDING), "sending is busy");
	expect(!isRelayBusyPhase(RELAY_UI_SENT), "sent is not busy");
	std::string label;
	formatStatusLabel(RELAY_UI_NO_NEW_ORDERS, &label);
	expect(label == "No new orders", "status label");
}

static void testRelayHitTestRequiresVisibleButtons(const std::string &root)
{
	const std::string snapshotPath = root + "/tactical-hit.yaml";
	writeFile(snapshotPath, "tactical: {}\n");
#ifdef _WIN32
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str(), 1);
#endif
	CompactBarLayout layout;
	expect(computeBaseViewportBarLayout(0, BASESCAPE_VIEWPORT_TOP_Y, BASESCAPE_VIEWPORT_WIDTH, &layout), "base bar layout");
	expect(!relayControlRectHitTest(layout.sendX + 1, layout.sendY + 1, layout.sendX, layout.sendY, false),
		"hidden relay rect does not hit-test");
	expect(relayControlRectHitTest(layout.sendX + 1, layout.sendY + 1, layout.sendX, layout.sendY, true),
		"visible relay rect hit-test");
}

static void testCompactBarLayoutDimensions()
{
	expect(compactBarFitsBaseViewport(BASESCAPE_VIEWPORT_WIDTH), "108px bar fits 192px base viewport");
	CompactBarLayout baseLayout;
	expect(computeBaseViewportBarLayout(0, BASESCAPE_VIEWPORT_TOP_Y, BASESCAPE_VIEWPORT_WIDTH, &baseLayout), "base layout computes");
	expect(baseLayout.sendY == 0, "base strip anchors at y=0 when viewport top is y=8");
	expect(baseLayout.sendX == 42, "base bar centered at x=42 on 192px viewport");
	expect(baseLayout.retrieveX == baseLayout.sendX + COMPACT_BUTTON_WIDTH + COMPACT_BUTTON_GAP, "retrieve follows send");

	CompactBarLayout battleLayout;
	expect(computeBattlescapeTopBarLayout(320, &battleLayout), "battlescape layout at 320px");
	expect(battleLayout.sendY == BATTLESCAPE_TOP_BAR_Y, "battlescape top bar y");
	expect(battleLayout.sendX == 106, "battlescape centered at 320px width");
}

static void testReconcileSupersededBriefing(const std::string &root)
{
	const std::string snapshotPath = root + "/tactical-live.yaml";
	writeFile(snapshotPath, "tactical: {}\n");
#ifdef _WIN32
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str(), 1);
#endif
	const std::string relayDir = root;
	const std::string requestPath = relayDir + "/companion-on-demand-request.json";
	const std::string statusPath = relayDir + "/companion-on-demand-status.json";
	const std::string briefingPath = relayDir + "/briefing-request.json";
	writeFile(briefingPath, "{\n  \"request_id\": \"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"\n}\n");
	writeFile(requestPath,
		"{\n"
		"  \"request_id\": \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\n"
		"  \"action\": \"send\",\n"
		"  \"status\": \"pending\",\n"
		"  \"created_at_ms\": 1,\n"
		"  \"briefing_request_id\": \"cccccccccccccccccccccccccccccccc\"\n"
		"}\n");
	const long long wallNowMs = 5000;
	expect(reconcilePendingOnDemandRelay(wallNowMs), "superseded briefing terminates pending request");
	std::ifstream pendingCheck(requestPath.c_str());
	expect(!pendingCheck.good(), "pending request file removed");
	std::string statusBody;
	std::ifstream statusIn(statusPath.c_str());
	std::ostringstream ss;
	ss << statusIn.rdbuf();
	statusBody = ss.str();
	std::string phase;
	bool terminal = false;
	readStatusPhase(statusBody, &phase, &terminal);
	expect(phase == "failed", "superseded status phase failed");
	expect(terminal, "superseded status terminal");
}

static void testReconcileOfflineRelay(const std::string &root)
{
	const std::string snapshotPath = root + "/tactical-live-offline.yaml";
	writeFile(snapshotPath, "tactical: {}\n");
#ifdef _WIN32
	_putenv_s("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str());
#else
	setenv("OPENXCOM_COMPANION_SNAPSHOT_PATH", snapshotPath.c_str(), 1);
#endif
	const std::string requestPath = root + "/companion-on-demand-request.json";
	writeFile(requestPath,
		"{\n"
		"  \"request_id\": \"dddddddddddddddddddddddddddddddd\",\n"
		"  \"action\": \"retrieve\",\n"
		"  \"status\": \"pending\",\n"
		"  \"created_at_ms\": 1000\n"
		"}\n");
	expect(reconcilePendingOnDemandRelay(4000), "offline relay terminates pending request");
	std::ifstream pendingCheck(requestPath.c_str());
	expect(!pendingCheck.good(), "offline pending request removed");
}

int main()
{
	const std::string root = "companion_on_demand_relay_test_tmp";
	COMPANION_MKDIR(root.c_str());
	testEnqueueAndDuplicateGuard(root);
	testStatusPhaseMapping();
	testCompactBarLayoutDimensions();
	testRelayHitTestRequiresVisibleButtons(root);
	testReconcileSupersededBriefing(root);
	testReconcileOfflineRelay(root);
	std::cout << (g_failures == 0 ? "OK" : "FAILED") << std::endl;
	return g_failures == 0 ? 0 : 1;
}
