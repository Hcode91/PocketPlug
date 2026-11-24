// PocketPlug - machine -> core registry
// SPDX-License-Identifier: BSD-3-Clause
//
// Add new machines here. Each entry picks a core and its configuration.

#include <algorithm>
#include "core_api.h"
#include "cores/vt1682/vt1682.h"
#include "cores/vc4000/vc4000.h"
#include "cores/tama/tama.h"
#include "cores/ocean/ocean.h"
#include "cores/vt369/vt369.h"
#include "cores/spg2xx/spg2xx.h"
#include "cores/apf/apf.h"
#include "cores/astrocde/astrocde.h"
#include "cores/sms/atgsms.h"
#include "cores/md/firecore.h"
#include "cores/gpl32/gpl32.h"
#include "cores/gizmondo/gizmondo.h"
#include "cores/gizmondo/gizgame.h"
#include "cores/zodiac/zodiac.h"
#include "cores/hyperscan/hyperscan.h"
#include "cores/playdia/playdia.h"
#include "cores/gamewave/gamewave.h"
#include "cores/st2302/bbl380.h"
#include "cores/trkfld/trkfld.h"
#include "cores/chess/academy.h"
#include "cores/chess/accord.h"
#include "cores/chess/almeria.h"
#include "cores/chess/mephmod1.h"
#include "cores/chess/arb.h"
#include "cores/chess/vcc.h"
#include "cores/bbcbc/bbcbc.h"
#include "cores/beena/beena.h"
#include "cores/gpl162xx/gpl162xx.h"
#include "cores/gpl162xx/gpl951.h"
#include "cores/chess/beluga.h"
#include "cores/chess/bkc.h"
#include "cores/xavix/xavix.h"
#include "cores/xavix2/xavix2.h"
#include "cores/xavix/xavix.h"
#include "cores/chess/avrmax.h"
#include "cores/hh/hh_hmcs40.h"
#include "cores/hh/hh_ucom4.h"
#include "cores/hh/hh_sm510.h"
#include "cores/hh/hh_sm511.h"
#include "cores/hh/hh_rw5000.h"
#include "cores/hh/hh_tms1k.h"
#include "cores/kits/acrnsys1.h"
#include "cores/kits/aim65.h"
#include "cores/altair/altair.h"
#include "cores/eu3a05/eu3a05.h"
#include "cores/eu3a05/eu3a14.h"
#include "cores/advision/advision.h"
#include "cores/adam/adam.h"
#include "cores/ngen/ngen.h"
#include "cores/unixpc/unixpc.h"
#include "cores/atari800/atari800.h"
#include "cores/acorn/acorn.h"
#include "cores/bbc/bbc.h"
#include "cores/archi/archi.h"
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
    if (id == "1292apvs" || id == "1392apvs" || id == "vc4000" || id == "spc4000" || id == "cx3000tc" || id == "tvc4000" || id == "mpu1000" || id == "mpu2000" || id == "pp1292" || id == "pp1392" || id == "f1392" || id == "fforce2" || id == "hmg1292" || id == "hmg1392" || id == "lnsy1392" || id == "vc6000" || id == "database" || id == "vmdtbase" || id == "rwtrntcs" || id == "telngtcs" || id == "krvnjvtv" || id == "oc2000" || id == "mpt05" || id == "h21")   // the VC 4000 family: same hardware   // 1392: same hardware, internal power supply
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
    if (id == "alienfev") {  // Chibi Pachi: Alien Fever (Epoch, 1997): E0C6S46 on a 1 MHz OSC3, 39x16 LCD
        TamaConfig c;
        c.romName = "alienfev.bin";
        c.romCrc = 0xe561599c;
        c.osc3 = 1000000;
        c.rawWidth = 39;
        c.directKeys = true;   // K00 Mode, K01 Select, K02 Sound, K03 Handle
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
    if (id == "a6plus") {     // A6Plus mini arcade cabinet (VT369 + SD card)
        Vt369Config c;
        c.romName = "w25q64jv.u3";
        c.romSize = 0x800000;
        c.ntscDmaBug = true;
        c.sdName = "a6plus.chd";
        c.i2cSecurity = true;
        c.spiFlashWrites = true;
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
    if (id == "arcade10")     // Fizz Creations Mini Arcade Console 10-in-1 (ST2302U + SPI flash)
        return new Bbl380Core();
    if (id == "apfm1000")     // APF M-1000 (Rocket Patrol built in, cartridges optional)
        return new ApfCore();
    if (id == "arcade3d") {   // Millennium Arcade 3D 15-in-1 (SPG24x, JungleTac, encrypted ROM)
        Spg2xxConfig c;
        c.romName = "arcade3d.u3";
        c.romSize = 0x800000;
        c.board = 3;
        c.pal = false;
        c.decryptZeus = true;
        return new Spg2xxCore(c);
    }
    if (id == "abltenni") {   // ABL Wireless Tennis (SPG24x, 60 Hz, motion racket on the buttons)
        Spg2xxConfig c;
        c.romName = "ablpnpwirelesstennis.bin";
        c.romSize = 0x400000;
        c.board = 1;
        c.pal = false;
        return new Spg2xxCore(c);
    }
    if (id == "ablkickb") {   // ABL Kick Boxing (SPG24x, 60 Hz, encrypted ROM)
        Spg2xxConfig c;
        c.romName = "ablkickboxing.bin";
        c.romSize = 0x800000;
        c.board = 2;
        c.pal = false;
        c.decryptAcFf = true;
        return new Spg2xxCore(c);
    }
    if (id == "backybbs") {   // Backyard Baseball & Soccer (SPG24x, 60 Hz, 93LC66 EEPROM; needs all 4 MB mapped)
        Spg2xxConfig c;
        c.romName = "backyardbs.bin";
        c.romSize = 0x400000;
        c.board = 4;
        c.pal = false;
        return new Spg2xxCore(c);
    }
    if (id == "ban_tam2") {   // Bandai Let's! TV Play Tamagotchi School Senshuken (SPG24x, 60 Hz)
        Spg2xxConfig c;
        c.romName = "tvgametamagotchi2.bin";
        c.romSize = 0x800000;
        c.board = 5;
        c.pal = false;
        return new Spg2xxCore(c);
    }
    if (id == "barbpet") {    // Mattel Barbie: I Love Pets - Pet Rescue (SPG24x, 60 Hz, 2 MB)
        Spg2xxConfig c;
        c.romName = "barbiepetrescue.u3";
        c.romSize = 0x200000;
        c.board = 6;
        c.pal = false;
        return new Spg2xxCore(c);
    }
    if (id == "ban_krkk") {   // Bandai Let's! TV Play Zunou Family Mattore (SPG24x, 60 Hz, mat)
        Spg2xxConfig c;
        c.romName = "quiz.bin";
        c.romSize = 0x400000;
        c.board = 5;
        c.pal = false;
        return new Spg2xxCore(c);
    }
    if (id == "afbm7800") {   // Atari Flashback (mini 7800): NES clone with an MMC3-style mapper
        Vt369Config c;
        c.romName = "atariflashbackmini7800.bin";
        c.romSize = 0x100000;
        c.soc = 2;
        c.palMode = 0;
        c.pal = true;              // MAME runs it with Dendy-style PAL timing
        c.twoPads = true;
        c.board = 4;
        return new Vt369Core(c);
    }
    if (id == "adam")         // Coleco ADAM (SmartWriter, ColecoVision cartridges)
        return new AdamCore();
    if (id == "advision")     // Entex Adventure Vision (BIOS + cartridge)
        return new AdvisionCore();
    if (id == "airblsjs")     // Air-Blaster Joystick (Elan EU3A05)
        return new Eu3a05Core();
    if (id == "batvgc")       // Thinkway motion-gear TV games (Elan EU3A14)
        return new Eu3a14Core(id);
    if (id == "al8800b")      // Altair 8800b (front panel)
        return new AltairCore(false);
    if (id == "al8800bt")     // Altair 8800b Turnkey
        return new AltairCore(true);
    if (id == "aim65")        // Rockwell AIM 65
        return new Aim65Core();
    if (id == "acrnsys1")     // Acorn System 1
        return new AcornSystem1Core();
    if (id == "accord")       // Novag Accord (model 875)
        return new AccordCore("novag_875_31x0b87p");
    if (HhTms1kCore::known(id))    // TI TMS1000-family handhelds (alphie, amaztron, arcmania, ...)
        return new HhTms1kCore(id);
    if (HhRw5000Core::known(id))   // Rockwell B6000 handhelds (autorace)
        return new HhRw5000Core(id);
    if (HhSm511Core::known(id))    // Sharp SM511 LCD handhelds (bassmate)
        return new HhSm511Core(id);
    if (HhSm510Core::known(id))    // Sharp SM5xx LCD handhelds (atakaast, auslalom)
        return new HhSm510Core(id);
    if (HhUcom4Core::known(id))    // NEC uCOM-4 handhelds (alnchase, ...)
        return new HhUcom4Core(id);
    if (HhHmcs40Core::known(id))   // Hitachi HMCS40 handhelds (alnattck, ...)
        return new HhHmcs40Core(id);
    if (id == "arbv2")        // ARB V2 Sargon 4.0 (AVE Auto Response Board rebuild)
        return new ArbV2Core();
    if (id == "vcc")          // Fidelity Voice Chess Challenger (Z80 + S14001A)
        return new VccCore(false);
    if (id == "avcc")         // Fidelity Advanced Voice Chess Challenger
        return new VccCore(true);
    if (id == "ban_naru" || id == "ban_bldj" || id == "ban_dbz" || id == "ban_db2j") {   // dbz / db2j: run but stay on a black screen so far   // SSD XaviX 2 + 24Cxx
        Xavix2Core::Config c;
        c.romName = id == "ban_naru" ? "naruto.bin" : id == "ban_bldj" ? "bldj.u2" : id == "ban_dbz" ? "dbz.u2a" : "db2j.u3";
        return new Xavix2Core(c);
    }
    if (id == "ban_omt" || id == "ban_onep") {   // SSD 2000 + 24C04 EEPROM
        XavixCore::Config c;
        c.romName = id == "ban_omt" ? "otmj.bin" : "onepiece.bin";
        c.romSize = id == "ban_omt" ? 0x400000 : 0x800000;
        c.cpu = 1;
        c.i2cSize = 0x200;
        c.i2cPage = 16;
        c.camera = true;
        if (id == "ban_omt") c.camBlobs = 1;
        return new XavixCore(c);
    }
    if (id == "ban_ordj" || id == "ban_pr2j" || id == "ban_ult") {   // SSD 2002 (SuperXaviX)
        XavixCore::Config c;
        c.romName = id == "ban_ordj" ? "ordj.u2" : id == "ban_pr2j" ? "pr2j.u2" : "ultraman.u1";
        c.romSize = 0x800000;
        c.cpu = 1;
        c.superXavix = true;
        c.i2cSize = id == "ban_ult" ? 0x100 : 0x200;
        c.i2cPage = id == "ban_ult" ? 8 : 16;
        c.camera = id != "ban_ordj";
        if (id == "ban_ordj") { c.in1Default = 0x20; c.drumSerial = true; }
        if (id == "ban_pr2j") { c.camBar = true; c.sdaBit = 7; c.sclBit = 6; c.camOnEx = true; c.exDefault[2] = 0x80; }
        if (id == "ban_ult") c.exDefault[2] = 0x80;   // MAME's default for EX2 bit 7: skips the camera calibration   // MAME's default: skips the drumstick calibration
        return new XavixCore(c);
    }
    if (id == "ban_dn1j" || id == "ban_gkrj" || id == "ban_kksj" || id == "ban_um2j" || id == "ban_utmj") {   // more SSD 2002 (SuperXaviX) sets
        XavixCore::Config c;
        c.romName = id == "ban_dn1j" ? "dn1j.u2" : id == "ban_gkrj" ? "gkrj.u2" : id == "ban_kksj" ? "kksj.u1" : id == "ban_um2j" ? "um2j.u1" : "utmj.u7";
        c.romSize = id == "ban_gkrj" ? 0x400000 : 0x800000;
        c.cpu = 1;
        c.superXavix = true;
        bool c02 = id == "ban_gkrj" || id == "ban_kksj" || id == "ban_utmj";
        c.i2cSize = c02 ? 0x100 : 0x200;
        c.i2cPage = c02 ? 8 : 16;
        if (id == "ban_gkrj" || id == "ban_um2j") c.exDefault[2] = 0x80;   // MAME's "demo mode" default: skips the calibration
        if (id == "ban_gkrj" || id == "ban_um2j") { c.camera = true; c.camOnEx = true; }   // two hands in front of the camera; clock on EX1 bit 2
        // ban_dn1j: IN0 bit 7 blue, IN1 bit 0 yellow, IN0 bit 6 green. ban_kksj / ban_utmj: their sensor (an ADC routine
        // run from the I/O event / timer interrupts, set up over EX0) isn't emulated yet, so they stop at the title / set-up screen
#ifdef XAVIX_DEBUG
        if (getenv("XKEX2")) c.exDefault[2] = uint8_t(strtoul(getenv("XKEX2"), 0, 16));
        if (getenv("XKCAM")) { c.camera = true; c.camBlobs = atoi(getenv("XKCAM")); if (getenv("XKCAMEX")) c.camOnEx = true; if (getenv("XKCAMBAR")) c.camBar = true; }
#endif
        return new XavixCore(c);
    }
    if (id == "ban_bkgj") {   // Boukenger: SSD 2002 (SuperXaviX), 4 MB, S-24CS04A; no camera
        XavixCore::Config c;
        c.romName = "bkgj.u2";
        c.romSize = 0x400000;
        c.cpu = 1;
        c.superXavix = true;
        c.i2cSize = 0x200;
        c.i2cPage = 16;   // controls: IN0 bit 0 the gun's trigger, bits 1 / 2 the mat's right / left panels
        return new XavixCore(c);
    }
    if (id == "ban_krrj") {   // Keroro (SSD XaviX, 4 MB)
        XavixCore::Config c;
        c.romName = "krrj.u2";
        c.romSize = 0x400000;
        return new XavixCore(c);
    }
    if (id == "backgamc")     // Fidelity Backgammon Challenger (Z80)
        return new BkcCore();
    if (id == "gizmondo")     // Tiger Telematics Gizmondo (S3C2440 + DiskOnChip G3 + GoForce 4500)
        return new GizmondoCore();
    if (id == "hyperscan")    // Mattel HyperScan (SPG290 + CD)
        return new HyperScanCore();
    if (id == "gamewave")     // ZAPiT Game Wave (game scripts run directly)
        return new GameWaveCore();
    if (id == "playdia")      // Bandai Playdia (disc stream player)
        return new PlaydiaCore();
    if (id == "zodiac")       // Tapwave Zodiac (full hardware)
        return new ZodiacCore();
    if (id == "gizgame")      // Gizmondo games run directly (Windows CE calls handled by the emulator)
        return new GizGameCore();
    if (id == "arcadege")     // Millennium Arcade Genius (GeneralPlus GPL32600, work in progress)
        return new GplCore();
    if (id == "bananlap" || id == "bananlapa" || id == "bandolap" || id == "bandplap" || id == "bandslap" || id == "banpear")
        return new GplCore();     // Bandai kids' laptops: GeneralPlus GPL32612, booting from SPI flash
    if (id == "atgame40")     // AtGames 40 Bonus Games in 1 (Firecore Mega Drive clone)
        return new FirecoreCore();
    if (id == "banmrid")      // Bandai Kamen Rider Ryuki TV game: a Mega Drive on a chip running one 4 MB game (NTSC Japan)
        return new FirecoreCore("banmrid");
    if (id == "atgsms")       // AtGames SMS / Game Gear plug & play
        return new AtgSmsCore();
    if (id == "astrocde")     // Bally Professional Arcade (Z80 + Astrocade custom chips)
        return new AstrocadeCore("astro.bin");
    if (id == "astrocdl")     // Bally Home Library Computer
        return new AstrocadeCore("ballyhlc.bin");
    if (id == "astrocdw")     // Bally Computer System (white case)
        return new AstrocadeCore("bioswhit.bin");
    if (id == "avrmax")       // Elektor AVR-Max (ATmega88, LED)
        return new AvrMaxCore("elektor_081101-41.ic1", false);
    if (id == "avrmaxg")      // Elektor AVR-Max-Schachzwerg (German)
        return new AvrMaxCore("elektor_081101-41_d.ic1", false);
    if (id == "atm18mcc")     // Elektor ATM18 Mini Chess Computer (LCD)
        return new AvrMaxCore("avrmax_cc2_en-v1.0.bin", true);
    if (id == "amsterd")      // Mephisto Amsterdam (68000)
        return new MephMod1Core(false, "amsterda-u.bin", "amsterda-l.bin");
    if (id == "berl16")       // Mephisto Berlin 68000 (v0.03)
        return new AlmeriaCore(false, "berlin_68000_even.bin", "berlin_68000_odd_b003.bin", true);
    if (id == "berl16a")      // Mephisto Berlin 68000 (v0.02)
        return new AlmeriaCore(false, "berlin_68000_even.bin", "berlin_68000_odd_b002.bin", true);
    if (id == "berlinp")      // Mephisto Berlin Professional 68020
        return new AlmeriaCore(true, "berlin_020_v400.u2", "", true);
    if (id == "berlinpl")     // Mephisto Berlin Professional 68020 (London upgrade)
        return new AlmeriaCore(true, "berlin_020_london.u2", "", true);
    if (id == "alm32")        // Mephisto Almeria 32 Bit (68020)
        return new AlmeriaCore(true, "alm32.bin", "");
    if (id == "alm16")        // Mephisto Almeria 16 Bit v0.13 (68000)
        return new AlmeriaCore(false, "almeria_16bit_v0.13_even", "almeria_16bit_v0.13_odd");
    if (id == "alm16a")       // Mephisto Almeria 16 Bit v0.121
        return new AlmeriaCore(false, "almeria_16bit_v0.121_even", "almeria_16bit_v0.121_odd");
    if (id == "academy")      // Mephisto Academy (English, 04-10-88)
        return new AcademyCore("academy_engl.16k", "academy_engl.32k");
    if (id == "abl4play")     // 4 Player System 10-in-1 (trkfldch SoC, 65C816)
        return new TrkfldCore("abl4play.bin", 0x800000);
    if (id == "ablpinb") {    // ABL Pinball (VT02/03 PAL, plunger and flippers)
        Vt369Config c;
        c.romName = "abl_pinball.bin";
        c.romSize = 0x200000;
        c.soc = 2;
        c.palMode = 0;
        c.pal = true;
        c.board = 1;
        return new Vt369Core(c);
    }
    if (id == "ablping") {    // ABL Ping Pong (VT02/03 "sports" PAL, sensor bat on the extended IO port)
        Vt369Config c;
        c.romName = "abl_pingpong.bin";
        c.romSize = 0x200000;
        c.soc = 2;
        c.palMode = 0;
        c.pal = true;
        c.board = 2;
        const uint8_t order[6] = { 2, 5, 0, 4, 3, 1 };
        std::copy(order, order + 6, c.order2012);
        return new Vt369Core(c);
    }
    if (id == "ablmini") {    // ABL Double Players Mini Joystick 80-in-1 (VT02/03 Waixing PAL, 8 MB)
        Vt369Config c;
        c.romName = "ablmini.bin";
        c.romSize = 0x800000;
        c.soc = 2;
        c.palMode = 0;
        c.pal = true;
        const uint8_t scr[6] = { 3, 2, 1, 0, 5, 4 }, order[6] = { 1, 0, 5, 4, 3, 2 };
        std::copy(scr, scr + 6, c.scramble8000);
        std::copy(order, order + 6, c.order2012);
        c.twoPads = true;
        return new Vt369Core(c);
    }
    if (id == "bb6578") {     // DaiDaiXing TV Games Baseball (SH6578, NTSC, 512 KB)
        Vt369Config c;
        c.romName = "tv game baseball.bin";
        c.romSize = 0x80000;
        c.soc = 3;
        c.extProt = true;
        return new Vt369Core(c);
    }
    if (id == "bandgpad" || id == "bancook") {   // Bandai SH6578 sets (NTSC, 1 MB)
        Vt369Config c;
        c.romName = id == "bandgpad" ? "gamepad.bin" : "mx27c8000.ic2";
        c.romSize = 0x100000;
        c.soc = 3;
        return new Vt369Core(c);
    }
    if (id == "ablwikid") {   // ABL Wikid Joystick 14-in-1 (SH6578 PAL, 2 MB in two 1 MB halves)
        Vt369Config c;
        c.romName = "mx29f1610atc.u2";
        c.romSize = 0x200000;
        c.soc = 3;
        c.pal = true;
        c.board = 3;
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
    if (id == "a400")         // Atari 400, NTSC (Atari800)
        return new Atari800Core(Atari800Core::k400);
    if (id == "a400pal")      // Atari 400, PAL (Atari800)
        return new Atari800Core(Atari800Core::k400PAL);
    if (id == "a800") return new Atari800Core(Atari800Core::k800);
    if (id == "a800pal") return new Atari800Core(Atari800Core::k800PAL);
    if (id == "a600xl") return new Atari800Core(Atari800Core::k600XL);
    if (id == "a800xl") return new Atari800Core(Atari800Core::k800XL);
    if (id == "a800xlp") return new Atari800Core(Atari800Core::k800XLPAL);
    if (id == "a65xe") return new Atari800Core(Atari800Core::k65XE);
    if (id == "a800xe") return new Atari800Core(Atari800Core::k800XE);
    if (id == "aa305" || id == "aa310" || id == "aa440" || id == "aa4101" || id == "aa540" ||
        id == "aa3000" || id == "aa5000" || id == "aa3010" || id == "aa3010_de" || id == "aa3020" ||
        id == "aa4000" || id == "aa4" || id == "aa500")   // Acorn Archimedes family (Arculator)
        return new ArchiCore(id);
    if (BbcCore::known(id))   // Acorn BBC Micro family (B-em)
        return new BbcCore(id);
    if (id == "beluga")        // Novag Beluga
        return new BelugaCore();
    if (id == "beijuehh")      // Beijue 220-in-1 handheld (GPL16218B)
        return new Gpl162xxCore(Gpl162xxCore::BEIJUEHH);
    if (id == "beambox")       // Rescue Bots Beam Box (GPL16258VB + NAND)
        return new Gpl162xxCore(Gpl162xxCore::BEAMBOX);
    if (id == "bfmpac" || id == "bfgalaga" || id == "bfdigdug")   // Basic Fun mini arcades (GPL951xx)
        return new Gpl951Core();
    if (id == "beena")         // Sega Toys Advanced Pico BEENA
        return new BeenaCore();
    if (id == "bbcbc")         // Unicard Bridge Companion
        return new BbcbcCore();
    if (id == "a7000")        // Acorn A7000 (RPCEmu), RISC OS 3.60
        return new AcornCore(AcornCore::kA7000);
    if (id == "a7000p")       // Acorn A7000+ (RPCEmu), RISC OS 3.71
        return new AcornCore(AcornCore::kA7000Plus);
    if (id == "3b1")          // AT&T UNIX PC / PC 7300 / 3B1 (68010, FreeBee)
        return new UnixPcCore();
    if (id == "ngen")         // Convergent NGEN CP-001 / Burroughs B26 workstation (80186)
        return new NgenCore();
    if (id == "oceanpet")    // built-in original game, no ROM
        return new OceanPetCore();
    return nullptr;
}
