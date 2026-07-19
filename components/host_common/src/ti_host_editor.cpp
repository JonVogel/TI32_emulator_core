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

// ---------------------------------------------------------------------------
// Paste ring + editor input pump.
// ---------------------------------------------------------------------------

// Decoupled 16 KB paste ring so USB-CDC can drop a whole program's
// worth of bytes faster than the editor consumes them. See the doc
// comment on pasteDrainSerial in ti_host.h.
#define PASTE_BUF_SIZE 16384
static uint8_t s_pasteBuf[PASTE_BUF_SIZE];
static int     s_pasteHead = 0;
static int     s_pasteTail = 0;

void pasteDrainSerial()
{
  while (Serial.available())
  {
    int next = (s_pasteHead + 1) % PASTE_BUF_SIZE;
    if (next == s_pasteTail) break;   // full — back-pressure onto Serial
    s_pasteBuf[s_pasteHead] = (uint8_t)Serial.read();
    s_pasteHead = next;
  }
}

bool pasteAvailable() { return s_pasteHead != s_pasteTail; }

int pasteRead()
{
  if (s_pasteHead == s_pasteTail) return -1;
  uint8_t c = s_pasteBuf[s_pasteTail];
  s_pasteTail = (s_pasteTail + 1) % PASTE_BUF_SIZE;
  return c;
}

// Reads the next editor byte from the paste buffer (Serial side) or
// BLE keyboard (via display.hostReadBleKey), normalizing Serial line
// endings (\r\n → one Enter; lone \n → \r), dropping tabs (since
// \t = RIGHT-arrow in the TI encoding), re-tagging Ctrl+H (0x08) as
// BACKSPACE, and translating ANSI CSI cursor sequences (ESC [ A/B/C/D
// et al) into their TI CHR$ codes. Returns -1 if nothing is available.
int editorReadChar()
{
  static bool skipNextLf = false;

  // Top up from Serial before each read so we don't block on the
  // editor's pace while USB has more bytes waiting.
  pasteDrainSerial();

  while (pasteAvailable())
  {
    uint8_t c = (uint8_t)pasteRead();
    if (c == '\r') { skipNextLf = true; return '\r'; }
    if (c == '\n')
    {
      if (skipNextLf) { skipNextLf = false; continue; }
      return '\r';
    }
    skipNextLf = false;
    if (c == '\t') continue;
    // Most PC terminals send Ctrl+H (0x08) for the BACKSPACE key; the
    // editor uses 0x08 internally for LEFT (BLE keyboards' arrow keys
    // are mapped to TI CHR$ codes). Re-tag serial 0x08 as 0x7F so it
    // hits the BACKSPACE handler. BLE input bypasses this path.
    if (c == 0x08) return 0x7F;
    // ANSI CSI cursor sequence — PC terminals send arrows as
    // ESC [ A/B/C/D (UP/DOWN/RIGHT/LEFT), DEL as ESC [ 3 ~, HOME as
    // ESC [ H. Without this the [, A, etc. get inserted as literal
    // chars. Terminals send the whole sequence back-to-back so the
    // rest is already in the paste buffer when we see ESC; if it
    // isn't, treat as a lone ESC (used by CAT/DIR cancel).
    if (c == 0x1B)
    {
      if (pasteAvailable() && s_pasteBuf[s_pasteTail] == '[')
      {
        pasteRead();
        if (!pasteAvailable()) return 0x1B;
        uint8_t code = (uint8_t)pasteRead();
        switch (code)
        {
          case 'A': return 0x0B;   // UP
          case 'B': return 0x0A;   // DOWN
          case 'C': return 0x09;   // RIGHT
          case 'D': return 0x08;   // LEFT
          case 'H': return 0x05;   // HOME = BEGIN (FCTN+5)
          case '3':
            if (pasteAvailable() && s_pasteBuf[s_pasteTail] == '~')
            {
              pasteRead();
              return 0x07;
            }
            continue;
          default: continue;
        }
      }
      return 0x1B;
    }
    return c;
  }

  const auto& d = getHostCommonDisplay();
  if (d.hostReadBleKey) return d.hostReadBleKey();
  return -1;
}

