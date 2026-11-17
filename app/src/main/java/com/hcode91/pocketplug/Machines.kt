package com.hcode91.pocketplug

/** One file of a ROM set, as listed by MAME. */
data class RomSpec(val name: String, val size: Long, val crc: Long)

/** Which on-screen controller / cabinet the emulator screen shows. */
enum class Skin { DANCE_MAT }

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
    )

    fun byId(id: String) = all.firstOrNull { it.id == id }
}
