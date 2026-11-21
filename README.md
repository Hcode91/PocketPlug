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
| A6Plus Mini Arcade (912-in-1, SD card) | `a6plus` | VT369 + SD card controller, SPI flash writes, I2C security chip | Menu and games run, including the ones loaded from the card into flash (MAME's driver does not get past the start) |
| 265-in-1 Handheld | `265games` | SunPlus SPG2xx (u'nSP CPU), 256 MB in 32 banks | Menu and games match MAME pixel-for-pixel in every tested frame; SPU sound (ADPCM, envelopes) matches MAME's level |
| NGEN CP-001 Workstation (Convergent NGEN / Burroughs B26) | `ngen` | Intel 80186 + 8259 / 8254 / 8251, MC6845 text video, WD2797 floppy, WD1010 winchester | Boots BTOS 4.0 from floppy, installs it onto the emulated 10 MB hard disk and boots from there (MAME's driver does not boot) |
| 4004 Nixie Clock (2008 home-built clock) | `4004clk` | Intel 4004 (own core) + 4001 ROM / 4002 RAM I/O | Matches MAME's tube and dot output frame-for-frame through power-on, start and the setting menus; set from the phone's clock; tick-tock sound |
| 500-in-1 Handheld Game | `500in1hh` | VT "4FFx" (NES-on-a-chip, scrambled opcodes), 16 MB | Language select, menu and games match MAME pixel-for-pixel in tested frames (MAME marks it not working, but it runs in both); NES APU sound |
| Joystick88 (Play Vision plug-and-play joystick) | `88in1joy` | VT02 / VT03 | Menu and games match MAME pixel-for-pixel in every tested frame |
| Mephisto Academy chess computer (Hegener + Glaser) | `academy` | W65C02 (own core), HD44780 LCD, magnet sensor board | Boots to its LCD title, plays (book moves, clock display), board LEDs and key LEDs as in MAME |
| Mephisto Almeria 16 Bit / 32 Bit chess modules (Hegener + Glaser) | `alm16`, `alm16a`, `alm32` | Motorola 68000 / 68020 (Musashi), HD44780 LCD, magnet sensor board | All three boot to the same LCD as MAME and answer 1. e2-e4 with e7-e5 (same LEDs and LCD as MAME's alm32) |
| Chibi Pachi: Alien Fever (pachislot keychain) | `alienfev` | Epson E0C6S46 (+1 MHz OSC3), 39x16 dot LCD | Title, menus and spinning reels; HANDLE, MODE, SELECT, SOUND buttons |
| 40 Bonus Games in 1 (AtGames, Firecore Mega Drive on a chip) | `atgame40` | 68000 (Musashi) + Z80 + own Mega Drive VDP + ymfm YM2612 + Sega PSG; the Firecore's flash banking and its extended graphics mode (128 KB VRAM, 6-bit tiles, 256-colour 24-bit palettes) worked out from the games | Menu and 35 of the 40 games show their screens (MAME shows 17); 5 draw only part of the picture |
| Arcade Genius (Millennium 2000 camera handheld, GeneralPlus GPL32600) | `arcadege` | ARM926 interpreter (from DidjDroid) running the NAND's application; NAND controller, interrupt controller, timers, RTC, DMA, GPIO keypad and the 2D PPU (true-colour and indexed layers, sprites) worked out from the application; 32-channel SPG-style SPU | Boots to the menu and the games play with sound effects (MAME doesn't run it); no camera or saving yet |
| 20 SEGA Master System & Game Gear Classics (AtGames plug & play) | `atgsms` | Z80 + SMS / Game Gear VDP (own), Sega PSG, the chip's flash base registers worked out from the menu's launch code | Menu and all 20 games boot (MAME has no driver for it); Game Gear games are scaled to fill the TV picture |
| Bally Professional Arcade / Home Library Computer / Computer System (Astrocade) | `astrocde`, `astrocdl`, `astrocdw` | Z80 + Astrocade data chip (bitmap, function generator, scanline interrupts) and I/O chip (sound, keypad, controllers) | Menu and Gunfight identical to MAME pixel for pixel; the sound follows MAME's; cartridges up to 8 KB |
| Alien Attack tabletop (Coleco, 1981) | `alnattck` | Hitachi HD38800 (HMCS44, own core), VFD drawn from MAME's SVG | Same screen as MAME frame for frame (title, attract); skill / off switch |
| Alien Chase tabletop (Tomy, 1984) | `alnchase` | NEC uPD553C (uCOM-43, own core), two-sided VFD from MAME's SVG | Every element's lit time over 600 frames within a few frames of MAME |
| Alphie robot (Playskool, 1978) | `alphie` | TI TMS1000 (own core) | LED and sound output match MAME with the same presses |
| Amaze-A-Tron (Coleco, 1978) | `amaztron` | TI TMS1100 (own core) | LEDs and digits match MAME with the same presses |
| Arcade Mania (Milton Bradley, 1983) | `arcmania` | TI TMS1100 (own core) | LEDs and sound match MAME with the same presses |
| Arrange Ball (Tandy, 1980) | `arrball` | TI TMS1000 (own core) | Lamps, digits and sound match MAME with the same presses |
| Astro Command tabletop (Epoch, 1982) | `astrocmd` | NEC uPD553C (uCOM-43, own core), VFD from MAME's SVG | Screen follows MAME closely through the attract mode and play |
| Ataka asteroidov (Elektronika IM-53, 1992) | `atakaast` | Kvazar KB1013VK1-2 (Sharp SM5A, own core), LCD from MAME's SVG | Every LCD element matches MAME frame for frame with the same presses |
| Autoslalom (Elektronika IM-23, 1991) | `auslalom` | Kvazar KB1013VK1-2 (Sharp SM5A, own core), LCD from MAME's SVG | Every LCD element matches MAME frame for frame with the same presses |
| Auto Race (Mattel, 1976) | `autorace` | Rockwell B6000 (own core) | Lamps and score match MAME with the same gear and steering |
| Mephisto Amsterdam chess computer (Hegener + Glaser, 1985) | `amsterd` | Motorola 68000 (Musashi), 4-digit LCD module, keypad module, magnet board | LCD and square LEDs as MAME; plays (book moves vary with timing, as on the real one) |
| ARB V2 Sargon 4.0 (AVE Auto Response Board rebuild) | `arbv2` | W65C02S + W65C22S at 16 MHz, magnet board, key and status LEDs | Answers 1. e2-e4 with the square and "computer move" LEDs as MAME |
| Novag Accord chess computer | `accord` | HD6301X0 (MAME's 6301 instruction set, own peripherals) | Starts, takes moves and thinks with the same LEDs as MAME; piezo sound |
| Elektor AVR-Max / AVR-Max-Schachzwerg / ATM18 Mini Chess Computer (2009) | `avrmax`, `avrmaxg`, `atm18mcc` | ATmega88 (own AVR core), 4-digit LED or 4 x 20 HD44780U LCD | Same display as MAME frame for frame through start-up, entering e2-e4 and the computer's reply |
| (Advanced) Voice Chess Challenger (Fidelity, 1979 / 1980) | `vcc`, `avcc` | Z80 + 8255, 4-digit LED, TSI S14001A speech (own ports) | Same digits as MAME through start-up, CL, EN and LV; the speech matches MAME's sound |
| Coleco Adam | `adam` | Z80 (superzazu/z80) + TMS9928A + SN76489A, ADAMnet: master, keyboard, printer and disk drive 6801s (own 6801 port with the serial interface), WD2793 | Boots SmartWriter and types, matching MAME's screen; boots 160 KB disks as MAME does; ColecoVision cartridges run (a homebrew cart matches MAME's frame). No tape drive yet |
| Air-Blaster Joystick (Advance Bright, 2005) | `airblsjs` | Elan EU3A05 (6502 SoC: banked 4 MB ROM, DMA, tile/sprite video, 6 ADPCM channels) | Menu and the games run (tested: the first 3D stage); identical to MAME's frames on the title. Enemy patterns differ run to run (the chip's random register) |
| Altair 8800b (MITS, 1976) | `al8800b` | Intel 8080, front panel (LEDs, address/data and control switches), 88-2SIO terminal, 88-DCDD 8" disks | Front panel works (examine, deposit, run, single step, accumulator switches; the bus lights while running), Kill the Bit built in; terminal and disks as on the turnkey (booting disks uses the turnkey's boot PROM) |
| Altair 8800b Turnkey (MITS, 1977) | `al8800bt` | Intel 8080, turnkey monitor and disk boot PROMs, 88-2SIO terminal (VT100 or ADM-3A codes), 88-DCDD 8" disks | Monitor runs; boots CP/M 2.2, Disk BASIC and their games from SIMH-style `.dsk` images (MAME doesn't run this set) |
| AIM 65 (Rockwell, 1977 microcomputer) | `aim65` | 6502 + two 6522 VIAs, 6532 RIOT, 6821 PIA, five 4-character 16-segment displays, 20-column thermal printer | Monitor runs and prints as in MAME: memory display and change, the printer, text entry (no cassette or TTY) |
| Acorn System 1 (1979 microcomputer kit) | `acrnsys1` | MOS 6502 + INS8154, 8-digit LED display, KB1 keypad | Monitor runs as in MAME: memory display and change, go, step through addresses (no cassette) |
| Entex Adventure Vision | `advision` | Intel 8048 (own MCS-48 port) + COP411 sound CPU, 40-LED spinning-mirror display | BIOS + cartridge; the test cart's picture matches MAME's frame exactly, sound effects follow MAME's timing |
| APF M-1000 / MP1000 | `apfm1000` | Motorola MC6800 (the MC6801 core in 6800 mode) + MC6847 VDG and MC6821 PIA (own ports) | Rocket Patrol from the BIOS; the menu and play frames match MAME pixel for pixel with the same presses |
| Atari Flashback (mini 7800) | `afbm7800` | NES-compatible clone with an MMC3-style mapper (VT02/03 PPU + 2A03 APU, PAL timing as in MAME) | Menu and games match MAME's frames; RESET on the console returns to the menu |
| 4 Player System 10-in-1 (Advance Bright) | `abl4play` | 65C816 TV game SoC ("trkfldch": tile layers, sprites, YUV palette, DMA) | Menu and all games start and play, up to four players; matches MAME's frames. No sound (unknown, as in MAME) |
| Pinball (Advance Bright TV game) (PAL) | `ablpinb` | VT02 / VT03 PAL, plunger and flipper inputs | Logo, table menu and all tables run as in MAME; plunger, flippers and nudge work |
| Super Ping Pong (Advance Bright sensor bat) (PAL) | `ablping` | VT02 / VT03 "sports" PAL + the bat's speech chip handshake | Menus and matches play (MAME's driver stops after the menu); the umpire's voice is silent (its data format is unknown) |
| Double Players Mini Joystick 80-in-1 (Advance Bright) (PAL) | `ablmini` | VT02 / VT03 Waixing PAL, 8 MB, two pads | Menu and games match MAME's frames; two players |
| Joystick 14-in-1 (Advance Bright "Wikid Joystick") (PAL) | `ablwikid` | SH6578 PAL, 2 MB in two halves | Menu and games match MAME's frames |
| Wireless Tennis (Advance Bright) | `abltenni` | SunPlus SPG24x + the wireless racket receiver | Menus match MAME; serving and rallies play with the on-screen racket (MAME's driver can't get past "press the button") |
| Kick Boxing (Advance Bright) | `ablkickb` | SunPlus SPG24x (encrypted ROM) + the limb sensor receiver | Menus, fighter select and fights; punches, kicks and guard from the on-screen gloves and feet (MAME's driver has no working sensors) |
| Millennium Arcade 3D 15-in-1 | `arcade3d` | SunPlus SPG24x (JungleTac encrypted ROM) | Title, game menu and games (Deep Storm) run; not in MAME 0.264 to compare against |
| Mini Arcade Console 10-in-1 (Fizz Creations) | `arcade10` | Sitronix ST2302U (W65C02 core + own SoC port), ST7735 LCD, SPI flash | Boot, menu and games match MAME's current driver frame for frame (built from MAME's source to compare) |
| TM 990/189 University Board (+ Video Board) | `990189`, `990189v` | TMS9980A + 2x TMS9901 (own cores), TMS9918 (own core) | University BASIC runs; the LED display and lights follow MAME frame-for-frame apart from a frame's lag in key echo; the video board's four colour demos run |
| 1200XL Home Computer (Atari) | `a1200xl` | Atari800 (6502C, ANTIC, GTIA, POKEY, PIA) | Real 1200XL OS: logo, self test, function keys and the L1 / L2 lights; loads disks, programs, cartridges and tapes; optional BASIC cartridge |
| 800 Home Computer (Atari) | `a800`, `a800pal` | Atari800, 48 KB | Real OS: Memo Pad; loads disks, programs, cartridges and tapes; optional BASIC cartridge |
| 800XL / 600XL Home Computers (Atari) | `a800xl`, `a800xlp`, `a600xl` | Atari800, 64 KB | BASIC built in; loads disks, programs, cartridges and tapes |
| 65XE / 800XE Home Computers (Atari) | `a65xe`, `a800xe` | Atari800, 64 KB | BASIC built in; loads disks, programs, cartridges and tapes |
| Archimedes family (Acorn): 305, 310, 440, 410/1, 540, A3000, A3010 (UK / German), A3020, A4000, A5000, A4 laptop, A500 | `aa305`, `aa310`, `aa440`, `aa4101`, `aa540`, `aa3000`, `aa3010`, `aa3010_de`, `aa3020`, `aa4000`, `aa5000`, `aa4`, `aa500` | Arculator (ARM2 / ARM250 / ARM3, MEMC, VIDC, IOC, IOEB, 82C711, WD1772) | Arthur, RISC OS 2 and RISC OS 3 start to the desktop, chosen in the menu; floppies, HostFS, and MAME's RISC OS 3.11 hard disk on the IDE machines (MAME's drivers don't run) |
| A7000 / A7000+ Desktop Computers (Acorn) | `a7000`, `a7000p` | RPCEmu (ARM7500 / ARM7500FE, IOMD, VIDC20), 32 MB | RISC OS 3.60 / 3.71 start to the desktop; mouse, keyboard, floppies (.adf) and HostFS |
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
| A6Plus Mini Arcade | `a6plus` | `w25q64jv.u3` | 8 MB (8,388,608 bytes) | `c62f9570` |
| | | `a6plus.chd` (the SD card, imported on its own) | about 95 MB (CHD v5) | data SHA1 `ce8fb234` |
| 265-in-1 Handheld | `265games` | `265games.bin` | 256 MB (268,435,456 bytes) | `761c92cb` |
| NGEN CP-001 Workstation | `ngen` | `72-00414_80186_cpu.bin` | 4 KB (4,096 bytes) | `e1387a03` |
| | | `72-00415_80186_cpu.bin` | 4 KB (4,096 bytes) | `a6dde7d9` |
| | | `72-00422_10mb_disk.bin` | 4 KB (4,096 bytes) | `f5b046b6` |
| | | Plus your own BTOS / CTOS system floppies (`.imd` ImageDisk, or raw `.img`), inserted from the disk menu | - | - |
| 4004 Nixie Clock | `4004clk` | `clock.u30` to `clock.u45` (16 files) | 256 bytes each | `3d8608a5` ... `a8d419ef` (checked one by one) |
| 500-in-1 Handheld Game | `500in1hh` | `500in1.bin` | 16 MB (16,777,216 bytes) | `50f44c1a` |
| Joystick88 | `88in1joy` | `88in1joystick.bin` | 4 MB (4,194,304 bytes) | `86b8d819` |
| Mephisto Academy | `academy` | `academy_engl.16k`, `academy_engl.32k` | 32 KB each | `ebda6674`, `a967922b` |
| | | `hd44780_a00.bin` (from MAME's `hd44780` zip) | 4 KB (4,096 bytes) | `e459877c` |
| Mephisto Almeria 32 Bit | `alm32` | `alm32.bin` + `hd44780_a00.bin` | 128 KB (131,072 bytes) | `38f4b305` |
| Mephisto Almeria 16 Bit | `alm16` | `almeria_16bit_v0.13_even`, `..._odd` + `hd44780_a00.bin` | 64 KB each | `ee5b6ec4`, `d0be4ee4` |
| Mephisto Almeria 16 Bit (v0.121) | `alm16a` | `almeria_16bit_v0.121_even`, `..._odd` + `hd44780_a00.bin` | 64 KB each | `3ab8fd3b`, `436c1d85` |
| Chibi Pachi: Alien Fever | `alienfev` | `alienfev.bin` | 12 KB (12,288 bytes) | `e561599c` |
| AtGames 40 Bonus Games in 1 | `atgame40` | `40bonusgamesin1.bin` | 16 MB | `4eba6e83` |
| AtGames SMS / GG plug & play | `atgsms` | `atgames_sms.u5` | 4 MB | `bf23cd7d` |
| Arcade Genius | `arcadege` | `tc58nvg0s3eta00.u3` | 132 MB | `9b4db25e` |
| Bally Astrocade BIOS | `astrocde` / `astrocdl` / `astrocdw` | `astro.bin` / `ballyhlc.bin` / `bioswhit.bin` | 8 KB | `ebc77f3a` / `d7c517ba` / `6eb53e79` |
| Alien Attack | `alnattck` | `hd38800a25`, `alnattck.svg` | 4,352 / 564,266 bytes | `18b50869`, `386bf327` |
| Alien Chase | `alnchase` | `d553c_258`, `alnchase.svg` | 2,048 / 576,555 bytes | `c5284ff5`, `b2c1734b` |
| Alphie | `alphie` | `us4280809`, `tms1000_common2_micro.pla` | 1,024 / 867 bytes | `f8f14013`, `d33da3cf` |
| Amaze-A-Tron | `amaztron` | `mp3405`, `tms1100_common3_micro.pla`, `tms1100_amaztron_output.pla` | 2,048 / 867 / 365 bytes | `9cbc0009`, `03574895`, `f3875384` |
| Arcade Mania | `arcmania` | `m34078a`, `tms1100_common1_micro.pla`, `tms1100_arcmania_output.pla` | 2,048 / 867 / 365 bytes | `90ea0087`, `62445fc9`, `a1517b15` |
| Arrange Ball | `arrball` | `mp0166`, `tms1000_common2_micro.pla`, `tms1000_arrball_output.pla` | 1,024 / 867 / 365 bytes | `a78694db`, `d33da3cf`, `ffc206fb` |
| Astro Command | `astrocmd` | `d553c_202.s01`, `astrocmd.svg` | 2,048 / 335,380 bytes | `b4b34883`, `950af2c4` |
| Ataka asteroidov | `atakaast` | `im-53.bin` (= `mc-25` from the `gnw_mmouse` zip), `atakaast.svg` | 1,856 / 105,570 bytes | `cb820c32`, `3d79aacc` |
| Autoslalom | `auslalom` | `im-23.bin`, `auslalom.svg` | 1,856 / 117,520 bytes | `3b6e726f`, `2f90fd4c` |
| Auto Race | `autorace` | `b6000ca` | 512 bytes | `a112c928` |
| Mephisto Amsterdam | `amsterd` | `amsterda-u.bin`, `amsterda-l.bin` | 32 KB each | `0a75514e`, `6e17d8fa` |
| ARB V2 | `arbv2` | `sargon_4.0` | 64 KB | `c519c9e8` |
| Novag Accord | `accord` | `novag_875_31x0b87p` | 4 KB (4,096 bytes) | `4d77b7db` |
| AVR-Max (and Schachzwerg) | `avrmax`, `avrmaxg` | `elektor_081101-41.ic1` / `elektor_081101-41_d.ic1` | 8 KB | `86d2a654` / `18ec7a56` |
| ATM18 Mini Chess Computer | `atm18mcc` | `avrmax_cc2_en-v1.0.bin`, `hd44780u_a02.bin` (from the `hd44780u` zip) | 8 / 4 KB | `715f4642`, `6d522b42` |
| Voice Chess Challenger | `vcc` | `cn19256n_101-32013`, `cn19174n_vcc_2`, `cn19175n_vcc_3`, `101-32107` | 4 KB each | `257bb5ab`, `f33095e7`, `624f0cd5`, `f35784f9` |
| Advanced Voice Chess Challenger | `avcc` | `101-64017`, `101-32010` (= `cn19175n_vcc_3`), `101-32107` (both from the `vcc` zip) | 8 / 4 / 4 KB | `f1133abf`, `624f0cd5`, `f35784f9` |
| Coleco Adam | `adam` | `os7.u2`, `eos 6 rev 57 08dd.u22`, `wp_r80.rom` (adam zip) | 8 KB, 16 KB, 32 KB | `3aa93ef3`, `ef6403c5`, `58d86a2a` |
| | | `master rev a 174b.u6` (adam zip), `keyboard.u2` (adam_kb), `printer.u2` (adam_prn) | 2 KB each | `035a7a3d`, `ef204746`, `e8db783b` |
| | | `adam disk u10 ad 31 rev a 09-27-84.u10` (adam_fdc) | 4 KB | `4b0b7143` |
| Air-Blaster Joystick | `airblsjs` | `airblsjs.bin` | 4 MB (4,194,304 bytes) | `d10a6a84` |
| Altair 8800b | `al8800b` | `8800b front panel.bin` | 256 bytes | `8b462c1b` |
| Altair 8800b Turnkey | `al8800bt` | `turnmon.bin`, `88dskrom.bin` | 256 bytes each | `5c629294`, `7c5232f3` |
| AIM 65 | `aim65` | `aim65mon.z23`, `aim65mon.z22` | 4 KB each | `90e44afe`, `d01914b0` |
| Acorn System 1 | `acrnsys1` | `acrnsys1.bin` | 512 bytes | `43dcfc16` |
| Adventure Vision | `advision` | BIOS: `ins8048-11kdp_n.u5`, `cop411l-kcn_n.u8` | 1 KB, 512 bytes | `279e33d1`, `81e95975` |
| MP1000 | `apfm1000` | BIOS: `apf_4000.rom`; cartridges optional | 2 KB | `cc6ac840` |
| | | Plus your own cartridge dumps (4 KB each), added as games | - | - |
| Atari Flashback | `afbm7800` | `atariflashbackmini7800.bin` | 1 MB (1,048,576 bytes) | `da4d9483` |
| 4 Player System 10-in-1 | `abl4play` | `abl4play.bin` | 8 MB (8,388,608 bytes) | `5d57fb70` |
| Pinball | `ablpinb` | `abl_pinball.bin` | 2 MB (2,097,152 bytes) | `b2ce20fb` |
| Super Ping Pong | `ablping` | `abl_pingpong.bin` | 2 MB (2,097,152 bytes) | `b31de1fb` |
| Double Players Mini Joystick 80-in-1 | `ablmini` | `ablmini.bin` | 8 MB (8,388,608 bytes) | `e65a2c3a` |
| Joystick 14-in-1 | `ablwikid` | `mx29f1610atc.u2` | 2 MB (2,097,152 bytes) | `f16abf79` |
| Wireless Tennis | `abltenni` | `ablpnpwirelesstennis.bin` | 4 MB (4,194,304 bytes) | `66bd8ef1` |
| Kick Boxing | `ablkickb` | `ablkickboxing.bin` | 8 MB (8,388,608 bytes) | `61394c45` |
| Arcade 3D 15-in-1 | `arcade3d` | `arcade3d.u3` | 8 MB (8,388,608 bytes) | `130843a5` |
| Mini Arcade Console 10-in-1 | `arcade10` | `st2x_internal_type1.bin`, `25q40.bin` | 8 KB, 512 KB | `f4dc1fc2`, `62784666` |
| TM 990/189 University Board | `990189` | `990-469.u32` | 2 KB (2,048 bytes) | `08df7edb` |
| | | `990-469.u33` | 4 KB (4,096 bytes) | `e9b4ac1b` |
| TM 990/189 + Video Board | `990189v` | the two `990189` files, plus `demo1000.u13` | 2 KB (2,048 bytes) | `c0e16685` |
| | | `demo1800.u11` | 2 KB (2,048 bytes) | `8737dc4b` |
| 1200XL Home Computer | `a1200xl` | `co60616b.rom` | 8 KB (8,192 bytes) | `6e29ec8d` |
| | | `co60617b.rom` | 8 KB (8,192 bytes) | `d73ce29a` |
| 800 Home Computer | `a800` | the three `a400` files: `co12399b.rom`, `co12499b.rom`, `co14599b.rom` | 2 + 4 + 4 KB | as `a400` |
| 800 Home Computer (PAL) | `a800pal` | `co12399b.rom` (in the `a800` zip: import that too), `co15199.rom`, `co15299.rom` | 2 + 4 + 4 KB | as `a400pal` |
| 800XL / 800XL (PAL) | `a800xl`, `a800xlp` | `co60302a.rom` (BASIC) | 8 KB (8,192 bytes) | `f0202fb3` |
| | | `co61598b.rom` (OS) | 16 KB (16,384 bytes) | `1f9cd270` |
| 600XL | `a600xl` | `co62024.rom` (OS) | 16 KB (16,384 bytes) | `643bcc98` |
| | | `co60302a.rom` (BASIC; in the `a800xl` zip: import that too) | 8 KB (8,192 bytes) | `f0202fb3` |
| 65XE | `a65xe` | `co24947a.rom` (BASIC) | 8 KB (8,192 bytes) | `7d684184` |
| | | `co61598b.rom` (OS; in the `a800xl` zip: import that too) | 16 KB (16,384 bytes) | `1f9cd270` |
| 800XE | `a800xe` | `co24947a.rom` (BASIC) | 8 KB (8,192 bytes) | `7d684184` |
| | | `c300717.rom` (OS) | 16 KB (16,384 bytes) | `29f133f7` |
| A7000 | `a7000` | `1203,101-01.bin` | 2 MB (2,097,152 bytes) | `2eeded56` |
| | | `1203,102-01.bin` | 2 MB (2,097,152 bytes) | `6db87d21` |
| A7000+ | `a7000p` | `1203,261-01.bin` | 2 MB (2,097,152 bytes) | `8e3c570a` |
| | | `1203,262-01.bin` | 2 MB (2,097,152 bytes) | `cf4615b4` |
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

## Starting a machine from another app

Launchers and trackers (such as BacklogQuest's home-screen tiles) can start a machine directly:

```
action:  com.hcode91.pocket.LAUNCH_GAME
target:  com.hcode91.pocketplug (its launcher activity)
extra:   game_id = the set name, e.g. "168pcase"
         or, for a cartridge machine, "set/cartridge title", e.g. "1292apvs/Grand Prix"
```

If the machine's files aren't imported yet, or a cartridge machine has more than one cartridge and
none was named, its page opens instead so you can sort it out there.

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

### The machine list

Machines you can play come first ("Ready to play"), then the ones still
needing ROMs. Search by name, year or chip, or filter by kind (TV games,
handhelds, pets, consoles, computers, chess & clocks). Each tile shows the
machine as drawn in its own skin; tap it for its page (description, ROM
status, import, play) or tap ▶ to play straight away. ☆ pins a machine to
Favourites; the last eight machines played sit in Recently played. Multi-game
units list their games on their page, as their own menus show them (read off
the menus in the emulator), and searching for a game finds the unit it's in. Find my
ROMs, Sort a folder and About are in the ⋮ menu.

### Find my ROMs

The quickest way to set machines up: tap **⋮ → Find my ROMs** and pick the folder
your ROMs are in. PocketPlug looks through it and every subfolder for the files
each machine needs, by checksum and size, so names and folder layout don't
matter: MAME zips (and zips inside zips), 7z files, loose files and CHD images
are all found. Archives are only listed, not unpacked, and loose files are only
read when their size is one a machine needs.

You get a list of machines that are ready to set up and ones still missing
files (with the missing names); one tap copies the found files in. Files over
100 MB can stay in your folder instead (the machine then reads them from there,
and stops working if they're moved, until you rescan). The folder is
remembered: tap Find my ROMs again to rescan; only new or changed files are read.

### Chess computers (Mephisto Academy, Mephisto Almeria, Novag Accord)

Import the MAME zip (`academy` and the Almerias also need MAME's `hd44780` zip, the LCD's
character ROM). Play on the board as on the real one: tap a piece to lift it
(it fades and shows in HAND), then tap the square to put it down; tapping an
occupied square captures. Under the board: spawn a new piece by tapping it in
the panel then a square, REMOVE drops the piece in hand, RESET puts all pieces
back, CLEAR empties the board, MENU has save states and reset.

- Academy: the computer shows its moves with the LEDs in the squares and on
  the LCD; ENT and CL together start a new game.
- Almeria: the display module under the board has the LCD (German texts)
  and six keys: ENT makes the computer move, CL clears, the arrows step
  through the menus. Its moves light the two squares' LEDs.
- Accord: press-sensory, so you also "press" the computer's pieces to move
  them. Its moves light the rank and file LEDs; GO makes it move.

### Coleco Adam

Import MAME's `adam`, `adam_kb`, `adam_prn` and `adam_fdc` zips (one at a
time, or a zip that holds them all). With nothing plugged in it starts SmartWriter, the
electronic typewriter. The keyboard is under the TV; SHIFT and CONTROL stay
down for the next key when tapped alone, and a hardware keyboard works too
(F1-F6 are the smart keys I-VI). Tap the console's front for disks and
cartridges:

- Disks: 160 KB `.dsk` images go in the disk drive and the computer restarts
  to boot them (the light by the bays shows the drive working). Changes are
  written back to the copy in the app's storage.
- Cartridges: ColecoVision `.col` / `.rom` / `.bin`, up to 32 KB. The machine
  restarts in game mode and the hand controller (joystick, the two side
  buttons, keypad) replaces the keyboard; the CONTROLLER / KEYBOARD switch
  flips between them. Pulling the cartridge out goes back to the computer.

Not done yet: the tape drive (`.ddp`), the bigger third-party disk drives and
megacarts.

### Air-Blaster Joystick

Import the MAME zip. The TV is at the top, the flight stick under it: put a
finger on the grip and push it to fly (the whole stick steers, as on the real
one), or use the hat switch on top. TRIGGER (on the front of the grip)
fires, MISSILE fires a missile, START and PAUSE are on the head, RESET is on
the base. Tap the TV for the menu. The game itself is known to be buggy on
the real unit (bosses, the refuel jet in the 3D stages).

### Altair 8800b and 8800b Turnkey

Import the MAME zips. The turnkey starts in its monitor on the terminal (a
`.` prompt; commands are upper case and addresses octal, ended with a space:
`M000100 ` shows and changes memory, `J177400 ` jumps to the disk boot
loader). Tap the disk unit to add disk images: 8" Altair `.dsk` files in the
SIMH layout (337,568 bytes), like the CP/M, Disk BASIC and games disks that
come with the Altair-Duino project. "Drive A and boot it" restarts and boots.
Most of those disks are set up for a VT100 terminal; the menu switches the
terminal to ADM-3A codes for software that wants one. `.bin` programs load at
0000 and run.

The 8800b starts on its front panel, stopped. Tap the address/data switches
to set them; push a control switch up or down for its upper or lower job
(EXAMINE, DEPOSIT NEXT, RUN, STOP...). Kill the Bit is in the disk menu.
TERMINAL shows the terminal and keyboard (FRONT PANEL goes back). Booting a
disk on the 8800b needs the turnkey's ROMs imported too (its disk boot
loader is a PROM card in the 8800b). The panel's own microcode PROM isn't
run: the panel's behaviour is built in.

Not done yet: the second 2SIO port, cassettes, interrupts other than the
2SIO's, 5.25" minidisks.

### AIM 65

Import the MAME zip. The unit is drawn whole: the thermal printer's paper on
the left, the 20-character display on the right and the full keyboard below.
The monitor takes one-letter commands: `M` and an address shows memory (Space
for the next bytes), `PRINT` turns the printer on and off, `I` enters text, `F1`
-`F3` call user routines. SHIFT and CTRL stay down until the next key. RESET
restarts the monitor; MENU is next to it. A hardware keyboard works too (F4 or
Insert for PRINT, `=` for the `: *` key).

Not done yet: the cassette interface, the TTY port and the optional ROMs
(BASIC, assembler) in the sockets.

### Acorn System 1

Import the MAME zip. The screen shows the board itself: the bubble LED
display (the left digit isn't wired on the real one either) and the KB1
keypad. Press `m`, type a four-digit address and press `m` again to see what's
there; type two digits to change it, and the up / down keys step through
memory. `g` runs a program from an address, `rst` resets. With a keyboard:
0-9, a-f, M (or -), G, P, S, L, R, arrow keys and F3 for rst.

### Adventure Vision

Import the `advision` zip first (the BIOS), then add your cartridge dumps with
Add games (raw 4 KB files, zips, or MAME's software-list zip). The red LED
picture appears in the window; the joystick is in the middle, with the four
buttons on both sides (the left and right panels work the same, as on the
real one). The sound switch mutes; tap the top band for the menu.

### APF MP1000

- Rocket Patrol is built into the console: import the apfm1000 BIOS zip and play it from the
  machine page. Cartridge dumps (or the apfm1000 software-list zip) add the other games; Space
  Destroyers' two chips are joined into one cartridge automatically.
- Each controller has an 8-way joystick, the red fire button on its top edge and the keypad
  (7 8 9 0 / 4 5 6 CL / 1 2 3 EN). The fire button and EN are the same key. The left controller
  is player 1. A gamepad plays player 1: d-pad, A/B fire, X/Y keys 1 and 2, number keys on a keyboard.

### Atari Flashback

Import the MAME zip. The TV shows the game; under it are the joystick (stick,
an orange fire button on each top corner, PAUSE and SELECT) and the console,
whose RESET goes back to the game menu. Fire picks a game in the menu.

### Advance Bright TV games

Import each MAME zip (`abl4play`, `ablpinb`, `ablping`, `ablmini`, `ablwikid`, `abltenni`,
`ablkickb`). The TV shows the game; under it is the controller the box came
with. Tap the TV (or Back) for the menu.

- Pinball: the red domes on each side are the flippers; hold PULL to draw the
  plunger back (longer = harder) and let go to launch. NUDGE shakes the table.
- Super Ping Pong: tap the bat's red rubber to swing (it is also OK in the
  menus); the handle has up, down and cancel.
- 4 Player System: four pads, two by two; player 1's has START and SELECT.
  START picks a game and confirms; left / right move along the player and
  START buttons on the player-select screen.
- Mini Joystick 80-in-1: one joystick per player, each with A, B, SELECT and START.
- Joystick 14-in-1: the horseshoe joystick, as for Joystick88.
- Wireless Tennis: the receiver's arrows and OK work the menus. In a match, tap
  the yellow SERVE button on the racket handle to toss, then tap or swipe the
  strings to swing.
- Kick Boxing: tap a glove to punch, a foot to kick, GUARD to block; the
  receiver's arrows and OK work the menus.

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

### Sorting a folder of ROMs

**⋮ → Sort a folder** on the machine list sorts any folder you pick
(Download, say) into a `Sorted` folder inside it, one folder per system:

- `Computers/<machine> [set]`, `Consoles and plug-and-play/<machine> [set]`,
  `Other systems/...`, `Arcade`, and `Devices and BIOS` for shared ROMs
  (keyboards and the like): MAME sets, recognised by the CRC32s of the chips
  inside each zip / 7z (read from the archive's own index, so nothing is
  unpacked), including zips of zips; the set the file is named after wins a
  tie. MAME hard disks (CHD) by their SHA1; one used by several machines goes
  to `Shared disks`.
- `Games/<system>`: console games and computer disks by their headers (iNES,
  the Game Boy / GBA logos, SEGA, TMR SEGA, Lynx, Atari 7800, ATR, Amiga
  "DOS", WOZ, ...), discs by their boot sector or ISO 9660 system name
  (PlayStation, PS2, PSP, Saturn, Dreamcast, Mega-CD, PC Engine CD, GameCube,
  Wii, CD-i), and otherwise by unambiguous extensions. A `.cue` / `.gdi` /
  `.m3u` takes its tracks with it.
- Disc CHDs: first by their SHA1 against MAME's software lists (about 11,600
  CD / GD-ROM / DVD / hard disk images: PlayStation, Saturn, Dreamcast,
  Mega-CD, PC Engine CD, CD-i, FM Towns, CD32 ...), which names the exact game
  too; otherwise the disc inside is read (the CHD reader decodes the cdlz /
  cdzl / cdfl CD codecs) and identified by its boot sector like a loose image.

Nothing moves until you've seen where everything will go; files it can't
identify stay where they are, and a name already taken in the target folder
goes to `Sorted/Duplicates`. ★ marks sets PocketPlug runs. The index
(`app/src/main/assets/romindex.pprx`, 39,000 machines and 260,000 chips) is
made from MAME's source by `tools/mkromindex.py`; it holds only names, years,
makers and checksums.

### A6Plus Mini Arcade

Import the MAME `a6plus` zip (`w25q64jv.u3`), then import `a6plus.chd`, the
SD card image, as a second file. The card stays compressed: PocketPlug reads
it straight from the CHD (lzma, zlib, huff and flac hunks), so nothing is
unpacked to storage. Drawn as a white and blue mini cabinet with a red
ball-top stick, eight red buttons (**B** / **A** and turbo **TB** / **TA**,
twice so either hand reaches them) and small **SELECT**, **START** and
**RESET** buttons. Tap the marquee (or Back) for the menu.

Pick 2 ENGLISH for the English list. Most of the 912 games are on the card:
choosing one shows a loading screen while the menu copies it into the flash
chip, as on the real machine, and RESET goes back to the list. Only the copy
in memory changes, never the imported ROM. Save states keep the copied game.

What it needed beyond MAME: the SD card controller (registers 4310-434f,
worked out from the menu's code), the SPI flash controller's erase and
page-program DMA (4350-435f), and the small I2C chip on port 414b that hands
the games 60 bytes of tiny routines at start-up (pad, sprite and PPU address
access). That chip isn't dumped; its contents are rebuilt from how the games
call it. The games also set bit 4 of video register 201e, which on this unit
keeps the NES palette.

### Acorn Archimedes family

Import the MAME zip for the machine: `aa310` covers the 305, 310, 440, 540 and
A3000 too (the A3000's RISC OS chips are in it; MAME's `a3000` is the Amiga
3000, not this). The A3020, A4000, A5000 and A4 also take
`riscos311_apps.chd`, MAME's 64 MB hard disk with the RISC OS 3.11
applications, as a second file: it's unpacked once into a writable disk, shown
as IDEDisc4 (PocketPlug sets *Configure IDEDiscs 1 for these machines).

The machine is Arculator (Sarah Walker's Archimedes emulator), each model set
up after its own presets: CPU (ARM2, ARM250 or ARM3 at 24 / 25 / 26 MHz),
MEMC, memory, and old IO (WD1772)
or new IO (82C711, IDE). Every model starts with 4 MB, the usual upgrade, as many
games won't start in less (Aggressor asks for 496 KB free, more than a 1 MB
machine has under RISC OS 2); the menu's **Memory** goes back to the memory it
was sold with. The menu's **Operating system** picks among the ROMs
each machine shipped with or was upgraded to: Arthur 0.30 / 1.20 and RISC OS
2.00 / 2.01 on the 305, 310 and 440; RISC OS 2.00 to 3.11 on the 410/1,
540 and A3000; 3.00 to 3.11 on the A5000. The A3010s, A3020, A4000 and A4 have
their own RISC OS 3.1 chips.

Drawn after the real ones, without names or logos: the 300 / 400 / 500 series
as the low beige box under its monitor with the orange-red function keys under
a clear strip; the A4000 and A5000 as their flat boxes; the A3000 (red keys),
A3010 (green keys) and A3020 (red keys) as the computer in the keyboard, under
a monitor; the A4 as the dark grey laptop with its LCD. The screen is the mouse
pad as on the A7000; the floppy slot opens disks & files (.adf, .apd, .hfe,
.jfd, .scp floppies and HostFS), the plate marked MENU the emulator menu.

Arculator's floppy timing follows its drive-sound samples, which aren't
included; silent stand-ins of the same lengths keep the timing.

### Atari 8-bit Home Computers (400, 800, 600XL, 800XL, 1200XL, 65XE, 130XE, 800XE)

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
- **800**: the 400's beige case with the wider cartridge housing (two slots),
  brown full-travel keys and gold console keys. Memo Pad when empty; 48 KB.
- **800XL / 600XL**: beige top with louvres over the dark keyboard (brown on the
  800XL, black on the 600XL), with the silver RESET / OPTION / SELECT / START /
  HELP keys down the right, named beside them. BASIC built in. The 600XL runs
  with 64 KB (the 1064 expansion) so XL software fits.
- **65XE / 800XE / 130XE**: light grey case with slanted ribs, light keys and
  the five console keys (HELP, START, SELECT, OPTION, RESET) at the top right.
  BASIC built in. (The 130XE skin was redrawn to match: it has the same case.)
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

### A7000 and A7000+ Desktop Computers

Import the MAME `a7000` or `a7000p` zip (the `rpc600` and keyboard zips aren't
needed). The machine is RPCEmu (interpreter build); RISC OS 3.60 (A7000) or
3.71 (A7000+) starts straight to the desktop with 32 MB of memory.

Drawn as the desktop: the light grey monitor on the slim system unit (floppy
drive, CD drive, power light), the keyboard and the three-button mouse.

- **The screen is the mouse pad**: drag to move the pointer, tap to click
  SELECT. Hold the mouse's buttons for SELECT, **MENU** (RISC OS has no menu
  bars: menus pop up on the middle button) and ADJUST.
- **Tap the floppy drive** for disks and files: insert a floppy image (`.adf`),
  swap between ones you've used, eject, or **copy a file into HostFS**. HostFS is
  a folder in the app's storage that RISC OS sees as a drive: click HostFS on the
  icon bar. Common extensions get their RISC OS file type (`.zip` Archive,
  `.txt` Text, `.bas` BASIC, `.jpg`, `.gif`, `.png`, `.htm`); a name with
  `,xxx` keeps that type. Files saved to HostFS are kept between visits.
- **MENU on the unit** (or Back) for reset and quit. No save states: RPCEmu has
  none.
- The clock follows the instructions run (32 MHz, 48 MHz on the A7000+): on a
  slow phone RISC OS runs slower rather than skipping.
- A real keyboard works: Esc, F1-F12, Insert / Home / Page Up / Delete / End
  (Copy) / Page Down and the arrows, as on the Acorn keyboard.

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
- Tried, not added: Retro Arcade FC A6Plus (`a6plus`). Its games live on an SD
  card that isn't in the MAME set (`a6plus.chd`, and MAME doesn't emulate the
  card either); with the flash chip alone it crashes at start.
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
  cores/acorn/          A7000 / A7000+: RPCEmu (GPL-2+) in rpcemu/, host side in rpcemu_glue.c, acorn.cpp
  cores/atari800/       Atari 8-bit computers: Atari800 (GPL-2+) as libatari800 in a800/, glue in atari800.cpp
  cores/unixpc/         PC 7300 / 3B1: FreeBee (GPL-3) + Musashi 68010, glue in unixpc.cpp
                        (Musashi is shared with the Almeria core through freebee/m68k_bus.c)
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
