package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.hypot

/**
 * Early-80s home computers in the style of the 1200XL (cream case, a brushed
 * metal band with the console keys, lights and function keys above dark brown
 * keys) and the 130XE (light grey wedge with ribs behind the keys, dark grey
 * keys and a column of RESET / OPTION / SELECT / START / HELP on the right).
 * A TV sits above, a one-button joystick and a disk drive in front. No maker's
 * name or logo is reproduced.
 *
 * Keys report POKEY keyboard codes (0..63, as Atari800's akey.h) plus the
 * extra codes of [Atari800Keys]. SHIFT and CONTROL latch for the next key
 * when tapped on their own. The cartridge slot and the drive open the disk
 * menu, the small MENU plate by the power light the emulator menu.
 */
class AtariXlView(ctx: Context, private val model: Int) : View(ctx) {

    private val xe = model == MODEL_XE
    private val m400 = model == MODEL_400
    private val m800 = model == MODEL_800
    private val xl = model == MODEL_800XL || model == MODEL_600XL
    /** console keys in a column right of the keys (400, 800, 800XL, 600XL) */
    private val col = m400 || m800 || xl
    /** console keys in a row above the keys (1200XL: across the metal band; XE: at the top right) */
    private val rowAbove = !col

    /** Colours of each case: body, key well, keys, wide (control) keys, console keys. */
    private class Style(
        val bodyTop: Int, val bodyBottom: Int, val well: Int,
        val keyTop: Int, val keySide: Int, val keyInk: Int,
        val wideTop: Int, val wideSide: Int, val wideInk: Int,
        val conTop: Int, val conSide: Int, val conInk: Int,
        val flat: Boolean = false,
    )

    private val style = when (model) {
        MODEL_XE -> Style(0xFFCCCAC4.toInt(), 0xFFB3B1AB.toInt(), 0xFFA9A7A1.toInt(),
            0xFFE6E2D8.toInt(), 0xFFBDB8AC.toInt(), 0xFF4A4844.toInt(),
            0xFFE6E2D8.toInt(), 0xFFBDB8AC.toInt(), 0xFF4A4844.toInt(),
            0xFFC2C0BA.toInt(), 0xFF9C9A94.toInt(), 0xFF3C3B38.toInt())
        MODEL_400 -> Style(0xFFDDD3BA.toInt(), 0xFFC2B597.toInt(), 0xFF3A2C22.toInt(),
            0xFFCDBB92.toInt(), 0xFFA89670.toInt(), 0xFF3A2C22.toInt(),
            0xFFDE8A3A.toInt(), 0xFFB56A24.toInt(), 0xFF2E2018.toInt(),
            0xFFE2C66E.toInt(), 0xFFB79C4E.toInt(), 0xFF3A2C22.toInt(), flat = true)
        MODEL_800 -> Style(0xFFDDD3BA.toInt(), 0xFFC2B597.toInt(), 0xFF3A2C22.toInt(),
            0xFF5A4636.toInt(), 0xFF3A2C22.toInt(), 0xFFF0E6D2.toInt(),
            0xFF6B5442.toInt(), 0xFF45352A.toInt(), 0xFFF0E6D2.toInt(),
            0xFFE2B84E.toInt(), 0xFFB08A2E.toInt(), 0xFF3A2C22.toInt())
        MODEL_800XL -> Style(0xFF443A31.toInt(), 0xFF2F2822.toInt(), 0xFF1E1A17.toInt(),
            0xFF46464A.toInt(), 0xFF2A2A2D.toInt(), 0xFFF2F2F2.toInt(),
            0xFF4E4E52.toInt(), 0xFF2E2E31.toInt(), 0xFFF2F2F2.toInt(),
            0xFFB9BABC.toInt(), 0xFF86878A.toInt(), 0xFF2A2A2A.toInt())
        MODEL_600XL -> Style(0xFF2E2D2C.toInt(), 0xFF1F1E1D.toInt(), 0xFF141413.toInt(),
            0xFF333336.toInt(), 0xFF1C1C1E.toInt(), 0xFFF2F2F2.toInt(),
            0xFF3A3A3D.toInt(), 0xFF202022.toInt(), 0xFFF2F2F2.toInt(),
            0xFFD6D6D4.toInt(), 0xFFA4A4A2.toInt(), 0xFF2A2A2A.toInt())
        else -> Style(0xFFE6DFCD.toInt(), 0xFFCFC6B0.toInt(), 0xFF2A2420.toInt(),   // 1200XL
            0xFF4A413A.toInt(), 0xFF2E2823.toInt(), 0xFFF0E8D8.toInt(),
            0xFF4A413A.toInt(), 0xFF2E2823.toInt(), 0xFFF0E8D8.toInt(),
            0xFF5A524B.toInt(), 0xFF3B3530.toInt(), 0xFFF0E8D8.toInt())
    }

