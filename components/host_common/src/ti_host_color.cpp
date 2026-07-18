// ti_host_color.cpp — TI-99/4A color palette + resolveColor. First
// piece extracted from box + sunton main.cpp; identical byte-for-byte
// in both files before this move, so migrating both hosts to use the
// shared copy is a no-op behavior change.

#include "ti_host.h"

namespace tihost
{

// RGB565 palette that matches the arduino-cli reference build's colors.
// Index 0 unused (TI uses 1-based colors). Index 1 is "transparent" —
// resolves to the current CALL SCREEN color at render time.
const uint16_t tiPalette[17] =
{
  0x0000,   // 0 unused
  0x0000,   // 1 transparent (resolves to screen color in resolveColor)
  0x0000,   // 2 black
  0x0585,   // 3 medium green
  0x2D8B,   // 4 light green
  0x0012,   // 5 dark blue
  0x0417,   // 6 light blue
  0x8000,   // 7 dark red
  0x0EBF,   // 8 cyan
  0xE000,   // 9 medium red
  0xF2A3,   // 10 light red
  0xD5C0,   // 11 dark yellow
  0xE600,   // 12 light yellow
  0x0280,   // 13 dark green
  0xB816,   // 14 magenta
  0xC618,   // 15 gray
  0xFFFF,   // 16 white
};

// Per-character color palette indices (1-16; 1=transparent → screen).
uint8_t charFgIdx[256];
uint8_t charBgIdx[256];
uint8_t screenColorIdx = 8;   // cyan default

uint16_t resolveColor(uint8_t idx)
{
  if (idx < 1 || idx > 16) return 0;
  if (idx == 1)
  {
    return tiPalette[screenColorIdx];
  }
  return tiPalette[idx];
}

void gfxResetColors()
{
  for (int i = 0; i < 256; i++)
  {
    charFgIdx[i] = 2;   // black
    charBgIdx[i] = 1;   // transparent (→ screen color)
  }
  screenColorIdx = 8;   // cyan
}

} // namespace tihost
