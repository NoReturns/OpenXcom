#pragma once
/*
 * Open X-Com Companion — battlescape squad ownership panel (M1-WP7A).
 */
#include "../Engine/State.h"
#include <vector>

namespace OpenXcom
{

class BattlescapeGame;
class BattlescapeState;
class Frame;
class Text;
class TextButton;
class ArrowButton;
class SavedBattleGame;
class SavedGame;
class InventoryState;

class CompanionSquadOwnershipState : public State
{
public:
	CompanionSquadOwnershipState(
		BattlescapeState *parent,
		SavedBattleGame *battle,
		SavedGame *campaign,
		BattlescapeGame *game,
		InventoryState *inventoryRefresh = 0);
	~CompanionSquadOwnershipState();
	void think() override;
	void handle(Action *action) override;

private:
	// Avenger-scale roster (~20 soldiers): scroll when list exceeds visible rows.
	static const int MAX_VISIBLE_ROWS = 11;
	static const int ROW_HEIGHT = 10;

	BattlescapeState *_parent;
	InventoryState *_inventoryRefresh;
	SavedBattleGame *_battle;
	SavedGame *_campaign;
	BattlescapeGame *_battleGame;
	Frame *_frame;
	Text *_title;
	Text *_header;
	Text *_message;
	TextButton *_btnOk;
	ArrowButton *_btnScrollUp, *_btnScrollDown;
	Text *_txtName[MAX_VISIBLE_ROWS];
	Text *_txtId[MAX_VISIBLE_ROWS];
	Text *_txtOwner[MAX_VISIBLE_ROWS];
	Text *_txtStatus[MAX_VISIBLE_ROWS];
	TextButton *_btnToggle[MAX_VISIBLE_ROWS];
	int _rowSoldierId[MAX_VISIBLE_ROWS];
	std::vector<int> _allSoldierIds;
	int _scrollOffset;

	void rebuildList();
	void btnOkClick(Action *action);
	void btnToggleClick(Action *action);
	void btnScrollUpClick(Action *action);
	void btnScrollDownClick(Action *action);
	void applyScroll();
	void updateScrollButtons();
};

}
