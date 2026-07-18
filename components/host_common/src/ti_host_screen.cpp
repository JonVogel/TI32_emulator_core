// ti_host_screen.cpp — TI-99/4A character grid + cursor state.
//
// First slice of the screen buffer extraction. Only the data lives
// here for now; the drawCell/refreshScreen/scrollUp/tiPrintChar
// functions that operate on these arrays still live in each host's
// main.cpp until the TiDisplay drawCell hook lands. Once host_common
// can call back into the host's drawCell, those functions all become
// shared too.

#include "ti_host.h"

namespace tihost
{

char     screenBuf[TI_MAX_ROWS][TI_MAX_COLS];
char     prevScreenBuf[TI_MAX_ROWS][TI_MAX_COLS];
int      cursorCol = 0;
int      cursorRow = 0;
// Default fg = black, bg = cyan (TI power-on). Hosts overwrite these
// in initDisplay() based on their initial CALL SCREEN color.
uint16_t fgColor = 0x0000;
uint16_t bgColor = 0x0EBF;

} // namespace tihost
