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
    if (id == "ngen")         // Convergent NGEN CP-001 / Burroughs B26 workstation (80186)
        return new NgenCore();
    if (id == "oceanpet")    // built-in original game, no ROM
        return new OceanPetCore();
    return nullptr;
}
