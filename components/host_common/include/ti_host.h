// ti_host.h — shared host layer between the TI Extended BASIC engine
// and a per-board main.cpp (LovyanGFX SPI panel on TI32-box, Arduino_GFX
// RGB parallel on TI32-sunton, LovyanGFX ST7701 RGB on TI32-guition).
//
// Before this component existed, box and sunton each carried a ~4000-line
// main.cpp with byte-identical (or near-identical) copies of the line
// editor, tiPrintChar family, boot screen, sprite core, command
// dispatch, and mount/DSK plumbing. Adding a third host (guition) made
// that duplication load-bearing — hence the split.
//
// A host provides:
//   * A TiHostConfig — screen geometry, panel offsets, feature bits.
//   * A TiDisplay implementation — six low-level rasterization hooks.
//   * A setup()/loop() that calls hostCommonInit() then hostCommonTick().
//
// Everything else — screenBuf, cursor state, palette, resolveColor,
// tiPrintChar/String/ClearScreen/HCHAR/VCHAR, line editor with soft-
// wrap, boot screen, sprite draw/erase/move, and the whole
// processInput command dispatch — lives inside host_common and calls
// back through the TiDisplay hooks.

#pragma once

#include <stdint.h>
#include <stddef.h>

// LineEdit / EditResult / EditMode — needed by the editor primitives
// this component now owns. Pulls in tiny types with no runtime deps.
#include "line_editor.h"

namespace tihost
{

// ---------------------------------------------------------------------------
// Screen geometry + host feature flags. Filled in once at startup by the
// host and passed to hostCommonInit(). Kept `constexpr`-friendly so the
// display hot loops (drawCell, sprite blit) can pick up dimensions
// without an indirect load.
//
// COLS/ROWS are the TI-99/4A character grid — always 32x24 for BASIC
// compatibility; the field exists so display code can compute pixel
// coordinates without host-side #defines. CHAR_W/CHAR_H are per-host
// (native 8x8 on box, 2x-scaled 16x16 on sunton, native 8x8 centered
// on guition). DISPLAY_X_OFFSET/DISPLAY_Y_OFFSET position the TI grid
// within a possibly-larger panel.
//
// Feature flags let host_common skip work whose backing peripheral
// doesn't exist on this board (e.g., no audio codec, no BLE keyboard,
// no on-screen pairing UI overlay).
// ---------------------------------------------------------------------------
struct TiHostConfig
{
  // TI character grid — always 32x24 for now. Field exists for future
  // widescreen mods (e.g. Extended BASIC 40-column mode).
  int cols;
  int rows;
  // Per-cell pixel size on the physical panel. drawCell uses these
  // to size the pixBuf it hands to hostPushCell.
  int char_w;
  int char_h;
  // Physical panel dimensions in pixels — used for scroll rects,
  // background fill outside the grid area, and pairing-UI takeover.
  int screen_w;
  int screen_h;
  // Top-left corner of the TI grid within the panel. Centers a 256x192
  // (native) or 512x384 (2x) grid inside a larger display; zero for
  // hosts whose grid exactly fills the panel.
  int display_x_offset;
  int display_y_offset;

