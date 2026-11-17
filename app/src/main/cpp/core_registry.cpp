// PocketPlug - machine -> core registry
// SPDX-License-Identifier: BSD-3-Clause
//
// Add new machines here. Each entry picks a core and its configuration.

#include "core_api.h"
#include "cores/vt1682/vt1682.h"
#include "cores/vc4000/vc4000.h"
#include "cores/tama/tama.h"

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
    return nullptr;
}
