// ti_host_text.cpp — TI-style text output pipeline: scrollUp,
// tiClearScreen, tiPrintChar / tiPrintString / printLine / printError.
//
// Sits on top of the drawCell/refreshScreen primitives in
// ti_host_render.cpp; dispatches per-host actions (sprite redraw
// after scroll, TI-style border repaint, error honk) through the
// TiDisplay optional hooks. Behavior mirrors what both box and
// sunton main.cpp did before the extraction, minus the small
// per-host cleanups noted inline.

#include <Arduino.h>
#include "ti_host.h"

namespace tihost
{

// Reference accessors defined in ti_host_render.cpp. Not in ti_host.h
// on purpose — hosts don't need direct access; they operate on the
// copies they registered via hostCommonInit.
TiHostConfig& getHostCommonConfig();
TiDisplay&    getHostCommonDisplay();
static TiHostConfig& cfg()     { return getHostCommonConfig(); }
static TiDisplay&    display() { return getHostCommonDisplay(); }

void scrollUp()
{
  const auto& c = cfg();
  const auto& d = display();
  memcpy(&screenBuf[0][0], &screenBuf[1][0], c.cols * (c.rows - 1));
  memset(&screenBuf[c.rows - 1][0], 0x20, c.cols);
  refreshScreen();
  // Repaint the pixel strip along the newly-blank bottom row so we
  // don't leave stale pixels from a prior scroll. Uses the shared
  // hostFillRect (both hosts have this wired) with bgColor.
  if (d.hostFillRect)
  {
    int y = (c.rows - 1) * c.char_h + c.display_y_offset;
    d.hostFillRect(c.display_x_offset, y, c.cols * c.char_w, c.char_h,
                   bgColor);
  }
  // Sunton has a TI-style border ring outside the grid that gets nicked
  // by scroll writes. Repaint it if the host provides the hook.
  if (d.hostPaintBorder) d.hostPaintBorder();
  // Software sprites live above the char grid on both current hosts;
  // give the host a chance to re-blit them so scrolling doesn't erase
  // any sprite that overlapped the bottom row.
  if (d.hostPostScroll) d.hostPostScroll();
}

} // namespace tihost — reopen for the global-scope tiClearScreen.

void tiClearScreen()
{
  const auto& c = tihost::getHostCommonConfig();
  const auto& d = tihost::getHostCommonDisplay();
  memset(tihost::screenBuf, ' ', c.rows * c.cols);
  // Force full redraw on the next refresh by invalidating prevScreenBuf.
  memset(tihost::prevScreenBuf, 0, c.rows * c.cols);
  if (d.hostFillBackground) d.hostFillBackground(tihost::bgColor);
  else if (d.hostFillScreen) d.hostFillScreen(tihost::bgColor);
  // TI behavior: cursor lands on the bottom row after CLEAR.
  tihost::cursorCol = 0;
  tihost::cursorRow = c.rows - 1;
}

namespace tihost {

// tiPrintChar / tiPrintString / tiClearScreen are strong overrides of
// weak symbols declared at global scope in the interpreter's
// ti_platform.h — the interpreter links against those names directly,
// so they can't live inside a namespace. printLine / printError are
// host_common-internal helpers and stay namespaced.
void printLine(const char* s);   // fwd decl for printError

} // namespace tihost — reopen after the globals below.

void tiPrintChar(char cc)
{
  const auto& c = tihost::getHostCommonConfig();
  // Mirror to serial for copy/paste. \n gets a matching \r for terminals
  // that don't auto-translate.
  Serial.write(cc);
  if (cc == '\n') Serial.write('\r');

  if (cc == '\n')
  {
    tihost::scrollUp();
    tihost::cursorRow = c.rows - 1;
    tihost::cursorCol = 0;
    return;
  }

  // Column wrap — scroll up and start fresh at col 0
  if (tihost::cursorCol >= c.cols)
  {
    tihost::scrollUp();
    tihost::cursorRow = c.rows - 1;
    tihost::cursorCol = 0;
  }

  tihost::screenBuf[tihost::cursorRow][tihost::cursorCol] = cc;
  tihost::drawCell(tihost::cursorCol, tihost::cursorRow);
  tihost::prevScreenBuf[tihost::cursorRow][tihost::cursorCol] = cc;
  tihost::cursorCol++;
}

void tiPrintString(const char* s)
{
  while (*s) tiPrintChar(*s++);
  // Sunton preserves an auto-flush at end of tiPrintString + printLine
  // so text streams appear immediately on its double-buffered RGB
  // panel. Box's hostFlush is a no-op so this is free there.
  const auto& d = tihost::getHostCommonDisplay();
  if (d.hostFlush) d.hostFlush();
}

namespace tihost {

void printLine(const char* s)
{
  // tiPrintString + tiPrintChar live at global scope (strong overrides
  // of ti_platform.h weak symbols) — the `::` disambiguates them from
  // the tihost namespace they don't live in.
  ::tiPrintString(s);
  ::tiPrintChar('\n');
}

// TI-style error print: blank line, error message, blank line, BEL to
// serial + host-provided honk (if wired).
void printError(const char* s)
{
  printLine("");
  printLine(s);
  printLine("");
  Serial.write(0x07);
  const auto& d = display();
  if (d.hostHonk) d.hostHonk();
}

} // namespace tihost