  // Optional features — host sets true if it wires the underlying
  // hardware and hook. host_common no-ops the code path when false.
  bool has_audio       = false;   // CALL SOUND / CALL SAY produce audio
  bool has_ble_kb      = false;   // BLE HID keyboard input
  bool has_wifi        = false;   // web_files + CALL WIFI hooks
  bool has_pairing_ui  = false;   // full-screen pairing takeover UI
};

// ---------------------------------------------------------------------------
// TiDisplay — the six-function display abstraction. Each host provides
// a concrete implementation (typically a struct with static methods that
// wrap tft.pushImage / tft->draw16bitRGBBitmap / etc.). host_common only
// calls the panel through these hooks; no other panel reference exists
// in the shared code.
//
// hostBegin(): one-shot panel init at boot. Turn on backlight, set
//   rotation, clear to bg color. Returns false if the panel can't come
//   up (host prints an error and halts).
// hostPushCell(): blit a WxH RGB565 pixel block at (px, py) in panel
//   coordinates. Used by drawCell for characters + sprite renderer.
// hostFillRect(): solid-color rectangle in panel coords. Used by scroll,
//   pairing UI, sprite erase-restore.
// hostPutPixel(): single RGB565 pixel. Sprite fallback path only —
//   host_common batches into hostPushCell wherever it can.
// hostFillScreen(): whole-panel solid fill. Boot screen + tiClearScreen.
// hostFlush(): commit any batched draws to the panel. No-op on hosts
//   with direct-to-panel drivers (LovyanGFX SPI); real work on hosts
//   with a double-buffered RGB parallel display (Arduino_GFX RGBPanelDB).
// ---------------------------------------------------------------------------
// Optional hooks below hostFlush can be left null; host_common no-ops
// them. hostPaintBorder is the sunton-only TI-style screen-color frame
// around the char grid. hostHonk is the box-only speaker beep on
// printError (sunton has no audio codec). hostPostScroll runs after
// scrollUp shifts the grid — hosts with software sprites use it to
// redraw the sprite layer on top of the freshly-scrolled cells.
// hostFillBackground handles the different "clear whole background"
// semantics between hosts (box paints the entire panel to bg; sunton
// paints outside-grid to black then draws a TI-cyan border ring).
struct TiDisplay
{
  bool (*hostBegin)(uint16_t bg_color);
  void (*hostPushCell)(int px, int py, int w, int h, const uint16_t* pixels);
  void (*hostFillRect)(int px, int py, int w, int h, uint16_t color);
  void (*hostPutPixel)(int px, int py, uint16_t color);
  void (*hostFillScreen)(uint16_t color);
  void (*hostFlush)();
  void (*hostPaintBorder)();     // optional — repaint TI-style border ring
  void (*hostHonk)();            // optional — audible error beep
  void (*hostPostScroll)();      // optional — redraw sprite overlay
  void (*hostFillBackground)(uint16_t bg);  // optional — full background paint
};

// ---------------------------------------------------------------------------
// TI palette + per-character color state. Extracted from box + sunton
// main.cpp (byte-identical between the two). resolveColor() maps a
// palette index (1..16) to RGB565; index 1 is "transparent" and returns
// the current screen color instead.
//
// charFgIdx[]/charBgIdx[] hold per-char foreground/background palette
// indices — CALL COLOR writes here, drawCell reads. screenColorIdx is
// the CALL SCREEN color that "transparent" resolves to.
// ---------------------------------------------------------------------------
extern const uint16_t tiPalette[17];
extern uint8_t        charFgIdx[256];
extern uint8_t        charBgIdx[256];
extern uint8_t        screenColorIdx;

uint16_t resolveColor(uint8_t idx);

// Reset every character to black-on-transparent and screen color to
// cyan (8) — the TI power-on defaults. Called by CALL CLEAR / gfxReset
// and once at boot before the splash paints.
void gfxResetColors();

// ---------------------------------------------------------------------------
// Splash-screen assets. The 3x3 "TEXAS INSTRUMENTS" logo character
// grid (one 8x8 pattern per cell, row-major) and the 8x8 copyright ©
// glyph both come out of the TI-99/4A ROM boot screen. Each host
// still owns drawTexasLogo() itself (it calls the per-host drawCell)
// but the bitmap data lives here so all three hosts share one copy.
//
// Layout of tiLogoChars: 129=(1,1) 130=(1,2) 131=(1,3)
//                        132=(2,1) 133=(2,2) 134=(2,3)
//                        135=(3,1) 136=(3,2) 137=(3,3)
// ---------------------------------------------------------------------------
extern const uint8_t tiLogoChars[9][8];
extern const uint8_t copyrightBitmap[8];

// ---------------------------------------------------------------------------
// TI-99/4A character grid + cursor state.
//
// Every host draws to a fixed 32x24 character grid, so the storage
// arrays are compile-time sized (constexpr constants below). Runtime
// cfg.cols/cfg.rows are used by draw loops but the buffers themselves
// are sized to the TI maximum — future 40-column XB modes would bump
// TI_MAX_COLS and everything else follows.
//
// screenBuf holds the current character at each grid cell.
// prevScreenBuf holds what was last painted to the panel — refreshScreen
// diffs the two and redraws only the changed cells.
//
// cursorRow/cursorCol are in TI grid coordinates (0..ROWS-1, 0..COLS-1),
// NOT pixel coordinates. Boot screens + line editor mutate these; the
// per-host drawCursor turns them into panel-space pixels.
//
// fgColor/bgColor are the current text foreground/background as RGB565
// values, kept as a cache so the host's own tft.setTextColor and
// fillRect calls (for scrolling, border, etc) don't need to look
// through resolveColor + charFgIdx + palette on the hot path.
// ---------------------------------------------------------------------------
constexpr int TI_MAX_COLS = 32;
constexpr int TI_MAX_ROWS = 24;

extern char     screenBuf[TI_MAX_ROWS][TI_MAX_COLS];
extern char     prevScreenBuf[TI_MAX_ROWS][TI_MAX_COLS];
extern int      cursorCol;
extern int      cursorRow;
extern uint16_t fgColor;
extern uint16_t bgColor;

// ---------------------------------------------------------------------------
// Shared render surface. All of these read screenBuf + charPatterns +
// palette state and dispatch to the panel via the TiDisplay hooks
// registered by hostCommonInit(). Host code calls these instead of
// touching the panel directly.
//
// drawCell(): render one char at (col, row) via nearest-neighbor scale
//   from the 8x8 ROM font to cfg.char_w x cfg.char_h, then hostPushCell
//   at (cfg.display_x_offset + col*cfg.char_w, cfg.display_y_offset +
//   row*cfg.char_h). Handles 1x (box), 2x (sunton), non-integer (10x10
//   guition planned) uniformly.
// refreshScreen(): diff screenBuf vs prevScreenBuf, drawCell each
//   changed cell, update prevScreenBuf.
// redrawScreen(): unconditional redraw of the whole 32x24 grid.
// scrollUp(): shift the char grid up one row, fill bottom with spaces,
//   refresh, then hostPaintBorder() if set + hostFillRect the bottom
//   pixel strip below the grid to bg.
// tiClearScreen(): memset screenBuf to spaces, hostFillScreen(bgColor),
//   home cursor at bottom-left row.
// tiPrintChar/String/printLine: TI-style text output — writes to
//   screenBuf, drawCells, mirrors to Serial, wraps + scrolls at EOL.
// printError(): blank line, msg, blank line, hostHonk() if wired.
// ---------------------------------------------------------------------------
void drawCell(int col, int row);
void refreshScreen();
void redrawScreen();
void scrollUp();
void tiClearScreen();
void tiPrintChar(char c);
void tiPrintString(const char* s);
void printLine(const char* s);
void printError(const char* s);

// ---------------------------------------------------------------------------
// Line editor primitives.
//
// LineEdit / EditResult / EditMode are declared in the interpreter's
// line_editor.h — we don't re-declare here, we just include it below
// (the interpreter is a REQUIRES dep of host_common). Callers include
// ti_host.h and get both.
//
// Split ownership:
//   - This file owns the pure buffer/cursor primitives (drawCursor,
//     editPosToRC, editSyncCursor, redrawLineTail, editTypeChar,
//     editBackspace, editDeleteAtCursor, editEraseLine, editReplaceLine)
//     and their small state (editInsertMode, editMode,
//     lastRecalledLineNum).
//   - processEditChar / getInputLine / checkInput are still per-host —
//     they touch the interpreter's `em` object and each host's own
//     BLE keyboard driver. Those get hooks + moves in later commits.
// ---------------------------------------------------------------------------
extern bool editInsertMode;
extern EditMode editMode;
extern int  lastRecalledLineNum;

void drawCursor(bool visible);
void editPosToRC(const LineEdit& s, int pos, int& outRow, int& outCol);
void editSyncCursor(const LineEdit& s);
void redrawLineTail(const LineEdit& s, int fromPos, int eraseExtra);
void editDeleteAtCursor(LineEdit& s);
void editBackspace(LineEdit& s);
void editTypeChar(LineEdit& s, uint8_t c);
void editEraseLine(LineEdit& s);
void editReplaceLine(LineEdit& s, const char* src);

// ---------------------------------------------------------------------------
// Called once from the host's setup() after Serial + display are up.
// Wires the host's config + display hooks into host_common, then
// initializes screenBuf, palette, font tables, and interpreter callbacks.
// Returns false on any init failure (bad config, hostBegin returned
// false, out of memory); host should halt in that case.
// ---------------------------------------------------------------------------
bool hostCommonInit(const TiHostConfig& cfg, const TiDisplay& display);

// ---------------------------------------------------------------------------
// Called every iteration of the host's loop(). Drives the editor prompt,
// blinks the cursor, drains serial paste, ticks any pending /api/run
// dispatch. Host stays responsible for its own peripherals (audio,
// BLE, WiFi, web files) — call those before/after hostCommonTick().
// ---------------------------------------------------------------------------
void hostCommonTick();

} // namespace tihost
