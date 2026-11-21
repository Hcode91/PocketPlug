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
    private val apfPads = arrayOfNulls<ApfControllerView>(2)
    private var a3dPad: Arcade3dPadView? = null
    private var mini: MiniArcadeView? = null
    private var pet: EggPetView? = null
    private var phone: PhoneCaseView? = null
    private var arcade: ArcadeCabinetView? = null
    private var handheld: HandheldView? = null
    private var nixie: NixieClockView? = null
    private var joypad: JoystickPadView? = null
    private var ablPad: AblPadView? = null
    private var flightStick: FlightStickView? = null
    private var keychain: KeychainGameView? = null
    private var hh: HhView? = null
    private val linkedFds = ArrayList<android.os.ParcelFileDescriptor>()
    private var chess: ChessComputerView? = null
    private var kit: KitBoardView? = null
    private var tabletop: TabletopView? = null
    private var adam: AdamView? = null
    private var aim: AimView? = null
    private var altair: AltairView? = null
    private var wide: WideHandheldView? = null
    private var uboard: UniversityBoardView? = null
    private var workstation: DesktopSkin? = null
    private var atari: AtariXlView? = null
    private var acorn: AcornView? = null
    private var atariStick = 0
    /** Atari models without BASIC inside: the menu plugs a BASIC cartridge in */
    private val atariNoBasic = setOf("1200xl", "400", "800")
    // 3B1 mouse: motion gathered between frames
    private val mouseLock = Any()
    private var mouseDx = 0f
    private var mouseDy = 0f
    // computer keyboards: key events go to the core between frames
    private val keyQueue = java.util.concurrent.ConcurrentLinkedQueue<Int>()
    private var floppyFile: java.io.File? = null
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
            val cartPath = intent.getStringExtra("cart") ?: ""
            val f = java.io.File(cartPath)
            cartFile = if (cartPath.isEmpty()) null else f
            // consoles with a BIOS get its files first, then the cartridge (none: the built-in game)
            val extra = if (cartPath.isEmpty()) emptyList() else listOf(f)
            names = (machine.roms.map { it.name } + extra.map { it.name }).toTypedArray()
            paths = (machine.roms.map { RomStore.romFile(this, machine, it).absolutePath } + extra.map { it.absolutePath }).toTypedArray()
        } else {
            names = machine.roms.map { it.name }.toTypedArray()
            // files left in the user's folder are read through an open descriptor
            paths = machine.roms.map { spec ->
                RomStore.linkOf(this, machine, spec)?.let { u ->
                    runCatching { contentResolver.openFileDescriptor(u, "r") }.getOrNull()?.let { pfd ->
                        linkedFds += pfd
                        "/proc/self/fd/${pfd.fd}"
                    } ?: run {
                        Toast.makeText(this, "${spec.name} isn't in your ROM folder any more - rescan it", Toast.LENGTH_LONG).show()
                        finish(); return
                    }
                } ?: RomStore.romFile(this, machine, spec).absolutePath
            }.toTypedArray()
        }
        val err = if (handle != 0L) Native.load(handle, names, paths) else "This machine isn't supported by the core"
        if (err != null) {
            Toast.makeText(this, err, Toast.LENGTH_LONG).show()
            finish(); return
        }
        Native.videoInfo(handle).let { width = it[0]; height = it[1]; refresh = it[2] / 1000.0; sampleRate = it[3] }

        val prefs = getSharedPreferences("settings", MODE_PRIVATE)
        // recently played, newest first, for the machine list
        val recent = (prefs.getString("recent", "") ?: "").split(',').filter { it.isNotEmpty() && it != machine.id }
        prefs.edit().putString("recent", (listOf(machine.id) + recent).take(8).joinToString(",")).apply()
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
            Skin.WORKSTATION -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val v = WorkstationView(this).apply {
                    onKey = { code, down -> keyQueue.add(code or (if (down) 0x100 else 0)) }
                    onMenu = { showMenu() }
                    onDisks = { showDiskMenu() }
                }
                workstation = v
                setContentView(v)
                attachDisks()
            }
            Skin.ACORN -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val v = AcornView(this, machine.badge).apply {
                    onKey = { code, down -> keyQueue.add(code or (if (down) 0x100 else 0)) }
                    onMenu = { showMenu() }
                    onDisks = { showAcornMenu() }
                    onMouseMove = { dx, dy -> synchronized(mouseLock) { mouseDx += dx; mouseDy += dy } }
                    onMouseButtons = { setTouch(0, it) }
                }
                acorn = v
                setContentView(v)
                // an Archimedes started with another of its operating systems
                archiOs[machine.id]?.let { (_, def) ->
                    val os = prefs.getInt("os_${machine.id}", def)
                    if (os != def) Native.setOption(handle, 1, os)
                }
                // the core starts with 4 MB; "as sold" goes back to the original memory
                if (machine.id in archiAsSold && !prefs.getBoolean("mem4_${machine.id}", true))
                    Native.setOption(handle, 2, 0)
            }
            Skin.DESKTOP_3B1 -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val v = Desktop3b1View(this).apply {
                    onKey = { code, down -> keyQueue.add(code or (if (down) 0x100 else 0)) }
                    onMenu = { showMenu() }
                    onDisks = { showDiskMenu() }
                    onMouseMove = { dx, dy -> synchronized(mouseLock) { mouseDx += dx; mouseDy += dy } }
                    onMouseButtons = { setTouch(0, it) }
                }
                workstation = v
                setContentView(v)
                attachDisks()
            }
            Skin.ATARI_XL -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val v = AtariXlView(this, when (machine.badge) {
                    "130xe", "65xe", "800xe" -> AtariXlView.MODEL_XE
                    "400" -> AtariXlView.MODEL_400
                    "800" -> AtariXlView.MODEL_800
                    "800xl" -> AtariXlView.MODEL_800XL
                    "600xl" -> AtariXlView.MODEL_600XL
                    else -> AtariXlView.MODEL_1200XL
                }).apply {
                    onKey = { code, down -> keyQueue.add(code or (if (down) 0x100 else 0)) }
                    onMenu = { showMenu() }
                    onDisks = { showAtariMenu() }
                    onJoy = { setTouch(0, it) }
                }
                atari = v
                setContentView(v)
                if (machine.badge in atariNoBasic && prefs.getBoolean("basic_${machine.id}", false))
                    Native.setOption(handle, 1, 1)
            }
            Skin.NIXIE_CLOCK -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val v = NixieClockView(this).apply {
                    tickTock = prefs.getBoolean("ticktock_${machine.id}", true)
                    mainsHz = prefs.getInt("mains_${machine.id}", 50)
                    onInput = { setTouch(0, it) }
                    onMenu = { showMenu() }
                    onTickTock = { on ->
                        prefs.edit().putBoolean("ticktock_${machine.id}", on).apply()
                        synchronized(coreLock) { Native.setOption(handle, 1, if (on) 1 else 0) }
                    }
                    onMains = { hz ->
                        prefs.edit().putInt("mains_${machine.id}", hz).apply()
                        synchronized(coreLock) { Native.setOption(handle, 2, hz) }
                    }
                }
                nixie = v
                setContentView(v)
                // the chosen display and retirement date stay as they were left
                val live = liveFile()
                if (live.exists()) Native.loadState(handle, live.absolutePath)
            }
            Skin.JOYSTICK_TV -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val m = JoystickPadView(this).apply { onInput = { setTouch(0, it) } }
                joypad = m
                setContentView(StackLayout(this).apply {
                    setBackgroundColor(0xFF0B0814.toInt())
                    add(tv, 0.92f)
                    add(m, 0.8f)
                })
            }
            Skin.CHESS -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val v = ChessComputerView(this, machine.badge).apply {
                    onInput = { setTouch(0, it) }
                    onBoard = { code -> keyQueue.add(code or 0x100) }
                    onMenu = { showMenu() }
                }
                chess = v
                setContentView(v)
            }
            Skin.ADAM -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val v = AdamView(this, width, height).apply {
                    onKey = { code, down -> keyQueue.add(code or (if (down) 0x100 else 0)) }
                    onPad = { setTouch(0, it) }
                    onMenu = { showMenu() }
                    onMedia = { showAdamMenu() }
                }
                adam = v
                setContentView(v)
            }
            Skin.ALTAIR -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
                val vt = prefs.getInt("altair_term", 0)
                Native.setOption(handle, 0, vt)
                val v = AltairView(this, machine.id == "al8800bt").apply {
                    vtMode = vt
                    onKey = { code -> keyQueue.add(code or 0x100) }
                    onInput = { setTouch(0, it) }
                    onMenu = { showMenu() }
                    onDisks = { showAltairMenu() }
                    onPower = {
                        synchronized(coreLock) { Native.reset(handle) }
                        Toast.makeText(this@EmulatorActivity, "Power off and on: memory is cleared", Toast.LENGTH_SHORT).show()
                    }
                }
                altair = v
                setContentView(v)
            }
            Skin.AIM65 -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
                val v = AimView(this).apply {
                    onKey = { code, down -> keyQueue.add(code or (if (down) 0x100 else 0)) }
                    onReset = { setTouch(0, if (it) 1 else 0) }
                    onMenu = { showMenu() }
                }
                aim = v
                setContentView(v)
            }
            Skin.TABLETOP -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val muted = prefs.getBoolean("mute_${machine.id}", false)
                if (muted) volume = 0f
                val v = TabletopView(this, width, height).apply {
                    this.muted = muted
                    onInput = { setTouch(0, it) }
                    onMenu = { showMenu() }
                    onMute = { m ->
                        volume = if (m) 0f else 1f
                        prefs.edit().putBoolean("mute_${machine.id}", m).apply()
                    }
                }
                tabletop = v
                setContentView(v)
            }
            Skin.KIT -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
                val v = KitBoardView(this, machine.badge).apply {
                    onInput = { setTouch(0, it) }
                    onMenu = { showMenu() }
                }
                kit = v
                setContentView(v)
            }
            Skin.HH -> {
                val hhSkin = HhSkin.forMachine(machine.id)!!
                requestedOrientation = if (hhSkin.landscape) ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE else ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val v = HhView(this, hhSkin, width, height).apply {
                    onInput = { setTouch(0, it) }
                    onOption = { id, value -> synchronized(coreLock) { Native.setOption(handle, id, value) } }
                    onMenu = { showMenu() }
                }
                for ((id, value) in v.initialOptions()) Native.setOption(handle, id, value)
                hh = v
                setContentView(v)
            }
            Skin.KEYCHAIN -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val v = KeychainGameView(this).apply {
                    onInput = { setTouch(0, it) }
                    onMenu = { showMenu() }
                }
                keychain = v
                setContentView(v)
            }
            Skin.FLIGHT_STICK -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val m = FlightStickView(this).apply { onInput = { setTouch(0, it) } }
                flightStick = m
                setContentView(StackLayout(this).apply {
                    setBackgroundColor(0xFF0E0F12.toInt())
                    add(tv, 0.875f)
                    add(m, 1.1f)
                })
            }
            Skin.ABL_TV -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val m = AblPadView(this, machine.badge).apply {
                    onInput = { setTouch(0, it) }
                    onReset = { synchronized(coreLock) { Native.reset(handle) } }
                }
                ablPad = m
                setContentView(StackLayout(this).apply {
                    setBackgroundColor(0xFF0B0814.toInt())
                    add(tv, 0.92f)
                    add(m, 0.8f)
                })
            }
            Skin.WIDE_HANDHELD -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
                val v = WideHandheldView(this).apply {
                    onInput = { setTouch(0, it) }
                    onMenu = { showMenu() }
                }
                wide = v
                setContentView(v)
            }
            Skin.UNIVERSITY_BOARD -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val v = UniversityBoardView(this, machine.id == "990189v").apply {
                    onKey = { code, down -> keyQueue.add(code or (if (down) 0x100 else 0)) }
                    onMenu = { showMenu() }
                    onJoy = { setTouch(0, it) }
                }
                uboard = v
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
                val v = ArcadeCabinetView(this, a6 = machine.id == "a6plus").apply {
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
            Skin.APF -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val console = ApfConsoleView(this, if (cartFile != null) title.toString() else "").apply {
                    onReset = { synchronized(coreLock) { Native.reset(handle) } }
                    onPower = { powerCycle() }
                }
                val row = android.widget.LinearLayout(this).apply { orientation = android.widget.LinearLayout.HORIZONTAL }
                for (pl in 0..1) {
                    val v = ApfControllerView(this, pl).apply { onInput = { setTouch(1 + pl, it) } }
                    apfPads[pl] = v
                    row.addView(v, android.widget.LinearLayout.LayoutParams(0, android.widget.LinearLayout.LayoutParams.MATCH_PARENT, 1f))
                }
                setContentView(StackLayout(this).apply {
                    setBackgroundColor(0xFF0B0814.toInt())
                    add(tv, 0.75f)
                    add(console, 0.34f)
                    add(row, 0.95f)
                })
            }
            Skin.MINI_ARCADE -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val v = MiniArcadeView(this).apply {
                    onInput = { setTouch(0, it) }
                    onMenu = { showMenu() }
                    onReset = { synchronized(coreLock) { Native.reset(handle) } }
                }
                mini = v
                setContentView(v)
            }
            Skin.TV_CONSOLE -> {
                requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT
                val console = Arcade3dConsoleView(this).apply {
                    onReset = { synchronized(coreLock) { Native.reset(handle) } }
                    onPower = { powerCycle() }
                }
                val pad = Arcade3dPadView(this).apply { onInput = { setTouch(1, it) } }
                a3dPad = pad
                setContentView(StackLayout(this).apply {
                    setBackgroundColor(0xFF0B0814.toInt())
                    add(tv, 0.85f)
                    add(console, 0.42f)
                    add(pad, 0.5f)
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
        attachSetupBubble()
        hideSystemBars()
    }

    // ---- getting-started steps (machines that need more than loading the ROM) ----
    private var setupBubble: SetupBubble? = null

    private fun attachSetupBubble() {
        if (machine.setup.isEmpty()) return
        val prefs = getSharedPreferences("settings", MODE_PRIVATE)
        // open the first time; afterwards just the "?" bubble
        val seen = prefs.getBoolean("setup_seen_${machine.id}", false)
        val b = SetupBubble(this, machine.title, machine.setup, startOpen = !seen) {
            prefs.edit().putBoolean("setup_hidden_${machine.id}", true).apply()
        }
        if (prefs.getBoolean("setup_hidden_${machine.id}", false)) b.visibility = android.view.View.GONE
        prefs.edit().putBoolean("setup_seen_${machine.id}", true).apply()
        addContentView(b, android.view.ViewGroup.LayoutParams(
            android.view.ViewGroup.LayoutParams.MATCH_PARENT, android.view.ViewGroup.LayoutParams.MATCH_PARENT))
        setupBubble = b
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
        if (nixie != null && handle != 0L) { synchronized(coreLock) { Native.saveState(handle, liveFile().absolutePath) }; return }
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
        saveDisks()
        super.onPause()
    }

    // ---- disks (computers) ----
    private fun diskDir() = java.io.File(filesDir, "disks/${machine.id}").apply { mkdirs() }
    private fun floppyDir() = java.io.File(diskDir(), "floppies").apply { mkdirs() }
    private fun hardDiskExt() = if (machine.skin == Skin.DESKTOP_3B1) "img" else "ngenhd"
    private fun hardDiskFile() = java.io.File(diskDir(), "harddisk.${hardDiskExt()}")

    /** Winchester drive 0 always attached (a new one starts unformatted); the last floppy goes back in. */
    private fun attachDisks() {
        Native.mediaInsert(handle, 1, hardDiskFile().absolutePath)?.let {
            Toast.makeText(this, "Hard disk: $it", Toast.LENGTH_LONG).show()
        }
        val last = getSharedPreferences("settings", MODE_PRIVATE).getString("floppy_${machine.id}", null)
        val f = last?.let { java.io.File(floppyDir(), it) }
        if (f != null && f.exists()) insertFloppy(f, quiet = true)
    }

    private fun insertFloppy(f: java.io.File, quiet: Boolean = false) {
        saveDisks()
        val err = synchronized(coreLock) { Native.mediaInsert(handle, 0, f.absolutePath) }
        if (err != null) {
            Toast.makeText(this, err, Toast.LENGTH_LONG).show()
            if (!quiet && f.parentFile == floppyDir() && f != floppyFile) f.delete()
            return
        }
        floppyFile = f
        workstation?.floppyLabel = f.nameWithoutExtension
        getSharedPreferences("settings", MODE_PRIVATE).edit().putString("floppy_${machine.id}", f.name).apply()
        if (!quiet) Toast.makeText(this, "Inserted ${f.nameWithoutExtension}", Toast.LENGTH_SHORT).show()
    }

    private fun ejectFloppy() {
        saveDisks()
        synchronized(coreLock) { Native.mediaEject(handle, 0) }
        floppyFile = null
        workstation?.floppyLabel = null
        getSharedPreferences("settings", MODE_PRIVATE).edit().remove("floppy_${machine.id}").apply()
    }

    /** Changed disks are written back (floppies as .imd copies in the app's storage). */
    private fun saveDisks() {
        if (altair != null && handle != 0L) {
            synchronized(coreLock) { for (d in 0 until 2) altairDisk[d]?.let { Native.mediaSave(handle, d, it.absolutePath) } }
            return
        }
        if (adam != null && handle != 0L) {
            adamDisk?.let { f -> synchronized(coreLock) { Native.mediaSave(handle, 0, f.absolutePath) } }
            return
        }
        if (workstation == null || handle == 0L) return
        synchronized(coreLock) {
            floppyFile?.let { Native.mediaSave(handle, 0, it.absolutePath) }
            Native.mediaSave(handle, 1, hardDiskFile().absolutePath)
        }
    }

    // ---- Atari: disks, programs, cartridges, tapes ----
    /** Both Atari models share one library of imported files. */
    private fun atariDir() = java.io.File(filesDir, "atari8").apply { mkdirs() }
    private var atariLoaded: java.io.File? = null
    private var atariDisk2: java.io.File? = null

    private fun loadAtari(f: java.io.File, drive: Int) {
        val err = synchronized(coreLock) { Native.mediaInsert(handle, drive, f.absolutePath) }
        if (err != null) { Toast.makeText(this, err, Toast.LENGTH_LONG).show(); return }
        if (drive == 2) {
            atariLoaded = f
            atari?.mediaLabel = f.nameWithoutExtension
            Toast.makeText(this, "Loading ${f.nameWithoutExtension}", Toast.LENGTH_SHORT).show()
        } else {
            atariDisk2 = f
            Toast.makeText(this, "${f.nameWithoutExtension} in drive ${drive + 1}", Toast.LENGTH_SHORT).show()
        }
    }

    private fun pickAtari(request: Int) {
        val i = android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(android.content.Intent.CATEGORY_OPENABLE); type = "*/*"
        }
        startActivityForResult(i, request)
    }

    private fun showAtariMenu() {
        paused = true
        val files = atariDir().listFiles()?.filter { it.isFile }?.sortedBy { it.name.lowercase() } ?: emptyList()
        val items = ArrayList<Pair<String, () -> Unit>>()
        items += "Load a game or program (.atr .xex .car .rom .cas)..." to { pickAtari(PICK_ATARI) }
        for (f in files) items += "Load ${f.nameWithoutExtension}" to { loadAtari(f, 2) }
        items += "Swap the disk in drive 1 (no restart)..." to { pickAtari(PICK_ATARI_D1) }
        if (atariLoaded != null) items += "Remove the cartridge (restarts)" to {
            synchronized(coreLock) { Native.mediaEject(handle, 2) }
            atariLoaded = null; atari?.mediaLabel = null
        }
        items += "Eject drive 1" to { synchronized(coreLock) { Native.mediaEject(handle, 0) } }
        if (files.isNotEmpty()) items += "Delete a file from the list..." to {
            paused = true
            AlertDialog.Builder(this)
                .setTitle("Delete which file?")
                .setItems(files.map { it.name }.toTypedArray()) { _, w -> files[w].delete(); paused = false }
                .setOnCancelListener { paused = false }
                .show()
        }
        AlertDialog.Builder(this)
            .setTitle("Disks & cartridges")
            .setItems(items.map { it.first }.toTypedArray()) { _, which ->
                paused = false
                items[which].second()
            }
            .setOnCancelListener { paused = false }
            .show()
    }

    // ---- Acorn: floppies (.adf) and files copied into HostFS ----
    /** Archimedes operating systems per machine, in the core's order, and the default */
    private val archiOs: Map<String, Pair<List<String>, Int>> by lazy {
        val early = listOf("Arthur 0.30", "Arthur 1.20", "RISC OS 2.00", "RISC OS 2.01")
        val ro = listOf("RISC OS 2.00", "RISC OS 2.01", "RISC OS 3.00", "RISC OS 3.10", "RISC OS 3.11")
        mapOf(
            "aa305" to (early to 2), "aa310" to (early to 3), "aa440" to (early to 3),
            "aa4101" to (ro to 1), "aa540" to (ro.drop(1) to 3), "aa3000" to (ro to 4),
            "aa5000" to (ro.drop(2) to 2), "aa500" to (listOf("RISC OS 3.10", "RISC OS 2.00") to 0),
        )
    }

    /** Archimedes sold with less than 4 MB: their original memory */
    private val archiAsSold = mapOf(
        "aa305" to "512 KB", "aa310" to "1 MB", "aa4101" to "1 MB", "aa3000" to "1 MB",
        "aa3010" to "1 MB", "aa3010_de" to "1 MB", "aa3020" to "2 MB", "aa4000" to "2 MB", "aa4" to "2 MB",
    )

    private fun acornDir() = java.io.File(filesDir, "acorn").apply { mkdirs() }

    private fun showAcornMenu() {
        paused = true
        val floppies = acornDir().listFiles()?.filter { it.isFile }?.sortedBy { it.name.lowercase() } ?: emptyList()
        val items = ArrayList<Pair<String, () -> Unit>>()
        items += "Insert a floppy image (.adf)..." to { pickAtari(PICK_ACORN_FLOPPY) }
        for (f in floppies) items += "Insert ${f.nameWithoutExtension}" to { insertAcornFloppy(f) }
        items += "Eject the floppy" to {
            synchronized(coreLock) { Native.mediaEject(handle, 0) }
            acorn?.floppyLabel = null
        }
        items += "Copy a file into HostFS..." to { pickAtari(PICK_ACORN_HOSTFS) }
        if (floppies.isNotEmpty()) items += "Delete a floppy from the list..." to {
            paused = true
            AlertDialog.Builder(this)
                .setTitle("Delete which floppy?")
                .setItems(floppies.map { it.name }.toTypedArray()) { _, w -> floppies[w].delete(); paused = false }
                .setOnCancelListener { paused = false }
                .show()
        }
        AlertDialog.Builder(this)
            .setTitle("Disks & files")
            .setItems(items.map { it.first }.toTypedArray()) { _, which -> paused = false; items[which].second() }
            .setOnCancelListener { paused = false }
            .show()
    }

    private fun insertAcornFloppy(f: java.io.File) {
        val err = synchronized(coreLock) { Native.mediaInsert(handle, 0, f.absolutePath) }
        if (err != null) { Toast.makeText(this, err, Toast.LENGTH_LONG).show(); return }
        acorn?.floppyLabel = f.nameWithoutExtension
    }

    private val PICK_ACORN_FLOPPY = 46
    private val PICK_ACORN_HOSTFS = 47

    private val PICK_ADAM = 48

    // ---- ADAM: disks (.dsk) and ColecoVision cartridges; tapes to come ----
    private fun adamDir() = java.io.File(filesDir, "adam").apply { mkdirs() }
    private var adamCart: java.io.File? = null
    private var adamDisk: java.io.File? = null
    private fun isAdamDisk(f: java.io.File) = f.extension.equals("dsk", true)

    private fun showAdamMenu() {
        paused = true
        val files = adamDir().listFiles()?.filter { it.isFile }?.sortedBy { it.name.lowercase() } ?: emptyList()
        val items = ArrayList<Pair<String, () -> Unit>>()
        items += "Add a disk (.dsk) or cartridge (.col .rom .bin)..." to {
            startActivityForResult(android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT).apply {
                addCategory(android.content.Intent.CATEGORY_OPENABLE); type = "*/*"
            }, PICK_ADAM)
        }
        for (f in files.filter { isAdamDisk(it) }) items += "Disk: ${f.nameWithoutExtension}" to { loadAdamDisk(f, restart = true) }
        for (f in files.filter { !isAdamDisk(it) }) items += "Cartridge: ${f.nameWithoutExtension}" to { loadAdamCart(f) }
        if (adamDisk != null) items += "Eject the disk" to {
            saveDisks()
            synchronized(coreLock) { Native.mediaEject(handle, 0) }
            adamDisk = null; updateAdamLabel()
        }
        if (adamCart != null) items += "Pull the cartridge out (back to the computer)" to {
            synchronized(coreLock) { Native.mediaEject(handle, 2) }
            adamCart = null; adam?.showPad = false; updateAdamLabel()
        }
        if (files.isNotEmpty()) items += "Delete a file from the list..." to {
            paused = true
            AlertDialog.Builder(this)
                .setTitle("Delete which file?")
                .setItems(files.map { it.name }.toTypedArray()) { _, w -> files[w].delete(); paused = false }
                .setOnCancelListener { paused = false }
                .show()
        }
        AlertDialog.Builder(this)
            .setTitle("Disks & cartridges")
            .setItems(items.map { it.first }.toTypedArray()) { _, which -> paused = false; items[which].second() }
            .setOnCancelListener { paused = false }
            .show()
    }

    private fun updateAdamLabel() {
        adam?.mediaLabel = listOfNotNull(adamDisk?.nameWithoutExtension, adamCart?.nameWithoutExtension).joinToString(" · ").ifEmpty { null }
    }

    /** A disk goes in drive 1; the computer restarts to boot it (as pulling the reset switch would). */
    private fun loadAdamDisk(f: java.io.File, restart: Boolean) {
        saveDisks()
        val err = synchronized(coreLock) { Native.mediaInsert(handle, 0, f.absolutePath) }
        if (err != null) { Toast.makeText(this, err, Toast.LENGTH_LONG).show(); return }
        adamDisk = f
        if (restart && adamCart == null) synchronized(coreLock) { Native.reset(handle) }
        updateAdamLabel()
        Toast.makeText(this, "${f.nameWithoutExtension} is in the disk drive" + if (restart && adamCart == null) "; restarting" else "", Toast.LENGTH_SHORT).show()
    }

    private fun loadAdamCart(f: java.io.File) {
        val err = synchronized(coreLock) { Native.mediaInsert(handle, 2, f.absolutePath) }
        if (err != null) { Toast.makeText(this, err, Toast.LENGTH_LONG).show(); return }
        adamCart = f
        updateAdamLabel()
        adam?.showPad = true
    }

    // ADAM keyboard matrix codes (row * 8 + bit, as MAME's adam_kb)
    // ---- Altair: terminal keys from a hardware keyboard (ASCII), disks, programs ----
    private fun altairKey(code: Int, e: KeyEvent): IntArray {
        val vt = altair?.vtMode == 0
        fun arrow(c: Char, adm: Int) = if (vt) intArrayOf(27, '['.code, c.code) else intArrayOf(adm)
        return when (code) {
            KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> intArrayOf(13)
            KeyEvent.KEYCODE_DEL -> intArrayOf(8)
            KeyEvent.KEYCODE_FORWARD_DEL -> intArrayOf(127)
            KeyEvent.KEYCODE_ESCAPE -> intArrayOf(27)
            KeyEvent.KEYCODE_TAB -> intArrayOf(9)
            KeyEvent.KEYCODE_DPAD_UP -> arrow('A', 11)
            KeyEvent.KEYCODE_DPAD_DOWN -> arrow('B', 10)
            KeyEvent.KEYCODE_DPAD_RIGHT -> arrow('C', 12)
            KeyEvent.KEYCODE_DPAD_LEFT -> arrow('D', 8)
            else -> {
                val u = e.getUnicodeChar(e.metaState and KeyEvent.META_CTRL_MASK.inv())
                if (u <= 0 || u > 0x7e) intArrayOf()
                else if (e.isCtrlPressed && u.toChar().uppercaseChar() in '@'..'_') intArrayOf(u.toChar().uppercaseChar().code and 0x1f)
                else intArrayOf(u)
            }
        }
    }

    private val PICK_ALTAIR = 49
    private fun altairDir() = java.io.File(filesDir, "altair").apply { mkdirs() }
    private val altairDisk = arrayOfNulls<java.io.File>(2)

    private fun showAltairMenu() {
        paused = true
        val prefs = getSharedPreferences("settings", MODE_PRIVATE)
        val turnkey = machine.id == "al8800bt"
        val files = altairDir().listFiles()?.filter { it.isFile }?.sortedBy { it.name.lowercase() } ?: emptyList()
        val items = ArrayList<Pair<String, () -> Unit>>()
        items += "Add disk images (.dsk) or programs (.bin)..." to {
            startActivityForResult(android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT).apply {
                addCategory(android.content.Intent.CATEGORY_OPENABLE); type = "*/*"
            }, PICK_ALTAIR)
        }
        for (f in files.filter { it.extension.equals("dsk", true) }) items += "Disk: ${f.nameWithoutExtension}" to { pickAltairDrive(f) }
        for (f in files.filter { !it.extension.equals("dsk", true) }) items += "Program: ${f.nameWithoutExtension} (load at 0 and run)" to { loadAltairProgram(f) }
        if (!turnkey) items += "Kill the Bit (Dean McDaniel, 1975)" to {
            val f = java.io.File(cacheDir, "killbit.bin")
            f.writeBytes(byteArrayOf(0x21, 0, 0, 0x16, 0x80.toByte(), 0x01, 0x0e, 0, 0x1a, 0x1a, 0x1a, 0x1a, 0x09,
                0xd2.toByte(), 0x08, 0, 0xdb.toByte(), 0xff.toByte(), 0xaa.toByte(), 0x0f, 0x57, 0xc3.toByte(), 0x08, 0))
            loadAltairProgram(f, quiet = true)
            altair?.showTerminal = false
            Toast.makeText(this, "A lit bit runs along A15-A8. Flip the switch under it (then back) to kill it; a wrong switch adds a bit", Toast.LENGTH_LONG).show()
        }
        if (altairDisk[0] != null) items += "Boot from drive A" to { bootAltair() }
        for (d in 0 until 2) altairDisk[d]?.let { f ->
            items += "Eject ${'A' + d}: ${f.nameWithoutExtension}" to {
                saveDisks()
                synchronized(coreLock) { Native.mediaEject(handle, d) }
                altairDisk[d] = null; updateAltairLabels()
            }
        }
        val vt = prefs.getInt("altair_term", 0)
        items += (if (vt == 0) "Terminal codes: VT100 (tap for ADM-3A)" else "Terminal codes: ADM-3A (tap for VT100)") to {
            val n = 1 - vt
            prefs.edit().putInt("altair_term", n).apply()
            synchronized(coreLock) { Native.setOption(handle, 0, n) }
            altair?.vtMode = n
        }
        if (files.isNotEmpty()) items += "Delete a file from the list..." to {
            paused = true
            AlertDialog.Builder(this)
                .setTitle("Delete which file?")
                .setItems(files.map { it.name }.toTypedArray()) { _, w -> files[w].delete(); paused = false }
                .setOnCancelListener { paused = false }
                .show()
        }
        AlertDialog.Builder(this)
            .setTitle("Disks & programs")
            .setItems(items.map { it.first }.toTypedArray()) { _, which -> paused = false; items[which].second() }
            .setOnCancelListener { paused = false }
            .show()
    }

    private fun pickAltairDrive(f: java.io.File) {
        paused = true
        AlertDialog.Builder(this)
            .setTitle(f.nameWithoutExtension)
            .setItems(arrayOf("Drive A and boot it", "Drive A", "Drive B")) { _, w ->
                paused = false
                if (insertAltairDisk(f, if (w == 2) 1 else 0) && w == 0) bootAltair()
            }
            .setOnCancelListener { paused = false }
            .show()
    }

    private fun insertAltairDisk(f: java.io.File, drive: Int): Boolean {
        saveDisks()
        val err = synchronized(coreLock) { Native.mediaInsert(handle, drive, f.absolutePath) }
        if (err != null) { Toast.makeText(this, err, Toast.LENGTH_LONG).show(); return false }
        if (altairDisk[1 - drive]?.absolutePath == f.absolutePath) {
            synchronized(coreLock) { Native.mediaEject(handle, 1 - drive) }
            altairDisk[1 - drive] = null
        }
        altairDisk[drive] = f
        updateAltairLabels()
        return true
    }

    private fun updateAltairLabels() {
        altair?.diskLabels = arrayOf(altairDisk[0]?.nameWithoutExtension, altairDisk[1]?.nameWithoutExtension)
    }

    /** Turnkey: restart into the monitor and jump to the disk boot loader. Front panel: the boot loader
     *  PROM comes from the turnkey's ROM set, if it's been imported. */
    private fun bootAltair() {
        if (machine.id == "al8800bt") {
            synchronized(coreLock) { Native.reset(handle) }
            for (ch in "J177400 ") keyQueue.add(ch.code or 0x100)
            altair?.showTerminal = true
            return
        }
        val tk = Machines.byId("al8800bt")
        val rom = tk?.roms?.firstOrNull { it.name == "88dskrom.bin" }?.let { RomStore.romFile(this, tk, it) }
        if (rom == null || !rom.exists()) {
            Toast.makeText(this, "Booting disks needs the disk boot loader PROM: import the al8800bt (Turnkey) ROMs too", Toast.LENGTH_LONG).show()
            return
        }
        val err = synchronized(coreLock) { Native.mediaInsert(handle, 10, rom.absolutePath) }
        if (err != null) Toast.makeText(this, err, Toast.LENGTH_LONG).show()
        altair?.showTerminal = true
    }

    private fun loadAltairProgram(f: java.io.File, quiet: Boolean = false) {
        val err = synchronized(coreLock) { Native.mediaInsert(handle, 8, f.absolutePath) }
        if (err != null) { Toast.makeText(this, err, Toast.LENGTH_LONG).show(); return }
        if (!quiet) { altair?.showTerminal = true; Toast.makeText(this, "${f.nameWithoutExtension} is running from 0000", Toast.LENGTH_SHORT).show() }
    }

    // AIM 65 keyboard: row * 8 + bit of MAME's KEY.0-7
    private fun aimKey(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_SPACE -> 0; KeyEvent.KEYCODE_PERIOD -> 2; KeyEvent.KEYCODE_M -> 3; KeyEvent.KEYCODE_B -> 4
        KeyEvent.KEYCODE_C -> 5; KeyEvent.KEYCODE_Z -> 6
        KeyEvent.KEYCODE_APOSTROPHE -> 9; KeyEvent.KEYCODE_L -> 10; KeyEvent.KEYCODE_J -> 11; KeyEvent.KEYCODE_G -> 12
        KeyEvent.KEYCODE_D -> 13; KeyEvent.KEYCODE_A -> 14
        KeyEvent.KEYCODE_SYSRQ, KeyEvent.KEYCODE_INSERT -> 17; KeyEvent.KEYCODE_P -> 18; KeyEvent.KEYCODE_I -> 19
        KeyEvent.KEYCODE_Y -> 20; KeyEvent.KEYCODE_R -> 21; KeyEvent.KEYCODE_W -> 22; KeyEvent.KEYCODE_ESCAPE -> 23
        KeyEvent.KEYCODE_ENTER -> 24; KeyEvent.KEYCODE_MINUS -> 26; KeyEvent.KEYCODE_O -> 27; KeyEvent.KEYCODE_U -> 28
        KeyEvent.KEYCODE_T -> 29; KeyEvent.KEYCODE_E -> 30; KeyEvent.KEYCODE_Q -> 31
        KeyEvent.KEYCODE_CTRL_LEFT, KeyEvent.KEYCODE_CTRL_RIGHT -> 32; KeyEvent.KEYCODE_SEMICOLON -> 50
        KeyEvent.KEYCODE_9 -> 35; KeyEvent.KEYCODE_7 -> 36; KeyEvent.KEYCODE_5 -> 37; KeyEvent.KEYCODE_3 -> 38
        KeyEvent.KEYCODE_1 -> 39; KeyEvent.KEYCODE_SHIFT_LEFT -> 40; KeyEvent.KEYCODE_0 -> 42; KeyEvent.KEYCODE_8 -> 43
        KeyEvent.KEYCODE_6 -> 44; KeyEvent.KEYCODE_4 -> 45; KeyEvent.KEYCODE_2 -> 46; KeyEvent.KEYCODE_F3 -> 47
        KeyEvent.KEYCODE_SHIFT_RIGHT -> 48; KeyEvent.KEYCODE_DEL, KeyEvent.KEYCODE_FORWARD_DEL -> 49
        KeyEvent.KEYCODE_K -> 51; KeyEvent.KEYCODE_H -> 52; KeyEvent.KEYCODE_F -> 53; KeyEvent.KEYCODE_S -> 54
        KeyEvent.KEYCODE_F2 -> 55; KeyEvent.KEYCODE_SLASH -> 58; KeyEvent.KEYCODE_COMMA -> 59; KeyEvent.KEYCODE_N -> 60
        KeyEvent.KEYCODE_V -> 61; KeyEvent.KEYCODE_X -> 62; KeyEvent.KEYCODE_F1 -> 63
        KeyEvent.KEYCODE_EQUALS -> 34   // the ':  *' key
        KeyEvent.KEYCODE_F4 -> 17       // PRINT
        else -> -1
    }

    private fun adamKey(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_F7 -> 0
        in KeyEvent.KEYCODE_F1..KeyEvent.KEYCODE_F6 -> 1 + code - KeyEvent.KEYCODE_F1
        KeyEvent.KEYCODE_F8 -> 7
        in KeyEvent.KEYCODE_1..KeyEvent.KEYCODE_8 -> 8 + code - KeyEvent.KEYCODE_1
        KeyEvent.KEYCODE_TAB -> 16
        KeyEvent.KEYCODE_Q -> 17; KeyEvent.KEYCODE_W -> 18; KeyEvent.KEYCODE_E -> 19; KeyEvent.KEYCODE_R -> 20
        KeyEvent.KEYCODE_T -> 21; KeyEvent.KEYCODE_Y -> 22; KeyEvent.KEYCODE_U -> 23
        KeyEvent.KEYCODE_A -> 24; KeyEvent.KEYCODE_S -> 25; KeyEvent.KEYCODE_D -> 26; KeyEvent.KEYCODE_F -> 27
        KeyEvent.KEYCODE_G -> 28; KeyEvent.KEYCODE_H -> 29; KeyEvent.KEYCODE_J -> 30; KeyEvent.KEYCODE_K -> 31
        KeyEvent.KEYCODE_Z -> 32; KeyEvent.KEYCODE_X -> 33; KeyEvent.KEYCODE_C -> 34; KeyEvent.KEYCODE_V -> 35
        KeyEvent.KEYCODE_B -> 36; KeyEvent.KEYCODE_N -> 37; KeyEvent.KEYCODE_M -> 38; KeyEvent.KEYCODE_COMMA -> 39
        KeyEvent.KEYCODE_9 -> 40; KeyEvent.KEYCODE_0 -> 41; KeyEvent.KEYCODE_MINUS -> 42; KeyEvent.KEYCODE_EQUALS -> 43
        KeyEvent.KEYCODE_GRAVE -> 44; KeyEvent.KEYCODE_L -> 45; KeyEvent.KEYCODE_SEMICOLON -> 46; KeyEvent.KEYCODE_APOSTROPHE -> 47
        KeyEvent.KEYCODE_I -> 48; KeyEvent.KEYCODE_O -> 49; KeyEvent.KEYCODE_P -> 50
        KeyEvent.KEYCODE_LEFT_BRACKET -> 51; KeyEvent.KEYCODE_RIGHT_BRACKET -> 52
        KeyEvent.KEYCODE_PERIOD -> 53; KeyEvent.KEYCODE_SLASH -> 54; KeyEvent.KEYCODE_ENTER -> 55
        KeyEvent.KEYCODE_BACKSLASH -> 56; KeyEvent.KEYCODE_ESCAPE -> 57; KeyEvent.KEYCODE_SPACE -> 60
        KeyEvent.KEYCODE_INSERT -> 69; KeyEvent.KEYCODE_MOVE_HOME -> 96
        KeyEvent.KEYCODE_PAGE_UP -> 63; KeyEvent.KEYCODE_PAGE_DOWN -> 71; KeyEvent.KEYCODE_MOVE_END -> 70
        KeyEvent.KEYCODE_DPAD_UP -> 64; KeyEvent.KEYCODE_DPAD_RIGHT -> 65; KeyEvent.KEYCODE_DPAD_DOWN -> 66; KeyEvent.KEYCODE_DPAD_LEFT -> 67
        KeyEvent.KEYCODE_DEL -> 68; KeyEvent.KEYCODE_FORWARD_DEL -> 71
        KeyEvent.KEYCODE_CTRL_LEFT, KeyEvent.KEYCODE_CTRL_RIGHT -> 72
        KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SHIFT_RIGHT -> 80
        KeyEvent.KEYCODE_CAPS_LOCK -> 88
        else -> -1
    }

    private val PICK_ATARI = 44
    private val PICK_ATARI_D1 = 45

    private val PICK_FLOPPY = 41
    private val IMPORT_HD = 42
    private val EXPORT_HD = 43

    private fun showDiskMenu() {
        paused = true
        val floppies = floppyDir().listFiles()?.filter { it.isFile }?.sortedBy { it.name.lowercase() } ?: emptyList()
        val items = ArrayList<Pair<String, () -> Unit>>()
        items += "Insert a floppy image (.imd / .img)..." to {
            val i = android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT).apply {
                addCategory(android.content.Intent.CATEGORY_OPENABLE); type = "*/*"
            }
            startActivityForResult(i, PICK_FLOPPY)
        }
        for (f in floppies) if (f != floppyFile) items += "Insert ${f.nameWithoutExtension}" to { insertFloppy(f) }
        if (floppyFile != null) items += "Eject ${floppyFile?.nameWithoutExtension}" to { ejectFloppy() }
        items += "Hard disk: save a copy..." to {
            saveDisks()
            val i = android.content.Intent(android.content.Intent.ACTION_CREATE_DOCUMENT).apply {
                addCategory(android.content.Intent.CATEGORY_OPENABLE); type = "application/octet-stream"
                putExtra(android.content.Intent.EXTRA_TITLE, "${machine.id}-harddisk.${hardDiskExt()}")
            }
            startActivityForResult(i, EXPORT_HD)
        }
        items += "Hard disk: load a saved copy..." to {
            val i = android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT).apply {
                addCategory(android.content.Intent.CATEGORY_OPENABLE); type = "*/*"
            }
            startActivityForResult(i, IMPORT_HD)
        }
        items += "Hard disk: erase (start over)" to {
            paused = true
            AlertDialog.Builder(this)
                .setMessage("Erase the hard disk? Everything installed on it is lost and the machine restarts.")
                .setPositiveButton("Erase") { _, _ ->
                    hardDiskFile().delete()
                    synchronized(coreLock) { Native.mediaInsert(handle, 1, hardDiskFile().absolutePath); Native.reset(handle) }
                    paused = false
                }
                .setNegativeButton("Cancel") { _, _ -> paused = false }
                .setOnCancelListener { paused = false }
                .show()
        }
        AlertDialog.Builder(this)
            .setTitle("Disks")
            .setItems(items.map { it.first }.toTypedArray()) { _, which ->
                paused = false
                items[which].second()
            }
            .setOnCancelListener { paused = false }
            .show()
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: android.content.Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri = data?.data
        if (resultCode != RESULT_OK || uri == null) return
        when (requestCode) {
            PICK_ACORN_FLOPPY, PICK_ACORN_HOSTFS -> {
                var name = "file"
                contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                    if (it.moveToFirst()) name = it.getString(0) ?: name
                }
                val safe = name.replace(Regex("[^A-Za-z0-9._ ,()-]"), "_").ifEmpty { "file" }
                if (requestCode == PICK_ACORN_FLOPPY) {
                    // RPCEmu tells the floppy format by the extension
                    val dest = java.io.File(acornDir(), if (safe.contains('.')) safe else "$safe.adf")
                    runCatching { contentResolver.openInputStream(uri)?.use { i -> dest.outputStream().use { i.copyTo(it) } } }
                    insertAcornFloppy(dest)
                } else {
                    val tmp = java.io.File(cacheDir, safe)
                    runCatching { contentResolver.openInputStream(uri)?.use { i -> tmp.outputStream().use { i.copyTo(it) } } }
                    val err = synchronized(coreLock) { Native.mediaInsert(handle, 2, tmp.absolutePath) }
                    tmp.delete()
                    Toast.makeText(this, err ?: "Copied into HostFS: click the HostFS icon on the icon bar", Toast.LENGTH_LONG).show()
                }
            }
            PICK_ALTAIR -> {
                var name = "disk"
                contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                    if (it.moveToFirst()) name = it.getString(0) ?: name
                }
                val safe = name.replace(Regex("[^A-Za-z0-9._ ()-]"), "_").ifEmpty { "disk" }
                var dest = java.io.File(altairDir(), safe)
                runCatching { contentResolver.openInputStream(uri)?.use { i -> dest.outputStream().use { i.copyTo(it) } } }
                // 8" images without the .dsk extension
                if (!dest.extension.equals("dsk", true) && dest.length() in 330000L..342000L)
                    dest = java.io.File(altairDir(), dest.nameWithoutExtension + ".dsk").also { dest.renameTo(it) }
                if (dest.extension.equals("dsk", true)) pickAltairDrive(dest) else loadAltairProgram(dest)
            }
            PICK_ADAM -> {
                var name = "cartridge"
                contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                    if (it.moveToFirst()) name = it.getString(0) ?: name
                }
                val safe = name.replace(Regex("[^A-Za-z0-9._ ()-]"), "_").ifEmpty { "cartridge" }
                val dest = java.io.File(adamDir(), safe)
                runCatching { contentResolver.openInputStream(uri)?.use { i -> dest.outputStream().use { i.copyTo(it) } } }
                if (isAdamDisk(dest) || dest.length() == 163840L) {
                    val disk = if (isAdamDisk(dest)) dest else java.io.File(adamDir(), dest.nameWithoutExtension + ".dsk").also { dest.renameTo(it) }
                    loadAdamDisk(disk, restart = true)
                } else loadAdamCart(dest)
            }
            PICK_ATARI, PICK_ATARI_D1 -> {
                var name = "game"
                contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                    if (it.moveToFirst()) name = it.getString(0) ?: name
                }
                val safe = name.replace(Regex("[^A-Za-z0-9._ ()-]"), "_").ifEmpty { "game" }
                // a copy lives in the app's storage; disks are written back to it
                val dest = java.io.File(atariDir(), safe)
                runCatching { contentResolver.openInputStream(uri)?.use { i -> dest.outputStream().use { i.copyTo(it) } } }
                loadAtari(dest, if (requestCode == PICK_ATARI) 2 else 0)
            }
            PICK_FLOPPY -> {
                var name = "floppy"
                contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                    if (it.moveToFirst()) name = it.getString(0) ?: name
                }
                val safe = name.replace(Regex("[^A-Za-z0-9._ -]"), "_").ifEmpty { "floppy.imd" }
                val dest = java.io.File(floppyDir(), safe)
                runCatching { contentResolver.openInputStream(uri)?.use { i -> dest.outputStream().use { i.copyTo(it) } } }
                // a copy lives in the app's storage; changes are written back to it (in .imd format)
                insertFloppy(dest)
            }
            IMPORT_HD -> {
                val tmp = java.io.File(cacheDir, "hd.tmp")
                runCatching { contentResolver.openInputStream(uri)?.use { i -> tmp.outputStream().use { i.copyTo(it) } } }
                val err = synchronized(coreLock) { Native.mediaInsert(handle, 1, tmp.absolutePath) }
                if (err != null) {
                    tmp.delete()
                    synchronized(coreLock) { Native.mediaInsert(handle, 1, hardDiskFile().absolutePath) }
                    Toast.makeText(this, err, Toast.LENGTH_LONG).show(); return
                }
                synchronized(coreLock) {
                    tmp.copyTo(hardDiskFile(), overwrite = true)
                    tmp.delete()
                    Native.mediaInsert(handle, 1, hardDiskFile().absolutePath)
                    Native.reset(handle)
                }
                Toast.makeText(this, "Hard disk loaded; the machine restarted", Toast.LENGTH_SHORT).show()
            }
            EXPORT_HD -> {
                val ok = runCatching {
                    contentResolver.openOutputStream(uri)?.use { o -> hardDiskFile().inputStream().use { it.copyTo(o) } } != null
                }.getOrDefault(false)
                Toast.makeText(this, if (ok && hardDiskFile().exists()) "Hard disk saved" else "Nothing to save yet", Toast.LENGTH_SHORT).show()
            }
        }
    }

    override fun onDestroy() {
        stop()
        if (handle != 0L) { Native.destroy(handle); handle = 0L }
        linkedFds.forEach { runCatching { it.close() } }
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
        if (machine.skin == Skin.NIXIE_CLOCK) {
            // the clock keeps running while the app is closed: set it from the phone each time
            val now = System.currentTimeMillis()
            val prefs = getSharedPreferences("settings", MODE_PRIVATE)
            synchronized(coreLock) {
                Native.setOption(handle, 1, if (prefs.getBoolean("ticktock_${machine.id}", true)) 1 else 0)
                Native.setOption(handle, 2, prefs.getInt("mains_${machine.id}", 50))
                Native.setWallClock(handle, (now + java.util.TimeZone.getDefault().getOffset(now)) / 1000)
            }
            return
        }
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
            val n = synchronized(coreLock) {
                while (true) {
                    val ev = keyQueue.poll() ?: break
                    Native.keyEvent(handle, ev and 0xff, ev and 0x100 != 0)
                }
                if (machine.skin == Skin.DESKTOP_3B1 || machine.skin == Skin.ACORN) {
                    val (dx, dy) = synchronized(mouseLock) { (mouseDx to mouseDy).also { mouseDx = 0f; mouseDy = 0f } }
                    if (dx != 0f || dy != 0f) Native.setAnalog(handle, 9, dx, dy)
                }
                val r = Native.runFrame(handle, touchInputs or keyInputs, pixels, audio)
                workstation?.indicators = Native.mediaActivity(handle)
                atari?.indicators = Native.mediaActivity(handle)
                acorn?.indicators = Native.mediaActivity(handle)
                adam?.diskLight = Native.mediaActivity(handle) != 0
                r
            }
            if (workstation != null) workstation?.updateFrame(pixels)
            else if (atari != null) atari?.updateFrame(pixels)
            else if (acorn != null) acorn?.updateFrame(pixels)
            else if (pet != null) pet?.updateFrame(pixels)
            else if (phone != null) phone?.updateFrame(pixels)
            else if (arcade != null) arcade?.updateFrame(pixels)
            else if (handheld != null) handheld?.updateFrame(pixels)
            else if (mini != null) mini?.updateFrame(pixels)
            else if (nixie != null) nixie?.updateFrame(pixels)
            else if (chess != null) chess?.updateFrame(pixels)
            else if (keychain != null) keychain?.updateFrame(pixels)
            else if (hh != null) hh?.updateFrame(pixels)
            else if (kit != null) kit?.updateFrame(pixels)
            else if (tabletop != null) tabletop?.updateFrame(pixels)
            else if (adam != null) adam?.updateFrame(pixels)
            else if (aim != null) aim?.updateFrame(pixels)
            else if (altair != null) altair?.updateFrame(pixels)
            else if (wide != null) wide?.updateFrame(pixels)
            else if (uboard != null) uboard?.updateFrame(pixels)
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
        if (machine.skin != Skin.DESKTOP_3B1 && machine.skin != Skin.ACORN) items += "Save state" to {
            val ok = synchronized(coreLock) { Native.saveState(handle, stateFile.absolutePath) }
            Toast.makeText(this, if (ok) "State saved" else "Couldn't save state", Toast.LENGTH_SHORT).show()
        }
        if (machine.skin != Skin.DESKTOP_3B1 && machine.skin != Skin.ACORN) items += (if (stateFile.exists()) "Load state" else "Load state (none saved)") to {
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
            if (workstation == null && nixie == null) items += (if (tv.scanlines) "Scanlines: on" else "Scanlines: off") to {
                tv.scanlines = !tv.scanlines
                prefs.edit().putBoolean("scanlines", tv.scanlines).apply()
                tv.invalidate()
            }
        }
        if (machine.setup.isNotEmpty()) items += "Getting started steps" to {
            prefs.edit().putBoolean("setup_hidden_${machine.id}", false).apply()
            setupBubble?.show(true)
        }
        if (workstation != null) items += "Disks..." to { showDiskMenu() }
        if (atari != null) items += "Disks & cartridges..." to { showAtariMenu() }
        if (acorn != null) items += "Disks & files..." to { showAcornMenu() }
        archiAsSold[machine.id]?.let { sold ->
            val up = prefs.getBoolean("mem4_${machine.id}", true)
            items += (if (up) "Memory: 4 MB (upgraded)" else "Memory: $sold (as sold)") to {
                prefs.edit().putBoolean("mem4_${machine.id}", !up).apply()
                synchronized(coreLock) { Native.setOption(handle, 2, if (up) 0 else 1) }
                Toast.makeText(this, (if (up) "$sold, as sold" else "4 MB") + ": the computer restarted", Toast.LENGTH_SHORT).show()
            }
        }
        archiOs[machine.id]?.let { (names, def) ->
            if (names.size > 1) {
                val cur = prefs.getInt("os_${machine.id}", def).coerceIn(0, names.size - 1)
                items += "Operating system: ${names[cur]}..." to {
                    paused = true
                    AlertDialog.Builder(this)
                        .setTitle("Operating system")
                        .setSingleChoiceItems(names.toTypedArray(), cur) { d, w ->
                            d.dismiss()
                            paused = false
                            if (w != cur) {
                                prefs.edit().putInt("os_${machine.id}", w).apply()
                                synchronized(coreLock) { Native.setOption(handle, 1, w) }
                                Toast.makeText(this, "${names[w]}: the computer restarted", Toast.LENGTH_SHORT).show()
                            }
                        }
                        .setOnCancelListener { paused = false }
                        .show()
                }
            }
        }
        if (atari != null && machine.badge in atariNoBasic) {
            val on = prefs.getBoolean("basic_${machine.id}", false)
            items += (if (on) "BASIC cartridge: plugged in" else "BASIC cartridge: not plugged in") to {
                prefs.edit().putBoolean("basic_${machine.id}", !on).apply()
                synchronized(coreLock) { Native.setOption(handle, 1, if (on) 0 else 1) }
                Toast.makeText(this, if (on) "BASIC removed; the computer restarted"
                    else "BASIC plugged in (a free stand-in for the real cartridge); the computer restarted", Toast.LENGTH_LONG).show()
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
        Skin.JOYSTICK_TV, Skin.WIDE_HANDHELD -> caseKeyBit(code)
        Skin.ABL_TV -> AblPadView.keyBit(machine.badge, code)
        Skin.FLIGHT_STICK -> FlightStickView.keyBit(code)
        Skin.KEYCHAIN -> KeychainGameView.keyBit(code)
        Skin.HH -> HhView.keyBit(code)
        Skin.APF -> ApfBits.keyBit(code)
        Skin.MINI_ARCADE -> MiniArcadeView.keyBit(code)
        Skin.TV_CONSOLE -> A3dBits.keyBit(code)
        Skin.KIT -> kitKeyBit(code)
        Skin.TABLETOP -> when (code) {
            KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> 0x01
            KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> 0x02
            KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> 0x04
            KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> 0x08
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_1, KeyEvent.KEYCODE_SPACE -> 0x10
            KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_2 -> 0x20
            KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_3 -> 0x40
            KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_4 -> 0x80
            else -> 0
        }
        Skin.NIXIE_CLOCK -> when (code) {
            KeyEvent.KEYCODE_1, KeyEvent.KEYCODE_S, KeyEvent.KEYCODE_BUTTON_A -> NixieClockView.SELECT
            KeyEvent.KEYCODE_2, KeyEvent.KEYCODE_B, KeyEvent.KEYCODE_BUTTON_B -> NixieClockView.BROWSE
            else -> 0
        }
        else -> danceKeyBit(code)
    }

    // Acorn System 1 keypad: 0-9 a-f, M (memory, also '-'), G (go), P, S, L, R, arrows, F3 = rst
    private fun kitKeyBit(code: Int): Int {
        val bit = when (code) {
            in KeyEvent.KEYCODE_0..KeyEvent.KEYCODE_9 -> code - KeyEvent.KEYCODE_0
            in KeyEvent.KEYCODE_A..KeyEvent.KEYCODE_F -> 10 + code - KeyEvent.KEYCODE_A
            KeyEvent.KEYCODE_M, KeyEvent.KEYCODE_MINUS -> 16
            KeyEvent.KEYCODE_G, KeyEvent.KEYCODE_X -> 17
            KeyEvent.KEYCODE_P -> 18
            KeyEvent.KEYCODE_S -> 19
            KeyEvent.KEYCODE_L -> 20
            KeyEvent.KEYCODE_R -> 21
            KeyEvent.KEYCODE_DPAD_UP -> 22
            KeyEvent.KEYCODE_DPAD_DOWN -> 23
            KeyEvent.KEYCODE_F3 -> 24
            else -> return 0
        }
        return 1 shl bit
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
        if (atari != null && event.action == android.view.MotionEvent.ACTION_MOVE &&
            (event.source and android.view.InputDevice.SOURCE_JOYSTICK) == android.view.InputDevice.SOURCE_JOYSTICK) {
            val x = event.getAxisValue(android.view.MotionEvent.AXIS_X) + event.getAxisValue(android.view.MotionEvent.AXIS_HAT_X)
            val y = event.getAxisValue(android.view.MotionEvent.AXIS_Y) + event.getAxisValue(android.view.MotionEvent.AXIS_HAT_Y)
            var m = atariStick and AtariXlView.FIRE
            if (y < -0.5f) m = m or AtariXlView.UP
            if (y > 0.5f) m = m or AtariXlView.DOWN
            if (x < -0.5f) m = m or AtariXlView.LEFT
            if (x > 0.5f) m = m or AtariXlView.RIGHT
            if (m != atariStick) setAtariStick(m)
            return true
        }
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
        if (machine.skin == Skin.FLIGHT_STICK && event.action == android.view.MotionEvent.ACTION_MOVE &&
            (event.source and android.view.InputDevice.SOURCE_JOYSTICK) == android.view.InputDevice.SOURCE_JOYSTICK) {
            val x = event.getAxisValue(android.view.MotionEvent.AXIS_X) + event.getAxisValue(android.view.MotionEvent.AXIS_HAT_X)
            val y = event.getAxisValue(android.view.MotionEvent.AXIS_Y) + event.getAxisValue(android.view.MotionEvent.AXIS_HAT_Y)
            var m = 0
            if (y < -0.5f) m = m or FlightStickView.UP
            if (y > 0.5f) m = m or FlightStickView.DOWN
            if (x < -0.5f) m = m or FlightStickView.LEFT
            if (x > 0.5f) m = m or FlightStickView.RIGHT
            if (m != stickMask) { stickMask = m; setTouch(3, m); flightStick?.externalMask = keyInputs or m }
            return true
        }
        if (machine.skin == Skin.ABL_TV && event.action == android.view.MotionEvent.ACTION_MOVE &&
            (event.source and android.view.InputDevice.SOURCE_JOYSTICK) == android.view.InputDevice.SOURCE_JOYSTICK) {
            val x = event.getAxisValue(android.view.MotionEvent.AXIS_X) + event.getAxisValue(android.view.MotionEvent.AXIS_HAT_X)
            val y = event.getAxisValue(android.view.MotionEvent.AXIS_Y) + event.getAxisValue(android.view.MotionEvent.AXIS_HAT_Y)
            fun bit(code: Int) = AblPadView.keyBit(machine.badge, code)
            var m = 0
            if (y < -0.5f) m = m or bit(KeyEvent.KEYCODE_DPAD_UP)
            if (y > 0.5f) m = m or bit(KeyEvent.KEYCODE_DPAD_DOWN)
            if (x < -0.5f) m = m or bit(KeyEvent.KEYCODE_DPAD_LEFT)
            if (x > 0.5f) m = m or bit(KeyEvent.KEYCODE_DPAD_RIGHT)
            if (m != stickMask) { stickMask = m; setTouch(3, m); ablPad?.externalMask = keyInputs or m }
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

    private fun deskKey(code: Int): Int = if (machine.skin == Skin.DESKTOP_3B1) key3b1(code) else ngenKey(code)

    /** A real keyboard on the 3B1's (scan codes as freebee/keyboard.c; office keys on the spare PC keys). */
    private fun key3b1(code: Int): Int = when (code) {
        in KeyEvent.KEYCODE_A..KeyEvent.KEYCODE_Z -> 0x61 + (code - KeyEvent.KEYCODE_A)
        in KeyEvent.KEYCODE_0..KeyEvent.KEYCODE_9 -> 0x30 + (code - KeyEvent.KEYCODE_0)
        KeyEvent.KEYCODE_SPACE -> 0x20
        KeyEvent.KEYCODE_ENTER -> 0x0d
        KeyEvent.KEYCODE_NUMPAD_ENTER -> 0x0a
        KeyEvent.KEYCODE_TAB -> 0x09
        KeyEvent.KEYCODE_DEL -> 0x08
        KeyEvent.KEYCODE_FORWARD_DEL -> 0x7f
        KeyEvent.KEYCODE_ESCAPE -> 0x1b
        KeyEvent.KEYCODE_SHIFT_LEFT -> 0x48
        KeyEvent.KEYCODE_SHIFT_RIGHT -> 0x49
        KeyEvent.KEYCODE_CTRL_LEFT -> 0x4c
        KeyEvent.KEYCODE_CTRL_RIGHT -> 0x4d
        KeyEvent.KEYCODE_CAPS_LOCK -> 0x45
        KeyEvent.KEYCODE_NUM_LOCK -> 0x7e
        KeyEvent.KEYCODE_BREAK -> 0x25
        KeyEvent.KEYCODE_DPAD_UP -> 0x01
        KeyEvent.KEYCODE_DPAD_DOWN -> 0x0b
        KeyEvent.KEYCODE_DPAD_LEFT -> 0x0e
        KeyEvent.KEYCODE_DPAD_RIGHT -> 0x0f
        KeyEvent.KEYCODE_PAGE_UP -> 0x2a          // Help
        KeyEvent.KEYCODE_PAGE_DOWN -> 0x46        // Page
        KeyEvent.KEYCODE_MOVE_HOME -> 0x4a
        KeyEvent.KEYCODE_MOVE_END -> 0x4b
        KeyEvent.KEYCODE_INSERT -> 0x41           // Cmd
        KeyEvent.KEYCODE_F1 -> 0x15; KeyEvent.KEYCODE_F2 -> 0x16; KeyEvent.KEYCODE_F3 -> 0x17
        KeyEvent.KEYCODE_F4 -> 0x18; KeyEvent.KEYCODE_F5 -> 0x19; KeyEvent.KEYCODE_F6 -> 0x1a
        KeyEvent.KEYCODE_F7 -> 0x1c; KeyEvent.KEYCODE_F8 -> 0x1d
        KeyEvent.KEYCODE_F9 -> 0x1e               // Suspd
        KeyEvent.KEYCODE_F10 -> 0x04              // Exit
        KeyEvent.KEYCODE_F11 -> 0x07              // Cancl
        KeyEvent.KEYCODE_F12 -> 0x13              // Opts
        KeyEvent.KEYCODE_MINUS -> 0x2d
        KeyEvent.KEYCODE_EQUALS -> 0x3d
        KeyEvent.KEYCODE_LEFT_BRACKET -> 0x5b
        KeyEvent.KEYCODE_RIGHT_BRACKET -> 0x5d
        KeyEvent.KEYCODE_BACKSLASH -> 0x5c
        KeyEvent.KEYCODE_SEMICOLON -> 0x3b
        KeyEvent.KEYCODE_APOSTROPHE -> 0x27
        KeyEvent.KEYCODE_COMMA -> 0x2c
        KeyEvent.KEYCODE_PERIOD -> 0x2e
        KeyEvent.KEYCODE_SLASH -> 0x2f
        KeyEvent.KEYCODE_GRAVE -> 0x60
        else -> -1
    }

    /** A real keyboard on the phone types on the emulated one (CTOS keys on the spare PC keys). */
    private fun ngenKey(code: Int): Int = when (code) {
        in KeyEvent.KEYCODE_A..KeyEvent.KEYCODE_Z -> 0x61 + (code - KeyEvent.KEYCODE_A)
        in KeyEvent.KEYCODE_0..KeyEvent.KEYCODE_9 -> 0x30 + (code - KeyEvent.KEYCODE_0)
        KeyEvent.KEYCODE_SPACE -> 0x20
        KeyEvent.KEYCODE_ENTER -> 0x0a
        KeyEvent.KEYCODE_NUMPAD_ENTER -> 0x0d
        KeyEvent.KEYCODE_TAB -> 0x09
        KeyEvent.KEYCODE_DEL -> 0x08
        KeyEvent.KEYCODE_FORWARD_DEL -> 0x7f
        KeyEvent.KEYCODE_SHIFT_LEFT -> 0x48
        KeyEvent.KEYCODE_SHIFT_RIGHT -> 0x49
        KeyEvent.KEYCODE_CTRL_LEFT -> 0x4c
        KeyEvent.KEYCODE_CTRL_RIGHT -> 0x4d
        KeyEvent.KEYCODE_ALT_LEFT, KeyEvent.KEYCODE_ALT_RIGHT -> 0x43
        KeyEvent.KEYCODE_CAPS_LOCK -> 0x45
        KeyEvent.KEYCODE_INSERT -> 0x44
        KeyEvent.KEYCODE_ESCAPE -> 0x07
        KeyEvent.KEYCODE_DPAD_UP -> 0x01
        KeyEvent.KEYCODE_DPAD_DOWN -> 0x0b
        KeyEvent.KEYCODE_DPAD_LEFT -> 0x0e
        KeyEvent.KEYCODE_DPAD_RIGHT -> 0x0f
        KeyEvent.KEYCODE_PAGE_UP -> 0x05
        KeyEvent.KEYCODE_PAGE_DOWN -> 0x0c
        KeyEvent.KEYCODE_MOVE_HOME -> 0x02
        KeyEvent.KEYCODE_MOVE_END -> 0x04
        KeyEvent.KEYCODE_F1 -> 0x15; KeyEvent.KEYCODE_F2 -> 0x16; KeyEvent.KEYCODE_F3 -> 0x17
        KeyEvent.KEYCODE_F4 -> 0x18; KeyEvent.KEYCODE_F5 -> 0x19; KeyEvent.KEYCODE_F6 -> 0x1a
        KeyEvent.KEYCODE_F7 -> 0x1c; KeyEvent.KEYCODE_F8 -> 0x1d; KeyEvent.KEYCODE_F9 -> 0x1e
        KeyEvent.KEYCODE_F10 -> 0x1f
        KeyEvent.KEYCODE_F11 -> 0x00          // HELP
        KeyEvent.KEYCODE_F12 -> 0x1b          // GO
        KeyEvent.KEYCODE_MINUS -> 0x2d
        KeyEvent.KEYCODE_EQUALS -> 0x2b
        KeyEvent.KEYCODE_LEFT_BRACKET -> 0x5b
        KeyEvent.KEYCODE_RIGHT_BRACKET -> 0x5d
        KeyEvent.KEYCODE_SEMICOLON -> 0x3b
        KeyEvent.KEYCODE_APOSTROPHE -> 0x27
        KeyEvent.KEYCODE_COMMA -> 0x2c
        KeyEvent.KEYCODE_PERIOD -> 0x2e
        KeyEvent.KEYCODE_SLASH -> 0x2f
        KeyEvent.KEYCODE_GRAVE -> 0x5e
        KeyEvent.KEYCODE_NUMPAD_0 -> 0x4a; KeyEvent.KEYCODE_NUMPAD_1 -> 0x60; KeyEvent.KEYCODE_NUMPAD_2 -> 0x46
        KeyEvent.KEYCODE_NUMPAD_3 -> 0x47; KeyEvent.KEYCODE_NUMPAD_4 -> 0x7b; KeyEvent.KEYCODE_NUMPAD_5 -> 0x7d
        KeyEvent.KEYCODE_NUMPAD_6 -> 0x41; KeyEvent.KEYCODE_NUMPAD_7 -> 0x5c; KeyEvent.KEYCODE_NUMPAD_8 -> 0x7c
        KeyEvent.KEYCODE_NUMPAD_9 -> 0x21; KeyEvent.KEYCODE_NUMPAD_DOT -> 0x4b; KeyEvent.KEYCODE_NUMPAD_SUBTRACT -> 0x42
        else -> -1
    }

    /** A real keyboard on the TM 990/189's (codes: row * 8 + bit, MAME's LINE0-8). */
    private fun boardKey(code: Int, e: KeyEvent): Int {
        val ch = e.getUnicodeChar(0).toChar().uppercaseChar()
        if (ch in 'A'..'Z') {
            val i = ch - 'A'
            return when {
                i == 0 -> 0x1c
                i <= 5 -> 0x20 + (i - 1)          // B-F
                i <= 10 -> 0x28 + (i - 6)         // G-K
                i <= 15 -> 0x30 + (i - 11)        // L-P
                i <= 20 -> 0x38 + (i - 16)        // Q-U
                else -> 0x40 + (i - 21)           // V-Z
            }
        }
        return when (code) {
            KeyEvent.KEYCODE_0 -> 0x0c
            in KeyEvent.KEYCODE_1..KeyEvent.KEYCODE_5 -> 0x10 + (code - KeyEvent.KEYCODE_1)
            in KeyEvent.KEYCODE_6..KeyEvent.KEYCODE_9 -> 0x18 + (code - KeyEvent.KEYCODE_6)
            KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SHIFT_RIGHT -> 0x00
            KeyEvent.KEYCODE_SPACE -> 0x01
            KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> 0x02
            KeyEvent.KEYCODE_PERIOD -> 0x03          // $ =
            KeyEvent.KEYCODE_COMMA -> 0x04
            KeyEvent.KEYCODE_LEFT_BRACKET -> 0x08    // + (
            KeyEvent.KEYCODE_RIGHT_BRACKET -> 0x09   // - )
            KeyEvent.KEYCODE_MINUS -> 0x0a           // @ /
            KeyEvent.KEYCODE_EQUALS -> 0x0b          // > %
            KeyEvent.KEYCODE_SYSRQ -> UniversityBoardView.LOAD
            else -> -1
        }
    }

    /** A real keyboard / gamepad on the Atari: keys as POKEY codes, arrows and pad steer the joystick. */
    private fun atariKey(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_A -> 0x3f; KeyEvent.KEYCODE_B -> 0x15; KeyEvent.KEYCODE_C -> 0x12; KeyEvent.KEYCODE_D -> 0x3a
        KeyEvent.KEYCODE_E -> 0x2a; KeyEvent.KEYCODE_F -> 0x38; KeyEvent.KEYCODE_G -> 0x3d; KeyEvent.KEYCODE_H -> 0x39
        KeyEvent.KEYCODE_I -> 0x0d; KeyEvent.KEYCODE_J -> 0x01; KeyEvent.KEYCODE_K -> 0x05; KeyEvent.KEYCODE_L -> 0x00
        KeyEvent.KEYCODE_M -> 0x25; KeyEvent.KEYCODE_N -> 0x23; KeyEvent.KEYCODE_O -> 0x08; KeyEvent.KEYCODE_P -> 0x0a
        KeyEvent.KEYCODE_Q -> 0x2f; KeyEvent.KEYCODE_R -> 0x28; KeyEvent.KEYCODE_S -> 0x3e; KeyEvent.KEYCODE_T -> 0x2d
        KeyEvent.KEYCODE_U -> 0x0b; KeyEvent.KEYCODE_V -> 0x10; KeyEvent.KEYCODE_W -> 0x2e; KeyEvent.KEYCODE_X -> 0x16
        KeyEvent.KEYCODE_Y -> 0x2b; KeyEvent.KEYCODE_Z -> 0x17
        KeyEvent.KEYCODE_1 -> 0x1f; KeyEvent.KEYCODE_2 -> 0x1e; KeyEvent.KEYCODE_3 -> 0x1a; KeyEvent.KEYCODE_4 -> 0x18
        KeyEvent.KEYCODE_5 -> 0x1d; KeyEvent.KEYCODE_6 -> 0x1b; KeyEvent.KEYCODE_7 -> 0x33; KeyEvent.KEYCODE_8 -> 0x35
        KeyEvent.KEYCODE_9 -> 0x30; KeyEvent.KEYCODE_0 -> 0x32
        KeyEvent.KEYCODE_SPACE -> 0x21
        KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_NUMPAD_ENTER -> 0x0c
        KeyEvent.KEYCODE_DEL -> 0x34
        KeyEvent.KEYCODE_ESCAPE -> 0x1c
        KeyEvent.KEYCODE_TAB -> 0x2c
        KeyEvent.KEYCODE_CAPS_LOCK -> 0x3c
        KeyEvent.KEYCODE_MINUS -> 0x0e
        KeyEvent.KEYCODE_EQUALS -> 0x0f
        KeyEvent.KEYCODE_SEMICOLON -> 0x02
        KeyEvent.KEYCODE_APOSTROPHE -> 0x06          // +
        KeyEvent.KEYCODE_BACKSLASH -> 0x07           // *
        KeyEvent.KEYCODE_LEFT_BRACKET -> 0x36        // <
        KeyEvent.KEYCODE_RIGHT_BRACKET -> 0x37       // >
        KeyEvent.KEYCODE_COMMA -> 0x20
        KeyEvent.KEYCODE_PERIOD -> 0x22
        KeyEvent.KEYCODE_SLASH -> 0x26
        KeyEvent.KEYCODE_GRAVE -> 0x27               // inverse
        KeyEvent.KEYCODE_SHIFT_LEFT, KeyEvent.KEYCODE_SHIFT_RIGHT -> AtariXlView.SHIFT
        KeyEvent.KEYCODE_CTRL_LEFT, KeyEvent.KEYCODE_CTRL_RIGHT -> AtariXlView.CONTROL
        KeyEvent.KEYCODE_F1 -> 0x03; KeyEvent.KEYCODE_F2 -> 0x04; KeyEvent.KEYCODE_F3 -> 0x13; KeyEvent.KEYCODE_F4 -> 0x14
        KeyEvent.KEYCODE_F5, KeyEvent.KEYCODE_BUTTON_START -> AtariXlView.START
        KeyEvent.KEYCODE_F6, KeyEvent.KEYCODE_BUTTON_SELECT -> AtariXlView.SELECT
        KeyEvent.KEYCODE_F7, KeyEvent.KEYCODE_BUTTON_Y -> AtariXlView.OPTION
        KeyEvent.KEYCODE_F8 -> AtariXlView.HELP
        KeyEvent.KEYCODE_F9, KeyEvent.KEYCODE_BREAK -> AtariXlView.BREAK
        KeyEvent.KEYCODE_F10 -> AtariXlView.RESET
        else -> -1
    }

    private fun atariStickBit(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_DPAD_UP -> AtariXlView.UP
        KeyEvent.KEYCODE_DPAD_DOWN -> AtariXlView.DOWN
        KeyEvent.KEYCODE_DPAD_LEFT -> AtariXlView.LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT -> AtariXlView.RIGHT
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_X,
        KeyEvent.KEYCODE_ALT_LEFT, KeyEvent.KEYCODE_ALT_RIGHT, KeyEvent.KEYCODE_DPAD_CENTER -> AtariXlView.FIRE
        else -> 0
    }

    private fun setAtariStick(bits: Int) {
        atariStick = bits
        setTouch(3, bits)
        atari?.externalMask = bits
    }

    /** A real keyboard on the Acorn: Android key -> PS/2 set-2 code (0x90+ = E0 keys) */
    private fun acornKey(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_ESCAPE -> 0x76
        KeyEvent.KEYCODE_F1 -> 0x05; KeyEvent.KEYCODE_F2 -> 0x06; KeyEvent.KEYCODE_F3 -> 0x04; KeyEvent.KEYCODE_F4 -> 0x0c
        KeyEvent.KEYCODE_F5 -> 0x03; KeyEvent.KEYCODE_F6 -> 0x0b; KeyEvent.KEYCODE_F7 -> 0x83; KeyEvent.KEYCODE_F8 -> 0x0a
        KeyEvent.KEYCODE_F9 -> 0x01; KeyEvent.KEYCODE_F10 -> 0x09; KeyEvent.KEYCODE_F11 -> 0x78; KeyEvent.KEYCODE_F12 -> 0x07
        KeyEvent.KEYCODE_GRAVE -> 0x0e
        KeyEvent.KEYCODE_1 -> 0x16; KeyEvent.KEYCODE_2 -> 0x1e; KeyEvent.KEYCODE_3 -> 0x26; KeyEvent.KEYCODE_4 -> 0x25
        KeyEvent.KEYCODE_5 -> 0x2e; KeyEvent.KEYCODE_6 -> 0x36; KeyEvent.KEYCODE_7 -> 0x3d; KeyEvent.KEYCODE_8 -> 0x3e
        KeyEvent.KEYCODE_9 -> 0x46; KeyEvent.KEYCODE_0 -> 0x45
        KeyEvent.KEYCODE_MINUS -> 0x4e; KeyEvent.KEYCODE_EQUALS -> 0x55; KeyEvent.KEYCODE_DEL -> 0x66
        KeyEvent.KEYCODE_TAB -> 0x0d
        KeyEvent.KEYCODE_Q -> 0x15; KeyEvent.KEYCODE_W -> 0x1d; KeyEvent.KEYCODE_E -> 0x24; KeyEvent.KEYCODE_R -> 0x2d
        KeyEvent.KEYCODE_T -> 0x2c; KeyEvent.KEYCODE_Y -> 0x35; KeyEvent.KEYCODE_U -> 0x3c; KeyEvent.KEYCODE_I -> 0x43
        KeyEvent.KEYCODE_O -> 0x44; KeyEvent.KEYCODE_P -> 0x4d
        KeyEvent.KEYCODE_LEFT_BRACKET -> 0x54; KeyEvent.KEYCODE_RIGHT_BRACKET -> 0x5b
        KeyEvent.KEYCODE_ENTER -> 0x5a; KeyEvent.KEYCODE_NUMPAD_ENTER -> 0x9d
        KeyEvent.KEYCODE_CAPS_LOCK -> 0x58
        KeyEvent.KEYCODE_A -> 0x1c; KeyEvent.KEYCODE_S -> 0x1b; KeyEvent.KEYCODE_D -> 0x23; KeyEvent.KEYCODE_F -> 0x2b
        KeyEvent.KEYCODE_G -> 0x34; KeyEvent.KEYCODE_H -> 0x33; KeyEvent.KEYCODE_J -> 0x3b; KeyEvent.KEYCODE_K -> 0x42
        KeyEvent.KEYCODE_L -> 0x4b
        KeyEvent.KEYCODE_SEMICOLON -> 0x4c; KeyEvent.KEYCODE_APOSTROPHE -> 0x52; KeyEvent.KEYCODE_POUND -> 0x5d
        KeyEvent.KEYCODE_SHIFT_LEFT -> 0x12; KeyEvent.KEYCODE_SHIFT_RIGHT -> 0x59
        KeyEvent.KEYCODE_BACKSLASH -> 0x61
        KeyEvent.KEYCODE_Z -> 0x1a; KeyEvent.KEYCODE_X -> 0x22; KeyEvent.KEYCODE_C -> 0x21; KeyEvent.KEYCODE_V -> 0x2a
        KeyEvent.KEYCODE_B -> 0x32; KeyEvent.KEYCODE_N -> 0x31; KeyEvent.KEYCODE_M -> 0x3a
        KeyEvent.KEYCODE_COMMA -> 0x41; KeyEvent.KEYCODE_PERIOD -> 0x49; KeyEvent.KEYCODE_SLASH -> 0x4a
        KeyEvent.KEYCODE_CTRL_LEFT -> 0x14; KeyEvent.KEYCODE_CTRL_RIGHT -> 0x91
        KeyEvent.KEYCODE_ALT_LEFT -> 0x11; KeyEvent.KEYCODE_ALT_RIGHT -> 0x90
        KeyEvent.KEYCODE_SPACE -> 0x29
        KeyEvent.KEYCODE_INSERT -> 0x92; KeyEvent.KEYCODE_MOVE_HOME -> 0x93; KeyEvent.KEYCODE_PAGE_UP -> 0x94
        KeyEvent.KEYCODE_FORWARD_DEL -> 0x95; KeyEvent.KEYCODE_MOVE_END -> 0x96; KeyEvent.KEYCODE_PAGE_DOWN -> 0x97
        KeyEvent.KEYCODE_DPAD_UP -> 0x98; KeyEvent.KEYCODE_DPAD_LEFT -> 0x99
        KeyEvent.KEYCODE_DPAD_DOWN -> 0x9a; KeyEvent.KEYCODE_DPAD_RIGHT -> 0x9b
        else -> -1
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (acorn != null) {
            val k = acornKey(keyCode)
            if (k < 0) return super.onKeyDown(keyCode, event)
            if (event.repeatCount == 0) keyQueue.add(k or 0x100)
            return true
        }
        if (altair != null) {
            val seq = altairKey(keyCode, event)
            if (seq.isEmpty()) return super.onKeyDown(keyCode, event)
            for (ch in seq) keyQueue.add(ch or 0x100)
            return true
        }
        if (aim != null) {
            val k = aimKey(keyCode)
            if (k < 0) return super.onKeyDown(keyCode, event)
            if (event.repeatCount == 0) keyQueue.add(k or 0x100)
            return true
        }
        if (adam != null) {
            val k = adamKey(keyCode)
            if (k < 0) return super.onKeyDown(keyCode, event)
            if (event.repeatCount == 0) keyQueue.add(k or 0x100)
            return true
        }
        if (atari != null) {
            val s = atariStickBit(keyCode)
            if (s != 0) { setAtariStick(atariStick or s); return true }
            val k = atariKey(keyCode)
            if (k < 0) return super.onKeyDown(keyCode, event)
            if (event.repeatCount == 0) keyQueue.add(k or 0x100)
            return true
        }
        if (uboard != null) {
            val k = boardKey(keyCode, event)
            if (k < 0) return super.onKeyDown(keyCode, event)
            if (event.repeatCount == 0) keyQueue.add(k or 0x100)
            return true
        }
        if (workstation != null) {
            val k = deskKey(keyCode)
            if (k < 0) return super.onKeyDown(keyCode, event)
            if (event.repeatCount == 0) keyQueue.add(k or 0x100)
            return true
        }
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
        joypad?.externalMask = keyInputs or stickMask
        ablPad?.externalMask = keyInputs or stickMask
        flightStick?.externalMask = keyInputs or stickMask
        keychain?.externalMask = keyInputs
        hh?.externalMask = keyInputs
        wide?.externalMask = keyInputs or stickMask
        pads.forEach { it?.externalMask = keyInputs }
        apfPads.forEach { it?.externalMask = keyInputs }
        a3dPad?.externalMask = keyInputs
        mini?.externalMask = keyInputs
        return true
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        if (acorn != null) {
            val k = acornKey(keyCode)
            if (k < 0) return super.onKeyUp(keyCode, event)
            keyQueue.add(k)
            return true
        }
        if (altair != null) {
            return if (altairKey(keyCode, event).isEmpty()) super.onKeyUp(keyCode, event) else true
        }
        if (aim != null) {
            val k = aimKey(keyCode)
            if (k < 0) return super.onKeyUp(keyCode, event)
            keyQueue.add(k)
            return true
        }
        if (adam != null) {
            val k = adamKey(keyCode)
            if (k < 0) return super.onKeyUp(keyCode, event)
            keyQueue.add(k)
            return true
        }
        if (atari != null) {
            val s = atariStickBit(keyCode)
            if (s != 0) { setAtariStick(atariStick and s.inv()); return true }
            val k = atariKey(keyCode)
            if (k < 0) return super.onKeyUp(keyCode, event)
            keyQueue.add(k)
            return true
        }
        if (uboard != null) {
            val k = boardKey(keyCode, event)
            if (k < 0) return super.onKeyUp(keyCode, event)
            keyQueue.add(k)
            return true
        }
        if (workstation != null) {
            val k = deskKey(keyCode)
            if (k < 0) return super.onKeyUp(keyCode, event)
            keyQueue.add(k)
            return true
        }
        if (machine.skin == Skin.APVS_1292 && dpadBit(keyCode) != 0) { dpad = dpad and dpadBit(keyCode).inv(); applyDpad(); return true }
        val b = keyBit(keyCode)
        if (b == 0) return super.onKeyUp(keyCode, event)
        keyInputs = keyInputs and b.inv()
        mat?.externalMask = keyInputs
        pet?.externalMask = keyInputs
        phone?.externalMask = keyInputs or stickMask
        arcade?.externalMask = keyInputs or stickMask
        handheld?.externalMask = keyInputs or stickMask
        joypad?.externalMask = keyInputs or stickMask
        ablPad?.externalMask = keyInputs or stickMask
        flightStick?.externalMask = keyInputs or stickMask
        keychain?.externalMask = keyInputs
        hh?.externalMask = keyInputs
        wide?.externalMask = keyInputs or stickMask
        pads.forEach { it?.externalMask = keyInputs }
        apfPads.forEach { it?.externalMask = keyInputs }
        a3dPad?.externalMask = keyInputs
        mini?.externalMask = keyInputs
        return true
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemBars()
    }
}
