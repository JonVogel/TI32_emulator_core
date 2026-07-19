// ti_host_boot.cpp — TI-99/4A boot screen paint routines.
//
// Just the pixel-pushing. The wait-for-key loops between the two
// pages (and around them) stay per-host — box has BLE pairing UI
// takeover + audio cue; sunton has the tft->flush before the wait;
// guition doesn't have BLE at all. All three can call
// paintBootPage1() + waitForKey() + paintBootPage2() + waitForKey()
// + tiClearScreen() and get the same visual result.

#include <Arduino.h>
#include "ti_host.h"
#include "ti_font.h"
#include "ti_platform.h"  // TI32_VERSION for the row-24 build tag

namespace tihost
{

// Accessors defined in ti_host_render.cpp
TiHostConfig& getHostCommonConfig();
TiDisplay&    getHostCommonDisplay();

// Redefine char codes 129..137 with the TI logo patterns (from
// host_common's tiLogoChars) and place them in a 3x3 grid on screen
// with the top-left cell at (startRow, startCol).
void drawTexasLogo(int startRow, int startCol)
{
  for (int i = 0; i < 9; i++)
  {
    memcpy(charPatterns[129 + i], tiLogoChars[i], 8);
  }
  for (int r = 0; r < 3; r++)
  {
    for (int c = 0; c < 3; c++)
    {
      int ch = 129 + r * 3 + c;
      screenBuf[startRow + r][startCol + c] = (char)ch;
      drawCell(startCol + c, startRow + r);
    }
  }
}

// Small helper — centered text at row via screenBuf + drawCell.
// (Boot pages want cell-grid text, not host_common's flowing
// tiPrintString which would scroll on wrap.)
static void drawCenteredText(const char* text, int row)
{
  const auto& c = getHostCommonConfig();
  int len = (int)strlen(text);
  int col = (c.cols - len) / 2;
  if (col < 0) col = 0;
  for (int i = 0; i < len && col + i < c.cols; i++)
  {
    screenBuf[row][col + i] = text[i];
    drawCell(col + i, row);
  }
}

static void drawTextAt(const char* text, int row, int col)
{
  const auto& c = getHostCommonConfig();
  int len = (int)strlen(text);
  for (int i = 0; i < len && col + i < c.cols; i++)
  {
    screenBuf[row][col + i] = text[i];
    drawCell(col + i, row);
  }
}

void paintBootPage1()
{
  const auto& c = getHostCommonConfig();
  const auto& d = getHostCommonDisplay();

  // Clear display to cyan in the char area, black outside.
  if (d.hostFillBackground) d.hostFillBackground(tiPalette[8]);

  // Redefine char 128 as the © copyright symbol.
  memcpy(charPatterns[128], copyrightBitmap, 8);

  // Stripe colors — the same 15-slot palette (with 1-slot gap) that
  // the real TI title screen uses, left to right.
  const uint8_t stripes[] = {
    9, 4, 2, 12, 13, 14,        // left group
    5, 3, 14, 9, 15, 6, 10, 12, 9   // right group
  };
  const int numStripes = (int)sizeof(stripes);

  const int stripeW = (c.cols * c.char_w) / 16;
  const int stripeH = 3 * c.char_h;    // 3 rows tall
  const int gapEnd  = 7 * stripeW;     // gap = slot 6 (after 6 stripes)

  // Top band — TI rows 1-3
  int topY = c.display_y_offset;
  if (d.hostFillRect)
  {
    for (int i = 0; i < numStripes; i++)
    {
      int x = c.display_x_offset +
              ((i < 6) ? i * stripeW : (i - 6) * stripeW + gapEnd);
      d.hostFillRect(x, topY, stripeW, stripeH, tiPalette[stripes[i]]);
    }
    // Bottom band — TI rows 19-21 (0-indexed 18-20)
    int bottomY = c.display_y_offset + 18 * c.char_h;
    for (int i = 0; i < numStripes; i++)
    {
      int x = c.display_x_offset +
              ((i < 6) ? i * stripeW : (i - 6) * stripeW + gapEnd);
      d.hostFillRect(x, bottomY, stripeW, stripeH, tiPalette[stripes[i]]);
    }
  }

  // Texas logo — 3×3 char grid at TI rows 6-8 (0-indexed 5-7).
  drawTexasLogo(5, (c.cols - 3) / 2);

  drawCenteredText("TEXAS INSTRUMENTS",             9);
  drawCenteredText("HOME COMPUTER",                11);
  drawCenteredText("READY-PRESS ANY KEY TO BEGIN", 16);
  drawCenteredText("\x80" "1981    TEXAS INSTRUMENTS", 22);
  drawCenteredText("TI32 " TI32_VERSION,                23);

  Serial.println("PRESS ANY KEY TO CONTINUE");

  // Commit any batched draws (sunton double-buffered panel needs this;
  // box/guition hostFlush is a no-op).
  if (d.hostFlush) d.hostFlush();
}

void paintBootPage2()
{
  const auto& c = getHostCommonConfig();
  const auto& d = getHostCommonDisplay();

  if (d.hostFillBackground) d.hostFillBackground(tiPalette[8]);
  for (int r = 0; r < c.rows; r++)
  {
    memset(screenBuf[r], ' ', c.cols);
    memset(prevScreenBuf[r], 0, c.cols);
  }

  drawTextAt("TEXAS INSTRUMENTS",       0, 5);
  drawTextAt("HOME COMPUTER",           1, 7);
  drawTextAt("PRESS",                   3, 2);
  drawTextAt("1 FOR TI BASIC",          5, 2);
  drawTextAt("2 FOR TI EXTENDED BASIC", 7, 2);

  Serial.println("PRESS 1 OR 2 TO CONTINUE");

  if (d.hostFlush) d.hostFlush();
}

} // namespace tihost
