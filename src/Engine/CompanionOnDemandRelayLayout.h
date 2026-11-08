#pragma once
/*
 * Open X-Com Companion — context-sensitive relay control placement (M1-26A).
 *
 * Native pixel math at 320×200 basescape reference:
 * - Base map viewport: 192×192 at (0, 8); top hangar grid row is 32px tall inside the view.
 * - Strip above viewport top: y=0..7 (8px) plus facility tooltip row; compact bar is 108×11 px
 *   (52+4+52), centered on the viewport, anchored at y=0 (uses tooltip strip, not hangar tiles).
 * - Battlescape: map to y=0..(200-iconsH); SEND/RETRIEVE + AUTO RETRIEVE in one top row at y=2,
 *   centered as a cluster (SEND, RETRIEVE, AUTO…) so the bottom soldier icon bar stays clear.
 */

namespace OpenXcom
{

namespace CompanionOnDemandRelayLayout
{

const int COMPACT_BUTTON_WIDTH = 52;
const int COMPACT_BUTTON_HEIGHT = 11;
const int COMPACT_BUTTON_GAP = 4;
const int BASESCAPE_VIEWPORT_WIDTH = 192;
const int BASESCAPE_VIEWPORT_TOP_Y = 8;
const int BASESCAPE_STRIP_ABOVE_VIEW_PX = BASESCAPE_VIEWPORT_TOP_Y;
const int BATTLESCAPE_TOP_BAR_Y = 2;
/** @deprecated bottom-left placement removed; retained for ABI/tests referencing prior constant. */
const int BATTLESCAPE_AUTO_RETRIEVE_BOTTOM_MARGIN = 2;
const int BATTLESCAPE_REFERENCE_HEIGHT = 200;
const int AUTO_RETRIEVE_TOGGLE_WIDTH = 56;
const int AUTO_RETRIEVE_STEP_BUTTON_WIDTH = 14;
const int AUTO_RETRIEVE_INTERVAL_TEXT_WIDTH = 26;
const int AUTO_RETRIEVE_ROW_GAP = 2;

struct CompactBarLayout
{
	int sendX;
	int sendY;
	int retrieveX;
	int retrieveY;
	int buttonWidth;
	int buttonHeight;
	int totalWidth;
	int statusX;
	int statusY;
	int statusWidth;
	int manualNoticeX;
	int manualNoticeY;
	int manualNoticeWidth;
	int manualNoticeHeight;
	int autoRowY;
	int autoToggleX;
	int autoIntervalMinusX;
	int autoIntervalTextX;
	int autoIntervalPlusX;
	int autoRowHeight;
	int autoRowTotalWidth;
};

bool computeBaseViewportBarLayout(int viewportX, int viewportY, int viewportWidth, CompactBarLayout *out);
bool computeBattlescapeTopBarLayout(int screenWidth, int screenHeight, CompactBarLayout *out);
bool compactBarFitsBaseViewport(int viewportWidth);
bool compactBarFitsBattlescapeTop(int screenWidth, int topReservedRightPx);

} // namespace CompanionOnDemandRelayLayout

} // namespace OpenXcom
