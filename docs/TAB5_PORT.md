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

### Phase 1: bring-up on the Tab5 (scaled) — Size: S–M — **done**

Status (Oct 2026):
- **Running on the device:** `[env:tab5]` uses pioarduino 55.03.312 with board `m5stack-tab5-p4`. That board is for P4 chips before rev 3; this Tab5 is rev v1.3.
- **Code:** `src/board/tab5.cpp`.
- **Keyboard (M5Unit-KEYBOARD 0.1.0, Normal mode):** detected. Ctrl acts as Fn, Aa as Shift, Sym is the symbol layer.
- **SD over SPI:** reads the same card as the Cardputer.
- **Screen:**
  - The frame is drawn 5x into 1200x675, centred.
  - `board::present()` writes straight into the DSI framebuffer (portrait 720x1280, RGB565 little-endian; landscape (x, y) maps to panel (y, 1279 - x)) and copies only changed columns.
  - Frame push: about 3 ms on average, 30 ms for a full repaint. `pushRotateZoom` took 163 ms.
- **Touch, for now:** tap = Enter, tap the top bar = Esc, drag = arrows.
- **Still to check by hand:** typing, touch feel, speaker, IMU, battery, WiFi through the C6.
- **Backup:** the firmware that was on the unit before ("orcsdr_tab5") is saved as a full 16 MB flash image outside the repo.

1. Add `[env:tab5]` (pioarduino 54.03.21, `esp32-p4-evboard`, M5Unified/M5GFX from GitHub) and `board/tab5.cpp`.
2. **Keyboard:** `UnitTab5Keyboard` events → `input::Event`, including the auto-repeat and no-repeat rules. Check whether Normal mode (press/release by row/column) or Character mode (key names, Sym/Aa handled by the keyboard) fits better. Normal mode is likely, because auto-repeat needs key releases.
3. **SD** on the Tab5 pins, plus battery, speaker and the IMU for shake-to-roll.
4. **Draw the existing 240x135 canvas scaled to the full screen** (`pushRotateZoom` x5.33, or x5 into 1200x675 with a border). Everything is usable immediately, chunky but working.
5. Measure with `perf`: frame time, SD speed, keyboard latency.

Exit test: every app works on the Tab5 with the keyboard, using the same SD card as the Cardputer.

### Phase 2: native-resolution UI kit — Size: L — **done (touch still basic)**

Status (Oct 2026):
- **Two layouts:** `ui::Metrics` CLASSIC (240x135) and WIDE (1280x720). `W`, `H`, `HEADER_H` and `FOOTER_H` follow the current one.
  - An app opts in with `bool bigLayout() const override { return true; }`. Every app does now.
  - Apps without it still run at 240x135 and are drawn 5x.
- **Scaling helpers** (identity on the Cardputer, so its screens are unchanged):
  - `ui::u(px)` scales classic pixel sizes x3.2.
  - `ui::font(&fonts::X)` swaps each classic font for a bigger one: generated DejaVu Sans Mono bitmaps in `src/core/fonts_wide.h`, made by `tools/make_font.py`.
- **Wide canvas:** a landscape 1280x720 sprite in an aligned PSRAM buffer.
  - It is pushed with the PPA (rotate 90 plus byte swap) in about 43 ms.
  - Clears use the PPA fill, about 5 ms.
  - A full repaint is still about 75 ms, because LGFX pixel writes into PSRAM are slow. So the main loop redraws on input, for 1.5 s after it, while a roll animates, and otherwise every 250 ms.
- **Launcher:** a 7-column icon grid. The icons are drawn once at the classic size and zoomed.
- **Touch sizes:**
  - Header 68 px, with a 52 px back button. Footer 60 px, with key buttons at least 64 px wide and 14 px apart.
  - List rows are at least `ui::TOUCH_ROW` (60 px). `ui::touchRow(n)` / `touchPad(n)` turn a classic n-px row into a touch row with its contents centred.
  - Long menus become button grids: the Initiative menu (2 columns) and the condition picker (4 columns).
- **Touch** (`src/core/touch.h`):
  - Screens register tap targets while they draw, and a tap runs the topmost one under the finger. Overlays are registered last, so they cover what is under them.
  - The header has a back button.
  - Footer key hints are drawn as buttons that send their key.
  - Lists:
    - Most lists select and open a row on one tap.
    - Rows where Enter does something risky select on the first tap and act on the second (browser entries, Initiative, wizard options, Campaign outline).
    - Value fields step their value when tapped again.
  - Pop-ups close on a tap outside them.
  - Dragging sends Up/Down (one per 40 px), and a sideways swipe sends Left/Right.
  - `tap X Y` on the debug console fakes a tap.
  - Not done: an on-screen keyboard, true pixel scrolling (a drag moves the selection), long-press.

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
   - an on-screen back button (done)
   - tap a roll button to roll
