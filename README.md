# TI Extended BASIC Emulator — ESP

Shared host-portable layers for the TI-99/4A Extended BASIC simulator
on ESP32-family hardware. This repo consolidates what used to be five
separate submodules so that host projects (Box-3, big-display, OTG)
each track **one** dependency instead of five.

## Components

The `components/` directory contains five independent ESP-IDF
components. A host project that needs all of them adds this repo's
`components/` directory to its `EXTRA_COMPONENT_DIRS`, then references
the individual components by name.

| Component        | Purpose                                                          |
|------------------|------------------------------------------------------------------|
| `interpreter/`   | TI Extended BASIC language layer — tokenizer, parser, exec mgr, expression evaluator, variable table, sprites, line editor. Header-only except for `ti_platform.cpp` (weak no-op platform hooks). |
| `font/`          | 8×8 TI ROM font + TI logo glyphs.                                |
| `speech/`        | TMS5220 LPC-10 speech synth (ported from MAME) + `speech_rom.h` baked from a TI Speech Synthesizer cartridge ROM dump (committed; regeneratable via `extract.py`). |
| `ble_hid_host/`  | NimBLE-based multi-peer BLE HID host. Keyboard + gamepad reports, NVS-backed bond persistence. Generic — not TI-specific. |
| `file_handling/` | BASIC file I/O: LittleFS + SD + V9T9 .DSK image routing. Largely generic; `dsk_image.h` is the only TI-specific part. |

## How a host project uses this

```bash
# In your IDF project root:
git submodule add https://github.com/JonVogel/TI_EB_Emulator_ESP.git components/ti-emulator
git submodule update --init
```

Top-level `CMakeLists.txt`:
```cmake
# Tell IDF to scan the submodule's components/ directory for components.
set(EXTRA_COMPONENT_DIRS
    components                              # your own shim components
    components/ti-emulator/components       # the 5 components from this repo
)
```

Your `main/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        arduino                # the arduino-esp32 shim component (your project)
        interpreter            # TI Extended BASIC engine
        font                   # TI 8x8 font
        speech                 # TMS5220 (optional — host can omit if no audio)
        ble_hid_host           # BLE HID host (optional — host can omit if no BLE)
        file_handling          # File I/O (LittleFS / SD / DSK)
)
```

## Dependency on `arduino`

The `interpreter`, `ble_hid_host`, `file_handling`, `speech`, and `font`
components all `REQUIRES arduino`. That refers to an `arduino`
IDF-component name that **the host project must provide**, typically as
a tiny shim re-exposing the IDF Component Manager copy of
`espressif/arduino-esp32`. See any of the TI host projects for the
exact shim — e.g.,
[esp32-s3-box-basic-idf](https://github.com/JonVogel/esp32-s3-box-basic-idf)'s
`components/arduino/CMakeLists.txt`.

## The `interpreter` platform hooks

`interpreter/ti_platform.h` declares all the platform-glue functions
the language layer needs (character drawing, sprite drawing, sound,
speech, input, BLE pairing, WiFi, **cooperative yield**). Each has a
weak no-op default in `ti_platform.cpp`. The host project provides
strong overrides for whichever hooks it implements — the linker prefers
strong over weak, so the host's versions win automatically.

The `tiYield()` hook in particular is **important for FreeRTOS hosts**:
every long-running loop inside the interpreter calls it so that the
host can block briefly (`delay(1)` / `vTaskDelay(1)`) and let the IDLE
task run. Without that, dual-core ESP32 boards will trip the task
watchdog after 5 seconds of CPU-bound BASIC execution.

## History

This repo was created on 2026-05-18 by consolidating five previously-
separate JonVogel repos:

- `TI_Extended_Basic_Interpreter` → `components/interpreter/`
- `TI_Font` → `components/font/`
- `BleHidHost` → `components/ble_hid_host/`
- `ESP32_File_Handling` → `components/file_handling/`
- `TI_Speech` (was a directory in `esp32-s3-box-basic`, not its own repo) → `components/speech/`

The originals remain on GitHub as read-only archives for reference;
new development happens here.

## Active consumers

- [esp32-s3-box-basic-idf](https://github.com/JonVogel/esp32-s3-box-basic-idf) — ESP32-S3-Box-3, 320×240 SPI ST7789
- `ti-basic-rgb` (planned IDF port of `ti-extended-basic-esp32`) — 800×480 RGB panel
- `ti-basic-otg` (planned IDF port) — ESP32-S3-USB-OTG

`ti-scott-adams-esp32` is a frozen Scott Adams Adventure project that
uses earlier-SHA vendored copies of these layers; it doesn't track this
consolidated repo.

## License

Each component carries its own license — see the `LICENSE` and/or
`library.properties` inside its directory. The TI Speech ROM data
baked into `components/speech/speech_rom.h` is derived from copyrighted
Texas Instruments cartridge ROM; included here for educational /
preservation use, see `components/speech/extract.py` to regenerate
from your own ROM dump.
