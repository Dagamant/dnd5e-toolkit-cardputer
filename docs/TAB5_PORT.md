# Porting the D&D 5E Toolkit to the M5Stack Tab5

Plan for running this firmware on the **Tab5** (ESP32-P4, 5" 1280x720 touchscreen) with the **Tab5 Keyboard**, while keeping the Cardputer ADV build working from the same code.

## Goals

1. Every current tool works on the Tab5, using the keyboard, touch, or both.
2. The big screen is actually used: side-by-side panes, full stat blocks, a whole initiative order at a glance. The aim is more than the 240x135 UI made bigger.
3. One codebase and one SD card layout for both devices. A card written by one device works in the other.
4. New Tab5-only features where its hardware allows them: Wi-Fi, real-time clock, better audio, lots of RAM.

## The two devices

| | Cardputer ADV | Tab5 |
|---|---|---|
| SoC | ESP32-S3, 2x 240 MHz | ESP32-P4, 2x 360 MHz RISC-V |
| RAM | 512 KB SRAM (no PSRAM used) | 32 MB PSRAM |
| Flash | 8 MB | 16 MB |
| Screen | 1.14" 240x135, SPI | 5" 1280x720 IPS, MIPI-DSI, capacitive touch |
| Input | built-in keyboard (TCA8418 matrix) | Tab5 Keyboard: 70 keys over I2C (STM32, address 0x6D, SDA G0 / SCL G1 / INT G50), real arrows, Esc, Tab, Ctrl, Alt, Sym, Aa; plus touch |
| microSD | SPI (CS 12) | SPI (CS 42, SCK 43, MOSI 44, MISO 39) or 4-bit SDIO |
| Audio | small speaker | ES8388 codec, 1 W speaker, headphone jack, dual microphones |
| Motion | BMI270 IMU | BMI270 IMU |
| Clock | none (sessions numbered) | RX8130CE real-time clock with backup supercapacitor |
| Wireless | Wi-Fi/BLE (unused) | ESP32-C6 co-processor: Wi-Fi 6 |
| Power | internal cell | removable NP-F550 (2000 mAh, about 6 h), INA226 current/voltage monitor |
| Toolchain | Arduino core 3.3.12 (pioarduino 55.03.312) since Phase 0; 2.0.14 kept as `cardputer-adv-legacy` | Arduino core 3.x (pioarduino; M5Stack's guide pins 54.03.21, board `esp32-p4-evboard`), M5Unified >= 0.2.23, M5GFX >= 0.2.30 |

**Screen ratio:** 1280x720 is exactly 16/3 times 240x135, the same 16:9 shape. Existing screens can be shown scaled up as soon as the hardware works (Phase 1).

## What carries over

**Ports as-is or nearly so.** Pure logic, plain C++ and Arduino `String`, no drawing:

- `dice_expr`, `rng`, `db` (SRD tables and custom entries), `character` + `char_builder`, `spellmath`, `encounter`, `campaign`
- the encounter generator logic, Random Tables expansion, dungeon tables
- the SD data in `/gmtools`, including `tools/srd_extract.py` output
- the `Wizard` step model, the `Prompt` text editing, roll logging, settings in NVS

**Device-specific code to put behind a board layer.** About 10 files in `src/core`:

| Area | Now | Tab5 |
|---|---|---|
| keyboard | `keyboard_adv.h`, `input.cpp` (Cardputer matrix, `;.,/` arrows, `` ` `` Esc, Fn layer) | `UnitTab5Keyboard` (M5Unit-KEYBOARD library) → the same `input::Event`s; real arrows and Esc, so no Fn juggling |
| touch | none | new: `M5.Touch` → tap, drag-scroll and back events |
| storage | SPI pins, `SD.begin`, FAT32 format via `sd_diskio` internals | new pins (or SDIO via `SD_MMC`); the format code must be re-checked on core 3 / P4 |
| display | `M5Canvas gfx` 240x135, `pushSprite` | 1280x720 canvas in PSRAM (1.8 MB); redraw only what changed (see risks) |
| battery, speaker, IMU | `M5Cardputer.Power`, `Speaker`, `Imu` | `M5.Power`, `M5.Speaker`, `M5.Imu` (M5Unified covers both) |
| debug console | USB serial, hex screenshots | same; screenshots must be downscaled or compressed (a full shot is 3.7 MB as hex) |

**Needs real work: the UI.** About 850 drawing lines across the apps use hard-coded positions for 240x135 (`Font0`, 11 px rows, `W - 30` and so on). Footer hints also name Cardputer keys ("`` ` `` back", "Fn+X").

## Architecture

```
src/
  core/              shared logic (unchanged)
  ui/                layout + widgets, resolution-aware
    metrics.h        W, H, row height, font set, margins: chosen per board
    widgets/         List, TextView, Header, Footer, Toast, Prompt, Wizard,
                     RollOverlay, Pane (split view), Button (touch)
  board/
    board.h          interface: begin(), display, input source, SD pins, power, audio
    cardputer_adv.cpp
    tab5.cpp
  apps/              each app draws through ui/ widgets; big-screen layouts
                     added with `if (ui::wide())` or a separate draw function
```

- **Two PlatformIO environments:** `[env:cardputer]` and `[env:tab5]`, selected with `-DBOARD_CARDPUTER` / `-DBOARD_TAB5`. Shared `lib_deps`, board-specific `platform`, `board` and pins.
- **Keep the custom M5GFX UI and don't move to LVGL.** LVGL would mean rewriting every screen and dropping the shared code with the Cardputer. M5GFX already drives the Tab5 and gives full control of the look.
- **Key hints:** footer text comes from a key-name helper, e.g. `keys::back()` gives `` ` `` on the Cardputer and `Esc` on the Tab5, so one hint string works on both devices.
- **Input stays event-based:** touch adds new events (`K_TAP x,y`, `K_SCROLL dy`, `K_BACK` from an on-screen button). Apps that ignore them still work with the keyboard.

## Phases

### Phase 0: prepare on the Cardputer (before the Tab5 arrives) — Size: M — **done**

What was done, and where it differs from the plan below:
- **Toolchain:** Arduino core **3.3.12** (pioarduino **55.03.312**, ESP-IDF 5.5). 54.03.21 shipped an esptool that crashes with current `click`. Fixes needed:
  - `f_mkfs` now takes a parameter struct
  - the global canvas had to use `&M5.Display`: `&M5Cardputer.Display` is a reference that isn't initialised yet during static construction, which crashed at boot on core 3
- **Board layer:** `src/board/board.h` and `cardputer_adv.cpp` cover start-up, the keyboard (key ids, events, auto-repeat rules), SD pins and display rotation. Display, speaker, battery and IMU use M5Unified's `M5.*` objects directly, which work on both devices.
- **Metrics:** `ui::Metrics` / `ui::CLASSIC` live in `src/core/ui.h` rather than a separate `ui/` folder. The header, footer, toast, TextView, Prompt, Wizard and roll overlay take canvas size, bar heights, fonts, line height, character width and margin from it. Finer layout numbers wait for Phase 2 with the real screen. A pixel comparison of 12 screens showed no visual change.
- **Key hints:** hints are written with "Fn"; `ui::keyHints()` swaps in `board::fnName()` when drawing footers and toasts. The Tab5 board will map a modifier (e.g. Ctrl) to `Event::fn`, so app logic needs no changes.
- **Debug tooling:**
  - `shot <scale>`: run-length encoded and optionally downscaled; more than twice as fast even on the Cardputer
  - `tools/dev.py` drives the console from a PC
- **Still to confirm:** typing on the physical Cardputer keyboard with the new build. Injected test keys skip the keyboard driver.

Original plan:

1. **Move the Cardputer build to Arduino core 3.x (pioarduino)**, so the two devices share one toolchain. Test everything on the ADV, especially:
   - the TCA8418 polling reader
   - SD format (`sdcard_init` / `f_mkfs`)
   - `SET_LOOP_TASK_STACK_SIZE`
   - the debug console's base64 upload
2. **Create the `board/` layer** and move pins, keyboard, battery, speaker and IMU calls behind it. No behaviour change.
3. **Create `ui/metrics.h`.** Replace the fixed sizes in the shared widgets (header, footer, list rows, toast, prompt, roll overlay, wizard, TextView) with metrics. Apps keep working at 240x135.
4. **Add `keys::` hint names** and switch the footers to them.
5. **Prepare the debug console for 1280x720:** `shot` takes an optional scale (e.g. `shot 4` = 320x180) and run-length encodes the data.

Exit test: the Cardputer firmware behaves exactly as today, passes the same device tests, and builds from the new structure.

### Phase 1: bring-up on the Tab5 (scaled) — Size: S–M

1. Add `[env:tab5]` (pioarduino 54.03.21, `esp32-p4-evboard`, M5Unified/M5GFX from GitHub) and `board/tab5.cpp`.
2. **Keyboard:** `UnitTab5Keyboard` events → `input::Event`, including the auto-repeat and no-repeat rules. Check whether Normal mode (press/release by row/column) or Character mode (key names, Sym/Aa handled by the keyboard) fits better. Normal mode is likely, because auto-repeat needs key releases.
3. **SD** on the Tab5 pins, plus battery, speaker and the IMU for shake-to-roll.
4. **Draw the existing 240x135 canvas scaled to the full screen** (`pushRotateZoom` x5.33, or x5 into 1200x675 with a border). Everything is usable immediately, chunky but working.
5. Measure with `perf`: frame time, SD speed, keyboard latency.

Exit test: every app works on the Tab5 with the keyboard, using the same SD card as the Cardputer.

### Phase 2: native-resolution UI kit — Size: L

1. **Metrics for the Tab5:**
   - fonts: smooth anti-aliased fonts (M5GFX VLW, or FreeSans 12/18/24), body text about 22 px
   - rows: about 36 px tall, which suits touch
   - margins: 16 px
2. **Widgets drawn at native resolution:**
   - lists with real scrollbars
   - a header with clock and battery
   - toasts
   - multi-line prompts (about 60 characters per line)
   - a roll overlay with big dice results
3. **`Pane` split view:** a list on the left and the detail on the right. This one change upgrades Spells, Items, Monsters, Rules, Hazards, Archive and Notes at once, because they share `DbBrowser` or similar list/detail code.
4. **Touch:**
   - tap to select or open
   - drag to scroll
   - an on-screen back button
   - tap a roll button to roll
5. **Redraw only when something changes, and only the changed area.** A 1.8 MB full-screen push 60 times a second wastes power.

Exit test: the browsers and core widgets look native on the Tab5 and still render correctly at 240x135 on the Cardputer.

### Phase 3: big-screen layouts per app — Size: L (can ship one app at a time)

Highest value first:

| App | Tab5 layout |
|---|---|
| **Initiative** | Whole turn order as a table (init, name, HP bar, AC, conditions as chips) with the current monster's stat block in a side pane. Tap HP to damage or heal, tap a condition to read its rule. |
| **Monsters / Encounters** | Stat block laid out like the book (two columns, ability-score table). The generator's options and result side by side, with a difficulty meter and quick swaps. |
| **Characters** | One-page sheet: abilities, saves and skills in one column; attacks, spells and slots in another; inventory below. Tap anything to roll. |
| **Campaign** | Sections as columns or cards; session plan on the left, campaign threads, NPCs and locations on the right; drag items between sections. |
| **Spells / Items / Rules / Hazards** | Split list and detail (from Phase 2) with filter chips instead of Tab cycling. |
| **Dungeon** | Draw the five rooms as a simple map (boxes and corridors) next to the room text. |
| **Dice** | Several dice tumbling at once, a history column, quick-roll buttons. |
| **Notes** | Full-screen editor: about 25 lines x 80 columns, with the cursor movement the Prompt already supports. |
| Tables, Travel, Shop, Archive, Settings | Mostly fine after Phase 2; small layout touches. |

The Cardputer keeps its current layouts. Wide layouts are separate draw paths that use the same app state.

### Phase 4: Tab5-only features — Size: M each, all optional

- **Wi-Fi web companion:** a small web page served by the Tab5 for editing campaign notes, session plans, custom items and monsters, and for uploading files from a laptop. This replaces `tools/sdsync.py` for everyday use.
- **Real dates (RTC):**
  - session logs named by date
  - "Session 4 - 12 Oct" in the planner
  - timestamps in notes
  - days in Travel
- **Ambience board:** play rain, tavern or battle loops and music from the SD card through the speaker or headphones.
- **Player view:** a full-screen screen to show the players (initiative order with names only, a handout, a map image). Switch back with one key.
- **Search all of the SRD:** with 32 MB of RAM, load all SRD text and search every spell, item, monster and rule at once.
- **Bigger stored data:** room for longer notes, custom image handouts, and so on.

## Risks and open questions

| Risk | Mitigation |
|---|---|
| Core 3.x migration breaks something on the Cardputer (keyboard reader, SD format internals) | Do it first (Phase 0) while the ADV is available to test; keep the 2.0.14 environment until it passes. |
| Full-screen redraw on the 1280x720 MIPI-DSI screen may be slow or power hungry | Redraw only on change and push only the changed area. Measure in Phase 1 before designing animations. |
| Tab5 screen controller varies by production batch (ILI9881C+GT911, ST7123, ST7121) | Use the newest M5Unified/M5GFX; check the sticker on your unit. |
| Keyboard event semantics (key releases for auto-repeat, modifier reporting) | Prototype both keyboard modes in Phase 1; the protocol PDF has the register details. |
| SD in SPI vs SDIO mode | Start with SPI (the official Arduino example), and try `SD_MMC` 4-bit for speed later. |
| The device fonts only cover ASCII; the extractor already converts SRD text to plain ASCII | Keep ASCII on both devices to share the data files. |
| Touch and keyboard both changing focus | Touch moves the same selection the keys use; there's one focus model. |
| Debug tooling at high resolution | Scaled, compressed `shot` in Phase 0; `dev.py` updated to match. |

## Decisions to make

1. **Keep the Cardputer as a supported device?** Recommended: yes. Shared core, separate layouts; it stays the pocket version.
2. **Custom M5GFX UI or LVGL?** Recommended: M5GFX (see Architecture).
3. **Keyboard-first or touch-first on the Tab5?** Recommended: keyboard-first with touch as a full alternative, because the keyboard attaches and you already know the keys.
4. **Which Phase 4 features matter most?** The web companion and real dates give the most day-to-day value.

## Suggested order of work

1. Phase 0 now: toolchain upgrade, board layer, metrics, key hints, screenshot scaling.
2. When the Tab5 arrives: Phase 1 bring-up (a day or two of hardware work), then measure.
3. Phase 2 UI kit, then Phase 3 apps in this order: Initiative → Monsters/Encounters → Characters → Campaign → the rest.
4. Phase 4 features as wanted.

## References

- [Tab5 documentation](https://docs.m5stack.com/en/core/Tab5)
- [Tab5 Keyboard documentation](https://docs.m5stack.com/en/tab5/Tab5_Keyboard) and [Arduino keyboard guide](https://docs.m5stack.com/en/arduino/projects/tab5/tab5_keyboard)
- [Tab5 microSD Arduino example](https://docs.m5stack.com/en/arduino/m5tab5/microsd)
- [ESP-IDF `m5stack_tab5` board support package](https://components.espressif.com/components/espressif/m5stack_tab5)
