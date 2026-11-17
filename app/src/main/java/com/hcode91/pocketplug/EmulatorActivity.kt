package com.hcode91.pocketplug

import android.app.Activity
import android.app.AlertDialog
import android.content.pm.ActivityInfo
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.os.Bundle
import android.os.SystemClock
import android.view.KeyEvent
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.Toast

class EmulatorActivity : Activity() {

    private lateinit var machine: MachineDef
    private var handle = 0L
    private lateinit var tv: TvView
    private var mat: DanceMatView? = null

    @Volatile private var touchInputs = 0
    @Volatile private var keyInputs = 0
    @Volatile private var running = false
    @Volatile private var paused = false
    private var thread: Thread? = null
    private val coreLock = Any()

    private var width = 256
    private var height = 240
    private var refresh = 50.0
    private var sampleRate = 44100

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        machine = Machines.byId(intent.getStringExtra("machine") ?: "") ?: run { finish(); return }
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        handle = Native.create(machine.id)
        val names = machine.roms.map { it.name }.toTypedArray()
        val paths = machine.roms.map { RomStore.romFile(this, machine, it).absolutePath }.toTypedArray()
        val err = if (handle != 0L) Native.load(handle, names, paths) else "This machine isn't supported by the core"
        if (err != null) {
            Toast.makeText(this, err, Toast.LENGTH_LONG).show()
            finish(); return
        }
        Native.videoInfo(handle).let { width = it[0]; height = it[1]; refresh = it[2] / 1000.0; sampleRate = it[3] }

        val prefs = getSharedPreferences("settings", MODE_PRIVATE)
        tv = TvView(this, width, height).apply {
            scanlines = prefs.getBoolean("scanlines", true)
            onMenu = { showMenu() }
        }

        when (machine.skin) {
            Skin.DANCE_MAT -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val m = DanceMatView(this).apply { onInput = { touchInputs = it } }
                mat = m
                setContentView(StackLayout(this).apply {
                    setBackgroundColor(0xFF0B0814.toInt())
                    add(tv, 0.92f)   // TV cabinet incl. margins
                    add(m, 1.0f)     // square mat
                })
            }
        }
        hideSystemBars()
    }

    private fun hideSystemBars() {
        window.insetsController?.let {
            it.hide(WindowInsets.Type.systemBars())
            it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    }

    override fun onResume() {
        super.onResume()
        if (handle != 0L) start()
    }

    override fun onPause() {
        stop()
        super.onPause()
    }

    override fun onDestroy() {
        stop()
        if (handle != 0L) { Native.destroy(handle); handle = 0L }
        super.onDestroy()
    }

    private fun start() {
        if (running) return
        running = true
        thread = Thread({ emuLoop() }, "PocketPlug-emu").apply { priority = Thread.MAX_PRIORITY; start() }
    }

    private fun stop() {
        running = false
        thread?.join(2000)
        thread = null
    }

    private fun emuLoop() {
        val pixels = IntArray(width * height)
        val audio = ShortArray(8192)
        val minBuf = AudioTrack.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT)
        val track = runCatching {
            AudioTrack.Builder()
                .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setAudioFormat(AudioFormat.Builder().setSampleRate(sampleRate)
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT).setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
                .setBufferSizeInBytes(maxOf(minBuf, sampleRate / 10 * 2))
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build().also { it.play() }
        }.getOrNull()

        val frameNs = (1_000_000_000.0 / refresh).toLong()
        var next = SystemClock.elapsedRealtimeNanos()
        while (running) {
            if (paused) { Thread.sleep(16); next = SystemClock.elapsedRealtimeNanos(); continue }
            val n = synchronized(coreLock) { Native.runFrame(handle, touchInputs or keyInputs, pixels, audio) }
            tv.updateFrame(pixels)
            if (track != null && n > 0) {
                // a blocking write paces emulation to the audio clock
                track.write(audio, 0, n)
            } else {
                next += frameNs
                val wait = next - SystemClock.elapsedRealtimeNanos()
                if (wait > 0) Thread.sleep(wait / 1_000_000, (wait % 1_000_000).toInt())
                else if (wait < -frameNs * 5) next = SystemClock.elapsedRealtimeNanos()
            }
        }
        track?.run { pause(); flush(); release() }
    }

    private fun showMenu() {
        paused = true
        val prefs = getSharedPreferences("settings", MODE_PRIVATE)
        val stateFile = RomStore.stateFile(this, machine)
        val items = arrayOf(
            "Resume",
            "Save state",
            if (stateFile.exists()) "Load state" else "Load state (none saved)",
            "Reset",
            if (tv.scanlines) "Scanlines: on" else "Scanlines: off",
            "Quit to library",
        )
        AlertDialog.Builder(this)
            .setTitle(machine.title)
            .setItems(items) { _, which ->
                when (which) {
                    1 -> {
                        val ok = synchronized(coreLock) { Native.saveState(handle, stateFile.absolutePath) }
                        Toast.makeText(this, if (ok) "State saved" else "Couldn't save state", Toast.LENGTH_SHORT).show()
                    }
                    2 -> if (stateFile.exists()) {
                        val ok = synchronized(coreLock) { Native.loadState(handle, stateFile.absolutePath) }
                        Toast.makeText(this, if (ok) "State loaded" else "Couldn't load state", Toast.LENGTH_SHORT).show()
                    }
                    3 -> synchronized(coreLock) { Native.reset(handle) }
                    4 -> {
                        tv.scanlines = !tv.scanlines
                        prefs.edit().putBoolean("scanlines", tv.scanlines).apply()
                        tv.invalidate()
                    }
                    5 -> { finish(); return@setItems }
                }
                paused = false
            }
            .setOnCancelListener { paused = false }
            .show()
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() { showMenu() }

    // ---- gamepad / keyboard ----
    private fun keyBit(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> DanceMatBits.UP
        KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> DanceMatBits.DOWN
        KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> DanceMatBits.LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> DanceMatBits.RIGHT
        KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_Q -> DanceMatBits.UP_LEFT
        KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_E -> DanceMatBits.UP_RIGHT
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_ENTER -> DanceMatBits.START
        KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_ESCAPE -> DanceMatBits.BACK
        else -> 0
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        val b = keyBit(keyCode)
        if (b == 0) return super.onKeyDown(keyCode, event)
        keyInputs = keyInputs or b
        mat?.externalMask = keyInputs
        return true
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        val b = keyBit(keyCode)
        if (b == 0) return super.onKeyUp(keyCode, event)
        keyInputs = keyInputs and b.inv()
        mat?.externalMask = keyInputs
        return true
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()
    }
}
