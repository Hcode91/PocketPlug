# PocketPlug

An Android emulator for plug-and-play TV games, built to grow into a multi-system
app (like MAME, one core per chip family), with each machine shown the way it was
really played: on a CRT TV, with on-screen controls drawn like the original
controller.

## Supported machines

| Machine | Set | Core | Status |
|---|---|---|---|
| Retro Dance Mat (110 song Super StepMania + 9-in-1 games) (PAL) | `110dance` | VT1682 | Boots, menus, songs and mini-games playable, sound working |
| Radofin 1292 / 1392 Advanced Programmable Video System | `1292apvs`, `1392apvs` | VC 4000 family (Signetics 2650 + 2636) | All 37 software-list cartridges run; output matches MAME |
| Egg Pet - Generation 1 (1996 keychain virtual pet) | `tama` | Epson E0C6S46 | Clock, egg, hatching and pet match MAME segment-for-segment over 10 min; lives in real time |
| 168-in-1 Game Phone Case | `168pcase` | VT369 (NES-on-a-chip) | Menu and games match MAME pixel-for-pixel in every tested frame (Contra, Panda Adventure, Aether, 60+ others); NES APU sound |
| 240-in-1 Mini Arcade (palm-sized cabinet) | `240in1ar` | VT369 + sound CPU, scrambled opcodes, 128 MB | Menu, sports, puzzle and arcade games match MAME pixel-for-pixel in every tested frame; sampled music via the sound CPU (ADPCM, mixer, DACs) |
| Egg Pet - Ocean (original) | `oceanpet` | Built in (no ROM) | PocketPlug's own sea pet in the spirit of the 1998 ocean egg; real time, classic or gentle mode |
| Egg Pet - Angel (1997 angel virtual pet) | `tamaang` | Epson E0C6S48 (+1 MHz OSC3) | Matches MAME segment-for-segment over 10 min (intro, clock, hatching); shake sensor; lives in real time |

## Required files

What each machine needs, with the checksums PocketPlug checks on import. A MAME set
`.zip` / `.7z` or the bare file all work.

| Machine | MAME set | File | Size | CRC32 |
|---|---|---|---|---|
| Retro Dance Mat | `110dance` | `110songdancemat.bin` | 32 MB (33,554,432 bytes) | `cd668e41` |
| 1292 / 1392 APVS | `1292apvs`, `1392apvs` | No system ROM; cartridge dumps (up to 6 KB each) or MAME's `vc4000` software-list zip | - | Known carts get titles |
| Egg Pet - Generation 1 | `tama` | `tama.bin` | 12 KB (12,288 bytes) | `5c864cb1` |
| Egg Pet - Angel | `tamaang` | `tamaang.bin` | 16 KB (16,384 bytes) | `87bcb59f` |
| Egg Pet - Ocean (original) | - | Nothing (built in) | - | - |
| 168-in-1 Game Phone Case | `168pcase` | `25q32.u7` | 4 MB (4,194,304 bytes) | `98e8e97a` |
| 240-in-1 Mini Arcade | `240in1ar` | `mw-106-2g.u3` | 128 MB (134,217,728 bytes) | `c46d2ca9` |

MAME artwork, samples, BIOS files and parent sets are not needed.

## Using it

1. Install the APK from the latest GitHub Actions run (artifact **PocketPlug-apk**).
2. Tap **Import ROM** on a machine and pick your dump. A MAME set `.zip`, a `.7z`
   or the raw ROM file (e.g. `110songdancemat.bin`) all work. The checksum is
   compared with MAME's.
3. Tap **Play**.

Cartridge consoles (like the 1292 APVS) have a game library instead (compatible
consoles such as the 1292 and 1392 share one): tap
**Add games** and pick a cartridge dump, a .zip/.7z of dumps, or a whole MAME
software-list zip (e.g. `vc4000.zip`). Known cartridges get their proper titles.
Tap a game to play it, hold it to remove it.

### 1292 APVS controls

The console panel has **OFF/ON** (power-cycles the console), **RESET**,
**START** and **GAME SELECT**, and the cartridge in the slot shows the current
game's name. The 1292 and 1392 each look like their real consoles. Each hand
controller has an analog joystick (it springs back to centre), the 12-key
keypad (1-9, CL, 0, EN) and a FIRE button. On the real controllers the fire
button is wired to key 2, so it lights that key too. Most games are chosen with
GAME SELECT and started with START.

Some cartridges check their inputs only now and then (Cockpit, for example,
looks about every 1.4 seconds), so the core keeps a quick tap held until the
game has read it. In Cockpit, GAME SELECT picks the difficulty and keypad
**3** starts the flight; START isn't used.

Gamepad/keyboard: left stick, D-pad or arrows for player 1's joystick, the right
stick for player 2's; A/B/Space for fire; Start/Select; number keys for the
keypad, Backspace/X for CL and Enter/Y for EN.

### Retro Dance Mat controls

The mat on screen is drawn like the real vinyl mat and is multi-touch, so
two-arrow jumps work. **SELECT** (red, top left) is the game's back button and
**START** (blue, top right) confirms; pressing START during a song pauses it, as
on the real unit. The game only reads the two upper diagonal arrows - the lower
corner arrows are printed on the mat but do nothing in 110dance.

Gamepad/keyboard: D-pad or WASD for the arrows, L1/R1 or Q/E for the diagonals,
A/Start/Enter for START, B/Select/Esc for BACK.

### Egg Pet controls