    var onKey: ((Int, Boolean) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var onDisks: (() -> Unit)? = null
    /** joystick: bit 0 up, 1 down, 2 left, 3 right, 4 fire */
    var onJoy: ((Int) -> Unit)? = null
    /** bit 0 drive busy, bit 4 / 5 the 1200XL's L1 / L2 */
    var indicators = 0
        set(v) { if (v != field) { field = v; postInvalidate() } }
    var mediaLabel: String? = null
        set(v) { field = v; postInvalidate() }
    /** joystick bits from a real gamepad, shown on the stick */
    var externalMask = 0
        set(v) { if (v != field) { field = v; postInvalidate() } }

    companion object {
        const val W = 336
        const val H = 240
        const val MODEL_1200XL = 0
        /** 130XE, 65XE, 800XE */
        const val MODEL_XE = 1
        const val MODEL_400 = 2
        const val MODEL_800 = 3
        const val MODEL_800XL = 4
        const val MODEL_600XL = 5
        const val SHIFT = 64
        const val CONTROL = 65
        const val START = 66
        const val SELECT = 67
        const val OPTION = 68
        const val BREAK = 69
        const val RESET = 70
        const val HELP = 0x11
        const val UP = 1
        const val DOWN = 2
        const val LEFT = 4
        const val RIGHT = 8
        const val FIRE = 16
    }

    private enum class Kind { CHAR, WIDE, CONSOLE, LIGHTS }

    private class Key(val label: String, val code: Int, val kind: Kind, val shiftLabel: String) {
        val rect = RectF()
        var down = false
    }

    private data class K(val label: String, val code: Int, val units: Float = 1f, val kind: Kind = Kind.CHAR, val shift: String = "")

    private val c = Kind.CONSOLE
    private val w = Kind.WIDE

    // the main block, shared; the two models differ in the top row and the right-hand keys
    private val numberRow: List<K> get() = listOf(
        K("ESC", 0x1c, 1f, w), K("1", 0x1f, shift = "!"), K("2", 0x1e, shift = "\""), K("3", 0x1a, shift = "#"),
        K("4", 0x18, shift = "$"), K("5", 0x1d, shift = "%"), K("6", 0x1b, shift = "&"), K("7", 0x33, shift = "'"),
        K("8", 0x35, shift = "@"), K("9", 0x30, shift = "("), K("0", 0x32, shift = ")"),
        K("<", 0x36, shift = "CLR"), K(">", 0x37, shift = "INS"))
    private val qRow: List<K> get() = listOf(
        K("TAB", 0x2c, 1.5f, w), K("Q", 0x2f), K("W", 0x2e), K("E", 0x2a), K("R", 0x28), K("T", 0x2d), K("Y", 0x2b),
        K("U", 0x0b), K("I", 0x0d), K("O", 0x08), K("P", 0x0a), K("-", 0x0e, shift = "_ ↑"), K("=", 0x0f, shift = "| ↓"))
    private val aRow: List<K> get() = listOf(
        K("CONTROL", CONTROL, 1.75f, w), K("A", 0x3f), K("S", 0x3e), K("D", 0x3a), K("F", 0x38), K("G", 0x3d), K("H", 0x39),
        K("J", 0x01), K("K", 0x05), K("L", 0x00), K(";", 0x02, shift = ":"), K("+", 0x06, shift = "\\ ←"),
        K("*", 0x07, shift = "^ →"))
    private val zRow: List<K> get() = listOf(
        K("Z", 0x17), K("X", 0x16), K("C", 0x12), K("V", 0x10), K("B", 0x15), K("N", 0x23), K("M", 0x25),
        K(",", 0x20, shift = "["), K(".", 0x22, shift = "]"), K("/", 0x26, shift = "?"))

    private val mainRows: List<List<K>> = listOf(
        numberRow + listOf(K("BACK S", 0x34, 1.25f, w, "DELETE"), K("BREAK", BREAK, 1.25f, w)),
        qRow + listOf(K("RETURN", 0x0c, 2f, w)),
        aRow + listOf(if (m400 || m800) K("LOWR", 0x3c, 1.75f, w, "CAPS") else K("CAPS", 0x3c, 1.75f, w)),
        listOf(K("SHIFT", SHIFT, 2.25f, w)) + zRow + listOf(K("SHIFT", SHIFT, 2.25f, w), K("◪", 0x27, 1f, w)),
        listOf(K("", 0x21, 9f, w)),
    )
    private val rows: List<List<K>> = if (col) mainRows else if (xe) listOf(
        // the XE's five slanted console keys, top right above the keyboard
        listOf(K("", -1, 8.4f, Kind.LIGHTS), K("HELP", HELP, 1.42f, c), K("START", START, 1.42f, c),
            K("SELECT", SELECT, 1.42f, c), K("OPTION", OPTION, 1.42f, c), K("RESET", RESET, 1.42f, c)),
    ) + mainRows else listOf(
        listOf(K("RESET", RESET, 1.3f, c), K("", -1, 1.7f, Kind.LIGHTS), K("START", START, 1.2f, c), K("SELECT", SELECT, 1.2f, c),
            K("OPTION", OPTION, 1.2f, c), K("◪", 0x27, 1.05f, c), K("BREAK", BREAK, 1.15f, c), K("HELP", HELP, 1.05f, c),
            K("F1", 0x03, 1.05f, c), K("F2", 0x04, 1.05f, c), K("F3", 0x13, 1.05f, c), K("F4", 0x14, 1.05f, c)),
        numberRow + listOf(K("BACK S", 0x34, 2f, w, "DELETE")),
        qRow + listOf(K("RETURN", 0x0c, 1.5f, w)),
        aRow + listOf(K("CAPS", 0x3c, 1.25f, w)),
        listOf(K("SHIFT", SHIFT, 2.5f, w)) + zRow + listOf(K("SHIFT", SHIFT, 2.5f, w)),
        listOf(K("", 0x21, 9f, w)),
    )
    // the console keys, top to bottom in a column right of the keyboard
    private val column: List<K> = when {
        xl -> listOf(K("RESET", RESET, 1f, c), K("OPTION", OPTION, 1f, c),
            K("SELECT", SELECT, 1f, c), K("START", START, 1f, c), K("HELP", HELP, 1f, c))
        m400 || m800 -> listOf(K("RESET", RESET, 1f, c, "SYSTEM"), K("OPTION", OPTION, 1f, c),
            K("SELECT", SELECT, 1f, c), K("START", START, 1f, c))
        else -> emptyList()
    }

    private val keys = ArrayList<Key>()
    private val keyByRow = ArrayList<List<Key>>()
    private val columnKeys: List<Key>

    init {
        for (r in rows) {
            val list = r.map { Key(it.label, it.code, it.kind, it.shift) }
            keyByRow += list
            keys += list.filter { it.kind != Kind.LIGHTS }
        }
        columnKeys = column.map { Key(it.label, it.code, it.kind, it.shift) }
        keys += columnKeys
    }

    private val frame = Bitmap.createBitmap(W, H, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(W * H)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER }
    private val pixelPaint = Paint()   // sharp pixels

    private val tvBody = RectF()
    private val screen = RectF()
    private val case = RectF()
    private val band = RectF()          // 1200XL metal band / 130XE ribbed back
    private val slot = RectF()          // cartridge slot
    private val keyWell = RectF()
    private val powerLight = PointF()
    private val menuPlate = RectF()
    private val lights = RectF()        // 1200XL power / L1 / L2
    private val stickBase = RectF()
    private val fireButton = RectF()
    private val drive = RectF()
    private val driveSlot = RectF()
    private var unit = 1f

    private val pointers = HashMap<Int, Key>()
    private val latched = HashSet<Key>()
    private val usedWhileHeld = HashSet<Key>()
    private var stickId = -1
    private var stickDir = 0
    private var stickX = 0f
    private var stickY = 0f
    private val fireIds = HashSet<Int>()

    fun updateFrame(px: IntArray) {
        if (px.size < W * H) return
        System.arraycopy(px, 0, framePx, 0, W * H)
        postInvalidate()
    }

    override fun onSizeChanged(vw: Int, vh: Int, ow: Int, oh: Int) {
        val wf = vw.toFloat()
        // TV
        val tw = wf * 0.94f
        val sw = tw * 0.86f
        val sh = sw / 1.4f
        tvBody.set((wf - tw) / 2, vh * 0.01f, (wf + tw) / 2, vh * 0.01f + sh + tw * 0.16f)
        screen.set(wf / 2 - sw / 2, tvBody.top + tw * 0.05f, wf / 2 + sw / 2, tvBody.top + tw * 0.05f + sh)

        // computer
        val cw = wf * 0.985f
        val maxUnits = rows.maxOf { r -> r.sumOf { it.units.toDouble() }.toFloat() } + (if (col) 2.0f else 0f)
        unit = cw * 0.94f / maxUnits
        val keyRows = rows.size
        val kh = unit * 1.22f                 // keys a little taller than wide: easier to hit
        val bandH = unit * 1.1f
        val caseH = bandH + kh * keyRows + unit * (if (col) 0.9f else 0.6f) - (if (xe) kh * 0.2f else 0f)
        case.set((wf - cw) / 2, tvBody.bottom + vh * 0.012f, (wf + cw) / 2, tvBody.bottom + vh * 0.012f + caseH)
        band.set(case.left, case.top, case.right, case.top + bandH)
        slot.set(case.centerX() - unit * 2.6f, band.top + bandH * 0.25f, case.centerX() + unit * 2.6f, band.top + bandH * 0.62f)
        val kbW = unit * maxUnits
        val kbLeft = case.centerX() - kbW / 2
        keyWell.set(kbLeft - unit * 0.18f, band.bottom + unit * 0.12f, kbLeft + kbW + unit * 0.18f, case.bottom - unit * 0.25f)
        var y = band.bottom + unit * 0.3f
        val mainW = rows.drop(if (rowAbove) 1 else 0).maxOf { r -> r.sumOf { it.units.toDouble() }.toFloat() }
        val mainLeft = kbLeft
        for ((ri, r) in rows.withIndex()) {
            val total = r.sumOf { it.units.toDouble() }.toFloat()
            val consoleRow = rowAbove && ri == 0
            val rowH = if (consoleRow) kh * 0.8f else kh
            // the space bar is centred under the main block
            var x = if (r.size == 1) mainLeft + (mainW - total) * unit / 2 else mainLeft
            for ((ki, k) in r.withIndex()) {
                keyByRow[ri][ki].rect.set(x + unit * 0.04f, y + rowH * 0.05f, x + k.units * unit - unit * 0.04f, y + rowH * 0.95f)
                x += k.units * unit
            }
            y += if (consoleRow) rowH + unit * 0.22f else rowH
        }
        // the 1200XL's console keys sit on the metal band, above the well of the main keys
        if (rowAbove) keyWell.top = keyByRow[0][0].rect.bottom + unit * 0.2f
        if (xe) {
            // the XE has its keys in a light tray, the console keys above it on the case
            keyWell.top = keyByRow[0][1].rect.bottom + unit * 0.15f
        }
        if (col) {
            val cx0 = mainLeft + mainW * unit + unit * 0.35f
            var cy = band.bottom + unit * 0.3f
            val ch = kh * keyRows / columnKeys.size
            for (k in columnKeys) {
                // the XLs print the key names beside the keys: narrower keys leave room
                k.rect.set(cx0, cy + ch * 0.1f, cx0 + unit * (if (xl) 1.05f else 1.55f), cy + ch * 0.9f)
                cy += ch
            }
            powerLight.set(cx0 + unit * 0.78f, band.top + bandH * 0.4f)
        } else if (xe) {
            // power light low on the front left
            powerLight.set(case.left + unit * 0.9f, case.bottom - unit * 0.35f)
        } else {
            val l = keyByRow[0][1].rect
            lights.set(l)
            powerLight.set(l.left + l.width() * 0.2f, l.top + l.height() * 0.38f)
        }
        menuPlate.set(slot.right + unit * 1.2f, slot.top - unit * 0.05f, slot.right + unit * 3.0f, slot.bottom + unit * 0.05f)

        // joystick (left) and disk drive (right) in front of the computer
        val below = vh - case.bottom
        val zoneTop = case.bottom + below * 0.06f
        val zoneH = below * 0.88f
        val bs = minOf(zoneH, wf * 0.46f)
        stickBase.set(wf * 0.04f, zoneTop + (zoneH - bs) / 2, wf * 0.04f + bs, zoneTop + (zoneH + bs) / 2)
        val fb = bs * 0.2f
        fireButton.set(stickBase.left + bs * 0.06f, stickBase.top + bs * 0.06f, stickBase.left + bs * 0.06f + fb, stickBase.top + bs * 0.06f + fb)
        val dw = wf * 0.42f
        val dh = minOf(zoneH * 0.75f, dw * 0.62f)
        drive.set(wf * 0.96f - dw, zoneTop + (zoneH - dh) / 2, wf * 0.96f, zoneTop + (zoneH + dh) / 2)
        driveSlot.set(drive.left + dw * 0.1f, drive.top + dh * 0.42f, drive.right - dw * 0.1f, drive.top + dh * 0.54f)
    }

    override fun onDraw(cv: Canvas) {
        cv.drawColor(0xFF1E1B18.toInt())
        drawTv(cv)
        drawComputer(cv)
        drawJoystick(cv)
        drawDrive(cv)
    }

    private fun drawTv(cv: Canvas) {
        val tw = tvBody.width()
        paint.color = 0x66000000
        cv.drawRoundRect(tvBody.left + tw * 0.01f, tvBody.top + tw * 0.012f, tvBody.right + tw * 0.01f, tvBody.bottom + tw * 0.012f, tw * 0.04f, tw * 0.04f, paint)
        paint.color = 0xFF000000.toInt()
        paint.shader = LinearGradient(0f, tvBody.top, 0f, tvBody.bottom, 0xFF4A4038.toInt(), 0xFF2E2823.toInt(), Shader.TileMode.CLAMP)
        cv.drawRoundRect(tvBody, tw * 0.04f, tw * 0.04f, paint)
        paint.shader = null
        paint.color = 0xFF0A0A0A.toInt()
        cv.drawRoundRect(screen.left - tw * 0.018f, screen.top - tw * 0.018f, screen.right + tw * 0.018f, screen.bottom + tw * 0.018f, tw * 0.035f, tw * 0.035f, paint)
        frame.setPixels(framePx, 0, W, 0, 0, W, H)
        cv.drawBitmap(frame, null, screen, pixelPaint)
        paint.shader = RadialGradient(screen.centerX(), screen.centerY(), screen.width() * 0.75f, 0x00000000, 0x30000000, Shader.TileMode.CLAMP)
        cv.drawRect(screen, paint)
        paint.shader = null
        // knobs and the speaker grille under the tube
        val ky = (screen.bottom + tvBody.bottom) / 2 + tw * 0.01f
        for (i in 0..1) {
            paint.color = 0xFF1A1714.toInt()
            cv.drawCircle(tvBody.right - tw * (0.08f + i * 0.07f), ky, tw * 0.022f, paint)
            paint.color = 0xFF6A6058.toInt()
            cv.drawCircle(tvBody.right - tw * (0.08f + i * 0.07f), ky, tw * 0.012f, paint)
        }
        stroke.color = 0xFF1F1A16.toInt(); stroke.strokeWidth = tw * 0.004f
        for (i in 0..5) {
            val gx = tvBody.left + tw * (0.06f + i * 0.025f)
            cv.drawLine(gx, ky - tw * 0.02f, gx, ky + tw * 0.02f, stroke)
        }
    }

    private fun drawComputer(cv: Canvas) {
        val r = unit * 0.4f
        val st = style
        paint.color = 0x77000000
        cv.drawRoundRect(case.left + unit * 0.1f, case.top + unit * 0.15f, case.right + unit * 0.1f, case.bottom + unit * 0.15f, r, r, paint)
        paint.color = 0xFF000000.toInt()
        paint.shader = LinearGradient(0f, case.top, 0f, case.bottom, st.bodyTop, st.bodyBottom, Shader.TileMode.CLAMP)
        cv.drawRoundRect(case, r, r, paint)
        paint.shader = null
        when {
            m400 || m800 -> {
                // the raised housing at the back with its hinged cartridge door (the 800's is wider: two slots)
                val hw = if (m800) 5.2f else 4.2f
                val hood = RectF(case.centerX() - unit * hw, band.top + band.height() * 0.06f, case.centerX() + unit * hw, band.bottom - band.height() * 0.02f)
                paint.color = 0xFFB3A786.toInt()
                cv.drawRoundRect(hood.left - unit * 0.1f, hood.top + unit * 0.08f, hood.right + unit * 0.1f, hood.bottom + unit * 0.1f, unit * 0.2f, unit * 0.2f, paint)
                paint.color = 0xFF000000.toInt()
        paint.shader = LinearGradient(0f, hood.top, 0f, hood.bottom, 0xFFE6DDC6.toInt(), 0xFFCDC1A2.toInt(), Shader.TileMode.CLAMP)
                cv.drawRoundRect(hood, unit * 0.2f, unit * 0.2f, paint)
                paint.shader = null
                stroke.color = 0xFFA89C7C.toInt(); stroke.strokeWidth = unit * 0.05f
                for (i in 0..4) {
                    val vy = band.top + band.height() * (0.25f + i * 0.13f)
                    cv.drawLine(case.right - unit * 2.4f, vy, case.right - unit * 0.6f, vy, stroke)
                }
            }
            xl -> {
                // beige top with louvres along the edges, the dark lower case around the keys
                val top = RectF(case.left, case.top, case.right, band.bottom + unit * 0.05f)
                paint.color = 0xFF000000.toInt()
        paint.shader = LinearGradient(0f, top.top, 0f, top.bottom, 0xFFE8E2D2.toInt(), 0xFFD8D0BC.toInt(), Shader.TileMode.CLAMP)
                cv.drawRoundRect(top, r, r, paint)
                cv.drawRect(top.left, top.centerY(), top.right, top.bottom, paint)
                paint.shader = null
                paint.color = 0xFF8E8778.toInt()
                var gx = top.left + unit * 0.6f
                while (gx < top.right - unit * 0.6f) {
                    cv.drawRect(gx, top.top + unit * 0.05f, gx + unit * 0.12f, top.top + unit * 0.14f, paint)
                    gx += unit * 0.34f
                }
                var gy = top.top + unit * 0.3f
                while (gy < top.bottom - unit * 0.1f) {
                    cv.drawRect(top.left + unit * 0.05f, gy, top.left + unit * 0.2f, gy + unit * 0.08f, paint)
                    cv.drawRect(top.right - unit * 0.2f, gy, top.right - unit * 0.05f, gy + unit * 0.08f, paint)
                    gy += unit * 0.24f
                }
            }
            xe -> {
                // slanted ribs across the back
                stroke.color = 0xFFB0AEA8.toInt(); stroke.strokeWidth = unit * 0.07f
                val bt = band.top + band.height() * 0.1f; val bb = band.bottom - band.height() * 0.05f
                var gx = band.left + unit * 1.2f
                while (gx < band.right - unit * 0.6f) {
                    cv.drawLine(gx, bt, gx - unit * 0.5f, bb, stroke)
                    gx += unit * 0.32f
                }
                // the ledge under the ribs
                paint.color = 0xFFBDBBB5.toInt()
                cv.drawRect(case.left + unit * 0.1f, band.bottom - unit * 0.05f, case.right - unit * 0.1f, band.bottom + unit * 0.08f, paint)
            }
            else -> {
                // brushed metal band behind the console keys
                val metal = RectF(case.left + unit * 0.15f, case.top + unit * 0.15f, case.right - unit * 0.15f,
                    keyByRow[0][0].rect.bottom + unit * 0.18f)
                paint.color = 0xFF000000.toInt()
        paint.shader = LinearGradient(0f, metal.top, 0f, metal.bottom,
                    intArrayOf(0xFFD5D6D4.toInt(), 0xFFA9ABA8.toInt(), 0xFFC4C5C2.toInt(), 0xFF9C9E9B.toInt()), null, Shader.TileMode.CLAMP)
                cv.drawRoundRect(metal, r * 0.6f, r * 0.6f, paint)
                paint.shader = null
                stroke.color = 0x22000000; stroke.strokeWidth = unit * 0.015f
                var gy = metal.top + unit * 0.05f
                while (gy < metal.bottom) { cv.drawLine(metal.left, gy, metal.right, gy, stroke); gy += unit * 0.06f }
            }
        }
        // cartridge slot (tap: media menu)
        paint.color = when { xe -> 0xFF9E9C96.toInt(); m400 || m800 -> 0xFFA99C7B.toInt(); xl -> 0xFFA9A18E.toInt(); else -> 0xFF7E7868.toInt() }
        cv.drawRoundRect(slot.left - unit * 0.12f, slot.top - unit * 0.1f, slot.right + unit * 0.12f, slot.bottom + unit * 0.1f, unit * 0.1f, unit * 0.1f, paint)
        paint.color = 0xFF1A1714.toInt()
        cv.drawRoundRect(slot, unit * 0.06f, unit * 0.06f, paint)
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL)
        text.color = 0xFFCFC8B8.toInt(); text.textSize = slot.height() * 0.5f
        cv.drawText(mediaLabel ?: "tap: load a game", slot.centerX(), slot.centerY() + text.textSize * 0.36f, text)

        // keyboard well
        paint.color = style.well
        cv.drawRoundRect(keyWell, unit * 0.15f, unit * 0.15f, paint)
        for (k in keys) drawKey(cv, k)

        // lights and the menu plate
        if (model == MODEL_1200XL) {
            val l = lights
            val names = arrayOf("POWER", "L1", "L2")
            val on = booleanArrayOf(true, indicators and 0x10 != 0, indicators and 0x20 != 0)
            for (i in 0..2) {
                val x = l.left + l.width() * (0.14f + i * 0.36f)
                drawLed(cv, x, l.top + l.height() * 0.35f, on[i], if (i == 0) 0xFFFF3B2E.toInt() else 0xFFFFB02E.toInt())
                text.color = 0xFF2A2724.toInt(); text.textSize = unit * 0.16f
                cv.drawText(names[i], x, l.bottom - l.height() * 0.1f, text)
            }
        } else if (xe) {
            paint.color = 0xFFFF3B2E.toInt()
            cv.drawRoundRect(powerLight.x - unit * 0.3f, powerLight.y - unit * 0.04f, powerLight.x + unit * 0.3f, powerLight.y + unit * 0.04f, unit * 0.04f, unit * 0.04f, paint)
        } else {
            drawLed(cv, powerLight.x, powerLight.y, true, 0xFFFF3B2E.toInt())
            text.color = if (xl) 0xFFBDB7A8.toInt() else 0xFF45423C.toInt(); text.textSize = unit * 0.2f
            if (!xl) cv.drawText("POWER", powerLight.x, powerLight.y + unit * 0.42f, text)
        }
        if (xl) {
            // the 800XL / 600XL print the console key names beside the keys
            text.color = 0xFFA8A296.toInt(); text.textSize = unit * 0.19f
            text.textAlign = Paint.Align.LEFT
            for (k in columnKeys) cv.drawText(k.label, k.rect.right + unit * 0.08f, k.rect.centerY() + text.textSize * 0.36f, text)
            text.textAlign = Paint.Align.CENTER
        }
        paint.color = when { xe -> 0xFFB0AEA8.toInt(); m400 || m800 -> 0xFFB5A884.toInt(); xl -> 0xFFC7BFAB.toInt(); else -> 0xFF8C8E8B.toInt() }
        cv.drawRoundRect(menuPlate, unit * 0.08f, unit * 0.08f, paint)
        text.color = 0xFF2A2724.toInt(); text.textSize = minOf(menuPlate.height() * 0.42f, menuPlate.width() * 0.3f)
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
        cv.drawText("MENU", menuPlate.centerX(), menuPlate.centerY() + text.textSize * 0.36f, text)
    }

