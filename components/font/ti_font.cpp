// ti_font.cpp — definitions for the two font-related globals that
// need external linkage: the active font mode (PC vs V9T9 TI) and
// the mutable per-char pattern table CALL CHAR writes into.
//
// The ROM font arrays themselves (tiFont, tiFontV9t9_32to127) stay
// `static const` in ti_font.h — they're read-only, and duplicating
// ~4KB per TU that includes the header is fine on 16MB flash. Only
// the mutable state needs to be shared.

// Arduino.h provides PROGMEM + memcpy_P, referenced by the ROM font
// arrays and initCharPatterns() in ti_font.h. Header itself doesn't
// include it because the original library was consumed straight into
// an .ino file where Arduino.h was implicit — pulling it in here so
// this stand-alone .cpp TU compiles.
#include <Arduino.h>
#include "ti_font.h"

TiFontMode g_tiFontMode = TI_FONT_PC;
uint8_t    charPatterns[TI_CHAR_COUNT][TI_CHAR_BYTES];
