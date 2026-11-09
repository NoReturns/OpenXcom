#pragma once
/*
 * Open X-Com Companion — send/retrieve relay controls overlay (M1-25E / M1-26A).
 */
namespace OpenXcom
{

class Mod;
class SavedGame;
class SavedBattleGame;
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
	Text *txtManualNotice;
	TextButton *btnAutoRetrieve;
	TextButton *btnAutoIntervalLess;
	TextButton *btnAutoIntervalMore;
	Text *txtAutoInterval;
};

void createUi(State *parent, Ui *out, const char *interfaceId);
void enterBattlescapeSession();
void leaveBattlescapeSession();
void layoutForBaseViewport(Ui *ui, int viewportX, int viewportY, int viewportWidth);
void layoutForGeoscapeGlobe(Ui *ui, int globeX, int globeY, int globeWidth);
void layoutForBattlescapeTop(Ui *ui, int screenWidth, int screenHeight);
void tick(Ui *ui);
void tickBattlescape(Ui *ui, SavedBattleGame *battle = 0);
void setControlsVisible(Ui *ui, bool visible);
bool onRetrievePressed();
bool onAutoRetrieveTogglePressed();
bool onAutoRetrieveIntervalLessPressed();
bool onAutoRetrieveIntervalMorePressed();
bool onStrategicSendPressed(SavedGame *save, Mod *mod);
bool absPointHitsVisibleRelayControl(const Ui *ui, int absX, int absY);
bool tryConsumeRelayMouseAction(State *state, Action *action, Ui *ui);

} // namespace CompanionOnDemandRelayOverlay

} // namespace OpenXcom