5. **Redraw only when something changes, and only the changed area.** A 1.8 MB full-screen push 60 times a second wastes power.

Exit test: the browsers and core widgets look native on the Tab5 and still render correctly at 240x135 on the Cardputer.

### Phase 3: big-screen layouts per app — Size: L (can ship one app at a time) — **first pass done**

Status (Oct 2026):
- **Every app** draws at native resolution.
- **Layouts beyond plain scaling:**
  - Initiative: table plus a detail pane with the condition rules.
  - Spells, Items, Monsters, Rules and Hazards: list plus entry pane.
  - Dice: a touch roller modelled on the RPG Watch one: a die picker (d2–d100), number-of-dice and modifier steppers (hold to repeat), normal/advantage/disadvantage, a big ROLL button. Every die bounces around the tray and lands in a grid, with a live total, natural 20/1 call-outs and struck-through dropped dice. Shapes are in `src/core/die_shapes.cpp`; tapping the tray or a recent roll rolls it again.
  - Characters: pages with big tabs (Abilities, Skills, Combat, Spells, Items, Features).
    - Abilities: six large ability boxes (tapping one asks "check or save"), plus HP, AC, initiative, speed, proficiency, passive Perception and inspiration.
    - Skills: 18 tiles, and a tap rolls.
    - Combat: resources and rests beside attacks.
    - Spell casting picks the slot with big < > buttons.
  - Campaign: outline plus the selected item's text.
  - Travel: options plus result panel.
  - Shop: a centred calculator column.
  - Web: a bigger QR.
- **Not done from the table below:**
  - Monsters does not yet have the two-column stat block.
  - Dungeon does not yet have the room map.
  - Notes still uses the classic editor width.

Highest value first:

| App | Tab5 layout |
|---|---|
| **Initiative** | Whole turn order as a table (init, name, HP bar, AC, conditions as chips) with the current monster's stat block in a side pane. Tap HP to damage or heal, tap a condition to read its rule. |
| **Monsters / Encounters** | Stat block laid out like the book (two columns, ability-score table). The generator's options and result side by side, with a difficulty meter and quick swaps. |
| **Characters** | One-page sheet: abilities, saves and skills in one column; attacks, spells and slots in another; inventory below. Tap anything to roll. |
| **Campaign** | Sections as columns or cards; session plan on the left, campaign threads, NPCs and locations on the right; drag items between sections. |
| **Spells / Items / Rules / Hazards** | Split list and detail (from Phase 2) with filter chips instead of Tab cycling. |
| **Dungeon** | Draw the five rooms as a simple map (boxes and corridors) next to the room text. |
| **Dice** | Done: picker, count and modifier steppers, bouncing dice, recent rolls. |
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

## Home screen picture and theme

The launcher draws `src/board/wallpaper_jpg.h` (a 1280x720 JPEG made by
`tools/make_wallpaper.py picture.png [crop_top]`, source kept in
`assets/wallpaper.jpg`) with the icon cells darkened over it. It is decoded
once and copied in each frame. A 1280x720 `/wallpaper.jpg` on the SD card
replaces it without rebuilding. Icons are captured on magenta (`ICON_KEY`)
and drawn with that colour see-through.

The palette in `ui.h` (`C_BG` ... `C_FIREFLY`) is taken from the picture:
night-sky navy backgrounds, parchment text, firelight amber accents, ember
reds, forest green, moonlight and lake blues. It is Tab5-only (`#ifdef
BOARD_TAB5`); the Cardputer keeps its original palette.

Fireflies (`drawFireflies` in `main.cpp`): the 14 painted into the picture
(the `STILL` table, picked from the built-in picture; a different SD picture
will have its own) pulse brighter and dimmer, and 14 more drift along slow
looping paths, blinking on and off. The home screen therefore keeps drawing.
To keep that cheap, the finished screen (picture, title, icons, names,
blurb) is kept in a second PSRAM copy until the selection changes. While
nothing else is on screen (no app, popup, keyboard, toast or wipe:
`g_homeShown`), each frame only puts back the squares last frame's fireflies
and the bobbing icon touched, draws the new ones, and sends just those
squares to the display (`ui::presentOnly` now takes several rectangles).
That is about 18 ms a frame instead of about 135 ms; copying the whole saved
screen in alone costs about 45 ms.

## Animation speed

