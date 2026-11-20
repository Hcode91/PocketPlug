# Licensing

PocketPlug's own code and the MAME-derived parts are under the **GNU General
Public License, version 2 or (at your option) any later version** (`../LICENSE`).
The 3B1 core includes FreeBee, which is GPL version 3 only, so the app as built
(the APK and the native library) is distributed under the **GNU General Public
License, version 3** (`GPL-3.0.txt`), as the "or any later version" terms of the
other parts allow.

Individual files keep their own SPDX tags:

| Files | Licence | Origin |
|---|---|---|
| `app/src/main/cpp/cores/vc4000/*` | GPL-2.0-or-later | MAME `src/mame/interton/vc4000*` (Peter Trauner, Manfred Schneider, Robbbert) |
| `app/src/main/cpp/cores/vt1682/*` | BSD-3-Clause | MAME `src/mame/nintendo/vt1682*` (David Haywood) - `MAME-vt1682.txt` |
| `app/src/main/cpp/common/s2650.*` | BSD-3-Clause | MAME `src/devices/cpu/s2650` (Juergen Buchmueller) |
| `app/src/main/cpp/cores/e0c6s46/*` | BSD-3-Clause | MAME `src/devices/cpu/e0c6200` (hap) - `MAME-e0c6200.txt` |
| `app/src/main/cpp/cores/vt369/*` | GPL-2.0-or-later | MAME VT SoC + PPU incl. VT02/03, VT "4FFx", SH6578 and the nes_clone.cpp afbm7800 mapper (BSD-3, David Haywood, Ernesto Corvi, Brad Oliver, Fabio Priuli), VT3xx sound CPU (BSD-3, David Haywood), VT369 ADPCM decoder (GPL-2.0+, NewRisingSun) and NES APU (GPL-2.0+, Matthew Conte) |
| `app/src/main/cpp/common/hd6301*` | BSD-3-Clause | MAME 6800/6801 instruction set (Aaron Giles), peripherals after MAME's hd6301x |
| `app/src/main/cpp/common/hd44780.*` | BSD-3-Clause | Port of MAME's HD44780 (Sandro Ronco) |
| `app/src/main/cpp/common/chessboard.h` | BSD-3-Clause | Logic of MAME's sensorboard (hap) |
| `app/src/main/cpp/common/w65c02.*` | GPL-2.0-or-later | Own W65C02S core |
| `app/src/main/cpp/cores/hh/*` | GPL-2.0-or-later | Ports of MAME's handheld/hh_hmcs40.cpp, hh_ucom4.cpp, hh_tms1k.cpp (hap), BSD-3 |
| `app/src/main/cpp/common/hmcs40.*`, `ucom4.*`, `tms1k.*`, `pwmdisplay.h`, `svgscreen.*` | BSD-3-Clause | Ports of MAME's cpu/hmcs40, cpu/ucom4, cpu/tms1000 (Wilbert Pol, hap), video/pwm and screen_svg (hap, Olivier Galibert) |
| `app/src/main/cpp/common/nanosvg/*` | Zlib | NanoSVG (Mikko Mononen), MAME's copy with shape titles - `nanosvg/LICENSE.txt` |
| `app/src/main/cpp/cores/chess/*` | GPL-2.0-or-later | Ports of MAME's academy.cpp, amsterdam.cpp, mdisplay1, chess/ave_arb.cpp (hap), modular.cpp (Dirk Verwiebe, Cowering, Sandro Ronco, hap) / mboard / mdisplay2 (Sandro Ronco, hap) and novag/accord.cpp (hap), BSD-3 |
| `app/src/main/cpp/common/z80/*` | MIT | superzazu/z80 (Nicolas Allemand), compiled as C++ - `common/z80/LICENSE` |
| `app/src/main/cpp/common/m6801*` | BSD-3-Clause | MAME 6800/6801 instruction set (Aaron Giles) and m6801 peripherals incl. the serial interface |
| `app/src/main/cpp/common/tms9928a.h` | BSD-3-Clause | PocketPlug (the TM 990 core's 9918 plus graphics II and the mixed modes, after MAME's tms9928a.cpp) |
| `app/src/main/cpp/common/sn76489.h` | BSD-3-Clause | Port of MAME's sn76496.cpp (Nicola Salmoria et al.) |
| `app/src/main/cpp/cores/adam/*` | GPL-2.0-or-later | Port of MAME's coleco/adam.cpp and bus/adamnet (Curt Coder), BSD-3 |
| `app/src/main/cpp/common/mcs48.*` | BSD-3-Clause | Port of MAME's MCS-48 core (Dan Boris, Mirko Buffoni, Aaron Giles, Chris Kirmse) |
| `app/src/main/cpp/common/cop410.*` | BSD-3-Clause | Port of MAME's COP400 core, COP410 subset (Curt Coder) |
| `app/src/main/cpp/cores/advision/*` | GPL-2.0-or-later | Port of MAME's entex/advision.cpp (Dan Boris, hap), BSD-3 |
| `app/src/main/cpp/cores/kits/*` | GPL-2.0-or-later | Ports of MAME's acorn/acrnsys1.cpp (Dirk Best, Robbbert) and machine/ins8154.cpp, BSD-3; rockwell/aim65.cpp and aim65_m.cpp (Peter Trauner, Dan Boris, Dirk Best, Robbbert), GPL-2.0+, with MAME's 6532 RIOT, 6821 PIA and DL1416 behaviour |
| `app/src/main/cpp/cores/eu3a05/*` | GPL-2.0-or-later | Port of MAME's tvgames/elan_eu3a05.cpp and the machine/elan_eu3a05* devices (David Haywood, R. Belmont), BSD-3, with MAME's OKI ADPCM tables (okiadpcm.cpp, Andrew Gardner, Aaron Giles) |
| `app/src/main/cpp/common/i8080.*` | BSD-3-Clause | PocketPlug (written for this project; passes TST8080, 8080PRE, CPUTEST and 8080EXM) |
| `app/src/main/cpp/cores/altair/*` | GPL-2.0-or-later | PocketPlug, after MAME's mits/altair.cpp (Miodrag Milanovic, Robbbert, BSD-3) and the 88-DCDD behaviour documented by SIMH's altair_dsk.c |
| `app/src/main/cpp/common/via6522.h` | BSD-3-Clause | Port of MAME's 6522via.cpp (Peter Trauner, Mathis Rosenhauer) |
| `app/src/main/cpp/cores/trkfld/g65816/*` | BSD-3-Clause | MAME G65816 CPU (Karl Stenerud; R. Belmont, Angelo Salese) |
| `app/src/main/cpp/cores/trkfld/trkfld.*` | GPL-2.0-or-later | Port of MAME's trkfldch driver (BSD-3, David Haywood) |
| `app/src/main/cpp/cores/spg2xx/unsp.*` | GPL-2.0-or-later | MAME u'nSP CPU (Segher Boessenkool, Ryan Holtz, David Haywood) |
| `app/src/main/cpp/cores/spg2xx/*` (rest) | BSD-3-Clause | MAME SPG2xx devices (Ryan Holtz, David Haywood, Jonathan Gevaryahu), IMA ADPCM (Andrew Gardner, Aaron Giles), attotime/scheduler/screen timing (Aaron Giles); glue in spg2xx.cpp is GPL-2.0-or-later |
| `app/src/main/cpp/cores/ngen/*` | BSD-3-Clause | PocketPlug (written from scratch; the I/O map follows MAME's BSD-3 `skeleton/ngen.cpp` notes by Barry Rodewald) |
| `app/src/main/cpp/cores/tm990/*` | BSD-3-Clause | PocketPlug (CPU, 9901 and 9918 written from scratch; board wiring, cycle counts and the 9918 renderer follow MAME's BSD-3 `ti/tm990189.cpp`, `cpu/tms9900` (Michael Zapf) and `video/tms9928a.cpp`) |
| `app/src/main/cpp/cores/nixie4004/*` | BSD-3-Clause | PocketPlug (CPU written from scratch; the board wiring follows MAME's BSD-3 `homebrew/4004clk.cpp` by Miodrag Milanovic) |
| `app/src/main/cpp/cores/acorn/rpcemu/*` | GPL-2.0-or-later | RPCEmu (Sarah Walker, Peter Howkins, Matthew Howkins and contributors; github.com/Septercius/rpcemu-dev) - `rpcemu/COPYING`. Interpreter build without the Qt front end, networking or CD-ROM ioctl. Changed for PocketPlug: `rpcemu_idle()` doesn't sleep and stops when a frame's time is used up; `paths.h` from its Qt front end |
| `app/src/main/cpp/cores/acorn/acorn_modules.c`, `acorn_cmos.c` | GPL-2.0-or-later | RPCEmu's HostFS, HostFSFiler and SyncClock RISC OS modules (`riscos-progs/`) and default CMOS RAM, as C arrays |
| `app/src/main/cpp/cores/acorn/acorn.*`, `rpcemu_glue.*` | GPL-2.0-or-later | PocketPlug glue (the host side after RPCEmu's `qt6/` front end) |
| `app/src/main/cpp/cores/atari800/a800/*` | GPL-2.0-or-later | Atari800 (the Atari800 development team, github.com/atari800/atari800) built as its `libatari800` library - `a800/COPYING`, `a800/CREDITS`. Built-in replacement ROMs in `a800/roms/` are AltirraOS and Altirra BASIC (Avery Lee, all-permissive notice in each file), as shipped with Atari800. Changed for PocketPlug: `libatari800/input.c` takes raw keyboard codes from the front end; generated `config.h` |
| `app/src/main/cpp/cores/atari800/atari800.*` | GPL-2.0-or-later | PocketPlug glue around libatari800 |
| `app/src/main/cpp/cores/unixpc/freebee/*` (except `musashi/`) | GPL-3.0 | FreeBee 3B1 emulator (Philip Pemberton and contributors, github.com/philpem/freebee) - `GPL-3.0.txt`. Changed for PocketPlug: no SDL (keyboard, disk timers and dialer audio driven by the core), no TOML config, no serial PTY, ROMs passed in memory; its bus functions are reached through `m68k_bus.c` (MIT) so the Almeria core can share Musashi |
| `app/src/main/cpp/cores/unixpc/freebee/musashi/*` | MIT | Musashi 680x0 core (Karl Stenerud et al., FreeBee's fork) - `musashi/readme.txt`; SoftFloat (John R. Hauser) - `musashi/softfloat/README.txt` |
| `app/src/main/cpp/cores/unixpc/unixpc.*` | GPL-3.0-or-later | PocketPlug glue around FreeBee (main loop after FreeBee's `main.c`) |
| `app/src/main/cpp/cores/archi/arculator/*` | GPL-2.0-or-later | Arculator 2.2 (Sarah Walker and contributors, github.com/sarah-walker-pcem/arculator) - `arculator/COPYING`. Changed for PocketPlug: no wxWidgets / SDL front end; `romload.c` reads one ROM image without `chdir()`; `ddnoise.c` uses silent stand-ins for the drive sounds; `keyboard_sdl.h` gives SDL's scancode numbers without SDL; `main.c` leaves logging to the front end |
| `app/src/main/cpp/cores/archi/archi_data.c` | GPL-2.0-or-later | Arculator's support ROM (`roms/arcrom_ext`) and default CMOS RAM (`cmos/*/cmos.bin`) as C arrays |
| `app/src/main/cpp/cores/archi/archi.*`, `arc_glue.*`, `arc_rename.h` | GPL-2.0-or-later | PocketPlug glue around Arculator |
| `app/src/main/cpp/common/chd/chd.*` | GPL-2.0-or-later | PocketPlug CHD v5 reader (format, map and huffman decoding follow MAME's BSD-3 `chd.cpp` / `chdcodec.cpp` / `huffman.cpp` by Aaron Giles) |
| `app/src/main/cpp/common/chd/lzma/*` | Public domain | LZMA SDK decoder (Igor Pavlov), as in MAME's `3rdparty/lzma` |
| `app/src/main/cpp/common/chd/flac/*` | BSD-3-Clause | libFLAC 1.4.3 decoder (Josh Coalson, Xiph.Org Foundation), as in MAME's `3rdparty/flac` - `flac/COPYING.Xiph`. Only the decoder files, with a plain-C `config.h` |
| everything else | BSD-3-Clause | PocketPlug - `BSD-3-Clause-PocketPlug.txt` |

BSD-3-Clause code is compatible with, and distributed here under, the GPL.
