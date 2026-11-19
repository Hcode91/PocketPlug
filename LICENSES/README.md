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
| `app/src/main/cpp/cores/vt369/*` | GPL-2.0-or-later | MAME VT SoC + PPU incl. VT02/03, VT "4FFx" and SH6578 (BSD-3, David Haywood, Ernesto Corvi, Brad Oliver, Fabio Priuli), VT3xx sound CPU (BSD-3, David Haywood), VT369 ADPCM decoder (GPL-2.0+, NewRisingSun) and NES APU (GPL-2.0+, Matthew Conte) |
| `app/src/main/cpp/common/hd6301*` | BSD-3-Clause | MAME 6800/6801 instruction set (Aaron Giles), peripherals after MAME's hd6301x |
| `app/src/main/cpp/common/hd44780.*` | BSD-3-Clause | Port of MAME's HD44780 (Sandro Ronco) |
| `app/src/main/cpp/common/chessboard.h` | BSD-3-Clause | Logic of MAME's sensorboard (hap) |
| `app/src/main/cpp/common/w65c02.*` | GPL-2.0-or-later | Own W65C02S core |
| `app/src/main/cpp/cores/chess/*` | GPL-2.0-or-later | Ports of MAME's academy.cpp / mboard / mdisplay2 (Sandro Ronco, hap) and novag/accord.cpp (hap), BSD-3 |
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
| `app/src/main/cpp/cores/unixpc/freebee/*` (except `musashi/`) | GPL-3.0 | FreeBee 3B1 emulator (Philip Pemberton and contributors, github.com/philpem/freebee) - `GPL-3.0.txt`. Changed for PocketPlug: no SDL (keyboard, disk timers and dialer audio driven by the core), no TOML config, no serial PTY, ROMs passed in memory |
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