A full 1280x720 frame costs ~95 ms (about 40 ms of it is the PPA rotating the
whole canvas to the panel). Animations avoid full frames:

- `App::animating()` keeps the loop drawing at full speed while it is true
  (otherwise the big screen drops to 4 frames a second 1.5 s after input).
- `ui::presentOnly(x, y, w, h)` + `board::presentRect()` send just one block
  of the canvas to the panel; the rest keeps the last frame.
- `touch::freeze(true)` keeps the tap targets of the last full frame while a
  screen redraws only part of itself; any tap or key unfreezes it.
- `ui::fillRound()` (used by `drawPanel`) fills big rounded panels by PPA DMA
  instead of pixel by pixel in PSRAM (12 ms -> 3 ms for the dice tray).
- `dieshape::draw` draws its lines as filled quads, not `drawWideLine`.

Dice: while the dice tumble only the tray is redrawn and sent: ~25 ms a
frame (35-40 fps) instead of ~155 ms (6 fps). `perf` on the debug console
prints frame and push times.

## At the table: shared dice, combat screen, DM panel

- **`core/dice_tray`**: the tumbling dice, shared by the Dice app and the
  roll popup. `Tray::start(results, rect)` throws the dice of several
  results at once, edged per result with `dicetray::tint(i)` (gold, ember,
  violet, green, blue); `liveTotal(i, r)` gives the running total.
