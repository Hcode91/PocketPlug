# Licensing

PocketPlug as a whole is distributed under the **GNU General Public License,
version 2 or (at your option) any later version** (`../LICENSE`), because it
includes code derived from GPL-2.0+ parts of MAME.

Individual files keep their own SPDX tags:

| Files | Licence | Origin |
|---|---|---|
| `app/src/main/cpp/cores/vc4000/*` | GPL-2.0-or-later | MAME `src/mame/interton/vc4000*` (Peter Trauner, Manfred Schneider, Robbbert) |
| `app/src/main/cpp/cores/vt1682/*` | BSD-3-Clause | MAME `src/mame/nintendo/vt1682*` (David Haywood) - `MAME-vt1682.txt` |
| `app/src/main/cpp/common/s2650.*` | BSD-3-Clause | MAME `src/devices/cpu/s2650` (Juergen Buchmueller) |
| `app/src/main/cpp/cores/e0c6s46/*` | BSD-3-Clause | MAME `src/devices/cpu/e0c6200` (hap) - `MAME-e0c6200.txt` |
| `app/src/main/cpp/cores/vt369/*` | GPL-2.0-or-later | MAME VT SoC + PPU (BSD-3, David Haywood, Ernesto Corvi, Brad Oliver, Fabio Priuli), VT3xx sound CPU (BSD-3, David Haywood), VT369 ADPCM decoder (GPL-2.0+, NewRisingSun) and NES APU (GPL-2.0+, Matthew Conte) |
| `app/src/main/cpp/cores/spg2xx/unsp.*` | GPL-2.0-or-later | MAME u'nSP CPU (Segher Boessenkool, Ryan Holtz, David Haywood) |
| `app/src/main/cpp/cores/spg2xx/*` (rest) | BSD-3-Clause | MAME SPG2xx devices (Ryan Holtz, David Haywood, Jonathan Gevaryahu), IMA ADPCM (Andrew Gardner, Aaron Giles), attotime/scheduler/screen timing (Aaron Giles); glue in spg2xx.cpp is GPL-2.0-or-later |
| `app/src/main/cpp/cores/ngen/*` | BSD-3-Clause | PocketPlug (written from scratch; the I/O map follows MAME's BSD-3 `skeleton/ngen.cpp` notes by Barry Rodewald) |
| everything else | BSD-3-Clause | PocketPlug - `BSD-3-Clause-PocketPlug.txt` |

BSD-3-Clause code is compatible with, and distributed here under, the GPL.
