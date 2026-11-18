package com.hcode91.pocketplug

/** One file of a ROM set, as listed by MAME. */
data class RomSpec(val name: String, val size: Long, val crc: Long)

/** Which on-screen controller / cabinet the emulator screen shows. */
enum class Skin { DANCE_MAT, APVS_1292, EGG_PET, PHONE_CASE, ARCADE_CABINET, HANDHELD, WORKSTATION, DESKTOP_3B1, NIXIE_CLOCK, JOYSTICK_TV, WIDE_HANDHELD, UNIVERSITY_BOARD, ATARI_XL }

/** ROM_SET: one fixed set of ROMs (plug & play units). CARTRIDGE: a console that loads separate games. */
enum class MediaKind { ROM_SET, CARTRIDGE }

/**
 * A playable machine. Each machine maps to a native core by [id]
 * (see core_registry.cpp). Add new machines here and in the registry.
 */
data class MachineDef(
    val id: String,
    val title: String,
    val subtitle: String,
    val chip: String,
    val region: String,
    val year: String,
    val roms: List<RomSpec>,
    val skin: Skin,
    val media: MediaKind = MediaKind.ROM_SET,
    /** Largest cartridge file accepted (CARTRIDGE machines). */
    val cartMaxSize: Long = 0,
    /** Known cartridges: CRC32 -> title. */
    val knownCarts: Map<Long, String> = emptyMap(),
    /** Compatible machines share one game library (folder name). */
    val cartLibrary: String = id,
    /** Model name printed on the console (skins that show one). */
    val badge: String = "",
)

/** Input bits for the dance mat (match UIO port A on the VT1682, 1 = pressed). */
object DanceMatBits {
    const val UP_RIGHT = 1 shl 0
    const val UP_LEFT = 1 shl 1
    const val BACK = 1 shl 2
    const val START = 1 shl 3
    const val UP = 1 shl 4
    const val DOWN = 1 shl 5
    const val LEFT = 1 shl 6
    const val RIGHT = 1 shl 7
}

/**
 * Input bits for the VC 4000 family (see vc4000.h). Each player's keypad is
 * 12 bits: 1 2 3 4 5 6 7 8 9 CLEAR 0 ENTER. The fire button is keypad 2.
 */
object ApvsBits {
    const val KEYS_PER_PLAYER = 12
    const val K1 = 0; const val K2 = 1; const val K3 = 2; const val K4 = 3; const val K5 = 4; const val K6 = 5
    const val K7 = 6; const val K8 = 7; const val K9 = 8; const val KCLEAR = 9; const val K0 = 10; const val KENTER = 11
    const val START = 1 shl 24
    const val SELECT = 1 shl 25
    fun key(player: Int, k: Int) = 1 shl (player * KEYS_PER_PLAYER + k)
    fun fire(player: Int) = key(player, K2)
}

/** Buttons of the egg-shaped LCD pets: A (left), B (middle), C (right). */
object EggPetBits {
    const val A = 1
    const val B = 2
    const val C = 4
    /** The angel model's vibration (shake) sensor. */
    const val SHAKE = 8
}

/** VC 4000 software list titles (MAME hash/vc4000.xml), keyed by CRC32. */
private val vc4000Titles: Map<Long, String> = mapOf(
        0x5c7f11e0L to "Car Races",
        0xec2a91b3L to "Blackjack",
        0x682b876aL to "Paddle Games",
        0x468e4779L to "Tank Battle/Air Battle",
        0xe38e0c03L to "Mathematics I",
        0x66c5975eL to "Mathematics II",
        0xf3d37699L to "Air/Sea Battle",
        0xea8f717fL to "Memory I/Flag Capture",
        0x4d3f9cfbL to "Intelligence I",
        0xbdd652b7L to "Winter Sports",
        0xaaea290fL to "Hippodrome",
        0x31978ad4L to "Hunting",
        0xe9c53288L to "Chess",
        0x55011f0aL to "Motocross",
        0x8ab96827L to "Intelligence II",
        0x52b8c6e7L to "Intelligence III",
        0x0ab80f3eL to "Cirque",
        0x922c9f0dL to "Boxing Match",
        0xb1c31f9aL to "Outer Space Combat",
        0xd68da80aL to "Melodie/Melody",
        0x619c07c9L to "Intelligence IV/Attack (Reversi)",
        0x1b948aebL to "Chess II",
        0xadd99c6eL to "Pinball",
        0x05af8228L to "Soccer",
        0xa47fe1bfL to "Bowling/Ninepins",
        0x7b868473L to "Draughts",
        0xd399ce07L to "Golf",
        0x11dd7f0dL to "Cockpit",
        0xa640a330L to "Hangman",
        0x08209d98L to "Solitaire",
        0x50fd6a18L to "Casino",
        0x9497204aL to "Invaders",
        0xa46c2f9eL to "Super Invaders",
        0x08cd8135L to "Jacquet",
        0x27cce96fL to "Monster-Man",
        0xb3ed1129L to "Hyperespace",
        0x306e37bbL to "Super-Space",
)