// Test whether the current buffer contains nothing but decimal digits.
bool editBufferIsAllDigits(const LineEdit& s)
{
  if (s.len == 0) return false;
  for (int i = 0; i < s.len; i++)
  {
    if (!isdigit((unsigned char)s.buf[i])) return false;
  }
  return true;
}

// True if the buffer is exactly "<digits> " (at least one trailing
// space) with nothing else — the NUMBER-mode auto-fill form. Lets the
// editor detect "Enter without adding anything" so it can exit NUMBER
// mode cleanly instead of deleting the line.
bool editorBufferIsAutoFillOnly(const LineEdit& s)
{
  if (s.len == 0) return false;
  int p = 0;
  if (!isdigit((unsigned char)s.buf[p])) return false;
  while (p < s.len && isdigit((unsigned char)s.buf[p])) p++;
  if (p >= s.len || s.buf[p] != ' ') return false;
  while (p < s.len && s.buf[p] == ' ') p++;
  return p >= s.len;
}

// ---------------------------------------------------------------------------
// Interpreter integration.
// ---------------------------------------------------------------------------
static TiInterpreterHooks s_interp = {};

void setInterpreterHooks(const TiInterpreterHooks& hooks) { s_interp = hooks; }

int findProgramLineIndex(int lineNum)
{
  if (!s_interp.findProgramLineIndex) return -1;
  return s_interp.findProgramLineIndex(lineNum);
}
bool commitEditedLine(const LineEdit& s)
{
  if (!s_interp.commitEditedLine) return true;
  return s_interp.commitEditedLine(s);
}
void loadProgramLineToEdit(LineEdit& s, int idx)
{
  if (!s_interp.loadProgramLineToEdit) return;
  s_interp.loadProgramLineToEdit(s, idx);
}
int programSize()
{
  if (!s_interp.programSize) return 0;
  return s_interp.programSize();
}

// ---------------------------------------------------------------------------
// Editor session state + processEditChar.
// ---------------------------------------------------------------------------
char lastCommandLine[MAX_INPUT_LEN + 1] = {0};
bool numModeActive = false;
int  numModeStart  = 0;
int  numModeIncr   = 0;
int  numModeNext   = 0;

