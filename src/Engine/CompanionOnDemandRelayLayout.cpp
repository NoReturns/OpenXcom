/*
 * Open X-Com Companion — context-sensitive relay control placement (M1-26A).
 */
#include "CompanionOnDemandRelayLayout.h"
#include "CompanionManualUploadHudCore.h"
#include "CompanionOnDemandRelayCore.h"
#include <algorithm>

namespace OpenXcom
{

namespace CompanionOnDemandRelayLayout
{

namespace
{

void assignStatusStrip(int anchorX, int anchorWidth, int buttonY, int buttonHeight, CompactBarLayout *out)
{
	if (out == 0)
	{
		return;
	}
	const int maxWidth = CompanionOnDemandRelayCore::RELAY_STATUS_WIDTH;
	const int statusWidth = std::min(maxWidth, anchorWidth);
	const int statusX = anchorX + (anchorWidth - statusWidth) / 2;
	out->statusWidth = statusWidth;
	out->statusX = statusX;
	out->statusY = buttonY + buttonHeight + 1;
	out->manualNoticeWidth = statusWidth;
	out->manualNoticeX = statusX;
	out->manualNoticeY = out->statusY + CompanionOnDemandRelayCore::RELAY_STATUS_HEIGHT + 1;
	out->manualNoticeHeight = CompanionManualUploadHudCore::MANUAL_NOTICE_TEXT_HEIGHT;
}

} // namespace

bool compactBarFitsBaseViewport(int viewportWidth)
{
	const int total = COMPACT_BUTTON_WIDTH + COMPACT_BUTTON_GAP + COMPACT_BUTTON_WIDTH;
	return viewportWidth >= total;
}

bool compactBarFitsBattlescapeTop(int screenWidth, int topReservedRightPx)
{
	const int total = COMPACT_BUTTON_WIDTH + COMPACT_BUTTON_GAP + COMPACT_BUTTON_WIDTH;
	const int usable = screenWidth - topReservedRightPx;
	return usable >= total;
}

bool computeBaseViewportBarLayout(int viewportX, int viewportY, int viewportWidth, CompactBarLayout *out)
{
	if (out == 0 || viewportWidth <= 0)
	{
		return false;
	}
	if (!compactBarFitsBaseViewport(viewportWidth))
	{
		return false;
	}
	const int totalWidth = COMPACT_BUTTON_WIDTH + COMPACT_BUTTON_GAP + COMPACT_BUTTON_WIDTH;
	const int barX = viewportX + (viewportWidth - totalWidth) / 2;
	int barY = viewportY - COMPACT_BUTTON_HEIGHT - 1;
	if (barY < 0)
	{
		barY = 0;
	}
	out->buttonWidth = COMPACT_BUTTON_WIDTH;
	out->buttonHeight = COMPACT_BUTTON_HEIGHT;
	out->totalWidth = totalWidth;
	out->sendX = barX;
	out->sendY = barY;
	out->retrieveX = barX + COMPACT_BUTTON_WIDTH + COMPACT_BUTTON_GAP;
	out->retrieveY = barY;
	assignStatusStrip(viewportX, viewportWidth, out->sendY, out->buttonHeight, out);
	return true;
}

bool computeBattlescapeTopBarLayout(int screenWidth, int screenHeight, CompactBarLayout *out)
{
	if (out == 0 || screenWidth <= 0)
	{
		return false;
	}
	const int topReservedRightPx = 36;
	if (!compactBarFitsBattlescapeTop(screenWidth, topReservedRightPx))
	{
		return false;
	}
	const int layoutHeight = screenHeight > 0 ? screenHeight : BATTLESCAPE_REFERENCE_HEIGHT;
	const int totalWidth = COMPACT_BUTTON_WIDTH + COMPACT_BUTTON_GAP + COMPACT_BUTTON_WIDTH;
	const int barX = (screenWidth - totalWidth) / 2;
	out->buttonWidth = COMPACT_BUTTON_WIDTH;
	out->buttonHeight = COMPACT_BUTTON_HEIGHT;
	out->totalWidth = totalWidth;
	out->sendX = barX;
	out->sendY = BATTLESCAPE_TOP_BAR_Y;
	out->retrieveX = barX + COMPACT_BUTTON_WIDTH + COMPACT_BUTTON_GAP;
	out->retrieveY = BATTLESCAPE_TOP_BAR_Y;
	assignStatusStrip(0, screenWidth, out->sendY, out->buttonHeight, out);
	const int autoRowTotal = AUTO_RETRIEVE_TOGGLE_WIDTH + AUTO_RETRIEVE_ROW_GAP
	    + AUTO_RETRIEVE_STEP_BUTTON_WIDTH + AUTO_RETRIEVE_ROW_GAP
	    + AUTO_RETRIEVE_INTERVAL_TEXT_WIDTH + AUTO_RETRIEVE_ROW_GAP
	    + AUTO_RETRIEVE_STEP_BUTTON_WIDTH;
	out->autoRowTotalWidth = autoRowTotal;
	out->autoRowY = layoutHeight - COMPACT_BUTTON_HEIGHT - BATTLESCAPE_AUTO_RETRIEVE_BOTTOM_MARGIN;
	out->autoToggleX = BATTLESCAPE_AUTO_RETRIEVE_MARGIN_X;
	out->autoIntervalMinusX = out->autoToggleX + AUTO_RETRIEVE_TOGGLE_WIDTH + AUTO_RETRIEVE_ROW_GAP;
	out->autoIntervalTextX = out->autoIntervalMinusX + AUTO_RETRIEVE_STEP_BUTTON_WIDTH + AUTO_RETRIEVE_ROW_GAP;
	out->autoIntervalPlusX = out->autoIntervalTextX + AUTO_RETRIEVE_INTERVAL_TEXT_WIDTH + AUTO_RETRIEVE_ROW_GAP;
	out->autoRowHeight = COMPACT_BUTTON_HEIGHT;
	return true;
}

} // namespace CompanionOnDemandRelayLayout

} // namespace OpenXcom
