/*
 * Open X-Com Companion — battlescape squad ownership panel (M1-WP7A).
 */
#include "CompanionSquadOwnershipState.h"
#include "BattlescapeState.h"
#include "InventoryState.h"
#include "../Engine/CompanionBattlescapeOwnershipCore.h"
#include "../Engine/CompanionCommandRuntime.h"
#include "../Engine/Game.h"
#include "../Engine/Action.h"
#include "../Engine/Options.h"
#include "../Engine/LocalizedText.h"
#include "../Interface/Frame.h"
#include "../Interface/Text.h"
#include "../Interface/TextButton.h"
#include "../Interface/ArrowButton.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/SavedGame.h"
#include <algorithm>

namespace OpenXcom
{

namespace
{

using CompanionBattlescapeOwnershipCore::SquadOwnershipRow;
using CompanionBattlescapeOwnershipCore::buildSquadOwnershipRows;
using CompanionBattlescapeOwnershipCore::tryApplyOwnershipToggle;
using CompanionSoldierOwnershipCore::CONTROL_AI;

static const int COL_NAME_X = 28;
static const int COL_NAME_W = 64;
static const int COL_ID_X = 94;
static const int COL_ID_W = 14;
static const int COL_OWNER_X = 110;
static const int COL_OWNER_W = 36;
static const int COL_STATUS_X = 148;
static const int COL_STATUS_W = 44;
static const int COL_TOGGLE_X = 194;
static const int COL_TOGGLE_W = 58;

} // namespace

CompanionSquadOwnershipState::CompanionSquadOwnershipState(
	BattlescapeState *parent,
	SavedBattleGame *battle,
	SavedGame *campaign,
	BattlescapeGame *game,
	InventoryState *inventoryRefresh) :
	_parent(parent),
	_inventoryRefresh(inventoryRefresh),
	_battle(battle),
	_campaign(campaign),
	_battleGame(game),
	_scrollOffset(0)
{
	_screen = false;
	for (int i = 0; i < MAX_VISIBLE_ROWS; ++i)
	{
		_rowSoldierId[i] = -1;
		_txtName[i] = 0;
		_txtId[i] = 0;
		_txtOwner[i] = 0;
		_txtStatus[i] = 0;
		_btnToggle[i] = 0;
	}

	_frame = new Frame(288, 158, 16, 18);
	_title = new Text(260, 9, 30, 24);
	_header = new Text(260, 9, 30, 34);
	_message = new Text(260, 18, 30, 148);
	_btnOk = new TextButton(40, 12, 140, 162);
	_btnScrollUp = new ArrowButton(ARROW_SMALL_UP, 11, 8, 276, 40);
	_btnScrollDown = new ArrowButton(ARROW_SMALL_DOWN, 11, 8, 276, 148);

	_game->getSavedGame()->getSavedBattle()->setPaletteByDepth(this);

	add(_frame, "infoBox", "battlescape");
	add(_title, "infoBox", "battlescape");
	add(_header, "infoBox", "battlescape");
	add(_message, "infoBox", "battlescape");
	add(_btnOk, "button", "battlescape");
	add(_btnScrollUp, "button1", "battlescape");
	add(_btnScrollDown, "button1", "battlescape");

	const int rowY0 = 44;
	for (int i = 0; i < MAX_VISIBLE_ROWS; ++i)
	{
		const int y = rowY0 + i * ROW_HEIGHT;
		_txtName[i] = new Text(COL_NAME_W, ROW_HEIGHT, COL_NAME_X, y);
		_txtId[i] = new Text(COL_ID_W, ROW_HEIGHT, COL_ID_X, y);
		_txtOwner[i] = new Text(COL_OWNER_W, ROW_HEIGHT, COL_OWNER_X, y);
		_txtStatus[i] = new Text(COL_STATUS_W, ROW_HEIGHT, COL_STATUS_X, y);
		_btnToggle[i] = new TextButton(COL_TOGGLE_W, ROW_HEIGHT, COL_TOGGLE_X, y);
		add(_txtName[i], "infoBox", "battlescape");
		add(_txtId[i], "infoBox", "battlescape");
		add(_txtOwner[i], "infoBox", "battlescape");
		add(_txtStatus[i], "infoBox", "battlescape");
		add(_btnToggle[i], "button", "battlescape");
		_btnToggle[i]->onMouseClick((ActionHandler)&CompanionSquadOwnershipState::btnToggleClick);
	}

	centerAllSurfaces();

	_frame->setHighContrast(true);
	_frame->setThickness(9);
	_title->setBig();
	_title->setHighContrast(true);
	_title->setText(tr("STR_COMPANION_SQUAD_OWNERSHIP"));
	_header->setHighContrast(true);
	_header->setText(tr("STR_COMPANION_SQUAD_HEADER"));
	_message->setHighContrast(true);
	_message->setWordWrap(true);
	_message->setVisible(false);
	_btnOk->setText(tr("STR_OK"));
	_btnOk->onMouseClick((ActionHandler)&CompanionSquadOwnershipState::btnOkClick);
	_btnOk->onKeyboardPress((ActionHandler)&CompanionSquadOwnershipState::btnOkClick, Options::keyCancel);
	_btnScrollUp->onMouseClick((ActionHandler)&CompanionSquadOwnershipState::btnScrollUpClick);
	_btnScrollDown->onMouseClick((ActionHandler)&CompanionSquadOwnershipState::btnScrollDownClick);

	rebuildList();
}

CompanionSquadOwnershipState::~CompanionSquadOwnershipState()
{
}

void CompanionSquadOwnershipState::rebuildList()
{
	_allSoldierIds.clear();
	const std::vector<SquadOwnershipRow> rows = buildSquadOwnershipRows(
		_campaign,
		_battle,
		_battleGame,
		_game->getLanguage(),
		CompanionCommandRuntime::getInFlightActorUnitId());
	for (size_t i = 0; i < rows.size(); ++i)
	{
		_allSoldierIds.push_back(rows[i].soldierId);
	}
	if (_scrollOffset + MAX_VISIBLE_ROWS > (int)_allSoldierIds.size())
	{
		_scrollOffset = std::max(0, (int)_allSoldierIds.size() - MAX_VISIBLE_ROWS);
	}
	applyScroll();
	updateScrollButtons();
}

void CompanionSquadOwnershipState::updateScrollButtons()
{
	const bool scrollable = (int)_allSoldierIds.size() > MAX_VISIBLE_ROWS;
	_btnScrollUp->setVisible(scrollable);
	_btnScrollDown->setVisible(scrollable);
}

void CompanionSquadOwnershipState::btnScrollUpClick(Action *)
{
	if (_scrollOffset > 0)
	{
		--_scrollOffset;
		applyScroll();
	}
}

void CompanionSquadOwnershipState::btnScrollDownClick(Action *)
{
	if (_scrollOffset + MAX_VISIBLE_ROWS < (int)_allSoldierIds.size())
	{
		++_scrollOffset;
		applyScroll();
	}
}

void CompanionSquadOwnershipState::applyScroll()
{
	const std::vector<SquadOwnershipRow> rows = buildSquadOwnershipRows(
		_campaign,
		_battle,
		_battleGame,
		_game->getLanguage(),
		CompanionCommandRuntime::getInFlightActorUnitId());

	for (int slot = 0; slot < MAX_VISIBLE_ROWS; ++slot)
	{
		_rowSoldierId[slot] = -1;
		_txtName[slot]->setVisible(false);
		_txtId[slot]->setVisible(false);
		_txtOwner[slot]->setVisible(false);
		_txtStatus[slot]->setVisible(false);
		_btnToggle[slot]->setVisible(false);
	}

	for (size_t i = 0; i < rows.size(); ++i)
	{
		if ((int)i < _scrollOffset || (int)i >= _scrollOffset + MAX_VISIBLE_ROWS)
		{
			continue;
		}
		const int slot = (int)i - _scrollOffset;
		const SquadOwnershipRow &row = rows[i];
		_rowSoldierId[slot] = row.soldierId;
		_txtName[slot]->setVisible(true);
		_txtId[slot]->setVisible(true);
		_txtOwner[slot]->setVisible(true);
		_txtStatus[slot]->setVisible(true);
		_txtName[slot]->setText(row.displayName);
		_txtName[slot]->setColor(CompanionBattlescapeOwnershipCore::soldierNameColorForEffectiveOwner(
			_game->getMod(), row.effectiveOwner));
		_txtId[slot]->setText(std::to_string(row.soldierId));
		std::string ownerLabel = (row.effectiveOwner == CONTROL_AI)
			? tr("STR_COMPANION_CONTROL_ASTER")
			: tr("STR_COMPANION_CONTROL_HUMAN");
		if (row.unsavedVsGeoscapeSave)
		{
			ownerLabel += "*";
		}
		_txtOwner[slot]->setText(ownerLabel);
		if (!row.statusNote.empty())
		{
			_txtStatus[slot]->setText(tr(row.statusNote));
		}
		else
		{
			_txtStatus[slot]->setText("");
		}
		if (row.canAssignToAster || row.canReclaimForHuman)
		{
			_btnToggle[slot]->setVisible(true);
			if (row.canReclaimForHuman)
			{
				_btnToggle[slot]->setText(tr("STR_COMPANION_SQUAD_RECLAIM"));
				_btnToggle[slot]->setTooltip("STR_COMPANION_RECLAIM_CONTROL");
			}
			else
			{
				_btnToggle[slot]->setText(tr("STR_COMPANION_SQUAD_ASSIGN_ASTER"));
				_btnToggle[slot]->setTooltip("STR_COMPANION_ASSIGN_TO_ASTER");
			}
		}
	}
	updateScrollButtons();
}

void CompanionSquadOwnershipState::btnToggleClick(Action *action)
{
	for (int slot = 0; slot < MAX_VISIBLE_ROWS; ++slot)
	{
		if (_btnToggle[slot] != action->getSender() || _rowSoldierId[slot] <= 0)
		{
			continue;
		}
		const std::vector<SquadOwnershipRow> rows = buildSquadOwnershipRows(
			_campaign,
			_battle,
			_battleGame,
			_game->getLanguage(),
			CompanionCommandRuntime::getInFlightActorUnitId());
		bool assignToAster = true;
		for (size_t i = 0; i < rows.size(); ++i)
		{
			if (rows[i].soldierId == _rowSoldierId[slot])
			{
				assignToAster = rows[i].canAssignToAster;
				break;
			}
		}
		const CompanionBattlescapeOwnershipCore::OwnershipChangeOutcome outcome =
			tryApplyOwnershipToggle(
				_campaign,
				_battle,
				_battleGame,
				_rowSoldierId[slot],
				assignToAster,
				CompanionCommandRuntime::getInFlightActorUnitId());
		if (!outcome.ok)
		{
			const std::string key =
				CompanionBattlescapeOwnershipCore::changeErrorMessageKey(outcome.error);
			_message->setVisible(true);
			_message->setText(key.empty() ? tr("STR_COMPANION_HANDOFF_BLOCKED") : tr(key));
		}
		else
		{
			_message->setVisible(false);
			if (_parent != 0)
			{
				_parent->updateSoldierInfo(false);
			}
			if (_inventoryRefresh != 0)
			{
				_inventoryRefresh->refreshCompanionOwnershipDisplay();
			}
		}
		rebuildList();
		return;
	}
}

void CompanionSquadOwnershipState::btnOkClick(Action *)
{
	if (_inventoryRefresh != 0)
	{
		_inventoryRefresh->refreshCompanionOwnershipDisplay();
	}
	if (_parent != 0)
	{
		_parent->updateSoldierInfo(false);
	}
	_game->popState();
}

void CompanionSquadOwnershipState::handle(Action *action)
{
	State::handle(action);
	if (action->getDetails()->type == SDL_MOUSEBUTTONDOWN)
	{
		if (action->getDetails()->button.button == SDL_BUTTON_RIGHT)
		{
			btnOkClick(action);
		}
		else if (action->getDetails()->button.button == SDL_BUTTON_WHEELUP)
		{
			btnScrollUpClick(action);
		}
		else if (action->getDetails()->button.button == SDL_BUTTON_WHEELDOWN)
		{
			btnScrollDownClick(action);
		}
	}
}

void CompanionSquadOwnershipState::think()
{
	State::think();
}

}
