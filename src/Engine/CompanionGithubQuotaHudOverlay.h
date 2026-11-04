#pragma once
/*
 * Open X-Com Companion — update in-game GitHub quota HUD text (M1-25D).
 */
namespace OpenXcom
{

class Text;

namespace CompanionGithubQuotaHudOverlay
{

const int HUD_TEXT_WIDTH = 220;
const int HUD_TEXT_HEIGHT = 30;
const int HUD_MARGIN_X = 4;

Text *createHudText(int anchorY);
void layoutHudText(Text *target, int screenHeight, int bottomReservePx);
void tick(Text *target);

} // namespace CompanionGithubQuotaHudOverlay

} // namespace OpenXcom
