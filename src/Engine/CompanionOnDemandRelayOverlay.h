#pragma once
/*
 * Open X-Com Companion — send/retrieve relay controls overlay (M1-25E / M1-26A).
 */
namespace OpenXcom
{

class Action;
class Text;
class TextButton;
class State;

namespace CompanionOnDemandRelayOverlay
{

struct Ui
{
	TextButton *btnSend;
	TextButton *btnRetrieve;
	Text *txtStatus;
};

void createUi(State *parent, Ui *out, const char *interfaceId);
void layoutForBaseViewport(Ui *ui, int viewportX, int viewportY, int viewportWidth);
void layoutForBattlescapeTop(Ui *ui, int screenWidth);
void tick(Ui *ui);
void setControlsVisible(Ui *ui, bool visible);
bool onRetrievePressed();
bool onStrategicSendPressed();
bool tryConsumeRelayMouseAction(State *state, Action *action, Ui *ui);

} // namespace CompanionOnDemandRelayOverlay

} // namespace OpenXcom
