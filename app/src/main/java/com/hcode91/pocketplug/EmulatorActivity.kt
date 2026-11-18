package com.hcode91.pocketplug

import android.app.Activity
import android.app.AlertDialog
import android.content.pm.ActivityInfo
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Bundle
import android.os.SystemClock
import android.view.KeyEvent
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.Toast

class EmulatorActivity : Activity() {

    companion object {
        /** Longest absence we fast-forward (a neglected pet won't survive it anyway). */
        const val MAX_CATCH_UP = 14 * 86400.0
        /** The arcade cabinet's VOLUME button steps through these. */
        val VOLUME_STEPS = floatArrayOf(1f, 0.55f, 0.25f, 0f)
    }

    private lateinit var machine: MachineDef
    private var handle = 0L
    private lateinit var tv: TvView
    private var mat: DanceMatView? = null
    private val pads = arrayOfNulls<ApvsControllerView>(2)
    private var pet: EggPetView? = null
    private var phone: PhoneCaseView? = null
    private var arcade: ArcadeCabinetView? = null
    private var handheld: HandheldView? = null
    @Volatile private var volume = 1f
    private var stickMask = 0
    // LCD pets keep living while the app is closed: the clock is fast-forwarded on return
    private var liveSavedAt = 0L
    @Volatile private var catchUpSeconds = 0.0
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
            Skin.PHONE_CASE -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val classic = machine.id == "36pcase"
                val v = PhoneCaseView(this, classic).apply {
                    onInput = { setTouch(0, it) }
                    onMenu = { showMenu() }
                    onReset = { synchronized(coreLock) { Native.reset(handle) } }
                    soundOn = prefs.getBoolean("sound_${machine.id}", true)
                    onSound = { on ->
                        volume = if (on) 1f else 0f
                        prefs.edit().putBoolean("sound_${machine.id}", on).apply()
                    }
                }
                if (classic) volume = if (v.soundOn) 1f else 0f
                phone = v
                setContentView(v)
            }
            Skin.HANDHELD -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val v = HandheldView(this).apply {
                    onInput = { setTouch(0, it) }
                    onMenu = { showMenu() }
                }
                handheld = v
                setContentView(v)
            }
            Skin.ARCADE_CABINET -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val level = prefs.getInt("volume_${machine.id}", 0).coerceIn(0, ArcadeCabinetView.VOLUME_LEVELS - 1)
                volume = VOLUME_STEPS[level]
                val v = ArcadeCabinetView(this).apply {
                    volumeLevel = level
                    onInput = { setTouch(0, it) }
                    onMenu = { showMenu() }
                    onReset = { synchronized(coreLock) { Native.reset(handle) } }
                    onVolume = {
                        volume = VOLUME_STEPS[it]
                        prefs.edit().putInt("volume_${machine.id}", it).apply()
                    }
                }
                arcade = v
                setContentView(v)
            }
            Skin.EGG_PET -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val p = EggPetView(this, petStyle()).apply {
                    onInput = { setTouch(0, it) }
                    onMenu = { showMenu() }
                }
                pet = p
                setContentView(p)
                // pick up the pet where it was left
                val live = liveFile()
                if (live.exists() && Native.loadState(handle, live.absolutePath))
                    liveSavedAt = prefs.getLong("live_${machine.id}", 0L)
            }
            Skin.DANCE_MAT -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val m = DanceMatView(this).apply { onInput = { setTouch(0, it) } }
                mat = m
                setContentView(StackLayout(this).apply {
                    setBackgroundColor(0xFF0B0814.toInt())
                    add(tv, 0.92f)   // TV cabinet incl. margins
                    add(m, 1.12f)    // mat incl. its SELECT/START strip
                })
            }
            Skin.APVS_1292 -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val console: View = if (machine.badge == "1392") {
                    Apvs1392ConsoleView(this, machine.badge, title).apply {
                        onInput = { setTouch(0, it) }
                        onTap = { Native.tapInputs(handle, it) }
                        onReset = { synchronized(coreLock) { Native.reset(handle) } }
                        onPower = { powerCycle() }
                    }
                } else {
                    ApvsConsoleView(this, machine.badge.ifEmpty { "1292" }, title).apply {
                        onInput = { setTouch(0, it) }
                        onTap = { Native.tapInputs(handle, it) }
                        onReset = { synchronized(coreLock) { Native.reset(handle) } }
                        onPower = { powerCycle() }
                    }
                }
                val row = android.widget.LinearLayout(this).apply { orientation = android.widget.LinearLayout.HORIZONTAL }
                for (pl in 0..1) {
                    val v = ApvsControllerView(this, pl, greyBody = machine.badge != "1392").apply {
                        onInput = { setTouch(1 + pl, it) }
                        onAnalog = { x, y -> Native.setAnalog(handle, pl, x, y) }
                    }
                    pads[pl] = v
                    row.addView(v, android.widget.LinearLayout.LayoutParams(0, android.widget.LinearLayout.LayoutParams.MATCH_PARENT, 1f))
                }
                setContentView(StackLayout(this).apply {
                    setBackgroundColor(0xFF0B0814.toInt())
                    add(tv, 0.92f)
                    add(console, 0.34f)
                    add(row, 0.92f)
                })
            }
        }
        hideSystemBars()
    }

    /** OFF/ON switch: the TV goes dark for a moment and the console restarts. */
    private fun powerCycle() {
        paused = true
        tv.powered = false
        tv.invalidate()
        tv.postDelayed({
            synchronized(coreLock) { Native.reset(handle) }
            tv.powered = true
            paused = false
        }, 700)
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

    private fun liveFile() = java.io.File(filesDir, "live").apply { mkdirs() }.let { java.io.File(it, "${machine.id}.state") }

    private fun realTime() = getSharedPreferences("settings", MODE_PRIVATE).getBoolean("realtime_${machine.id}", true)

    /** LCD pets: remember the exact state and when we left it. */
    private fun saveLive() {
        if (pet == null || handle == 0L) return
        if (synchronized(coreLock) { Native.saveState(handle, liveFile().absolutePath) }) {
            // time not yet fast-forwarded (app left mid catch-up) still counts next time
            liveSavedAt = System.currentTimeMillis() - (catchUpSeconds * 1000).toLong()
            getSharedPreferences("settings", MODE_PRIVATE).edit().putLong("live_${machine.id}", liveSavedAt).apply()
        }
    }

    // the angel model's vibration sensor: a real shake of the phone pulses K03
    private val shakeListener = object : SensorEventListener {
        private var lastShake = 0L
        override fun onSensorChanged(e: SensorEvent) {
            val g = Math.sqrt((e.values[0] * e.values[0] + e.values[1] * e.values[1] + e.values[2] * e.values[2]).toDouble()) / SensorManager.GRAVITY_EARTH
            val now = SystemClock.uptimeMillis()
            if (g > 1.8 && now - lastShake > 250) {
                lastShake = now
                setTouch(3, EggPetBits.SHAKE)
                pet?.externalMask = keyInputs or EggPetBits.SHAKE
                pet?.postDelayed({ setTouch(3, 0); pet?.externalMask = keyInputs }, 120)
            }
        }
        override fun onAccuracyChanged(s: Sensor?, a: Int) {}
    }

    override fun onResume() {
        super.onResume()
        if (handle != 0L) start()
        if (machine.badge == "angel" || machine.badge == "umino") {
            val sm = getSystemService(SENSOR_SERVICE) as SensorManager
            sm.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)?.let { sm.registerListener(shakeListener, it, SensorManager.SENSOR_DELAY_GAME) }
        }
    }

    override fun onPause() {
        (getSystemService(SENSOR_SERVICE) as SensorManager).unregisterListener(shakeListener)
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
        if (pet != null && liveSavedAt > 0 && realTime())
            catchUpSeconds = ((System.currentTimeMillis() - liveSavedAt) / 1000.0).coerceIn(0.0, MAX_CATCH_UP)
        running = true
        thread = Thread({ emuLoop() }, "PocketPlug-emu").apply { priority = Thread.MAX_PRIORITY; start() }
    }

    private fun stop() {
        running = false
        thread?.join(4000)
        thread = null
        saveLive()
    }

    /** Fast-forwards the pet's clock in one-minute steps, showing progress on its screen. */
    private fun catchUp() {
        val total = catchUpSeconds
        var left = total
        while (running && left > 0.5) {
            val step = minOf(left, 60.0)
            synchronized(coreLock) { Native.advanceTime(handle, step) }
            left -= step
            catchUpSeconds = left
            pet?.status = "Time passes...\n${formatSpan(total - left)} of ${formatSpan(total)}"
        }
        catchUpSeconds = 0.0
        pet?.status = null
    }

    /** Built-in pets keep the phone's local time and the chosen difficulty. */
    private fun syncPetClock() {
        if (machine.id != "oceanpet") return
        val now = System.currentTimeMillis()
        val local = (now + java.util.TimeZone.getDefault().getOffset(now)) / 1000
        synchronized(coreLock) {
            Native.setWallClock(handle, local)
            Native.setOption(handle, 1, if (gentle()) 1 else 0)
        }
    }

    private fun gentle() = getSharedPreferences("settings", MODE_PRIVATE).getBoolean("gentle_${machine.id}", false)

    private fun petStyle(): String = when (machine.badge) {
        "angel", "umino" -> machine.badge
        else -> getSharedPreferences("settings", MODE_PRIVATE).getString("shell_${machine.id}", "gen1") ?: "gen1"
    }

    private fun formatSpan(sec: Double): String {
        val s = sec.toLong()
        return when {
            s >= 86400 -> "${s / 86400}d ${s % 86400 / 3600}h"
            s >= 3600 -> "${s / 3600}h ${s % 3600 / 60}m"
            s >= 60 -> "${s / 60}m"
            else -> "${s}s"
        }
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

        if (catchUpSeconds > 1.0) catchUp()
        syncPetClock()

        val frameNs = (1_000_000_000.0 / refresh).toLong()
        var next = SystemClock.elapsedRealtimeNanos()
        var appliedVolume = -1f
        while (running) {
            if (track != null && volume != appliedVolume) { appliedVolume = volume; track.setVolume(volume) }
            if (paused) { Thread.sleep(16); next = SystemClock.elapsedRealtimeNanos(); continue }
            val n = synchronized(coreLock) { Native.runFrame(handle, touchInputs or keyInputs, pixels, audio) }
            if (pet != null) pet?.updateFrame(pixels)
            else if (phone != null) phone?.updateFrame(pixels)
            else if (arcade != null) arcade?.updateFrame(pixels)
            else if (handheld != null) handheld?.updateFrame(pixels)
            else tv.updateFrame(pixels)
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
        val items = ArrayList<Pair<String, () -> Unit>>()
        items += "Resume" to {}
        items += "Save state" to {
            val ok = synchronized(coreLock) { Native.saveState(handle, stateFile.absolutePath) }
            Toast.makeText(this, if (ok) "State saved" else "Couldn't save state", Toast.LENGTH_SHORT).show()
        }
        items += (if (stateFile.exists()) "Load state" else "Load state (none saved)") to {
            if (stateFile.exists()) {
                val ok = synchronized(coreLock) { Native.loadState(handle, stateFile.absolutePath) }
                Toast.makeText(this, if (ok) "State loaded" else "Couldn't load state", Toast.LENGTH_SHORT).show()
            }
        }
        if (pet != null) {
            items += "Reset (pin hole on the back)" to {
                paused = true
                AlertDialog.Builder(this)
                    .setMessage("Reset the pet? A new egg will appear and the current pet is gone.")
                    .setPositiveButton("Reset") { _, _ -> synchronized(coreLock) { Native.reset(handle) }; saveLive(); paused = false }
                    .setNegativeButton("Cancel") { _, _ -> paused = false }
                    .setOnCancelListener { paused = false }
                    .show()
            }
            if (machine.id == "tama") {
                items += (if (petStyle() == "reef") "Shell: deep-sea reef" else "Shell: classic teal") to {
                    prefs.edit().putString("shell_${machine.id}", if (petStyle() == "reef") "gen1" else "reef").apply()
                    saveLive()
                    recreate()
                }
            }
            if (machine.id == "oceanpet") {
                items += (if (gentle()) "Mode: gentle (never dies)" else "Mode: classic (can die)") to {
                    prefs.edit().putBoolean("gentle_${machine.id}", !gentle()).apply()
                    synchronized(coreLock) { Native.setOption(handle, 1, if (gentle()) 1 else 0) }
                    Toast.makeText(this, if (gentle()) "Gentle: neglect makes it sad or sick, but it never dies"
                        else "Classic: like the original, neglect can be fatal", Toast.LENGTH_LONG).show()
                }
            }
            items += (if (realTime()) "While closed: time keeps running" else "While closed: time stops") to {
                prefs.edit().putBoolean("realtime_${machine.id}", !realTime()).apply()
                Toast.makeText(this, if (realTime()) "Your pet lives on while the app is closed"
                    else "Your pet's time stops while the app is closed", Toast.LENGTH_LONG).show()
            }
        } else {
            items += "Reset" to { synchronized(coreLock) { Native.reset(handle) } }
            items += (if (tv.scanlines) "Scanlines: on" else "Scanlines: off") to {
                tv.scanlines = !tv.scanlines
                prefs.edit().putBoolean("scanlines", tv.scanlines).apply()
                tv.invalidate()
            }
        }
        items += "Quit to library" to { finish() }
        AlertDialog.Builder(this)
            .setTitle(title)
            .setItems(items.map { it.first }.toTypedArray()) { _, which ->
                paused = false
                items[which].second()
            }
            .setOnCancelListener { paused = false }
            .show()
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() { showMenu() }

    // ---- gamepad / keyboard ----
    private fun keyBit(code: Int): Int = when (machine.skin) {
        Skin.APVS_1292 -> apvsKeyBit(code)
        Skin.EGG_PET -> petKeyBit(code)
        Skin.PHONE_CASE, Skin.ARCADE_CABINET -> caseKeyBit(code)
        Skin.HANDHELD -> handheldKeyBit(code)
        else -> danceKeyBit(code)
    }

    private fun petKeyBit(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_1, KeyEvent.KEYCODE_Z -> EggPetBits.A
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_2, KeyEvent.KEYCODE_X -> EggPetBits.B
        KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_3, KeyEvent.KEYCODE_C -> EggPetBits.C
        KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_4, KeyEvent.KEYCODE_S -> EggPetBits.SHAKE
        else -> 0
    }

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
        if (machine.skin == Skin.HANDHELD && event.action == android.view.MotionEvent.ACTION_MOVE &&
            (event.source and android.view.InputDevice.SOURCE_JOYSTICK) == android.view.InputDevice.SOURCE_JOYSTICK) {
            val x = event.getAxisValue(android.view.MotionEvent.AXIS_X) + event.getAxisValue(android.view.MotionEvent.AXIS_HAT_X)
            val y = event.getAxisValue(android.view.MotionEvent.AXIS_Y) + event.getAxisValue(android.view.MotionEvent.AXIS_HAT_Y)
            var m = 0
            if (y < -0.5f) m = m or HandheldView.UP
            if (y > 0.5f) m = m or HandheldView.DOWN
            if (x < -0.5f) m = m or HandheldView.LEFT
            if (x > 0.5f) m = m or HandheldView.RIGHT
            if (m != stickMask) { stickMask = m; setTouch(3, m); handheld?.externalMask = keyInputs or m }
            return true
        }
        if ((machine.skin == Skin.PHONE_CASE || machine.skin == Skin.ARCADE_CABINET) && event.action == android.view.MotionEvent.ACTION_MOVE &&
            (event.source and android.view.InputDevice.SOURCE_JOYSTICK) == android.view.InputDevice.SOURCE_JOYSTICK) {
            // left stick and hat both drive the D-pad
            val x = event.getAxisValue(android.view.MotionEvent.AXIS_X) + event.getAxisValue(android.view.MotionEvent.AXIS_HAT_X)
            val y = event.getAxisValue(android.view.MotionEvent.AXIS_Y) + event.getAxisValue(android.view.MotionEvent.AXIS_HAT_Y)
            var m = 0
            if (y < -0.5f) m = m or 0x10
            if (y > 0.5f) m = m or 0x20
            if (x < -0.5f) m = m or 0x40
            if (x > 0.5f) m = m or 0x80
            if (m != stickMask) { stickMask = m; setTouch(3, m); phone?.externalMask = keyInputs or m; arcade?.externalMask = keyInputs or m }
            return true
        }
        if (machine.skin == Skin.APVS_1292 && event.action == android.view.MotionEvent.ACTION_MOVE &&
            (event.source and android.view.InputDevice.SOURCE_JOYSTICK) == android.view.InputDevice.SOURCE_JOYSTICK) {
            Native.setAnalog(handle, 0, event.getAxisValue(android.view.MotionEvent.AXIS_X), event.getAxisValue(android.view.MotionEvent.AXIS_Y))
            Native.setAnalog(handle, 1, event.getAxisValue(android.view.MotionEvent.AXIS_Z), event.getAxisValue(android.view.MotionEvent.AXIS_RZ))
            return true
        }
        return super.onGenericMotionEvent(event)
    }

    // NES-style pad: A 0x01, B 0x02, Select 0x04, Start 0x08, Up 0x10, Down 0x20, Left 0x40, Right 0x80
    private fun caseKeyBit(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_X -> 0x01
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_Z -> 0x02
        KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_SPACE, KeyEvent.KEYCODE_SHIFT_RIGHT -> 0x04
        KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_ENTER -> 0x08
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> 0x10
        KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> 0x20
        KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> 0x40
        KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> 0x80
        else -> 0
    }

    private fun handheldKeyBit(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_X -> HandheldView.A
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_Z -> HandheldView.B
        KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_ENTER -> HandheldView.START
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> HandheldView.UP
        KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> HandheldView.DOWN
        KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> HandheldView.LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> HandheldView.RIGHT
        else -> 0
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
        pet?.externalMask = keyInputs
        phone?.externalMask = keyInputs or stickMask
        arcade?.externalMask = keyInputs or stickMask
        handheld?.externalMask = keyInputs or stickMask
        pads.forEach { it?.externalMask = keyInputs }
        return true
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        if (machine.skin == Skin.APVS_1292 && dpadBit(keyCode) != 0) { dpad = dpad and dpadBit(keyCode).inv(); applyDpad(); return true }
        val b = keyBit(keyCode)
        if (b == 0) return super.onKeyUp(keyCode, event)
        keyInputs = keyInputs and b.inv()
        mat?.externalMask = keyInputs
        pet?.externalMask = keyInputs
        phone?.externalMask = keyInputs or stickMask
        arcade?.externalMask = keyInputs or stickMask
        handheld?.externalMask = keyInputs or stickMask
        pads.forEach { it?.externalMask = keyInputs }
        return true
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()
    }
}