- **Roll popup** (`rollui`, every app's rolls): on the big screen a 900-px
  box with the tray above one row per line (attack, damage, ...), each with
  its tint bar, live total, breakdown and damage type; crits show the doubled
  dice. While the dice move, `rollui::drawsAlone()` lets main skip drawing
  the app and the box redraws and shows only its middle.
- **Initiative** (big screen) is the combat screen:
  - left: turn order cards (initiative badge, name, AC shield, condition
    badges, HP bar); tap a card to select it, tap it again for its menu,
    tap its badges for the condition picker; + Add and Next turn on top.
  - right: the DM panel for the selection: -5 -1 / +1 +5 (hold to repeat),
    Hit and Heal prompts, condition chips (tap: rule), + Condition; for SRD
    monsters a stats line, ability scores (tap: saving throw) and the
    actions (tap: roll, from `monsterRolls()` shared with Monsters), and a
    Stat block button; quick dice d4-d100, adv, dis along the bottom.

## Touch and input: on-screen keyboard, smooth scrolling, icons

- **On-screen keyboard** (`core/osk`): shown only when the physical keyboard
  isn't there (`board::hasKeyboard()`, an I2C probe every 2 s). Prompts and
  the wizard's text steps open it; screens in text mode, or that call
  `osk::offer()` while drawing (the database lists' type-to-search), get a
  keyboard button in the bottom-right corner. Keys are injected as ordinary
  events; Shift is one-shot, tap again for caps; Del and the arrows repeat.
  Prompts centre themselves above it (`osk::height()`). `nokb` on the debug
  console pretends the keyboard is unplugged.
- **Smooth scrolling** (`touch::scroller(rect, &pos, max)`): a region that
  follows the finger by the pixel, coasts after a flick and eases back past
  the ends. The board sends drags to the scroller under the finger
  (`touch::press/drag/release`); elsewhere a drag is still Up/Down steps.
  Every `TextView` (entries, rules, sheets, journals) and the database lists
  (Monsters, Spells, Items, Rules, Hazards...) scroll this way. `drag X Y DY`
  on the debug console flicks.
- **Home screen icons**: 24x24 pixel art drawn by `tools/make_icons.py` into
  `src/core/icons_px.h` (run it with a .png name for a preview sheet), scaled
  4x once at start-up; matched to apps by name, the old drawn icons are the
  fallback.

## Long-press menus

Holding a finger still for half a second on a row, card or tile opens a
small menu beside it (`src/core/hold_menu.h`). A screen registers it with
`touch::onHold(x, y, w, h, fn)` next to the row's tap target; taps still go
to the row. Menu items that destroy something (`confirm = true`) need a
second tap ("Tap again to delete"). main.cpp routes keys to the open menu
(Up/Dn/Enter/Esc) and draws it over the app, after the roll popup. The
`hold X Y` debug console command does a long press.

The menus give a touch path to what used to need key combos, and the
big-screen footers now say "hold for more" in place of those keys (the
footer parser treats a "hold ..." group as plain text). The keys all
still work.

| Where | Hold on | Menu |
|---|---|---|
| Characters | a character | Open, Delete |
| Character sheet | ability tile | Check, Save, Score +1/-1 |
| | skill | Roll, Proficient, Expertise |
| | spell | Cast, Forget, Learn a spell |
| | resource, hit dice, slots | Use one, Get one back |
| | item | Actions, Quantity +1/-1, Add an item |
| Notes | a note | Open, Delete |
| | the editor text | Add the time, Save, Close |
| Campaign | a campaign | Open, Delete with its sessions |
| | a session | Open, Delete, New session, Rename campaign |
| | an outline item | Run (encounters), Edit, Mark, Add below, Move up/down, Delete, Mark played, Rename |
| Archive | a saved file | Open, Delete |
| Dice | a recent roll | Roll again, Save as macro, Remove |
| | a macro | Roll, Rename, Delete |
| Initiative | a combatant card | Damage, Heal, Condition, Set initiative, Rename, Duplicate, Remove |
| Monsters fight plan | party size/level | +1, -1 |
| | a monster | One more, One fewer, Remove |

Saving a macro writes the whole list to `/gmtools/macros.txt`, built-in
ones included (the file replaces the built-ins once it exists).

## Magic Shop and Travel on the big screen

Both are built from `src/core/widgets.h`: buttons (a chosen one filled),
read-out cards (label, big value, a line under it) and steppers (`-`/`+`,
optional big steps, a tap on the value types one in, holding repeats). The
Cardputer keeps its one-column layouts.

**Magic Shop** opens on the shelves, with tabs for Item prices, Spell
scrolls and Crafting.
- Shop: settlement (Village / Town / City sets the rarities), shop kind
  (General, Alchemist = potions, Arms & Armor, Arcanist = rings, rods,
  staffs, wands and Spell Scrolls, Curiosities = wondrous items), item count.
  Stock comes from `/gmtools/srd/magic.idx` (the SRD has only 3 Common items,
  so Spell Scrolls of random spells fill in, with rarity by spell level).
  Asking prices are 75-150% of the rarity's value, halved for consumables,
  rounded. Cards show rarity colour, kind, attunement and price. A tap reads
  the SRD text (for a scroll: its spell, save DC and attack bonus); holding
  gives Read, Sold / Back on the shelf, Haggled: 10% off, Swap for another.
  The shelf is kept in `/gmtools/shop.txt` (settlement kind count, then
  `name|type|rarity|price|attune|sold|spell level` lines).
- Item prices: rarity buttons, consumable toggle; price, craft time and cost.
- Spell scrolls: level buttons; scribing time and cost, the scroll's rarity
  and worth, save DC and attack bonus, cost to hire a caster.
- Crafting: item price (+-10, +-100, or typed), crafters; materials, days
  split between the crafters, days alone.

**Travel**: terrain buttons, pace (paces the terrain doesn't allow are dimmed;
a road raises the limit), steppers for distance, hours, party and the
guide's Survival bonus. On the right: miles a day, trip length, food and
water (doubled in the desert), a journey tracker ("Travel a day" moves along
the bar, ticks mark the planned days), what the pace means, the exhaustion
save past 8 hours, and Navigate / Forage / Search / Encounter cards that roll
(the checks as d20 + Survival vs the terrain's DC).

## 5-Room Dungeon map

On the big screen the dungeon is drawn as a map (left) beside the page being
read (right). The map is a 40x31 grid of 18 px tiles made from a seed
(`buildMap`): the five rooms are placed after the layout's shape (line,
zigzag, two ways through, loop), sized by role (the climax biggest), pushed
apart to keep two tiles of rock between them, then joined by L-shaped
corridors with doors where a corridor runs straight into a room, and a way
in with stairs from the map's left edge. Each room shows what it holds: the
guardian, a circle of runes, a pit trap, pillars round the boss, a chest.
The party marker walks the corridors to the room being read.

- Tap a room (or a numbered tab) to read it; hold a room to reroll it or
  switch the fog.
- Fog (players' view): only the rooms reached so far, the corridors out of
  them, and the doors at their far ends.
- The seed is saved with the dungeon (`Seed:` line), so a loaded one is
  drawn the same. Older files without one get a seed from their title.
- The saved-dungeons list: tap to load, hold for Load / Delete.

The Cardputer keeps its strip map.

## Big-screen layouts for the rest of the apps

These apps had kept their Cardputer list layouts. Each now has its own
big-screen drawing (`drawWide` or similar, behind `wide()`). The Cardputer
paths are unchanged.

**Notes**: the notes list on the left as cards, the open note on the right
with a toolbar (Time, Save, Delete, Close). Tap the text to place the cursor;
hold it for Add the time / Save / Close.

**Campaign**: the campaigns as cards, each showing its newest session.
Opening one goes straight to the session being planned (or to the campaign
notes) in three columns: Campaign notes, + New session and the sessions
(hold: Open / Mark played / Delete); the open plan's sections with done/total
counts; and the chosen section's items as cards that grow with their text.
Tap the box to tick an item, tap the text to edit it, hold for the full menu.
Encounter items get a Run button. Buttons cover + Add, Roll idea / Add party /
Plan fight, Mark played and Rename. Removing an item keeps you in its section.

**Archive**: the saved files grouped as cards on the left. The open one is
on the right in a TextView reader (smooth scrolling, `# ` headings, `- `
bullets), with Close and Delete. "Delete all roll logs" is a button with a
confirm tap.

**Encounters**: one screen. Steppers for party size, level, habitat and
creature type, plus difficulty and group buttons, on the left. On the right:
XP and difficulty, a bar marked with the three budgets, the activity, the
monsters as cards (tap: stat block; hold: Stat block / One more / One fewer /
Remove), and the actions (Fight with rolled or average HP, Monsters plan, Add
to session, Reroll).

**Random Tables**: the tables on the left with All / Built-in / SRD / Mine
filters (a tap rolls). On the right: Roll, the result in the biggest font it
fits as it is written out, and the earlier rolls from any table. "Entries"
(Tab) lists the whole table with its die ranges, the last roll picked out.

**Settings**: switches and tap-to-set sliders on the left; the SD card on the
right (size, Remount, Format with a confirm tap instead of the Y key) and the
licence text inline.

**Web access**: buttons for Start/Stop and Home WiFi, the details as cards,
the PIN big, and the QR code on the right (in hotspot mode a button flips it
between the WiFi and the page).

**Foundry setup**: three steps that are ticked off as they're done (web
access on, on the home network, the link key entered in Foundry), each with
a button. The key is shown big, and New key asks for a confirm tap.

## Clock

The Tab5's real-time clock (RX8130, through `M5.Rtc`) keeps the time with
the power off. `core/clock.cpp` (namespace `wallclock`) reads it at boot,
and sets both the system time and the RTC from the internet (NTP) whenever
web access joins a WiFi network. The RTC holds UTC; the time zone is a
POSIX TZ preset kept in Preferences (`tz`), UTC until chosen.

The home screen shows the time and date at the top left. Settings has a
Date & time card: the big time, when it was last set from the internet,
**Zone** (a grid of presets) and **Set time...** (steppers for year to
minute).

## Dungeon23

After Sean McCoy's Dungeon23 challenge: write one room a day, so a level
is a month and a twelve-level megadungeon takes a year. The room fields
follow the "Dungeon 23" worksheets (CC BY-NC 4.0; the PDF stays out of the
repo). Big screen only (`#ifdef BOARD_TAB5`).

Files are kept in `/gmtools/dungeon23/<id>/`:
- `dungeon.txt` holds the name, theme, history, nearest haven, hooks,
  rumours, the start date, the party size, and the party's level on level 1.
- `level01.txt` … `level12.txt` hold the level's info, then a
  `## Room N` section for each room.

All are `key: value` lines.

- **Dungeon screen**:
  - **Today** shows today's level and room, worked out from the start date
    and the clock.
  - The party steppers set who the monsters and treasure are sized for.
    Each level down adds one to the party level.
  - The intro fields each have a die to roll them.
  - The twelve level cards show each level's month, name, rooms written
    and checklist %.
- **Rooms**:
  - The rooms are laid out as a calendar with a row per week. Dots mark
    monster, treasure, trap and special. The last room (the last day of the
    month) is always the **boss**.
  - Beside it is the room's worksheet:
    - name and kind (room, puzzle, set piece, faction lair);
    - light and features;
    - description, inhabitants, three features and three treasures;
    - the kind's own fields (puzzle/solutions/failure,
      behaviour/wants/fears, or faction/leader/desires/defences).
  - Every field has a die. **Generate / Fill empty** fills whatever is
    blank, and **Fight!** sends the inhabitants to Initiative.
  - A room is dated the first time it's written.
- **Generators**:
  - Monsters come from the encounter generator (`core/encounter_gen`, moved
    out of the Encounters app):
    - normal rooms: Low or Moderate;
    - set pieces and the boss: High, solo or with a leader;
    - deeper levels sometimes draw from the Underdark.
  - Traps and loot come from the Random Tables. Traps are picked from the
    party's level band; the boss and set pieces roll a bigger hoard.
  - Rooms are stocked the classic way: d6 1–2 monster, 3 trap, 4 special,
    5–6 empty, with a chance of treasure.
  - Everything else comes from `dungeon23_tables.h`.
- **Level**: name, theme and description, and the number of rooms (the days
  in that month by default). **Stock every empty room** fills the level:
  every unwritten room, at least one puzzle and one faction lair, the
  encounter table and the level's own fields.
- **Encounters**: a d8 wandering-monster table, each entry with an omen.
  **Roll d8** shows the omen.
- **Checklist**: the % done of 13 items:
  - name/theme and description;
  - every room written;
  - the boss and its hoard;
  - 8 encounters and their 8 omens;
  - a puzzle and a faction lair;
  - 3 or more traps;
  - treasure in a third of the rooms;
  - two you tick yourself: map sketched, played through.

  Tapping an automatic item opens the tab where it's fixed.
- **Run**: the written rooms are shuffled onto a 9×5 grid and joined at
  random.
  - **Layout**:
    - The layout is built as a winding tree from the entrance, plus a few
      loops.
    - Doors are open, wooden, locked or secret.
    - The boss hangs off the deepest room with only one way in, and the
      stairs down are behind it.
    - The layout comes from a seed saved in the level file, so it redraws
      the same until **New layout**.
  - **DM view**: tap a room to see its details, then Mark explored, Fight!
    or Edit.
  - **Show the players**: a full-screen fogged map:
    - only explored rooms are shown, and the doorways out of them appear as
      "?";
    - secret doors stay hidden;
    - tap a room as the party enters it.
  - Once the boss's room is explored, the **Down** stairs open the next
    level's Run tab.

## Hex Map

A hexcrawl region generator. The SRD has no rules for making a hex map, so it
uses the methods hexcrawl GMs use by hand. Big screen only (`#ifdef BOARD_TAB5`);
the code is `src/apps/hexmap_app.cpp`, with its word lists in `hexmap_tables.h`
(original text).

The map is 24 × 16 pointy-top hexes, each 6 miles across (a day's travel is
about four hexes).

- **Seeded regions**: a coast along one or two edges (or a lake), one or two
  mountain ranges as random walks with hills on their flanks, then seeds of
  forest, grassland, hills, swamp (next to water), desert (arid) or tundra
  (cold).
- **Neighbour-weighted terrain**: every other hex grows from a filled
  neighbour, rolling on a "next hex" table (`NEXT`): forest mostly stays
  forest, hills climb into mountains, water edges into swamp. The climate
  swaps grassland for desert or tundra. A smoothing pass absorbs most lone
  hexes.
- **Rivers** start in the mountains or hills and run downhill to the nearest
  water (or off the map if there is none), and merge when they meet.
- **Settlements**: one town and three to five villages, placed by rivers,
  coasts and grassland at least four hexes apart. Roads take the cheapest
  path between them (a Dijkstra search over terrain costs, cheap along
  existing road), plus one road off the map.
- **Keyed hexes**: about one land hex in five (one water hex in fourteen)
  holds a ruin, dungeon, lair, camp, tower, shrine, landmark, wonder or
  resource, weighted by terrain. They are filled in from the encounter
  generator (sized for the party), the Random Tables (Location, NPC, Tavern,
  Rumor, Loot) and the word lists.
- **Hex templates** (after the Welsh Piper's): the side panel draws a hex as
  seven sub-hexes. Each outer one keeps the hex's terrain on 1–4 on a d6 and
  takes the neighbour's terrain on 5–6, so borders blend.

Playing it:
- The party (red marker) starts at the town. **Travel** moves it to the next
  hex: 2 hours, or 3 hours in mountains or swamp without a road (8 hours a
  day; **Make camp** starts the next day).
- Entering a hex marks it visited and the hexes around it seen (two hexes
  out from hills and mountains). Towns, villages, towers and landmarks are
  found from a hex away; everything else only when the party walks in.
- Each hex travelled has a 1-in-6 chance of an encounter (terrain habitat,
  with the SRD encounter distance), with **Fight!** to send it to Initiative.
  Keyed hexes with monsters get a **Fight** button too.
- Hold a hex to jump the party there, roll an encounter, add a note, re-roll
  what's there, clear it, or put something new there.
- **Players** (or P) shows the fog-of-war map: unseen hexes are dark, seen
  ones dimmed with nothing on them, and visited ones in full.
- **More...** renames the map, resets the fog of war, or re-rolls the whole
  map (keeping its name and party).

Files are kept in `/gmtools/hexmaps/<id>.txt`: `key: value` lines, with the
grids as one character per hex (terrain `wpfhmsdt`, river out-direction
`0`–`5` or `-`, road `'0'` + a 6-bit mask, seen `0`/`1`/`2`). Then comes a
`## Hex N` section for each keyed or noted hex.

On the web: the **Hex Map** page draws the map as SVG. Click a hex for the
same panel as on the device. It has a players' view toggle and a players'
map page for a TV (`#hex/<id>/players`) that follows the device.

## Web interface

Most apps can be used from a phone or laptop through web access. The page
talks to the apps that are running on the device, so a change made on
either side shows up on both.

- **Table**: runs the Initiative tracker.
  - Next and back turns.
  - Damage and heal.
  - Conditions with rounds.
  - Add combatants, monsters or the party.
  - A player view without the controls.
- **Dice**: die buttons with counts, a modifier, advantage and saved rolls. The rolls are made on the device.
- **Characters**: play a character's sheet.
  - Checks, saves, skills and attacks.
  - HP and hit dice, death saves and rests.
  - Resources and spell slots, and casting.
  - Coins and items, features and notes.
  - Characters are still made and levelled up on the device.
- **Encounters**: set up the plan and generate a fight. Send it to the Table or to the session.
- **Tables**: search the random tables and roll on them.
- **Dungeon23**:
  - Dungeon list and home page.
  - Room editor with dice for each field.
  - Level, Encounters and Checklist tabs.
  - The Run map with DM and fogged views.
  - A full-screen players' map for a TV, which updates as rooms are explored.
- **5-Room**: generate a dungeon, reroll a page, save and open dungeons.
- **Shop**: restock, swap and sell items, with price guides.
- **Travel**: terrain, pace and the journey day by day. Tap a check to roll it.
- **Reference**: dice in entries can be tapped to roll them. Monsters have **Add to the fight**.
- **Device**: clock and time zone, volume, brightness and sound.

Monsters, Spells, Items, Rules and Hazards are covered by Reference.
Archive is covered by Files. Foundry and Web have no web side.


## D&D Beyond import

Characters can be imported from a D&D Beyond share link
(`https://www.dndbeyond.com/characters/<id>/<code>`):

- **Where:**
  - On the Tab5: Characters > **Import from D&D Beyond**. You can type the
    whole link or just its number.
  - On the web: the Characters page's import box.
- **Requirements:** the character must be Public on D&D Beyond. The Tab5
  must be online, which means web access joined to a home WiFi.
- **How:** `core/ddb_import.cpp` downloads
  `character-service.dndbeyond.com/character/v5/character/<id>`. ArduinoJson
  streams that answer (300+ KB) through a filter, so only the fields used are
  kept in memory.
- **What's converted:**
  - Ability scores, with species, background and feat bonuses.
  - Hit points: base, plus Con and per-level bonuses such as Tough. Damage
    taken and temporary HP come across too.
  - Skills, expertise, saves and tools; feats (D&D Beyond's internal ones
    are skipped).
  - Spells. Feat spells (Magic Initiate) and species spells are kept apart
    from the class's, with their own casting ability and the free casts
    already used.
  - Slots used, class resources used, coins, death saves and inspiration.
  - Inventory: matched to the SRD by name, otherwise built from D&D Beyond's
    item data.
- **Updating:** the link is saved in the file as `ddb:`. Importing the same
  character again updates that file and keeps the notes. On the Tab5, hold a
  linked character for "Update from D&D Beyond". On the web, linked sheets
  have an Update button.
- **Limits:**
  - A multiclass character is kept as its biggest class at the total level.
  - Classes, features and items that aren't in the SRD have no rules data on
    the sheet.

## Spells from feats and species

A feat or species can grant spells: Magic Initiate, the Elf and Gnome
lineages, a Tiefling's Fiendish Legacy and Thaumaturgy.

- **Kept apart:** these spells are listed with where they come from, for example
  "Faerie Fire (Drow, free 1/1)". Species spells are worked out from the
  species, lineage and level, so they turn up at levels 3 and 5 on their own.
- **Casting:** cantrips are at will. A level 1 or higher spell is cast free
  once per Long Rest (a Forest Gnome's Speak with Animals as many times as
  the proficiency bonus). After that it needs a spell slot, if the class has
  any.
- **Ability:** the builder asks which ability the species spells use.
  Magic Initiate has its own.
- **Learning:** a class that doesn't cast spells (Fighter, Barbarian...)
  can't learn spells. Its spells come only from feats and species.
- **Saved as:** the file keeps `species_ability` and `free_used` (one entry
  per free cast spent, cleared by a Long Rest).
- **Free cast or slot:** while a free cast is left, the cast panel starts on
  "Free cast"; ">" picks a spell slot instead, so the spell can be upcast.

## Spell slots and upcasting

- **Slots:** the Spells page has one row per slot level with a pip per slot
  (Warlocks: "Pact slots", back on a Short Rest). Tap a full pip to use it, an
  empty one to get it back; tapping the row uses one too. Holding it opens Use
  one / Get one back. Keys: Enter uses one, `-`/`=` adjust.
- **Upcasting:** tapping a levelled spell opens the cast panel. `<`/`>` step
  through the free cast (if any) and every slot level with one left. The panel
  shows the attack, damage or healing and save DC for the chosen slot, and the
  spell's "Using a Higher-Level Spell Slot" rule. Upcast damage is added
  automatically when the rule gives dice per level (Burning Hands 3d6 -> 5d6 at
  level 3). Rules that add targets or darts are shown as text only.
- **Extra slots:** a slot made with Sorcery Points shows as a gold pip and is
  gone after a Long Rest.
- **Web:** the same choices are in a dropdown next to Cast. A higher slot is
  marked with an arrow, and choosing one shows the upcast rule.

## Class, feat and species resources

`src/core/resources.cpp` holds the counters and what spends them. They are worked
out from the class, subclass, level, feats and species, so a level up adds
them.

- **Counters:** Rages, Bardic Inspiration, Channel Divinity, Wild Shape,
  Second Wind, Action Surge, Indomitable, Focus Points, Uncanny Metabolism,
  Lay On Hands, Favored Enemy, Sorcery Points, Innate Sorcery, Arcane Recovery
  and the other limited features up to level 20, plus subclass ones
  (Wholeness of Body, Dark One's Own Luck...).
  - Feats: Lucky, Boon of Fate, Martial Adept, Mage Slayer, Gift of the
    Chromatic/Metallic Dragon.
  - Species: Breath Weapon, Draconic Flight, Stonecunning, Giant Ancestry,
    Large Form, Adrenaline Rush, Relentless Endurance.
  - Each knows how it comes back: on a Long Rest, all on a Short Rest, or one
    on a Short Rest.
- **Actions:** tapping a counter on the Combat page opens its panel: pips,
  `-`/`+`, and a button for each thing that spends it, with its cost (Flurry
  of Blows 1, Quivering Palm 4, the Metamagic options...).
  - Rolls use the character's numbers (Divine Spark scales with level, a
    Monk's DCs use Wisdom).
  - Second Wind, Wholeness of Body and Uncanny Metabolism heal you;
    Adrenaline Rush and Tireless give temporary HP.
- **Slot trades:** "Create a slot" and "Slot -> points" (Sorcerer) ask for a
  level. Arcane Recovery and Natural Recovery get back the highest spent slots
  that fit half your level. Magical Cunning gets back half the Pact slots.
  Wild Resurgence trades Wild Shape for a level 1 slot and back.
- **Your own counters:** hold a row on the Combat page > Add a counter
  ("Lucky, 3, long"), or "Add a counter" on the web page. Holding one of these
  counters (or X in its panel) removes it.
- **Feats page:** a feat with a counter shows it ("Luck Points 2/3 feat").
- **D&D Beyond:** uses already spent are matched by name or feature name
  (their "Rage" is our "Rages"). A feat or species use we have no rules for
  comes in as a counter of your own.
- **Saved as:** `res: name|cur|max`. A counter you added gets
  `|rest|c` on the end.

## Screen timeout

Settings > Screen timeout: 30 sec, 1, 2, 3, 5, 10 or 15 min, or Never (default 2 min).
It's also on the web page's Device tab.

- **Dimming:** 10 seconds before the timeout the screen dims as a warning.
  At the timeout the backlight turns off and the screen stops redrawing.
  Web access, timers and the clock keep running.
- **Waking:** any key or touch wakes it. The touch that wakes it does nothing
  else, so a button under your finger isn't pressed.
- **Stored as:** `scrtimeout` in the settings (NVS). The Cardputer still dims
  after a fixed 90 seconds.

## Keyboard lights

Settings > Keyboard lights: Normal, Dim or Off (also on the web page's Device
tab). Stored as `kblights` in the settings (NVS).

- **Normal and Dim:** the keyboard drives its two LEDs itself ("bind" mode).
  The right LED shows its mode and the left one shows modifiers. We only set
  the brightness: 20% for Normal (its default) and 3% for Dim.
- **Off:** switches the LEDs to "custom" mode and sets both to black.
- **Re-applied:** the keyboard forgets the setting when it loses power, so it
  is set again at start-up and whenever the keyboard comes back (it's checked
  every 2 seconds).
- A keyboard that wasn't plugged in at power-on is never started, so the
  setting can't reach it until a restart.