EditResult processEditChar(uint8_t c, LineEdit& s)
{
  // ----- handled identically in both edit modes -----

  // Enter: commit line. Only match '\r' — '\n' (10) is DOWN on TI.
  // Serial's '\n' is normalized to '\r' at the read site.
  if (c == '\r')
  {
    // NUMBER mode: if the user pressed Enter without adding anything
    // past the auto-fill, exit NUMBER mode and throw away the buffer
    // so we don't accidentally delete the line with that number.
    if (numModeActive && s.historyEnabled && editorBufferIsAutoFillOnly(s))
    {
      numModeActive = false;
      s.len = 0;
      s.pos = 0;
      s.buf[0] = '\0';
    }
    s.buf[s.len] = '\0';
    if (s.historyEnabled)
    {
      strncpy(lastCommandLine, s.buf, sizeof(lastCommandLine) - 1);
      lastCommandLine[sizeof(lastCommandLine) - 1] = '\0';
    }
    // Position the cursor at end of the visible line BEFORE the '\n'
    // scroll — with soft-wrap the last cell may be on a row below
    // s.startRow.
    {
      int erow, ecol;
      editPosToRC(s, s.len, erow, ecol);
      const auto& cfg = getHostCommonConfig();
      if (erow < 0)          erow = 0;
      if (erow >= cfg.rows)  erow = cfg.rows - 1;
      cursorRow = erow;
      cursorCol = ecol;
    }
    ::tiPrintChar('\n');
    editMode = EM_ENTRY;
    lastRecalledLineNum = -1;
    return EDIT_SUBMITTED;
  }

  // CLEAR — break
  if (c == 12) return EDIT_BROKEN;

  // ERASE (FCTN+3) — wipe line + drop to ENTRY
  if (c == 2)  { editEraseLine(s); return EDIT_CONTINUE; }

  // INS (FCTN+2) — toggle insert mode (global flag, both edit modes)
  if (c == 4)  { editInsertMode = !editInsertMode; return EDIT_CONTINUE; }

  // REDO (FCTN+8) — reload last-entered line, flip to EDIT
  if (c == 14)
  {
    if (s.historyEnabled && lastCommandLine[0] != '\0')
    {
      editReplaceLine(s, lastCommandLine);
      editMode = EM_EDIT;
    }
    return EDIT_CONTINUE;
  }

  // BKSP (127) — delete previous char.
  if (c == 127) { editBackspace(s); return EDIT_CONTINUE; }

  // Printable — typing always feeds the buffer.
  if (c >= 32 && c < 127) { editTypeChar(s, c); return EDIT_CONTINUE; }

  // ----- Cursor movement & DEL work in every edit context -----

  if (c == 8)  { if (s.pos > 0) { s.pos--; editSyncCursor(s); } return EDIT_CONTINUE; }  // LEFT
  if (c == 9)  { if (s.pos < s.len) { s.pos++; editSyncCursor(s); } return EDIT_CONTINUE; }  // RIGHT
  if (c == 7)  { editDeleteAtCursor(s); return EDIT_CONTINUE; }  // DEL

  // ----- UP/DOWN are mode-aware -----
  //
  //   INPUT (historyEnabled=false): no-op
  //   ENTRY (editor prompt, not yet recalled): if the buffer is all
  //     digits, jump to EDIT mode on that program line
  //   EDIT  (a line is currently under edit): commit the current
  //     buffer, then move to the previous/next program line; past
  //     the boundary exits EDIT mode.
  if (c == 11)   // UP (FCTN+E)
  {
    if (!s.historyEnabled) return EDIT_CONTINUE;
    if (editMode == EM_ENTRY)
    {
      if (editBufferIsAllDigits(s))
      {
        int idx = findProgramLineIndex(atoi(s.buf));
        if (idx >= 0) loadProgramLineToEdit(s, idx);
      }
      return EDIT_CONTINUE;
    }
    // EM_EDIT — commit + navigate to previous line
    int oldLine = lastRecalledLineNum;
    if (!commitEditedLine(s))
    {
      printError("* SYNTAX ERROR");
      editEraseLine(s);
      return EDIT_CONTINUE;
    }
    int idx = findProgramLineIndex(oldLine);
    if (idx < 0) { editEraseLine(s); return EDIT_CONTINUE; }
    if (idx > 0)
    {
      tiPrintChar('\n');
      s.startCol = cursorCol;
      s.startRow = cursorRow;
      s.len = 0; s.pos = 0; s.buf[0] = '\0';
      loadProgramLineToEdit(s, idx - 1);
    }
    else
    {
      editEraseLine(s);
    }
    return EDIT_CONTINUE;
  }

  if (c == 10)   // DOWN (FCTN+X)
  {
    if (!s.historyEnabled) return EDIT_CONTINUE;
    if (editMode == EM_ENTRY)
    {
      if (editBufferIsAllDigits(s))
      {
        int idx = findProgramLineIndex(atoi(s.buf));
        if (idx >= 0) loadProgramLineToEdit(s, idx);
      }
      return EDIT_CONTINUE;
    }
    int oldLine = lastRecalledLineNum;
    if (!commitEditedLine(s))
    {
      printError("* SYNTAX ERROR");
      editEraseLine(s);
      return EDIT_CONTINUE;
    }
    int idx = findProgramLineIndex(oldLine);
    if (idx < 0) { editEraseLine(s); return EDIT_CONTINUE; }
    if (idx < programSize() - 1)
    {
      tiPrintChar('\n');
      s.startCol = cursorCol;
      s.startRow = cursorRow;
      s.len = 0; s.pos = 0; s.buf[0] = '\0';
      loadProgramLineToEdit(s, idx + 1);
    }
    else
    {
      editEraseLine(s);
    }
    return EDIT_CONTINUE;
  }

  return EDIT_CONTINUE;
}

} // namespace tihost
