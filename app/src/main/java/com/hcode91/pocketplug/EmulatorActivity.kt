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
    private val pads = arrayOfNulls<ApvsControllerView>(2)
    private var cartFile: java.io.File? = null
    private var title = ""

    // inputs from the on-screen controls, one slot per control surface
    private val touchSources = IntArray(4)
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
        title = intent.getStringExtra("title") ?: machine.title
        val names: Array<String>
        val paths: Array<String>
        if (machine.media == MediaKind.CARTRIDGE) {
            val f = java.io.File(intent.getStringExtra("cart") ?: "")
            cartFile = f
            names = arrayOf(f.name)
            paths = arrayOf(f.absolutePath)
        } else {
            names = machine.roms.map { it.name }.toTypedArray()
            paths = machine.roms.map { RomStore.romFile(this, machine, it).absolutePath }.toTypedArray()
        }
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
                val m = DanceMatView(this).apply { onInput = { setTouch(0, it) } }
                mat = m
                setContentView(StackLayout(this).apply {
                    setBackgroundColor(0xFF0B0814.toInt())
                    add(tv, 0.92f)   // TV cabinet incl. margins
                    add(m, 1.0f)     // square mat
                })
            }
            Skin.APVS_1292 -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val console = ApvsConsoleView(this).apply {
                    onInput = { setTouch(0, it) }
                    onTap = { Native.tapInputs(handle, it) }
                    onReset = { synchronized(coreLock) { Native.reset(handle) } }
                }
                val row = android.widget.LinearLayout(this).apply { orientation = android.widget.LinearLayout.HORIZONTAL }
                for (pl in 0..1) {
                    val v = ApvsControllerView(this, pl).apply {
                        onInput = { setTouch(1 + pl, it) }
                        onAnalog = { x, y -> Native.setAnalog(handle, pl, x, y) }
                    }
                    pads[pl] = v
                    row.addView(v, android.widget.LinearLayout.LayoutParams(0, android.widget.LinearLayout.LayoutParams.MATCH_PARENT, 1f))
                }
                setContentView(StackLayout(this).apply {
                    setBackgroundColor(0xFF0B0814.toInt())
                    add(tv, 0.92f)
                    add(console, 0.2f)
                    add(row, 0.92f)
                })
            }
        }
        hideSystemBars()
    }

    private fun setTouch(source: Int, mask: Int) {
        touchSources[source] = mask
        touchInputs = touchSources.fold(0) { a, b -> a or b }
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
        val stateFile = cartFile?.let { RomStore.cartStateFile(this, machine, it) } ?: RomStore.stateFile(this, machine)
        val items = arrayOf(
            "Resume",
            "Save state",
            if (stateFile.exists()) "Load state" else "Load state (none saved)",
            "Reset",
            if (tv.scanlines) "Scanlines: on" else "Scanlines: off",
            "Quit to library",
        )
        AlertDialog.Builder(this)
            .setTitle(title)
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
    private fun keyBit(code: Int): Int = if (machine.skin == Skin.APVS_1292) apvsKeyBit(code) else danceKeyBit(code)

    private fun apvsKeyBit(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_SPACE -> ApvsBits.fire(0)
        KeyEvent.KEYCODE_BUTTON_START -> ApvsBits.START
        KeyEvent.KEYCODE_BUTTON_SELECT -> ApvsBits.SELECT
        KeyEvent.KEYCODE_1, KeyEvent.KEYCODE_NUMPAD_1 -> ApvsBits.key(0, ApvsBits.K1)
        KeyEvent.KEYCODE_2, KeyEvent.KEYCODE_NUMPAD_2 -> ApvsBits.key(0, ApvsBits.K2)
        KeyEvent.KEYCODE_3, KeyEvent.KEYCODE_NUMPAD_3 -> ApvsBits.key(0, ApvsBits.K3)
        KeyEvent.KEYCODE_4, KeyEvent.KEYCODE_NUMPAD_4 -> ApvsBits.key(0, ApvsBits.K4)
        KeyEvent.KEYCODE_5, KeyEvent.KEYCODE_NUMPAD_5 -> ApvsBits.key(0, ApvsBits.K5)
        KeyEvent.KEYCODE_6, KeyEvent.KEYCODE_NUMPAD_6 -> ApvsBits.key(0, ApvsBits.K6)
        KeyEvent.KEYCODE_7, KeyEvent.KEYCODE_NUMPAD_7 -> ApvsBits.key(0, ApvsBits.K7)
        KeyEvent.KEYCODE_8, KeyEvent.KEYCODE_NUMPAD_8 -> ApvsBits.key(0, ApvsBits.K8)
        KeyEvent.KEYCODE_9, KeyEvent.KEYCODE_NUMPAD_9 -> ApvsBits.key(0, ApvsBits.K9)
        KeyEvent.KEYCODE_0, KeyEvent.KEYCODE_NUMPAD_0 -> ApvsBits.key(0, ApvsBits.K0)
        KeyEvent.KEYCODE_DEL, KeyEvent.KEYCODE_BUTTON_X -> ApvsBits.key(0, ApvsBits.KCLEAR)
        KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER, KeyEvent.KEYCODE_BUTTON_Y -> ApvsBits.key(0, ApvsBits.KENTER)
        else -> 0
    }

    // d-pad / arrow keys steer player 1's stick fully
    private var dpad = 0
    private fun dpadBit(code: Int) = when (code) {
        KeyEvent.KEYCODE_DPAD_LEFT -> 1; KeyEvent.KEYCODE_DPAD_RIGHT -> 2
        KeyEvent.KEYCODE_DPAD_UP -> 4; KeyEvent.KEYCODE_DPAD_DOWN -> 8
        else -> 0
    }
    private fun applyDpad() {
        val x = (if (dpad and 2 != 0) 1f else 0f) - (if (dpad and 1 != 0) 1f else 0f)
        val y = (if (dpad and 8 != 0) 1f else 0f) - (if (dpad and 4 != 0) 1f else 0f)
        Native.setAnalog(handle, 0, x, y)
    }

    override fun onGenericMotionEvent(event: android.view.MotionEvent): Boolean {
        if (machine.skin == Skin.APVS_1292 && event.action == android.view.MotionEvent.ACTION_MOVE &&
            (event.source and android.view.InputDevice.SOURCE_JOYSTICK) == android.view.InputDevice.SOURCE_JOYSTICK) {
            Native.setAnalog(handle, 0, event.getAxisValue(android.view.MotionEvent.AXIS_X), event.getAxisValue(android.view.MotionEvent.AXIS_Y))
            Native.setAnalog(handle, 1, event.getAxisValue(android.view.MotionEvent.AXIS_Z), event.getAxisValue(android.view.MotionEvent.AXIS_RZ))
            return true
        }
        return super.onGenericMotionEvent(event)
    }

    private fun danceKeyBit(code: Int): Int = when (code) {
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
        if (machine.skin == Skin.APVS_1292 && dpadBit(keyCode) != 0) { dpad = dpad or dpadBit(keyCode); applyDpad(); return true }
        val b = keyBit(keyCode)
        if (b == 0) return super.onKeyDown(keyCode, event)
        if (machine.skin == Skin.APVS_1292 && event.repeatCount == 0 && (b and (ApvsBits.START or ApvsBits.SELECT)) != 0)
            Native.tapInputs(handle, b)
        keyInputs = keyInputs or b
        mat?.externalMask = keyInputs
        pads.forEach { it?.externalMask = keyInputs }
        return true
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        if (machine.skin == Skin.APVS_1292 && dpadBit(keyCode) != 0) { dpad = dpad and dpadBit(keyCode).inv(); applyDpad(); return true }
        val b = keyBit(keyCode)
        if (b == 0) return super.onKeyUp(keyCode, event)
        keyInputs = keyInputs and b.inv()
        mat?.externalMask = keyInputs
        pads.forEach { it?.externalMask = keyInputs }
        return true
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()
    }
}