object Machines {
    val all: List<MachineDef> = listOf(
        MachineDef(
            id = "110dance",
            title = "Retro Dance Mat",
            subtitle = "110 song Super StepMania + 9-in-1 games",
            chip = "VT1682",
            region = "PAL",
            year = "200?",
            roms = listOf(RomSpec("110songdancemat.bin", 0x2000000L, 0xcd668e41L)),
            skin = Skin.DANCE_MAT,
        ),
        MachineDef(
            id = "1292apvs",
            title = "1292 Advanced Programmable Video System",
            subtitle = "Radofin, 1976 - cartridge console (VC 4000 compatible)",
            chip = "Signetics 2650 + 2636",
            region = "Europe",
            year = "1976",
            roms = emptyList(),
            skin = Skin.APVS_1292,
            media = MediaKind.CARTRIDGE,
            cartMaxSize = 0x1800,
            knownCarts = vc4000Titles,
            cartLibrary = "vc4000",
            badge = "1292",
        ),
        MachineDef(
            id = "1392apvs",
            title = "1392 Advanced Programmable Video System",
            subtitle = "Radofin - the 1292 with a built-in power supply; shares its games",
            chip = "Signetics 2650 + 2636",
            region = "Europe",
            year = "1976",
            roms = emptyList(),
            skin = Skin.APVS_1292,
            media = MediaKind.CARTRIDGE,
            cartMaxSize = 0x1800,
            knownCarts = vc4000Titles,
            cartLibrary = "vc4000",
            badge = "1392",
        ),
        MachineDef(
            id = "tama",
            title = "Egg Pet - Generation 1",
            subtitle = "1996 keychain virtual pet (MAME set: tama). Lives in real time.",
            chip = "Epson E0C6S46",
            region = "Japan / World",
            year = "1996",
            roms = listOf(RomSpec("tama.bin", 0x3000L, 0x5c864cb1L)),
            skin = Skin.EGG_PET,
        ),
        MachineDef(
            id = "tamaang",
            title = "Egg Pet - Angel",
            subtitle = "1997 angel virtual pet (MAME set: tamaang). Shake it to help it. Lives in real time.",
            chip = "Epson E0C6S48",
            region = "Japan / World",
            year = "1997",
            roms = listOf(RomSpec("tamaang.bin", 0x4000L, 0x87bcb59fL)),
            skin = Skin.EGG_PET,
            badge = "angel",
        ),
        MachineDef(
            id = "168pcase",
            title = "168-in-1 Game Phone Case",
            subtitle = "Phone case with a built-in NES-style console and colour screen (MAME set: 168pcase)",
            chip = "VT369 (NES on a chip)",
            region = "China",
            year = "202x",
            roms = listOf(RomSpec("25q32.u7", 0x400000L, 0x98e8e97aL)),
            skin = Skin.PHONE_CASE,
        ),
        MachineDef(
            id = "36pcase",
            title = "36-in-1 Classic Games Phone Case",
            subtitle = "Black handheld-style phone case with 36 NES classics and a colour screen (MAME set: 36pcase)",
            chip = "VT369 (NES on a chip, sound CPU)",
            region = "Worldwide",
            year = "202x",
            roms = listOf(
                RomSpec("25q16.ic3", 0x200000L, 0xa8edb73eL),
                RomSpec("internal.bin", 0x1000L, 0xda5850f0L),
                RomSpec("serial-rom.bin", 0x100L, 0x877fb90bL),
            ),
            skin = Skin.PHONE_CASE,
        ),
        MachineDef(
            id = "240in1ar",
            title = "240-in-1 Mini Arcade",
            subtitle = "Palm-sized upright arcade cabinet with joystick and sampled-music games (MAME set: 240in1ar)",
            chip = "VT369 (NES on a chip, sound CPU)",
            region = "UK / Europe",
            year = "201x",
            roms = listOf(RomSpec("mw-106-2g.u3", 0x8000000L, 0xc46d2ca9L)),
            skin = Skin.ARCADE_CABINET,
        ),
        MachineDef(
            id = "265games",
            title = "265-in-1 Handheld",
            subtitle = "Colour handheld with 265 sports, arcade and puzzle games in five game zones (MAME set: 265games)",
            chip = "SunPlus SPG2xx (u'nSP)",
            region = "Europe",
            year = "201x",
            roms = listOf(RomSpec("265games.bin", 0x10000000L, 0x761c92cbL)),
            skin = Skin.HANDHELD,
        ),
        MachineDef(
            id = "4004clk",
            title = "4004 Nixie Clock",
            subtitle = "2008 home-built clock run by the Intel 4004, the first microprocessor: six nixie tubes, "
                + "date, time and a retirement countdown. Set from the phone's clock (MAME set: 4004clk)",
            chip = "Intel 4004 (4-bit), 4001 ROM x16, 4002 RAM",
            region = "USA",
            year = "2008",
            roms = (30..45).map { n ->
                RomSpec("clock.u$n", 0x100L, longArrayOf(
                    0x3d8608a5L, 0xc83af4bcL, 0x72442ae7L, 0x74789383L, 0xd817cc54L, 0xece36d21L, 0x65aa3cb1L, 0x4c2a2632L,
                    0x133da0d6L, 0x0628593cL, 0x1c2e94b5L, 0x48b4510dL, 0x4b768675L, 0xdf8db80fL, 0x23037c71L, 0xa8d419efL)[n - 30])
            },
            skin = Skin.NIXIE_CLOCK,
        ),
        MachineDef(
            id = "3b1",
            title = "PC 7300 / 3B1 Desktop",
            subtitle = "1985 AT&T desktop computer (the UNIX PC) with a mouse-driven window manager over System V. "
                + "Install from your own 3.51 disk set onto the built-in 64 MB hard disk (MAME set: 3b1)",
            chip = "Motorola 68010, WD2797 + WD2010",
            region = "USA",
            year = "1985",
            roms = listOf(
                RomSpec("72-00616.14c", 0x2000L, 0xc61f7ae0L),
                RomSpec("72-00617.15c", 0x2000L, 0x4e93ff40L),
            ),
            skin = Skin.DESKTOP_3B1,
        ),
        MachineDef(
            id = "a1200xl",
            title = "1200XL Home Computer",
            subtitle = "1982 Atari 8-bit computer with 64 KB, function keys and keyboard lights. Load your own "
                + "disk, program, cartridge or tape files (.atr .xex .car .rom .cas) (MAME set: a1200xl)",
            chip = "6502C, ANTIC, GTIA, POKEY (Atari800)",
            region = "USA",
            year = "1982",
            roms = listOf(
                RomSpec("co60616b.rom", 0x2000L, 0x6e29ec8dL),
                RomSpec("co60617b.rom", 0x2000L, 0xd73ce29aL),
            ),
            skin = Skin.ATARI_XL,
            badge = "1200xl",
        ),
        MachineDef(
            id = "a130xe",
            title = "130XE Home Computer",
            subtitle = "1985 Atari 8-bit computer with 128 KB and BASIC built in. Load your own disk, program, "
                + "cartridge or tape files (.atr .xex .car .rom .cas) (MAME set: a130xe)",
            chip = "6502C, ANTIC, GTIA, POKEY, FREDDIE (Atari800)",
            region = "USA / Europe",
            year = "1985",
            roms = listOf(
                RomSpec("co24947a.rom", 0x2000L, 0x7d684184L),
                RomSpec("co61598b.rom", 0x4000L, 0x1f9cd270L),
            ),
            skin = Skin.ATARI_XL,
            badge = "130xe",
        ),
        MachineDef(
            id = "500in1hh",
            title = "500-in-1 Handheld Game",
            subtitle = "White sideways handheld with a square colour screen, D-pad and A/B; 500 NES-style games "
                + "behind a Chinese / English menu (MAME set: 500in1hh)",
            chip = "VT \"4FFx\" NES-on-a-chip, scrambled opcodes",
            region = "China",
            year = "202x",
            roms = listOf(RomSpec("500in1.bin", 0x1000000L, 0x50f44c1aL)),
            skin = Skin.WIDE_HANDHELD,
        ),
        MachineDef(
            id = "88in1joy",
            title = "Joystick88",
            subtitle = "Plug-and-play joystick for the TV with 88 NES-style games: stick, A, B, turbo and "
                + "SELECT / START on a horseshoe pad (Play Vision; MAME set: 88in1joy)",
            chip = "VT02 / VT03 NES-on-a-chip",
            region = "UK / Europe",
            year = "201x",
            roms = listOf(RomSpec("88in1joystick.bin", 0x400000L, 0x86b8d819L)),
            skin = Skin.JOYSTICK_TV,
        ),
        MachineDef(
            id = "990189",
            title = "TM 990/189 University Board",
            subtitle = "1978 Texas Instruments teaching computer: a single board with a 10-digit LED display and "
                + "a 45-key calculator keyboard, running University BASIC (MAME set: 990189)",
            chip = "TMS9980A, TMS9901 x2",
            region = "USA",
            year = "1978",
            roms = listOf(
                RomSpec("990-469.u32", 0x800L, 0x08df7edbL),
                RomSpec("990-469.u33", 0x1000L, 0xe9b4ac1bL),
            ),
            skin = Skin.UNIVERSITY_BOARD,
        ),
        MachineDef(
            id = "990189v",
            title = "TM 990/189 + Video Board",
            subtitle = "The University Board with TI's colour video board (TMS9918): University BASIC draws in colour "
                + "and four demos are built in - NEW, LOAD0-3, RUN (MAME set: 990189v)",
            chip = "TMS9980A, TMS9901 x2, TMS9918",
            region = "USA",
            year = "1980",
            roms = listOf(
                RomSpec("990-469.u32", 0x800L, 0x08df7edbL),
                RomSpec("990-469.u33", 0x1000L, 0xe9b4ac1bL),
                RomSpec("demo1000.u13", 0x800L, 0xc0e16685L),
                RomSpec("demo1800.u11", 0x800L, 0x8737dc4bL),
            ),
            skin = Skin.UNIVERSITY_BOARD,
        ),
        MachineDef(
            id = "ngen",
            title = "NGEN CP-001 Workstation",
            subtitle = "1983 office workstation (Convergent Technologies NGEN, sold as the Burroughs B26) running "
                + "BTOS / CTOS from floppy and a 10 MB winchester. Insert your own system disks (MAME set: ngen)",
            chip = "Intel 80186, WD2797 + WD1010",
            region = "USA",
            year = "1983",
            roms = listOf(
                RomSpec("72-00414_80186_cpu.bin", 0x1000L, 0xe1387a03L),
                RomSpec("72-00415_80186_cpu.bin", 0x1000L, 0xa6dde7d9L),
                RomSpec("72-00422_10mb_disk.bin", 0x1000L, 0xf5b046b6L),
            ),
            skin = Skin.WORKSTATION,
        ),
        MachineDef(
            id = "oceanpet",
            title = "Egg Pet - Ocean (original)",
            subtitle = "Built-in sea pet in the spirit of the 1998 ocean egg: water to flush, a treasure-chest game, "
                + "predators while it naps, seven sea creatures to raise. Lives in real time.",
            chip = "PocketPlug original",
            region = "Built in",
            year = "2026",
            roms = emptyList(),
            skin = Skin.EGG_PET,
            badge = "umino",
        ),
    )

    fun byId(id: String) = all.firstOrNull { it.id == id }
}