The three yellow buttons are A, B and C (left to right), as on the real toy.
At first start the egg shows: press **B** for the clock, **A** sets the hour,
**B** the minutes, **C** confirms - the egg hatches a few minutes later. Tap the
key ring (or use Back) for the menu.

The pet lives in real time. When you leave the app its exact state is kept, and
when you come back the clock is fast-forwarded by the time you were away (up to
14 days; "Time passes..." shows on the screen while it catches up, about 10-20
seconds per day on a phone). Turn that off in the menu ("While closed: time
stops") to freeze the pet while the app is closed. **Reset** in the menu is the
pin hole on the back: a new egg.

Gamepad/keyboard: X / Left / 1 / Z = A, A / Enter / 2 / X = B, B / Right / 3 / C = C.

The Angel has a vibration sensor: shake the phone, touch either wing, or press
gamepad Y / Up / 4 / S. Its menu opens from the ball chain (or Back).

Gen 1 can also wear a deep-sea reef shell: menu → "Shell".

### Egg Pet - Ocean (original)

A built-in game - no ROM - modelled on how the 1998 ocean egg played, with
PocketPlug's own creatures and art. It follows the phone's clock.

- Icons (top): meal/snack, lights, treasure game, medicine;
  (bottom): flush, meter, discipline, attention.
- **A** moves through the icons, **B** picks, **C** goes back. B with no
  icon shows the clock.
- The water gets murky over time and with meals: the meter shows four
  skulls. Flush it; once the water turns black it takes four flushes, and
  murky water left too long makes the pet sick.
- Treasure game: guess the left (A) or right (B) chest, five rounds, three
  right to win - unless an octopus jumps out and inks the water.
- Around 1-3 pm it naps, and predators may come: tap the shell (or shake
  the phone, or use discipline) within 90 seconds or it gets hurt.
- It calls for discipline now and then; answer within 15 minutes.
- Care decides what it grows into: Speck → Jello → Finlet or Puffo → one of
  Hippa (seahorse), Stripes (clownfish), Clammy (clam), Bluey (whale),
  Skippy (mudskipper), and a secret one.
- **Classic** mode is as unforgiving as the original (poor discipline at a
  growth stage, repeated neglect sickness or a long untreated illness is
  fatal); **Gentle** mode (menu) never lets it die.

### 168-in-1 Game Phone Case

Import the MAME `168pcase` set (`25q32.u7`). The case is drawn as the real one:
D-pad, red **A**/**B**, round **SELECT**/**START**, and two **TURBO** buttons
(rapid-fire B / A). Tap the camera bump (or Back) for the menu. Gamepads: B/Y = A,
A/X = B, Start/Select, D-pad or left stick; keyboard X/Z, Enter, Space, WASD.

### 240-in-1 Mini Arcade

Import the MAME `240in1ar` set (`mw-106-2g.u3`, 128 MB). Drawn as a mini
upright cabinet: drag the red ball-top joystick, press the big **A**, and the
small **Start**, **Reset** and **Volume** buttons work like the real ones
(Volume steps loud / medium / quiet / mute and is remembered). Tap the marquee
(or Back) for the menu. Gamepads and keyboards use the phone-case mapping, so
B and Select are still available for the few games that want them.
MAME lists this set as not working, and several games are buggy on the real
machine too; PocketPlug matches MAME's picture in everything tested.

The **MENU** button on the TV (or the system Back gesture) has save/load state,
reset and the scanline toggle.

## Layout

```
app/src/main/cpp/
  core_api.h            interface every core implements
  core_registry.cpp     machine id -> core + configuration
  common/m6502.*        shared 6502 CPU
  common/s2650.*        shared Signetics 2650 CPU
  cores/vt1682/         V.R. Technology VT1682 (two 6502s, tile/sprite video, DAC sound)
  cores/vc4000/         Interton VC 4000 family / 1292 APVS (2650 + 2636 PVI)
  jni_bridge.cpp        Kotlin <-> core glue
app/src/main/java/com/hcode91/pocketplug/
  Machines.kt           machine list (ROM files, CRCs, skin)
  RomStore.kt           ROM import (.zip/.7z/raw) and state files
  TvView.kt             CRT television presentation
  DanceMatView.kt       dance mat controller skin
  ApvsViews.kt          1292 APVS console panel + hand controllers
tools/                  desktop headless runner for testing cores against real ROMs
```

### Adding a machine

1. Add a core under `app/src/main/cpp/cores/<chip>/` implementing `Core`, or reuse
   one with a new configuration.
2. Register it in `core_registry.cpp`.
3. Add a `MachineDef` in `Machines.kt` (and a skin if it needs a new controller).
4. Add its files to the **Required files** table above.

### Testing a core on a PC

```
make -C tools
./tools/headless 110dance 110songdancemat.bin 600 out/ "200:8,205:0"
```

Writes a screenshot every 50 frames and `audio.wav`. The last argument is an
optional input script (`frame:hexmask`). For cartridge machines pass the
cartridge file instead of the ROM, e.g. `./tools/headless 1292apvs golf.bin 400 out/`.

## Credits and licence

PocketPlug is distributed under the GNU GPL v2 or later (`LICENSE`), because the
VC 4000 core is a port of MAME's GPL-2.0+ driver (Peter Trauner, Manfred
Schneider, Robbbert). The VT1682 core (David Haywood), the 2650 CPU (Juergen
Buchmueller), the E0C6200/E0C6S46 MCU (hap) and PocketPlug's own code are
BSD-3-Clause. See `LICENSES/README.md`.
No ROMs or cartridges are included.