    private fun drawLed(cv: Canvas, x: Float, y: Float, on: Boolean, color: Int) {
        val rr = unit * 0.09f
        paint.color = 0xFF1A1714.toInt()
        cv.drawCircle(x, y, rr * 1.5f, paint)
        paint.color = if (on) color else 0xFF4A2A22.toInt()
        cv.drawCircle(x, y, rr, paint)
    }

    private fun drawKey(cv: Canvas, k: Key) {
        val r = k.rect
        val down = k.down || latched.contains(k)
        val st = style
        val (top, side, ink) = when {
            k.kind == Kind.CONSOLE -> Triple(st.conTop, st.conSide, st.conInk)
            k.kind == Kind.WIDE && k.code != 0x21 -> Triple(st.wideTop, st.wideSide, st.wideInk)
            else -> Triple(st.keyTop, st.keySide, st.keyInk)
        }
        if (st.flat) {
            // membrane: flat, it only darkens when pressed
            paint.color = if (down) side else top
            cv.drawRoundRect(r.left + unit * 0.03f, r.top + unit * 0.03f, r.right - unit * 0.03f, r.bottom - unit * 0.03f, unit * 0.05f, unit * 0.05f, paint)
            stroke.color = side; stroke.strokeWidth = unit * 0.03f
            cv.drawRoundRect(r.left + unit * 0.03f, r.top + unit * 0.03f, r.right - unit * 0.03f, r.bottom - unit * 0.03f, unit * 0.05f, unit * 0.05f, stroke)
            text.color = ink
            text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL)
            val my = r.centerY()
            if (k.shiftLabel.isNotEmpty()) {
                text.textSize = unit * (if (k.shiftLabel.length > 2) 0.17f else 0.26f)
                cv.drawText(k.shiftLabel, r.centerX(), my - unit * 0.08f, text)
                text.textSize = unit * (if (k.label.length > 1) 0.17f else 0.3f)
                cv.drawText(k.label, r.centerX(), my + unit * 0.25f, text)
            } else {
                text.textSize = if (k.kind == Kind.CHAR && k.label.length == 1) unit * 0.4f
                    else minOf(unit * 0.2f, r.width() * 0.9f / maxOf(1, k.label.length) * 1.6f)
                cv.drawText(k.label, r.centerX(), my + text.textSize * 0.36f, text)
            }
            return
        }
        val rad = unit * 0.1f
        val sink = if (down) r.height() * 0.06f else 0f
        paint.color = side
        cv.drawRoundRect(r.left, r.top + sink, r.right, r.bottom, rad, rad, paint)
        paint.color = if (down) side else top
        cv.drawRoundRect(r.left + unit * 0.06f, r.top + sink, r.right - unit * 0.06f, r.bottom - r.height() * 0.14f + sink * 0.5f, rad, rad, paint)
        text.color = ink
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL)
        val cy = (r.top + r.bottom - r.height() * 0.14f) / 2 + sink
        if (k.shiftLabel.isNotEmpty()) {
            text.textSize = unit * (if (k.shiftLabel.length > 2) 0.17f else 0.26f)
            cv.drawText(k.shiftLabel, r.centerX(), cy - unit * 0.06f, text)
            text.textSize = unit * (if (k.label.length > 1) 0.17f else 0.3f)
            cv.drawText(k.label, r.centerX(), cy + unit * 0.27f, text)
            return
        }
        val big = k.kind == Kind.CHAR && k.label.length == 1
        text.textSize = when {
            big -> unit * 0.4f
            k.label == "◪" -> unit * 0.36f
            else -> minOf(unit * 0.2f, r.width() * 0.9f / maxOf(1, k.label.length) * 1.6f)
        }
        cv.drawText(k.label, r.centerX(), cy + text.textSize * 0.36f, text)
    }

    private fun drawJoystick(cv: Canvas) {
        val b = stickBase
        val s = b.width()
        paint.color = 0x77000000
        cv.drawRoundRect(b.left + s * 0.02f, b.top + s * 0.03f, b.right + s * 0.02f, b.bottom + s * 0.03f, s * 0.12f, s * 0.12f, paint)
        paint.color = 0xFF000000.toInt()
        paint.shader = LinearGradient(0f, b.top, 0f, b.bottom, 0xFF2C2C2C.toInt(), 0xFF141414.toInt(), Shader.TileMode.CLAMP)
        cv.drawRoundRect(b, s * 0.12f, s * 0.12f, paint)
        paint.shader = null
        // a stripe across the top edge, as on the classic one-button stick
        paint.color = 0xFF8A5A2E.toInt()
        cv.drawRect(b.left + s * 0.3f, b.top + s * 0.06f, b.right - s * 0.08f, b.top + s * 0.1f, paint)
        // fire button (top left corner)
        val fire = fireIds.isNotEmpty() || externalMask and FIRE != 0
        paint.color = 0xFF3A0D0A.toInt()
        cv.drawOval(fireButton.left, fireButton.top + s * 0.012f, fireButton.right, fireButton.bottom + s * 0.012f, paint)
        paint.color = if (fire) 0xFFA61E16.toInt() else 0xFFD8291E.toInt()
        cv.drawOval(fireButton.left, fireButton.top + (if (fire) s * 0.01f else 0f), fireButton.right, fireButton.bottom + (if (fire) s * 0.01f else 0f), paint)
        // boot and stick
        val dir = stickDir or (externalMask and 0x0f)
        val cx = b.centerX() + s * 0.04f; val cy = b.centerY() + s * 0.04f
        var dx = 0f; var dy = 0f
        if (dir and LEFT != 0) dx -= 1f; if (dir and RIGHT != 0) dx += 1f
        if (dir and UP != 0) dy -= 1f; if (dir and DOWN != 0) dy += 1f
        val len = hypot(dx, dy).let { if (it == 0f) 1f else it }
        val tx = cx + dx / len * s * 0.16f; val ty = cy + dy / len * s * 0.16f
        paint.color = 0xFF0A0A0A.toInt()
        cv.drawCircle(cx, cy, s * 0.2f, paint)
        paint.color = 0xFF1E1E1E.toInt()
        cv.drawCircle(cx, cy, s * 0.15f, paint)
        stroke.color = 0xFF050505.toInt(); stroke.strokeWidth = s * 0.075f; stroke.strokeCap = Paint.Cap.ROUND
        cv.drawLine(cx, cy, tx, ty, stroke)
        stroke.strokeCap = Paint.Cap.BUTT
        paint.shader = RadialGradient(tx - s * 0.02f, ty - s * 0.02f, s * 0.07f, 0xFF3A3A3A.toInt(), 0xFF0C0C0C.toInt(), Shader.TileMode.CLAMP)
        cv.drawCircle(tx, ty, s * 0.06f, paint)
        paint.shader = null
    }

    private fun drawDrive(cv: Canvas) {
        val d = drive
        val dw = d.width()
        paint.color = 0x77000000
        cv.drawRoundRect(d.left + dw * 0.015f, d.top + dw * 0.02f, d.right + dw * 0.015f, d.bottom + dw * 0.02f, dw * 0.04f, dw * 0.04f, paint)
        paint.color = 0xFF000000.toInt()
        paint.shader = LinearGradient(0f, d.top, 0f, d.bottom, if (xl) 0xFFE6DFCD.toInt() else style.bodyTop,
            if (xl) 0xFFC9C0AA.toInt() else style.bodyBottom, Shader.TileMode.CLAMP)
        cv.drawRoundRect(d, dw * 0.04f, dw * 0.04f, paint)
        paint.shader = null
        // dark front panel with the slot and door lever
        val front = RectF(d.left + dw * 0.05f, d.top + d.height() * 0.3f, d.right - dw * 0.05f, d.bottom - d.height() * 0.12f)
        paint.color = 0xFF3B3631.toInt()
        cv.drawRoundRect(front, dw * 0.02f, dw * 0.02f, paint)
        paint.color = 0xFF0E0C0B.toInt()
        cv.drawRoundRect(driveSlot, driveSlot.height() / 2, driveSlot.height() / 2, paint)
        paint.color = 0xFF57514A.toInt()
        cv.drawRoundRect(d.centerX() - dw * 0.08f, driveSlot.bottom + front.height() * 0.08f, d.centerX() + dw * 0.08f,
            driveSlot.bottom + front.height() * 0.3f, dw * 0.01f, dw * 0.01f, paint)
        // busy light
        val busy = indicators and 1 != 0
        paint.color = 0xFF1A1714.toInt()
        cv.drawCircle(front.left + dw * 0.06f, front.top + front.height() * 0.2f, dw * 0.022f, paint)
        paint.color = if (busy) 0xFFFF3B2E.toInt() else 0xFF4A2A22.toInt()
        cv.drawCircle(front.left + dw * 0.06f, front.top + front.height() * 0.2f, dw * 0.014f, paint)
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL)
        text.color = 0xFF4E4A44.toInt(); text.textSize = d.height() * 0.1f
        cv.drawText("DISKS & CARTRIDGES", d.centerX(), d.top + d.height() * 0.19f, text)
    }

    // ---- keyboard ----
    private fun keyAt(x: Float, y: Float): Key? {
        if (!RectF(case).contains(x, y)) return null
        for (k in keys) if (k.rect.contains(x, y)) return k
        var best: Key? = null; var bd = unit * unit * 0.2f
        for (k in keys) {
            val ex = maxOf(0f, abs(x - k.rect.centerX()) - k.rect.width() / 2)
            val ey = maxOf(0f, abs(y - k.rect.centerY()) - k.rect.height() / 2)
            val d = ex * ex + ey * ey
            if (d < bd) { bd = d; best = k }
        }
        return best
    }

    private fun isLatch(k: Key) = k.code == SHIFT || k.code == CONTROL

    private fun press(k: Key) {
        if (isLatch(k) && latched.contains(k)) return
        if (!isLatch(k)) for (m in keys) if (m.down && isLatch(m)) usedWhileHeld.add(m)
        k.down = true
        onKey?.invoke(k.code, true)
    }

    private fun release(k: Key) {
        k.down = false
        if (isLatch(k)) {
            if (usedWhileHeld.remove(k)) { onKey?.invoke(k.code, false); return }
            if (!latched.remove(k)) { latched.add(k); return }
            onKey?.invoke(k.code, false)
            return
        }
        onKey?.invoke(k.code, false)
        if (latched.isNotEmpty() && k.kind != Kind.CONSOLE) {
            for (m in latched) onKey?.invoke(m.code, false)
            latched.clear()
        }
    }

    // ---- joystick ----
    private fun dirFor(x: Float, y: Float): Int {
        val cx = stickBase.centerX() + stickBase.width() * 0.04f
        val cy = stickBase.centerY() + stickBase.width() * 0.04f
        val dx = x - cx; val dy = y - cy
        if (hypot(dx, dy) < stickBase.width() * 0.08f) return 0
        val a = Math.toDegrees(atan2(dy.toDouble(), dx.toDouble()))   // 0 = right, 90 = down
        val sector = (((a + 360 + 22.5) % 360) / 45).toInt()
        return intArrayOf(RIGHT, RIGHT or DOWN, DOWN, DOWN or LEFT, LEFT, LEFT or UP, UP, UP or RIGHT)[sector]
    }

    private fun sendJoy() {
        onJoy?.invoke(stickDir or (if (fireIds.isNotEmpty()) FIRE else 0))
        invalidate()
    }

    private fun inFire(x: Float, y: Float) =
        RectF(fireButton).apply { inset(-fireButton.width() * 0.5f, -fireButton.height() * 0.5f) }.contains(x, y)

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                val id = e.getPointerId(i)
                val x = e.getX(i); val y = e.getY(i)
                if (e.actionMasked == MotionEvent.ACTION_DOWN) {
                    if (RectF(menuPlate).apply { inset(-unit * 0.2f, -unit * 0.2f) }.contains(x, y)) { onMenu?.invoke(); return true }
                    if (drive.contains(x, y) || RectF(slot).apply { inset(-unit * 0.3f, -unit * 0.25f) }.contains(x, y)) { onDisks?.invoke(); return true }
                }
                if (inFire(x, y)) {
                    performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                    fireIds.add(id); sendJoy(); return true
                }
                if (stickId < 0 && RectF(stickBase).apply { inset(-stickBase.width() * 0.08f, -stickBase.width() * 0.08f) }.contains(x, y)) {
                    stickId = id; stickX = x; stickY = y
                    stickDir = dirFor(x, y); sendJoy(); return true
                }
                val k = keyAt(x, y) ?: return true
                performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                pointers[id] = k
                press(k)
            }
            MotionEvent.ACTION_MOVE -> {
                if (stickId >= 0) {
                    val i = e.findPointerIndex(stickId)
                    if (i >= 0) {
                        val d = dirFor(e.getX(i), e.getY(i))
                        if (d != stickDir) { stickDir = d; sendJoy() }
                    }
                }
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val id = e.getPointerId(e.actionIndex)
                if (fireIds.remove(id)) { sendJoy(); return true }
                if (id == stickId) { stickId = -1; stickDir = 0; sendJoy(); return true }
                pointers.remove(id)?.let { release(it) }
            }
            MotionEvent.ACTION_CANCEL -> {
                for (k in pointers.values) release(k)
                pointers.clear()
                fireIds.clear(); stickId = -1; stickDir = 0; sendJoy()
            }
        }
        invalidate()
        return true
    }
}
