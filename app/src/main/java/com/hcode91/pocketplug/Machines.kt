package com.hcode91.pocketplug

/** One file of a ROM set, as listed by MAME. */
data class RomSpec(val name: String, val size: Long, val crc: Long)

/** Which on-screen controller / cabinet the emulator screen shows. */
enum class Skin { DANCE_MAT, APVS_1292, EGG_PET }

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
    )

    fun byId(id: String) = all.firstOrNull { it.id == id }
}
