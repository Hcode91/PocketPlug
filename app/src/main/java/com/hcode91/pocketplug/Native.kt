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
    /** Real-time machines: fast-forward the clock silently. False if unsupported. */
    @JvmStatic external fun advanceTime(handle: Long, seconds: Double): Boolean
    @JvmStatic external fun setWallClock(handle: Long, localSeconds: Long)
    @JvmStatic external fun setOption(handle: Long, id: Int, value: Int)
    /** Computers: a key of the machine's own keyboard went down / up. */
    @JvmStatic external fun keyEvent(handle: Long, code: Int, down: Boolean)
    /** Load a disk image into a drive; null on success, else an error. */
    @JvmStatic external fun mediaInsert(handle: Long, drive: Int, path: String): String?
    /** Write a drive's image back if it changed. */
    @JvmStatic external fun mediaSave(handle: Long, drive: Int, path: String): Boolean
    @JvmStatic external fun mediaEject(handle: Long, drive: Int)
    /** Bit n = drive n busy recently. */
    @JvmStatic external fun mediaActivity(handle: Long): Int
    /** The sorter: the first sectors of the disc in a CHD, after a kind byte ('c' CD, 'd' DVD, 'g' GD-ROM); null if not a disc */
    @JvmStatic external fun chdDiscHead(path: String): ByteArray?
}
