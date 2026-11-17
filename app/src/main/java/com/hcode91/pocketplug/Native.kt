package com.hcode91.pocketplug

object Native {
    init { System.loadLibrary("pocketplug") }

    @JvmStatic external fun create(machineId: String): Long
    /** Returns null on success, or an error message. */
    @JvmStatic external fun load(handle: Long, names: Array<String>, paths: Array<String>): String?
    @JvmStatic external fun destroy(handle: Long)
    @JvmStatic external fun reset(handle: Long)
    /** [width, height, refresh * 1000, sampleRate] */
    @JvmStatic external fun videoInfo(handle: Long): IntArray
    @JvmStatic external fun setAnalog(handle: Long, stick: Int, x: Float, y: Float)
    /** Press console buttons once; the core holds them until the game reacts. */
    @JvmStatic external fun tapInputs(handle: Long, mask: Int): Boolean
    @JvmStatic external fun runFrame(handle: Long, inputs: Int, pixels: IntArray, audio: ShortArray): Int
    @JvmStatic external fun saveState(handle: Long, path: String): Boolean
    @JvmStatic external fun loadState(handle: Long, path: String): Boolean
}
