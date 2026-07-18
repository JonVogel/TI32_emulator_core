// ti_host_editor.cpp — line editor primitives (buffer/cursor
// manipulation + soft-wrap redraw). Piece 1 of the editor extraction;
// processEditChar, getInputLine, and checkInput still live in each
// host's main.cpp because they touch the interpreter's `em` object
// (findProgramLineIndex, commitEditedLine, loadProgramLineToEdit) and
// each host's own BLE keyboard driver. Those move in later commits.
//
// Everything here is byte-identical to what box + sunton main.cpp
// carried before this commit (with the same recent backspace + serial-
// mirror + soft-wrap fixes). Only the panel-touching bits go through
// the TiDisplay hooks so all three hosts share one implementation.

#include <Arduino.h>
#include "ti_host.h"
#include "ti_font.h"

// Config + display accessors defined in ti_host_render.cpp.
namespace tihost {
TiHostConfig& getHostCommonConfig();
TiDisplay&    getHostCommonDisplay();
} // namespace tihost

namespace tihost
{

bool     editInsertMode      = false;
EditMode editMode            = EM_ENTRY;
int      lastRecalledLineNum = -1;

static TiHostConfig& C() { return getHostCommonConfig(); }
static TiDisplay&    D() { return getHostCommonDisplay(); }

// Draw a solid block cursor at the current position (inverted colors).
// When invisible, we redraw the cell underneath from screenBuf so the
// cursor block visually goes away.
void drawCursor(bool visible)
{
  const auto& c = C();
  const auto& d = D();
  int px = cursorCol * c.char_w + c.display_x_offset;
  int py = cursorRow * c.char_h + c.display_y_offset;
  if (visible)
  {
    if (d.hostFillRect)
    {
      d.hostFillRect(px, py, c.char_w, c.char_h, tiPalette[2]);
    }
  }
  else
  {
    drawCell(cursorCol, cursorRow);
  }
}

// Map a position in s.buf to a (row, col) on screen. Lines soft-wrap
// across rows when (startCol + pos) >= COLS — the next character
// lands at column 0 of the row below. Positions that fall above the
// top of the screen (when the line has scrolled and startRow went
// negative) return a negative row; callers must check before writing.
void editPosToRC(const LineEdit& s, int pos, int& outRow, int& outCol)
{
  const auto& c = C();
  int flat = s.startCol + pos;
  outRow = s.startRow + flat / c.cols;
  outCol = flat % c.cols;
}

void editSyncCursor(const LineEdit& s)
{
  const auto& c = C();
  int row, col;
  editPosToRC(s, s.pos, row, col);
  // If wrapping put the cursor off the visible area (line scrolled
  // past the top), clamp to the top row so the caret stays visible.
  // Typing-side scroll-up keeps this from happening in normal use.
  if (row < 0)         row = 0;
  if (row >= c.rows)   row = c.rows - 1;
  cursorRow = row;
  cursorCol = col;
}

// Redraw buffer content starting at fromPos, plus `eraseExtra`
// trailing cells (used after a shrink). Handles soft-wrap across
// multiple rows.
void redrawLineTail(const LineEdit& s, int fromPos, int eraseExtra)
{
  const auto& c = C();
  for (int i = fromPos; i < s.len; i++)
  {
    int row, col;
    editPosToRC(s, i, row, col);
    if (row < 0 || row >= c.rows) continue;
    screenBuf[row][col] = s.buf[i];
    drawCell(col, row);
  }
  for (int i = 0; i < eraseExtra; i++)
  {
    int row, col;
    editPosToRC(s, s.len + i, row, col);
    if (row < 0 || row >= c.rows) continue;
    screenBuf[row][col] = ' ';
    drawCell(col, row);
  }
}

// Remove the char at s.pos (if any) and redraw the tail.
void editDeleteAtCursor(LineEdit& s)
{
  if (s.pos >= s.len) return;
  for (int i = s.pos; i < s.len - 1; i++) s.buf[i] = s.buf[i + 1];
  s.len--;
  s.buf[s.len] = '\0';
  redrawLineTail(s, s.pos, 1);
  editSyncCursor(s);
}

// PC-style backspace — delete the char to the left of the cursor and
// move onto its position. Also mirrors `\b \b` to serial so a
// terminal-mode session visibly erases the char (editTypeChar echoes
// typed chars to Serial for paste visibility; without this mirror,
// backspace looked like a no-op on the USB console).
void editBackspace(LineEdit& s)
{
  if (s.pos == 0) return;
  s.pos--;
  editDeleteAtCursor(s);
  Serial.write("\b \b", 3);
  Serial.flush();
}

// Insert or overwrite `ch` at the cursor and advance. When pos
// reaches the right edge of the screen, the line soft-wraps to the
// next row. If that row would be past the bottom of the screen,
// scroll the whole display up by one — the line's earlier rows shift
// up with it and startRow decrements to track them.
void editTypeChar(LineEdit& s, uint8_t ch)
{
  if (s.len >= s.maxLen) return;
  const auto& c = C();

  if (editInsertMode && s.pos < s.len)
  {
    for (int i = s.len; i > s.pos; i--) s.buf[i] = s.buf[i - 1];
    s.buf[s.pos] = ch;
    s.len++;
    s.buf[s.len] = '\0';
    redrawLineTail(s, s.pos, 0);
    s.pos++;
    editSyncCursor(s);
  }
  else
  {
    s.buf[s.pos] = ch;
    if (s.pos == s.len)
    {
      s.len++;
      s.buf[s.len] = '\0';
    }
    int row, col;
    editPosToRC(s, s.pos, row, col);
    while (row >= c.rows)
    {
      scrollUp();
      s.startRow--;
      row--;
    }
    if (row >= 0)
    {
      screenBuf[row][col] = ch;
      drawCell(col, row);
    }
    Serial.write(ch);   // mirror to serial for paste visibility
    s.pos++;
    // Proactive scroll: if the cursor's NEW position would land on a
    // row past the bottom of the screen, scroll now. Without this,
    // the cursor jumps onto the bottom row at col 0 (where the ">"
    // prompt sits) and the wrap looks one char late.
    {
      int crow, ccol;
      editPosToRC(s, s.pos, crow, ccol);
      while (crow >= c.rows)
      {
        scrollUp();
        s.startRow--;
        crow--;
      }
    }
    editSyncCursor(s);
  }
}

// Wipe the current line and return to ENTRY mode.
void editEraseLine(LineEdit& s)
{
  int oldLen = s.len;
  s.len = 0;
  s.pos = 0;
  s.buf[0] = '\0';
  redrawLineTail(s, 0, oldLen);
  editSyncCursor(s);
  editMode = EM_ENTRY;
  lastRecalledLineNum = -1;
}

// Replace the line contents with `src`. Used by REDO and line-number
// recall (typing a line number then UP/DOWN).
void editReplaceLine(LineEdit& s, const char* src)
{
  int oldLen = s.len;
  int n = 0;
  while (src[n] && n < s.maxLen)
  {
    s.buf[n] = src[n];
    n++;
  }
  s.buf[n] = '\0';
  s.len = n;
  s.pos = 0;
  redrawLineTail(s, 0, (oldLen > s.len) ? (oldLen - s.len) : 0);
  editSyncCursor(s);
}

} // namespace tihost
