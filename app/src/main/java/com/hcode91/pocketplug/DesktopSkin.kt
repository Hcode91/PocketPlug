package com.hcode91.pocketplug

/** On-screen computers (monitor, drives and keyboard) as the emulator screen sees them. */
interface DesktopSkin {
    var onKey: ((Int, Boolean) -> Unit)?
    var onMenu: (() -> Unit)?
    var onDisks: (() -> Unit)?
    /** bit 0 floppy busy, bit 1 hard disk busy, bits 8-15 machine specific lights */
    var indicators: Int
    var floppyLabel: String?
    fun updateFrame(px: IntArray)
}
