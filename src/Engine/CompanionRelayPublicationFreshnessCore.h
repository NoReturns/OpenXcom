#pragma once
/*
 * Open X-Com Companion — event-driven tactical SEND freshness (M1).
 */
#include "CompanionOnDemandRelayCore.h"
#include <string>

namespace OpenXcom
{

namespace CompanionRelayPublicationFreshnessCore
{

struct TacticalBindingSnapshot
{
	std::string sessionId;
	int turn;
	int stateVersion;
	int mutationGeneration;
	std::string exportSha256;
};

struct ConfirmedPublicationView
{
	bool parsed;
	bool terminalSuccess;
	std::string sessionId;
	int turn;
	int stateVersion;
	std::string snapshotSha256;
};

enum SendButtonAppearance
{
	SEND_APPEARANCE_NORMAL = 0,
	SEND_APPEARANCE_NEEDS_PUBLICATION
};

struct SendFreshnessEvaluateInput
{
	bool companionConfigured;
	bool relayBusy;
	TacticalBindingSnapshot live;
	TacticalBindingSnapshot lastExport;
	ConfirmedPublicationView confirmed;
};

SendButtonAppearance evaluateSendButtonAppearance(const SendFreshnessEvaluateInput &input);

bool readBriefingAckPublicationConfirm(const std::string &ackDir, ConfirmedPublicationView *out);
bool readTacticalPublicationConfirmFile(const std::string &ackDir, ConfirmedPublicationView *out);
ConfirmedPublicationView mergePublicationConfirms(
	const ConfirmedPublicationView &briefingAck,
	const ConfirmedPublicationView &publicationConfirm);

void resetConfirmedPublicationBinding();
void onBattleSessionContextChanged();

void noteTacticalBattleMutation();
void notePlayerTacticalMutationIfSnapshotConfigured();
int getTacticalMutationGeneration();
int getMutationAtLastExport();
int getMutationAtLastConfirmed();

void noteSnapshotExportBinding(
	int stateVersion,
	const std::string &snapshotSha256LowerHex,
	const std::string &sessionId,
	int turn);
TacticalBindingSnapshot getLastExportBinding();
bool loadConfirmedPublicationForLiveScope(
	const std::string &ackDir,
	const TacticalBindingSnapshot &live,
	const TacticalBindingSnapshot &lastExport,
	ConfirmedPublicationView *out);

} // namespace CompanionRelayPublicationFreshnessCore

} // namespace OpenXcom
