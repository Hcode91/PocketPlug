// PocketPlug - machine -> core registry
// SPDX-License-Identifier: BSD-3-Clause
//
// Add new machines here. Each entry picks a core and its configuration.

#include "core_api.h"
#include "cores/vt1682/vt1682.h"
#include "cores/vc4000/vc4000.h"
#include "cores/tama/tama.h"
#include "cores/ocean/ocean.h"
#include "cores/vt369/vt369.h"
#include "cores/spg2xx/spg2xx.h"
#include "cores/ngen/ngen.h"
#include "cores/unixpc/unixpc.h"
#include "cores/atari800/atari800.h"
#include "cores/nixie4004/nixieclock.h"
#include "cores/tm990/tm990.h"

Core* createCore(const std::string& id)
{
    if (id == "110dance") {
        Vt1682Config c;
        c.pal = true;
        c.opcodeSwap = 0;            // the dance mat uses an unscrambled 6502
        c.romName = "110songdancemat.bin";
        c.romSize = 0x2000000;
        c.romCrc = 0xcd668e41;
        return new Vt1682Core(c);
    }
    // Radofin 1292 / 1392 Advanced Programmable Video System (VC 4000 family, cartridges)
    if (id == "1292apvs" || id == "1392apvs")   // 1392: same hardware, internal power supply
        return new Vc4000Core();
    // Epson E0C6S46 LCD pets
    if (id == "tama") {
        TamaConfig c;
        c.romName = "tama.bin";
        c.romCrc = 0x5c864cb1;
        return new TamaCore(c);
    }
    if (id == "tamaang") {   // Angel: E0C6S48 with a 1 MHz OSC3 and a shake sensor
        TamaConfig c;
        c.romName = "tamaang.bin";
        c.romCrc = 0x87bcb59f;
        c.variant = 1;
        c.osc3 = 1000000;
        return new TamaCore(c);
    }
    if (id == "168pcase") {   // Diier-D-10 168-in-1 phone case (VT369)
        Vt369Config c;
        c.romName = "25q32.u7";
        c.romSize = 0x400000;
        return new Vt369Core(c);
    }
    if (id == "265games") {   // 265-in-1 handheld (SPG2xx, 256 MB in 32 banks)
        Spg2xxConfig c;
        c.romName = "265games.bin";
        c.romSize = 0x10000000;
        // init_m527: the IRQ-disable patches MAME applies (word offsets in the ROM)
        for (uint32_t w : { 0x242500fu, 0x30284b5u, 0x340c055u, 0x4415e46u, 0x481cab4u, 0x4c12c3au,
                            0x5c0c1b4u, 0x600d62cu, 0x64103c5u, 0x68084ddu, 0x6d45cddu }) {
            c.romPatches.push_back(w); c.romPatchValues.push_back(0x0003);
        }
        c.romPatches.push_back(0x4821e25u); c.romPatchValues.push_back(0xffff);  // gym dancing
        return new Spg2xxCore(c);
    }
    if (id == "36pcase") {    // 36-in-1 classic games phone case (VT369, altswap, internal ROM + serial protection)
        Vt369Config c;
        c.romName = "25q16.ic3";
        c.romSize = 0x200000;
        c.crypt = 1;
        c.internalRomName = "internal.bin";
        c.protRomName = "serial-rom.bin";
        c.protStartByte = 1;
        c.case36 = true;
        return new Vt369Core(c);
    }
    if (id == "240in1ar") {   // 240-in-1 mini arcade cabinet (VT369, scrambled opcodes, 128 MB)
        Vt369Config c;
        c.romName = "mw-106-2g.u3";
        c.romSize = 0x8000000;
        c.crypt = 1;
        c.bank41e6 = true;
        return new Vt369Core(c);
    }
    if (id == "88in1joy") {   // Play Vision Joystick88 (VT02/03, 4 MB)
        Vt369Config c;
        c.romName = "88in1joystick.bin";
        c.romSize = 0x400000;
        c.soc = 2;
        c.palMode = 0;
        c.ntscDmaBug = true;
        return new Vt369Core(c);
    }
    if (id == "500in1hh") {   // 500-in-1 handheld (VT "4FFx" SoC, gbox2020 opcode swap, 16 MB)
        Vt369Config c;
        c.romName = "500in1.bin";
        c.romSize = 0x1000000;
        c.soc = 1;
        c.palMode = 0;
        c.crypt = 3;
        return new Vt369Core(c);
    }
    if (id == "990189")       // TI TM 990/189 University Board
        return new Tm990Core(false);
    if (id == "990189v")      // ... with the Video Board Interface (TMS9918)
        return new Tm990Core(true);
    if (id == "4004clk")      // 4004 Nixie Clock (Intel 4004)
        return new NixieClockCore();
    if (id == "a1200xl")      // Atari 1200XL (Atari800)
        return new Atari800Core(Atari800Core::k1200XL);
    if (id == "a130xe")       // Atari 130XE (Atari800)
        return new Atari800Core(Atari800Core::k130XE);
    if (id == "3b1")          // AT&T UNIX PC / PC 7300 / 3B1 (68010, FreeBee)
        return new UnixPcCore();
    if (id == "ngen")         // Convergent NGEN CP-001 / Burroughs B26 workstation (80186)
        return new NgenCore();
    if (id == "oceanpet")    // built-in original game, no ROM
        return new OceanPetCore();
    return nullptr;
}
