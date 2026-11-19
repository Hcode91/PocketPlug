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
| 36-in-1 Classic Games Phone Case | `36pcase` | VT369 + sound CPU, scrambled opcodes, internal ROM, serial protection chip | Language select, menu and games match MAME pixel-for-pixel in every tested frame; sound matches MAME's level |
| 240-in-1 Mini Arcade (palm-sized cabinet) | `240in1ar` | VT369 + sound CPU, scrambled opcodes, 128 MB | Menu, sports, puzzle and arcade games match MAME pixel-for-pixel in every tested frame; sampled music via the sound CPU (ADPCM, mixer, DACs) |
| 265-in-1 Handheld | `265games` | SunPlus SPG2xx (u'nSP CPU), 256 MB in 32 banks | Menu and games match MAME pixel-for-pixel in every tested frame; SPU sound (ADPCM, envelopes) matches MAME's level |
| NGEN CP-001 Workstation (Convergent NGEN / Burroughs B26) | `ngen` | Intel 80186 + 8259 / 8254 / 8251, MC6845 text video, WD2797 floppy, WD1010 winchester | Boots BTOS 4.0 from floppy, installs it onto the emulated 10 MB hard disk and boots from there (MAME's driver does not boot) |
| 4004 Nixie Clock (2008 home-built clock) | `4004clk` | Intel 4004 (own core) + 4001 ROM / 4002 RAM I/O | Matches MAME's tube and dot output frame-for-frame through power-on, start and the setting menus; set from the phone's clock; tick-tock sound |
| 500-in-1 Handheld Game | `500in1hh` | VT "4FFx" (NES-on-a-chip, scrambled opcodes), 16 MB | Language select, menu and games match MAME pixel-for-pixel in tested frames (MAME marks it not working, but it runs in both); NES APU sound |
| Joystick88 (Play Vision plug-and-play joystick) | `88in1joy` | VT02 / VT03 | Menu and games match MAME pixel-for-pixel in every tested frame |
| TM 990/189 University Board (+ Video Board) | `990189`, `990189v` | TMS9980A + 2x TMS9901 (own cores), TMS9918 (own core) | University BASIC runs; the LED display and lights follow MAME frame-for-frame apart from a frame's lag in key echo; the video board's four colour demos run |
| 1200XL Home Computer (Atari) | `a1200xl` | Atari800 (6502C, ANTIC, GTIA, POKEY, PIA) | Real 1200XL OS: logo, self test, function keys and the L1 / L2 lights; loads disks, programs, cartridges and tapes; optional BASIC cartridge |
| 400 Home Computer (Atari) | `a400`, `a400pal` | Atari800 (6502B, ANTIC, CTIA/GTIA, POKEY), 48 KB | Real OS rev. B (NTSC) / rev. A (PAL): Memo Pad; loads disks, programs, cartridges and tapes; optional BASIC cartridge |
| 130XE Home Computer (Atari) | `a130xe` | Atari800, 128 KB banked RAM | BASIC built in; loads disks, programs, cartridges and tapes |
| PC 7300 / 3B1 Desktop (AT&T UNIX PC) | `3b1` | Motorola 68010 + MMU, 720x348 bitmap, WD2797 floppy, WD2010 hard disk, keyboard + mouse (FreeBee) | Installs the 3.51 Foundation set from floppies onto the emulated 64 MB hard disk, boots from it to the window manager; mouse and full keyboard |
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
| 36-in-1 Classic Games Phone Case | `36pcase` | `25q16.ic3` | 2 MB (2,097,152 bytes) | `a8edb73e` |
| | | `internal.bin` | 4 KB (4,096 bytes) | `da5850f0` |
| | | `serial-rom.bin` | 256 bytes | `877fb90b` |
| 240-in-1 Mini Arcade | `240in1ar` | `mw-106-2g.u3` | 128 MB (134,217,728 bytes) | `c46d2ca9` |
| 265-in-1 Handheld | `265games` | `265games.bin` | 256 MB (268,435,456 bytes) | `761c92cb` |
| NGEN CP-001 Workstation | `ngen` | `72-00414_80186_cpu.bin` | 4 KB (4,096 bytes) | `e1387a03` |
| | | `72-00415_80186_cpu.bin` | 4 KB (4,096 bytes) | `a6dde7d9` |
| | | `72-00422_10mb_disk.bin` | 4 KB (4,096 bytes) | `f5b046b6` |
| | | Plus your own BTOS / CTOS system floppies (`.imd` ImageDisk, or raw `.img`), inserted from the disk menu | - | - |
| 4004 Nixie Clock | `4004clk` | `clock.u30` to `clock.u45` (16 files) | 256 bytes each | `3d8608a5` ... `a8d419ef` (checked one by one) |
| 500-in-1 Handheld Game | `500in1hh` | `500in1.bin` | 16 MB (16,777,216 bytes) | `50f44c1a` |
| Joystick88 | `88in1joy` | `88in1joystick.bin` | 4 MB (4,194,304 bytes) | `86b8d819` |
| TM 990/189 University Board | `990189` | `990-469.u32` | 2 KB (2,048 bytes) | `08df7edb` |
| | | `990-469.u33` | 4 KB (4,096 bytes) | `e9b4ac1b` |
| TM 990/189 + Video Board | `990189v` | the two `990189` files, plus `demo1000.u13` | 2 KB (2,048 bytes) | `c0e16685` |
| | | `demo1800.u11` | 2 KB (2,048 bytes) | `8737dc4b` |
| 1200XL Home Computer | `a1200xl` | `co60616b.rom` | 8 KB (8,192 bytes) | `6e29ec8d` |
| | | `co60617b.rom` | 8 KB (8,192 bytes) | `d73ce29a` |
| 400 Home Computer | `a400` | `co12399b.rom` | 2 KB (2,048 bytes) | `6a5d766e` |
| | | `co12499b.rom` | 4 KB (4,096 bytes) | `d818f3e8` |
| | | `co14599b.rom` | 4 KB (4,096 bytes) | `c1690a9b` |
| 400 Home Computer (PAL) | `a400pal` | `co12399b.rom` (in the `a400` zip: import that too) | 2 KB (2,048 bytes) | `6a5d766e` |
| | | `co15199.rom` | 4 KB (4,096 bytes) | `8e547f56` |
| | | `co15299.rom` | 4 KB (4,096 bytes) | `be55b413` |
| 130XE Home Computer | `a130xe` | `co24947a.rom` (BASIC) | 8 KB (8,192 bytes) | `7d684184` |
| | | `co61598b.rom` (OS; MAME keeps it in the `a800xl` zip, so import that zip too) | 16 KB (16,384 bytes) | `1f9cd270` |
| | | Plus your own software: `.atr` `.xfd` `.atx` `.dcm` `.pro` disks, `.xex` `.com` programs, `.car` / `.rom` cartridges, `.cas` tapes | - | - |
| PC 7300 / 3B1 Desktop | `3b1` | `72-00616.14c` | 8 KB (8,192 bytes) | `c61f7ae0` |
| | | `72-00617.15c` | 8 KB (8,192 bytes) | `4e93ff40` |
| | | Plus your own 3B1 system floppies (`.imd` ImageDisk, or raw `.img`), inserted from the disk menu | - | - |

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

Machines that need more than starting up (installing an operating system,
loading your own disks, setting a clock) show **Getting started** steps in a
small card over the machine the first time. ✕ shrinks it to a round **?**
bubble: drag it out of the way, tap it to read the steps again. "Hide for this
machine" puts it away; **Getting started steps** in the menu brings it back.

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

### 36-in-1 Classic Games Phone Case

Import the MAME `36pcase` zip (all three files). Drawn like the black
handheld-style case: D-pad, square red **A**/**B**, **S/P** (start/pause),
**SOUND** (mute on/off, remembered), **RESET**, and **ON/OFF** or the camera bump
for the menu. Gamepads use the 168-in-1 mapping, so Select is on the pad's
Select button.

### 240-in-1 Mini Arcade

Import the MAME `240in1ar` set (`mw-106-2g.u3`, 128 MB). Drawn as a mini
upright cabinet: drag the red ball-top joystick, press the big **A**, and the
small **Start**, **Reset** and **Volume** buttons work like the real ones
(Volume steps loud / medium / quiet / mute and is remembered). Tap the marquee
(or Back) for the menu. Gamepads and keyboards use the phone-case mapping, so
B and Select are still available for the few games that want them.
MAME lists this set as not working, and several games are buggy on the real
machine too; PocketPlug matches MAME's picture in everything tested.

### 265-in-1 Handheld

Import the MAME `265games` set (`265games.bin`, 256 MB; the file is memory-mapped,
not loaded into RAM). Drawn as a two-tone vertical handheld: D-pad, red **A**,
yellow **B** and **START**. Tap the "265 GAMES" logo under the screen (or Back)
for the menu. Gamepads: B/Y = A, A/X = B, Start, D-pad or left stick; keyboard
X/Z, Enter, WASD. Like MAME, the CPU runs at twice the crystal clock and the
ROM's interrupt-disable checks are patched, which the games need to run.

### NGEN CP-001 Workstation

Import the MAME `ngen` zip (the disk module's PROM, `72-00422_10mb_disk_15d.bin`,
is not needed). Drawn as the workstation: a green-screen monitor, the disk
module's floppy slot underneath and the CTOS keyboard (GO, CANCEL, FINISH, HELP,
MARK, BOUND, MOVE, COPY, NEXT / PREV PAGE, SCROLL, f1-f10). SHIFT and CODE latch
for the next key when tapped on their own; the LOCK and OVERTYPE keys light up.

- **Tap the floppy slot** for the disk menu: insert a floppy image (`.imd` or a
  raw `.img`), swap between floppies you've used, eject, and save / load / erase
  the hard disk. Floppies are copied into the app and changes are written back
  to the copy. The winchester (drive d0) lives in the app's storage and starts
  unformatted.
- **Tap the power rocker** on the monitor (or Back) for save/load state, reset
  and quit. Save states include the disks.
- **Installing BTOS 4.0 Standalone**: insert disk 1, type `INSTALL` as the user
  name, press GO, then GO again on the IVolume form. It formats and tests the
  winchester (a few minutes), then asks for disks 2 and 3 (insert each from the
  disk menu, then GO). Afterwards eject the floppy and choose Reset: it boots
  from the hard disk. Leave the user name empty and press GO for the Executive.
- **A real keyboard** works too: Enter = RETURN, Esc = CANCEL, F12 = GO, F11 =
  HELP, Ctrl = CODE, Alt = ACTION, Insert = OVERTYPE, Home = MARK, End = FINISH,
  Page Up / Down = PREV / NEXT PAGE, numeric-pad Enter = NEXT.

The machine has no clock chip: type the date at the sign-on screen.

### Joystick88

Import the MAME `88in1joy` zip. The TV shows the games; under it is the
horseshoe joystick: drag the ball-top stick, tap A / B, the TURBO buttons fire
A or B repeatedly while held, and SELECT / START sit in the middle. The TV's
MENU button (or Back) has save states and reset.

### 500-in-1 Handheld Game

Import the MAME `500in1hh` zip. Turn the phone sideways: the white handheld
has the D-pad on the left, A and B on the right and SELECT / START up top;
tap the strip under the screen (or Back) for the menu. Pick a language, then a
game (right / left page through the list).

### TM 990/189 University Board

Import the MAME `990189` zip; for the video board model import both the
`990189` and `990189v` zips (the second only holds the demo ROMs). Drawn as the
board: chips, user LEDs, IDLE / FWD lights and the red LOAD lever above the
ten-digit display and the 45-key keyboard (shifted signs in yellow above the
keys; SHIFT toggles them and lights the SHIFT lamp). The white chip labelled
MENU (or Back) opens the menu; LOAD restarts the monitor like the real switch.

- University BASIC is up when the display reads `rEAdY`. Type a line and press
  Ret, e.g. `PRINT 1+2`. The display scrolls along the line.
- With the video board, BASIC can draw in colour on the monitor. For the demos:
  `NEW` Ret, `LOAD0` (or 1-3) Ret, `RUN` Ret. The two red buttons by the screen
  are the joystick fire buttons.
- A real keyboard works: letters and digits, Space, Enter = Ret, Shift, and
  `[ ] - = . ,` for `+ - @ > $ ,`.

### 4004 Nixie Clock

Import the MAME `4004clk` zip. Drawn as a home-built nixie clock: six tubes and
two pairs of neon dots over the board with the 4004 in its white ceramic
package, on a walnut base with the **SELECT** and **BROWSE** buttons and the
**TICK-TOCK** and **MAINS** (50 / 60 Hz) switches. Tap the power switch (or
Back) for the menu.

- Like the real clock, it powers up with dark tubes and flashing dots and is
  started with SELECT; PocketPlug does that for you and sets the time and date
  from the phone (24 hours, month-day-year). It's set again from the phone
  every time you come back, as if it had kept running.
- **Displays**: SELECT, then BROWSE shows the display in use; SELECT steps the
  number, BROWSE takes it. 1 date, 2 days to retirement, 3 time, 4 time to
  retirement, 0 cycles through 1-3. Pressing SELECT five times goes to the
  retirement setting. The display and settings are kept between visits.
- A real keyboard: 1 or S = SELECT, 2 or B = BROWSE.

### 1200XL, 130XE and 400 Home Computers

Import the MAME `a1200xl` zip for the 1200XL, `a400` for the 400 (for the PAL
400 import `a400pal` and `a400` too: the floating-point chip is only in `a400`). For the 130XE import the `a130xe`
zip and then the `a800xl` zip as well: the 130XE's OS chip is the 800XL's, and
MAME only keeps it in that zip. The machine is the Atari800 emulator.

Drawn as the computer under a TV, with a one-button joystick and a disk drive in
front:

- **1200XL**: cream case with the metal band of RESET, the POWER / L1 / L2 lights,
  START, SELECT, OPTION, Inverse, BREAK, HELP and F1-F4 above dark brown keys.
  L1 lights when the keyboard is locked (CONTROL + F1), L2 for the international
  characters (CONTROL + F4). With nothing loaded it shows its logo; HELP starts
  the self test.
- **400**: beige case with the raised cartridge housing at the back and the flat
  membrane keyboard (tan keys, orange control keys), with SYSTEM RESET, OPTION,
  SELECT and START in yellow on the right. With nothing loaded it starts in Memo
  Pad. It runs with 48 KB, the common upgrade, so the bigger games fit.
- **130XE**: light grey case with ribs behind the keys, dark grey keys and the
  RESET / OPTION / SELECT / START / HELP column on the right. It starts in BASIC.
- Loading a game switches BASIC off, like holding OPTION on the real machine, as
  games use the memory BASIC sits on; BASIC programs (`.bas` / `.lst`) keep it.
  Removing the cartridge from the menu brings BASIC back.
- SHIFT and CONTROL latch for the next key when tapped on their own. Hold START,
  SELECT or OPTION as long as a game needs.
- **Joystick**: drag the stick (8 ways); the red button is fire. A gamepad's
  D-pad / left stick and A / B / X work too.
- **Tap the cartridge slot or the disk drive** for the disk menu: load a game or
  program (the file type is detected; the computer restarts with it), pick one you
  loaded before, swap the disk in drive 1 for multi-disk games, remove the
  cartridge, eject. Disks are copies in the app's storage and keep what games
  save on them. A raw `.rom` cartridge is matched by size (8 / 16 KB standard,
  32 / 64 / 128 KB XEGS); other boards need a `.car` file.
- **Tap MENU** (or Back) for save / load state, reset and quit. On the 1200XL and
  400 the menu also plugs a BASIC cartridge in or out (a free stand-in, Altirra
  BASIC, as neither had BASIC built in); the computer restarts.
- **A real keyboard** types on the Atari's: Enter = RETURN, Backspace = BACK S,
  Esc, Tab, Caps Lock = CAPS, `'` = `+`, `\` = `*`, `[` `]` = `<` `>`,
  `` ` `` = Inverse, F1-F4, F5 = START, F6 = SELECT, F7 = OPTION, F8 = HELP,
  F9 / Break = BREAK, F10 = RESET. The arrow keys and Alt steer the joystick.

### PC 7300 / 3B1 Desktop

Import the MAME `3b1` zip. Drawn as the desktop: the beige monitor with its green
tube on the system unit (floppy drive at the right of the front, power switch at
the left), a three-button mouse in front, and the long keyboard folded for an
upright phone: the office keys (Exit, Msg, Help, Cancl, Suspd, Rsume, Opts, Cmd,
Close/Open, Print, Clear/Rfrsh, Rstrt/Ref) and the editing keys (Clear Line,
Creat, Save, Undo, Redo, Find, Rplac, Move, Copy, Dlete, Dlete Char, Slect Mark,
Input Mode) are the two rows above F1-F8, and the cursor block (Beg, Home, End,
Page, Prev, Next, the arrows / Roll, Num Lock) is the bottom row. SHIFT and CTRL
latch for the next key when tapped on their own.

- **The screen is the mouse pad**: drag on it to move the pointer, tap it to
  click the left button. Hold the buttons on the drawn mouse for the middle and
  right buttons, or to drag with the left one.
- **Tap the floppy drive** for the disk menu (insert, swap, eject; save / load /
  erase the hard disk). The hard disk is a 64 MB drive (1024 cylinders, 8 heads,
  17 sectors) kept in the app's storage, written as you go; it starts empty.
  Floppies changed by the machine are written back to the app's copy as raw images.
- **Tap the power switch** (or Back) for reset and quit. There are no save
  states: like the real machine, shut UNIX down and boot again.
- **Installing UNIX 3.51** (about 15 minutes; use the replacement disk 8,
  `08_Foundation_Set_Ver_3.51_no_phinit`, so it comes up to the login):
  1. Insert `01_Diagnostic_Disk`, choose Reset, press a key at the configuration
     screen, choose **2** (Initialize Hard Disk), **9** (Miniscribe 64 MB), **No**
     (one user), **Yes** (initialize), then **No** to the surface test.
  2. Insert `02_Floppy_Boot_Disk` and choose **7** (reboot). When asked, insert
     `03_Floppy_File_System`, press a key and answer **yes**.
  3. Insert `04_Hard_Disk_Boot` when asked and press Return, then disks 5 to 12
     one by one, pressing Return after each.
  4. It reboots from the hard disk and asks for the date and time (press Enter).
- **A real keyboard** works too: Enter = Return, numeric-pad Enter = Enter,
  Esc = Esc, F9 = Suspd, F10 = Exit, F11 = Cancl, F12 = Opts, Insert = Cmd,
  Page Up = Help, Page Down = Page, Home / End, Break = Reset Break.

The **MENU** button on the TV (or the system Back gesture) has save/load state,
reset and the scanline toggle.

## To do

- **NGEN 386 models** (`ngen386`, `ngenb38`, `386i`) - parked. They need a 386
  CPU core with protected mode and a different I/O map; MAME only has skeleton
  drivers for them, and testing needs 386-era CTOS / BTOS II system disks
  (ImageDisk dumps), which we don't have yet. The 80186 NGEN is done.
- NGEN speaker sound (how the speaker is driven is still unknown).
- Left out on purpose: MAME's generic 386 / 486 PC motherboards (`386sc`,
  `386sc2c`, `386schg`, `386sxvhcom`, `3siud`, `486apio`, `486ccv`, `486igb21`,
  `486wb6a3`, `4dmshl3g`, `4dmuhl3s`, `4nd04a`). They're plain IBM PC/AT clones (DOS / Windows 3.x),
  better served by a PC emulator such as 86Box.
- Left out on purpose: the AT&T 3B2 minicomputers (`3b2_300`, `3b2_310`,
  `3b2_400`, `3b2_600`). Serial-terminal Unix servers with their own WE32100
  CPU and no screen or keyboard to recreate; SIMH already runs them well.
- Left out on purpose: the ADDS 4000/260 (`4000_260`), a 1995 serial text
  terminal, and the Mera-Elzab 7951om (`7951om`), a 1987 terminal. MAME
  doesn't emulate their serial ports, and without a host computer on the other
  end there's nothing to use.
- Left out on purpose: the SGI 4D/410 (`4d410`, a server MAME can't run), the
  Wavetek 600 Cellular Activation Tester (`600cat`, test equipment), Motorola's
  MC68705 programmer boards (`705p3prg`, `705p5prg`, `705r3prg`, `705u3prg`,
  switches and lights only) and the 68k Single Board Computer (`68ksbc`, used
  only over a serial terminal).
- Tried, not added: TimeTop Chaoji Zhishi Da Fuweng 1 / 2 (`6578cjz1`,
  `6578cjz2`). The SH6578 is supported in the VT core and the quiz runs to its
  title screen exactly as in MAME, but its answer pad talks over an
  undocumented port (MAME has no inputs for it either), so it can't be played.
- 3B1: save states (they'd have to carry the whole hard disk), the serial port
  and the modem / phone line. The dialer's beep and tones play.

## Layout

```
app/src/main/cpp/
  core_api.h            interface every core implements
  core_registry.cpp     machine id -> core + configuration
  common/m6502.*        shared 6502 CPU
  common/s2650.*        shared Signetics 2650 CPU
  cores/vt1682/         V.R. Technology VT1682 (two 6502s, tile/sprite video, DAC sound)
  cores/vc4000/         Interton VC 4000 family / 1292 APVS (2650 + 2636 PVI)
  cores/spg2xx/         SunPlus SPG2xx (u'nSP CPU, tiles/sprites, 16-voice SPU)
  cores/ngen/           Convergent NGEN: 80186 (+ timers, DMA, interrupts), 8259, 8254,
                        8251, 6845 text video, WD2797 / WD1010 disk module
  cores/vt369/          VT NES-on-a-chip family: VT02/03, VT "4FFx", VT369 (+ SH6578)
  cores/tm990/          TM 990/189: TMS9980A (tms9980.h), TMS9901s, TMS9918 (tms9918.h)
  cores/nixie4004/       Intel 4004 CPU (i4004.h) and the 4004 Nixie Clock board
  cores/atari800/       1200XL / 130XE / 400: Atari800 (GPL-2+) as libatari800 in a800/, glue in atari800.cpp
  cores/unixpc/         PC 7300 / 3B1: FreeBee (GPL-3) + Musashi 68010, glue in unixpc.cpp
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
