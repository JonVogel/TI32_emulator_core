// ti_host_render.cpp — shared cell rendering pipeline.
//
// Handles the drawCell scale-and-blit that used to be duplicated (with
// different scale factors) in each host's main.cpp. The 8x8 ROM font is
// nearest-neighbor scaled to cfg.char_w x cfg.char_h — 1x on box, 2x on
// sunton, non-integer on guition (planned 10x10). One algorithm covers
// all three uniformly; the host tells us what cell size to render into
// via the TiHostConfig passed to hostCommonInit().
//
// scrollUp/tiPrintChar/tiClearScreen/printError still live in each
// host's main.cpp for now — they need per-host hooks (post-scroll
// sprite redraw, TI border repaint, error honk) that get added in the
// next commit.

// Arduino.h before ti_font.h — the font header references PROGMEM +
// memcpy_P inline for its ROM arrays / initCharPatterns. See the same
// dance in ti_font.cpp for the reason.
#include <Arduino.h>
#include "ti_host.h"
#include "ti_font.h"

namespace tihost
{

// Copies of the config + display hooks stashed at init() time. Every
// render call reads from these. Both structs are tiny (~60 bytes
// combined) so copying is cheaper than the indirect pointer chase we'd
// need to keep them by reference.
static TiHostConfig s_cfg     = {};
static TiDisplay    s_display = {};

// Max supported cell size is 16x16 (sunton) so the pixel buffer never
// exceeds 512 bytes. Static so drawCell can be called from any stack
// depth without blowing FreeRTOS task stacks (the interpreter's
// PRINT/DISPLAY paths get deep).
static constexpr int kMaxCellW = 16;
static constexpr int kMaxCellH = 16;
static uint16_t s_pixBuf[kMaxCellW * kMaxCellH];

bool hostCommonInit(const TiHostConfig& cfg, const TiDisplay& display)
{
  s_cfg     = cfg;
  s_display = display;
  if (s_cfg.char_w > kMaxCellW || s_cfg.char_h > kMaxCellH) return false;
  if (s_cfg.cols  > TI_MAX_COLS || s_cfg.rows  > TI_MAX_ROWS) return false;
  return true;
}

// Nothing to tick yet — the editor + /api/run draining still live in
// each host's loop(). This function stub matches the API contract so
// the eventual move doesn't require a signature change in hosts.
void hostCommonTick() {}

void drawCell(int col, int row)
{
  uint8_t ch = (uint8_t)screenBuf[row][col];
  int w = s_cfg.char_w;
  int h = s_cfg.char_h;
  int px = col * w + s_cfg.display_x_offset;
  int py = row * h + s_cfg.display_y_offset;

  uint16_t fg = resolveColor(charFgIdx[ch]);
  uint16_t bg = resolveColor(charBgIdx[ch]);

  // Nearest-neighbor scale from the 8x8 ROM pattern to w x h. For 8→8
  // the mapping is identity; for 8→16 each source pixel becomes 2x2;
  // for 8→10 the mapping is 0,0,1,2,3,4,4,5,6,7 (non-uniform but
  // functional — a small font quality hit on non-integer scales in
  // exchange for a single shared drawCell across all hosts).
  for (int dy = 0; dy < h; dy++)
  {
    int sy = (dy * 8) / h;
    uint8_t bits = charPatterns[ch][sy];
    for (int dx = 0; dx < w; dx++)
    {
      int sx = (dx * 8) / w;
      s_pixBuf[dy * w + dx] = (bits & (0x80 >> sx)) ? fg : bg;
    }
  }
  if (s_display.hostPushCell)
  {
    s_display.hostPushCell(px, py, w, h, s_pixBuf);
  }
}

void refreshScreen()
{
  for (int r = 0; r < s_cfg.rows; r++)
  {
    for (int c = 0; c < s_cfg.cols; c++)
    {
      if (screenBuf[r][c] != prevScreenBuf[r][c])
      {
        drawCell(c, r);
        prevScreenBuf[r][c] = screenBuf[r][c];
      }
    }
  }
}

void redrawScreen()
{
  for (int r = 0; r < s_cfg.rows; r++)
  {
    for (int c = 0; c < s_cfg.cols; c++)
    {
      drawCell(c, r);
      prevScreenBuf[r][c] = screenBuf[r][c];
    }
  }
}

} // namespace tihost
