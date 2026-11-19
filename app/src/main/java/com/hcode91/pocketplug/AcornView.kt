package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.abs
import kotlin.math.hypot

/**
 * RISC OS computers, drawn after the real ones without makers' names or logos:
 *  - "a7000" / "a7000p": a light grey monitor on the slim system unit (floppy,
 *    CD drive, power light), the keyboard and the three-button mouse;
 *  - "archimedes": the 300 / 400 / 500 series: a low beige box under the
 *    monitor, the keyboard with orange-red function keys under a clear strip;
 *  - "a4000" / "a5000": the later flat boxes, the keyboard with grey keys;
 *  - "a3000" / "a3010" / "a3020": the computer built into the keyboard (red,
 *    green or red function keys), with a monitor above it;
 *  - "a4": the dark grey laptop, its screen in the lid.
 *
 * The screen is the mouse pad: drag to move the pointer, tap to click SELECT.
 * The mouse's own buttons can be held (MENU opens RISC OS menus). Keys report
 * PS/2 set-2 codes; 0x90+ are the E0 keys (see acorn.cpp). SHIFT, CTRL and
 * ALT latch for the next key when tapped on their own. The floppy drive opens
 * the disk menu, the MENU plate on the unit the emulator menu.
 */
class AcornView(ctx: Context, private val style: String) : View(ctx) {

    private val plus = style == "a7000p"
    private val kind = when (style) {
        "archimedes", "a4000", "a5000" -> KIND_DESK
        "a3000", "a3010", "a3020" -> KIND_KEYBOARD
        "a4" -> KIND_LAPTOP
        else -> KIND_A7000
    }
    // colours: case, keys (light / dark / function), key sides
    private val caseTop: Int; private val caseBottom: Int
    private val keyLight: Int; private val keyLightSide: Int
    private val keyDark: Int; private val keyDarkSide: Int
    private val keyFn: Int; private val keyFnSide: Int
    private val legend: Int
    init {
        fun c(v: Long) = v.toInt()
        when (style) {
            "archimedes" -> { caseTop = c(0xFFE6DFC9); caseBottom = c(0xFFCFC6AC); keyLight = c(0xFFE9E3D2); keyLightSide = c(0xFFC4BBA2)
                keyDark = c(0xFFA39B88); keyDarkSide = c(0xFF7F786A); keyFn = c(0xFFE0603A); keyFnSide = c(0xFFAC4127); legend = c(0xFF3A352C) }
            "a5000" -> { caseTop = c(0xFFEDE9DD); caseBottom = c(0xFFD6D0C0); keyLight = c(0xFFDAD7CF); keyLightSide = c(0xFFB3AFA5)
                keyDark = c(0xFFA6A39B); keyDarkSide = c(0xFF84817A); keyFn = c(0xFFA6A39B); keyFnSide = c(0xFF84817A); legend = c(0xFF2F2E2B) }
            "a4000" -> { caseTop = c(0xFFE3E0D6); caseBottom = c(0xFFCAC6BA); keyLight = c(0xFFB9B9B6); keyLightSide = c(0xFF95958F)
                keyDark = c(0xFF9A9A96); keyDarkSide = c(0xFF797975); keyFn = c(0xFF9A9A96); keyFnSide = c(0xFF797975); legend = c(0xFF262624) }
            "a3000" -> { caseTop = c(0xFFE8E2CE); caseBottom = c(0xFFD2CAB0); keyLight = c(0xFFDCD5C0); keyLightSide = c(0xFFB5AD95)
                keyDark = c(0xFF9F9784); keyDarkSide = c(0xFF7C7566); keyFn = c(0xFFD2343A); keyFnSide = c(0xFF9A2228); legend = c(0xFF34302A) }
            "a3010" -> { caseTop = c(0xFFEDEEEB); caseBottom = c(0xFFD5D6D2); keyLight = c(0xFFF3F3F0); keyLightSide = c(0xFFC9CAC5)
                keyDark = c(0xFF9C9D9A); keyDarkSide = c(0xFF797A77); keyFn = c(0xFF5FC46E); keyFnSide = c(0xFF3D9650); legend = c(0xFF262624) }
            "a3020" -> { caseTop = c(0xFFE9E8E2); caseBottom = c(0xFFD0CEC6); keyLight = c(0xFFEEEDE7); keyLightSide = c(0xFFC5C3BB)
                keyDark = c(0xFF96968F); keyDarkSide = c(0xFF74746E); keyFn = c(0xFFD2343A); keyFnSide = c(0xFF9A2228); legend = c(0xFF262624) }
            "a4" -> { caseTop = c(0xFF5E5C59); caseBottom = c(0xFF4A4846); keyLight = c(0xFFE8E6E0); keyLightSide = c(0xFFBDBAB2)
                keyDark = c(0xFF9E9C97); keyDarkSide = c(0xFF7B7975); keyFn = c(0xFFE8E6E0); keyFnSide = c(0xFFBDBAB2); legend = c(0xFF2A2927) }
            else -> { caseTop = c(0xFFD3D1CA); caseBottom = c(0xFFB9B6AE); keyLight = c(0xFFEEECE6); keyLightSide = c(0xFFC7C4BC)
                keyDark = c(0xFF9F9D97); keyDarkSide = c(0xFF7C7A74); keyFn = c(0xFF9F9D97); keyFnSide = c(0xFF7C7A74); legend = c(0xFF2F2E2B) }
        }
    }


    var onKey: ((Int, Boolean) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var onDisks: (() -> Unit)? = null
    var onMouseMove: ((Float, Float) -> Unit)? = null
    /** bit 0 SELECT, bit 1 MENU, bit 2 ADJUST */
    var onMouseButtons: ((Int) -> Unit)? = null
    /** bit 0 floppy busy */
    var indicators = 0
        set(v) { if (v != field) { field = v; postInvalidate() } }
    var floppyLabel: String? = null
        set(v) { field = v; postInvalidate() }

    companion object {
        private const val KIND_A7000 = 0
        private const val KIND_DESK = 1
        private const val KIND_KEYBOARD = 2
        private const val KIND_LAPTOP = 3
        const val W = 800
        const val H = 600
        const val SHIFT_L = 0x12
        const val SHIFT_R = 0x59
        const val CTRL = 0x14
        const val ALT = 0x11
    }

    private class Key(val label: String, val code: Int, val units: Float, val shiftLabel: String = "", val dark: Boolean = false, val fn: Boolean = false) {
        val rect = RectF()
        var down = false
    }

    private val rows: List<List<Key>> = listOf(
        listOf(Key("Esc", 0x76, 1f, dark = true)) + listOf(0x05, 0x06, 0x04, 0x0c, 0x03, 0x0b, 0x83, 0x0a, 0x01, 0x09, 0x78, 0x07)
            .mapIndexed { i, c -> Key("F${i + 1}", c, 1f, dark = true, fn = true) } +
            listOf(Key("Delete", 0x95, 1f, dark = true), Key("Copy", 0x96, 1f, dark = true)),
        listOf(Key("`", 0x0e, 1f, "¬"), Key("1", 0x16, 1f, "!"), Key("2", 0x1e, 1f, "\""), Key("3", 0x26, 1f, "£"),
            Key("4", 0x25, 1f, "$"), Key("5", 0x2e, 1f, "%"), Key("6", 0x36, 1f, "^"), Key("7", 0x3d, 1f, "&"),
            Key("8", 0x3e, 1f, "*"), Key("9", 0x46, 1f, "("), Key("0", 0x45, 1f, ")"), Key("-", 0x4e, 1f, "_"),
            Key("=", 0x55, 1f, "+"), Key("⟵", 0x66, 2f, dark = true)),
        listOf(Key("Tab", 0x0d, 1.5f, dark = true), Key("Q", 0x15, 1f), Key("W", 0x1d, 1f), Key("E", 0x24, 1f),
            Key("R", 0x2d, 1f), Key("T", 0x2c, 1f), Key("Y", 0x35, 1f), Key("U", 0x3c, 1f), Key("I", 0x43, 1f),
            Key("O", 0x44, 1f), Key("P", 0x4d, 1f), Key("[", 0x54, 1f, "{"), Key("]", 0x5b, 1f, "}"),
            Key("Return", 0x5a, 1.5f, dark = true)),
        listOf(Key("Caps", 0x58, 1.75f, dark = true), Key("A", 0x1c, 1f), Key("S", 0x1b, 1f), Key("D", 0x23, 1f),
            Key("F", 0x2b, 1f), Key("G", 0x34, 1f), Key("H", 0x33, 1f), Key("J", 0x3b, 1f), Key("K", 0x42, 1f),
            Key("L", 0x4b, 1f), Key(";", 0x4c, 1f, ":"), Key("'", 0x52, 1f, "@"), Key("#", 0x5d, 1f, "~"),
            Key("", 0x5a, 1.25f, dark = true)),
        listOf(Key("Shift", SHIFT_L, 1.25f, dark = true), Key("\\", 0x61, 1f, "|"), Key("Z", 0x1a, 1f), Key("X", 0x22, 1f),
            Key("C", 0x21, 1f), Key("V", 0x2a, 1f), Key("B", 0x32, 1f), Key("N", 0x31, 1f), Key("M", 0x3a, 1f),
            Key(",", 0x41, 1f, "<"), Key(".", 0x49, 1f, ">"), Key("/", 0x4a, 1f, "?"), Key("Shift", SHIFT_R, 1.75f, dark = true),
            Key("↑", 0x98, 1f, dark = true)),
        listOf(Key("Ctrl", CTRL, 1.4f, dark = true), Key("Alt", ALT, 1.3f, dark = true), Key("", 0x29, 7.3f),
            Key("Alt", 0x90, 1f, dark = true), Key("←", 0x99, 1f, dark = true), Key("↓", 0x9a, 1f, dark = true),
            Key("→", 0x9b, 1f, dark = true)),
    )
    private val keys = rows.flatten()

    private val frame = Bitmap.createBitmap(W, H, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(W * H)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER }
    private val pixelPaint = Paint(Paint.FILTER_BITMAP_FLAG)

    private val monitor = RectF()
    private val screen = RectF()
    private val unit = RectF()          // the system unit
    private val floppy = RectF()
    private val cdrom = RectF()
    private val menuPlate = RectF()
    private val board = RectF()         // the keyboard
    private val mouse = RectF()
    private val mouseButtons = Array(3) { RectF() }
    private var u = 1f                  // key unit

    // pointer handling
    private var padId = -1
    private var padX = 0f
    private var padY = 0f
    private var padMoved = false
    private var padDownAt = 0L
    private var buttons = 0
    private val buttonIds = HashMap<Int, Int>()
    private val pointers = HashMap<Int, Key>()
    private val latched = HashSet<Key>()
    private val usedWhileHeld = HashSet<Key>()

    fun updateFrame(px: IntArray) {
        if (px.size < W * H) return
        System.arraycopy(px, 0, framePx, 0, W * H)
        postInvalidate()
    }

    override fun onSizeChanged(vw: Int, vh: Int, ow: Int, oh: Int) {
        when (kind) {
            KIND_KEYBOARD -> { layoutKeyboardComputer(vw, vh); return }
            KIND_LAPTOP -> { layoutLaptop(vw, vh); return }
        }
        val wf = vw.toFloat()
        val mw = wf * 0.96f
        val sw = mw * 0.86f
        val sh = sw * 0.75f
        monitor.set((wf - mw) / 2, vh * 0.01f, (wf + mw) / 2, vh * 0.01f + sh + mw * 0.13f)
        screen.set(wf / 2 - sw / 2, monitor.top + mw * 0.05f, wf / 2 + sw / 2, monitor.top + mw * 0.05f + sh)
        val uh = wf * 0.13f
        unit.set(wf * 0.02f, monitor.bottom + wf * 0.01f, wf * 0.98f, monitor.bottom + wf * 0.01f + uh)
        floppy.set(unit.left + unit.width() * 0.07f, unit.top + uh * 0.4f, unit.left + unit.width() * 0.33f, unit.top + uh * 0.62f)
        cdrom.set(unit.left + unit.width() * 0.38f, unit.top + uh * 0.22f, unit.left + unit.width() * 0.72f, unit.top + uh * 0.78f)
        menuPlate.set(unit.right - unit.width() * 0.2f, unit.top + uh * 0.55f, unit.right - unit.width() * 0.06f, unit.top + uh * 0.82f)
        if (kind == KIND_DESK) {
            // the Archimedes boxes: badge plate on the left, floppy on the right
            menuPlate.set(unit.left + unit.width() * 0.05f, unit.top + uh * 0.3f, unit.left + unit.width() * 0.22f, unit.top + uh * 0.6f)
            floppy.set(unit.right - unit.width() * 0.36f, unit.top + uh * 0.45f, unit.right - unit.width() * 0.1f, unit.top + uh * 0.62f)
        }

        layoutKeys(wf, unit.bottom + wf * 0.025f, if (kind == KIND_DESK && style == "archimedes") 0.6f else 0f)
        layoutMouse(wf, vh.toFloat())
    }

    /** Keys from the top of the keyboard at y; extra = key units of clear strip above the F keys */
    private fun layoutKeys(wf: Float, top: Float, extra: Float) {
        val maxUnits = rows.maxOf { r -> r.sumOf { it.units.toDouble() }.toFloat() }
        u = wf * 0.94f / maxUnits
        val kh = u * 1.15f
        val left = (wf - u * maxUnits) / 2
        board.set(wf * 0.015f, top, wf * 0.985f, top + kh * rows.size + u * (0.7f + extra))
        var y = board.top + u * (0.35f + extra)
        for ((ri, r) in rows.withIndex()) {
            var x = left
            val rh = if (ri == 0) kh * 0.85f else kh
            for (k in r) {
                k.rect.set(x + u * 0.04f, y + rh * 0.06f, x + k.units * u - u * 0.04f, y + rh * 0.94f)
                x += k.units * u
            }
            y += rh + (if (ri == 0) u * 0.12f else 0f)
        }
        // the Return key spans two rows: join the halves
        val ret1 = rows[2].last().rect
        val ret2 = rows[3].last().rect
        ret2.top = ret1.bottom - u * 0.2f
    }

    private fun layoutMouse(wf: Float, vh: Float) {
        // the mouse, in front of the keyboard on the right
        val below = vh - board.bottom
        val mh = minOf(below * 0.8f, wf * 0.36f)
        val mwid = mh * 0.62f
        mouse.set(wf * 0.9f - mwid, board.bottom + (below - mh) / 2, wf * 0.9f, board.bottom + (below + mh) / 2)
        val bw = mouse.width() / 3
        for (i in 0..2) mouseButtons[i].set(mouse.left + bw * i + bw * 0.06f, mouse.top + mh * 0.06f,
            mouse.left + bw * (i + 1) - bw * 0.06f, mouse.top + mh * 0.38f)
    }

    /** A3000 / A3010 / A3020: a monitor, then the computer-in-a-keyboard with its ribbed top */
    private fun layoutKeyboardComputer(vw: Int, vh: Int) {
        val wf = vw.toFloat()
        val mw = wf * 0.9f
        val sw = mw * 0.84f
        val sh = sw * 0.75f
        monitor.set((wf - mw) / 2, vh * 0.01f, (wf + mw) / 2, vh * 0.01f + sh + mw * 0.13f)
        screen.set(wf / 2 - sw / 2, monitor.top + mw * 0.05f, wf / 2 + sw / 2, monitor.top + mw * 0.05f + sh)
        // the ribbed top of the case is the "unit"; keys below it
        val top = monitor.bottom + wf * 0.03f
        unit.set(wf * 0.015f, top, wf * 0.985f, top + wf * 0.16f)
        layoutKeys(wf, unit.bottom - wf * 0.01f, 0f)
        board.top = unit.top
        val uh = unit.height()
        floppy.set(unit.right - unit.width() * 0.3f, unit.top + uh * 0.42f, unit.right - unit.width() * 0.06f, unit.top + uh * 0.58f)
        menuPlate.set(unit.right - unit.width() * 0.24f, unit.bottom + u * 0.02f, unit.right - unit.width() * 0.06f, unit.bottom + u * 0.32f)
        layoutMouse(wf, vh.toFloat())
    }

    /** A4: the screen in the lid, the keyboard in the base */
    private fun layoutLaptop(vw: Int, vh: Int) {
        val wf = vw.toFloat()
        val lw = wf * 0.92f
        val sw = lw * 0.7f
        val sh = sw * 0.75f
        monitor.set((wf - lw) / 2, vh * 0.015f, (wf + lw) / 2, vh * 0.015f + sh + lw * 0.1f)
        screen.set(monitor.left + lw * 0.22f, monitor.top + lw * 0.05f, monitor.left + lw * 0.22f + sw, monitor.top + lw * 0.05f + sh)
        val top = monitor.bottom + wf * 0.005f
        unit.set(wf * 0.03f, top, wf * 0.97f, top + wf * 0.12f)
        layoutKeys(wf, unit.bottom - wf * 0.005f, 0f)
        board.left = unit.left; board.right = unit.right
        board.top = unit.top
        val uh = unit.height()
        menuPlate.set(unit.right - unit.width() * 0.34f, unit.top + uh * 0.25f, unit.right - unit.width() * 0.08f, unit.top + uh * 0.75f)
        floppy.set(unit.left + unit.width() * 0.36f, unit.top + uh * 0.4f, unit.left + unit.width() * 0.56f, unit.top + uh * 0.6f)
        layoutMouse(wf, vh.toFloat())
    }

    override fun onDraw(cv: Canvas) {
        cv.drawColor(0xFF2A2724.toInt())
        when (kind) {
            KIND_KEYBOARD -> { drawMonitor(cv); drawKeyboard(cv); drawKeyboardTop(cv) }
            KIND_LAPTOP -> { drawLid(cv); drawKeyboard(cv); drawLaptopTop(cv) }
            KIND_DESK -> { drawMonitor(cv); drawDeskUnit(cv); drawKeyboard(cv); if (style == "archimedes") drawClearStrip(cv) }
            else -> { drawMonitor(cv); drawUnit(cv); drawKeyboard(cv) }
        }
        drawMouse(cv)
    }

    private fun drawFloppySlot(cv: Canvas, w: Float, h: Float, light: Boolean) {
        paint.color = 0xFF1E1D1B.toInt()
        cv.drawRoundRect(floppy, floppy.height() / 2, floppy.height() / 2, paint)
        paint.color = if (indicators and 1 != 0) 0xFFFFB02E.toInt() else 0xFF5A4A2A.toInt()
        cv.drawCircle(floppy.left - w * 0.015f, floppy.centerY(), w * 0.006f, paint)
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL)
        text.color = if (light) 0xFFD8D6D0.toInt() else 0xFF5E5B55.toInt(); text.textSize = h
        cv.drawText(floppyLabel ?: "tap: disks & files", floppy.centerX(), floppy.top - h * 0.5f, text)
    }

    private fun drawPlate(cv: Canvas, w: Float, fill: Int, ink: Int) {
        paint.color = fill
        cv.drawRoundRect(menuPlate, w * 0.006f, w * 0.006f, paint)
        // two small lights, as on the real plates
        paint.color = 0xFF3FD24A.toInt()
        cv.drawCircle(menuPlate.right - menuPlate.width() * 0.1f, menuPlate.top + menuPlate.height() * 0.3f, menuPlate.height() * 0.09f, paint)
        paint.color = if (indicators and 1 != 0) 0xFFFFB02E.toInt() else 0xFF6A5A3A.toInt()
        cv.drawCircle(menuPlate.right - menuPlate.width() * 0.1f, menuPlate.top + menuPlate.height() * 0.68f, menuPlate.height() * 0.09f, paint)
        text.color = ink; text.textSize = menuPlate.height() * 0.42f
        text.typeface = Typeface.create(Typeface.SERIF, Typeface.NORMAL)
        cv.drawText("MENU", menuPlate.centerX() - menuPlate.width() * 0.06f, menuPlate.centerY() + text.textSize * 0.36f, text)
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL)
    }

    /** the low Archimedes / A4000 / A5000 boxes */
    private fun drawDeskUnit(cv: Canvas) {
        val b = unit
        val w = b.width()
        paint.color = 0x66000000
        cv.drawRoundRect(b.left + w * 0.006f, b.top + w * 0.008f, b.right + w * 0.006f, b.bottom + w * 0.008f, w * 0.01f, w * 0.01f, paint)
        paint.color = 0xFF000000.toInt()
        paint.shader = LinearGradient(0f, b.top, 0f, b.bottom, caseTop, caseBottom, Shader.TileMode.CLAMP)
        cv.drawRoundRect(b, w * 0.01f, w * 0.01f, paint)
        paint.shader = null
        // the lid's edge and a front panel band
        stroke.color = caseBottom; stroke.strokeWidth = w * 0.003f
        cv.drawLine(b.left + w * 0.01f, b.top + b.height() * 0.2f, b.right - w * 0.01f, b.top + b.height() * 0.2f, stroke)
        if (style != "archimedes") {
            paint.color = if (style == "a4000") 0xFFBDBAB0.toInt() else 0xFFC9C4B6.toInt()
            cv.drawRect(b.left + w * 0.01f, b.top + b.height() * 0.28f, b.right - w * 0.01f, b.top + b.height() * 0.78f, paint)
        } else {
            // the cooling slots along the right side
            stroke.color = caseBottom
            var x = b.right - w * 0.05f
            while (x < b.right - w * 0.015f) { cv.drawLine(x, b.top + b.height() * 0.3f, x, b.bottom - b.height() * 0.15f, stroke); x += w * 0.008f }
        }
        drawPlate(cv, w, if (style == "archimedes") 0xFFD9CFA6.toInt() else 0xFFE4DCB8.toInt(), 0xFF4E4A3E.toInt())
        // floppy bay
        paint.color = caseBottom
        cv.drawRoundRect(floppy.left - w * 0.02f, floppy.top - b.height() * 0.14f, floppy.right + w * 0.02f, floppy.bottom + b.height() * 0.14f, w * 0.006f, w * 0.006f, paint)
        drawFloppySlot(cv, w, b.height() * 0.12f, false)
    }

    /** the clear strip over the 300 / 400 series' function keys (for a key legend card) */
    private fun drawClearStrip(cv: Canvas) {
        val fk = rows[0].filter { it.fn }
        if (fk.isEmpty()) return
        val r = RectF(fk.first().rect.left, board.top + u * 0.1f, fk.last().rect.right, fk.first().rect.top - u * 0.08f)
        paint.color = 0x55FFFFFF
        cv.drawRoundRect(r, u * 0.08f, u * 0.08f, paint)
        stroke.color = 0x66FFFFFF; stroke.strokeWidth = u * 0.03f
        cv.drawRoundRect(r, u * 0.08f, u * 0.08f, stroke)
    }

    /** A3000 / A3010 / A3020: the ribbed top of the case over the keys */
    private fun drawKeyboardTop(cv: Canvas) {
        val b = unit
        val w = b.width()
        paint.color = 0xFF000000.toInt()
        paint.shader = LinearGradient(0f, b.top, 0f, b.bottom, caseTop, caseBottom, Shader.TileMode.CLAMP)
        cv.drawRoundRect(b, w * 0.015f, w * 0.015f, paint)
        paint.shader = null
        stroke.color = caseBottom; stroke.strokeWidth = w * 0.003f
        if (style == "a3000") {
            // vents over the left half, a plain right half
            var x = b.left + w * 0.08f
            while (x < b.left + w * 0.55f) { cv.drawLine(x, b.top + b.height() * 0.12f, x, b.bottom - b.height() * 0.2f, stroke); x += w * 0.012f }
        } else {
            // ribs across the whole top
            var x = b.left + w * 0.12f
            while (x < b.right - w * 0.02f) { cv.drawLine(x, b.top + b.height() * 0.06f, x, b.bottom - b.height() * 0.06f, stroke); x += w * 0.085f }
            var vx = b.left + w * 0.05f
            while (vx < b.right - w * 0.05f) { cv.drawLine(vx, b.top + b.height() * 0.04f, vx + w * 0.02f, b.top + b.height() * 0.04f, stroke); vx += w * 0.035f }
        }
        drawFloppySlot(cv, w, b.height() * 0.13f, false)
        drawPlate(cv, w, 0xFFE8E0BE.toInt(), 0xFF4E4A3E.toInt())
    }

    /** A4: the dark lid around the screen, its lights and knobs */
    private fun drawLid(cv: Canvas) {
        val m = monitor
        val w = m.width()
        paint.color = 0x66000000
        cv.drawRoundRect(m.left + w * 0.01f, m.top + w * 0.012f, m.right + w * 0.01f, m.bottom + w * 0.012f, w * 0.015f, w * 0.015f, paint)
        paint.color = 0xFF000000.toInt()
        paint.shader = LinearGradient(0f, m.top, 0f, m.bottom, 0xFF5A5856.toInt(), 0xFF484644.toInt(), Shader.TileMode.CLAMP)
        cv.drawRoundRect(m, w * 0.015f, w * 0.015f, paint)
        paint.shader = null
        paint.color = 0xFF2C2B2A.toInt()
        cv.drawRect(screen.left - w * 0.012f, screen.top - w * 0.012f, screen.right + w * 0.012f, screen.bottom + w * 0.012f, paint)
        frame.setPixels(framePx, 0, W, 0, 0, W, H)
        cv.drawBitmap(frame, null, screen, pixelPaint)
        // status lights and the contrast / brightness knobs
        val lx = m.left + w * 0.07f
        for (i in 0..4) {
            paint.color = if (i == 0) 0xFF5BE05E.toInt() else if (i == 2 && indicators and 1 != 0) 0xFFFFB02E.toInt() else 0xFF3A3937.toInt()
            cv.drawRect(lx, screen.top + w * (0.04f + i * 0.045f), lx + w * 0.008f, screen.top + w * (0.06f + i * 0.045f), paint)
        }
        for (i in 0..1) {
            paint.color = 0xFF1C1B1A.toInt()
            cv.drawCircle(lx + w * 0.02f, screen.top + w * (0.3f + i * 0.08f), w * 0.026f, paint)
        }
        // the speaker grille in the lid's corner
        stroke.color = 0xFF3C3B39.toInt(); stroke.strokeWidth = w * 0.003f
        var gx = screen.right + w * 0.025f
        while (gx < m.right - w * 0.015f) { cv.drawLine(gx, screen.bottom - w * 0.2f, gx, screen.bottom - w * 0.05f, stroke); gx += w * 0.008f }
    }

    /** A4: the base above the keys (grille, floppy, plate) */
    private fun drawLaptopTop(cv: Canvas) {
        val b = unit
        val w = b.width()
        paint.color = 0xFF000000.toInt()
        paint.shader = LinearGradient(0f, b.top, 0f, b.bottom, caseTop, caseBottom, Shader.TileMode.CLAMP)
        cv.drawRect(b, paint)
        paint.shader = null
        stroke.color = 0xFF3A3937.toInt(); stroke.strokeWidth = w * 0.003f
        var gx = b.left + w * 0.06f
        while (gx < b.left + w * 0.3f) { cv.drawLine(gx, b.top + b.height() * 0.2f, gx, b.bottom - b.height() * 0.2f, stroke); gx += w * 0.01f }
        drawFloppySlot(cv, w, b.height() * 0.16f, true)
        drawPlate(cv, w, 0xFF55524F.toInt(), 0xFFB9B6AE.toInt())
    }

    private fun drawMonitor(cv: Canvas) {
        val m = monitor
        val w = m.width()
        paint.color = 0x66000000
        cv.drawRoundRect(m.left + w * 0.01f, m.top + w * 0.012f, m.right + w * 0.01f, m.bottom + w * 0.012f, w * 0.03f, w * 0.03f, paint)
        paint.color = 0xFF000000.toInt()
        val (mt, mb) = when (style) {
            "archimedes", "a3000" -> 0xFFE4DDC7.toInt() to 0xFFCAC1A6.toInt()
            "a5000", "a4000", "a3010", "a3020" -> 0xFFE8E5DC.toInt() to 0xFFCFCBC0.toInt()
            else -> 0xFFD9D7D0.toInt() to 0xFFBEBBB3.toInt()
        }
        paint.shader = LinearGradient(0f, m.top, 0f, m.bottom, mt, mb, Shader.TileMode.CLAMP)
        cv.drawRoundRect(m, w * 0.03f, w * 0.03f, paint)
        paint.shader = null
        paint.color = 0xFF2F2F2E.toInt()
        cv.drawRoundRect(screen.left - w * 0.02f, screen.top - w * 0.02f, screen.right + w * 0.02f, screen.bottom + w * 0.02f, w * 0.03f, w * 0.03f, paint)
        frame.setPixels(framePx, 0, W, 0, 0, W, H)
        cv.drawBitmap(frame, null, screen, pixelPaint)
        paint.shader = RadialGradient(screen.centerX(), screen.centerY(), screen.width() * 0.8f, 0x00000000, 0x22000000, Shader.TileMode.CLAMP)
        cv.drawRect(screen, paint)
        paint.shader = null
        // power light and the controls strip under the tube
        val ly = (screen.bottom + m.bottom) / 2 + w * 0.012f
        paint.color = 0xFF1A1A1A.toInt()
        cv.drawCircle(m.right - w * 0.07f, ly, w * 0.012f, paint)
        paint.color = 0xFF3FD24A.toInt()
        cv.drawCircle(m.right - w * 0.07f, ly, w * 0.007f, paint)
        stroke.color = 0xFFA9A69E.toInt(); stroke.strokeWidth = w * 0.003f
        cv.drawLine(m.left + w * 0.06f, ly, m.left + w * 0.45f, ly, stroke)
    }

    private fun drawUnit(cv: Canvas) {
        val b = unit
        val w = b.width()
        paint.color = 0x66000000
        cv.drawRoundRect(b.left + w * 0.006f, b.top + w * 0.008f, b.right + w * 0.006f, b.bottom + w * 0.008f, w * 0.015f, w * 0.015f, paint)
        paint.color = 0xFF000000.toInt()
        paint.shader = LinearGradient(0f, b.top, 0f, b.bottom, 0xFFDCDAD3.toInt(), 0xFFC2BFB7.toInt(), Shader.TileMode.CLAMP)
        cv.drawRoundRect(b, w * 0.015f, w * 0.015f, paint)
        paint.shader = null
        // ribbed lower front
        stroke.color = 0xFFB3B0A8.toInt(); stroke.strokeWidth = w * 0.002f
        var gy = b.bottom - b.height() * 0.18f
        while (gy < b.bottom - b.height() * 0.04f) { cv.drawLine(b.left + w * 0.02f, gy, b.right - w * 0.02f, gy, stroke); gy += b.height() * 0.05f }
        // floppy drive (tap: disk menu)
        paint.color = 0xFFCFCCC4.toInt()
        cv.drawRoundRect(floppy.left - w * 0.01f, floppy.top - b.height() * 0.12f, floppy.right + w * 0.01f, floppy.bottom + b.height() * 0.12f, w * 0.008f, w * 0.008f, paint)
        paint.color = 0xFF1E1D1B.toInt()
        cv.drawRoundRect(floppy, floppy.height() / 2, floppy.height() / 2, paint)
        paint.color = 0xFFB3B0A8.toInt()
        cv.drawRoundRect(floppy.right - w * 0.04f, floppy.bottom + b.height() * 0.04f, floppy.right - w * 0.005f, floppy.bottom + b.height() * 0.12f, w * 0.004f, w * 0.004f, paint)
        paint.color = if (indicators and 1 != 0) 0xFFFFB02E.toInt() else 0xFF5A4A2A.toInt()
        cv.drawCircle(floppy.left + w * 0.012f, floppy.bottom + b.height() * 0.08f, w * 0.006f, paint)
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL)
        text.color = 0xFF5E5B55.toInt(); text.textSize = b.height() * 0.13f
        cv.drawText(floppyLabel ?: "tap: disks & files", floppy.centerX(), floppy.top - b.height() * 0.16f, text)
        // CD drive
        paint.color = 0xFFCFCCC4.toInt()
        cv.drawRoundRect(cdrom, w * 0.008f, w * 0.008f, paint)
        stroke.color = 0xFFA9A69E.toInt(); stroke.strokeWidth = w * 0.003f
        cv.drawRoundRect(cdrom, w * 0.008f, w * 0.008f, stroke)
        cv.drawLine(cdrom.left + w * 0.02f, cdrom.centerY(), cdrom.right - w * 0.02f, cdrom.centerY(), stroke)
        paint.color = 0xFF8E8B84.toInt()
        cv.drawRoundRect(cdrom.right - w * 0.05f, cdrom.bottom - b.height() * 0.2f, cdrom.right - w * 0.015f, cdrom.bottom - b.height() * 0.08f, w * 0.003f, w * 0.003f, paint)
        // power light and the menu plate
        paint.color = 0xFF1A1A1A.toInt()
        cv.drawCircle(b.right - w * 0.13f, b.top + b.height() * 0.32f, w * 0.009f, paint)
        paint.color = 0xFF3FD24A.toInt()
        cv.drawCircle(b.right - w * 0.13f, b.top + b.height() * 0.32f, w * 0.005f, paint)
        paint.color = 0xFFB9B6AE.toInt()
        cv.drawRoundRect(menuPlate, w * 0.006f, w * 0.006f, paint)
        text.color = 0xFF34322E.toInt(); text.textSize = menuPlate.height() * 0.5f
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
        cv.drawText("MENU", menuPlate.centerX(), menuPlate.centerY() + text.textSize * 0.36f, text)
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL)
        if (plus) {
            text.color = 0xFF6A675F.toInt(); text.textSize = b.height() * 0.12f
            cv.drawText("+", b.right - w * 0.04f, b.top + b.height() * 0.36f, text)
        }
    }

    private fun drawKeyboard(cv: Canvas) {
        val b = board
        val w = b.width()
        paint.color = 0x66000000
        cv.drawRoundRect(b.left + w * 0.006f, b.top + w * 0.01f, b.right + w * 0.006f, b.bottom + w * 0.01f, w * 0.02f, w * 0.02f, paint)
        paint.color = 0xFF000000.toInt()
        paint.shader = LinearGradient(0f, b.top, 0f, b.bottom, caseTop, caseBottom, Shader.TileMode.CLAMP)
        cv.drawRoundRect(b, w * 0.02f, w * 0.02f, paint)
        paint.shader = null
        for (k in keys) drawKey(cv, k)
    }

    private fun drawKey(cv: Canvas, k: Key) {
        val r = k.rect
        val down = k.down || latched.contains(k)
        val top = if (k.fn) keyFn else if (k.dark) keyDark else keyLight
        val side = if (k.fn) keyFnSide else if (k.dark) keyDarkSide else keyLightSide
        val rad = u * 0.1f
        val sink = if (down) r.height() * 0.06f else 0f
        paint.color = side
        cv.drawRoundRect(r.left, r.top + sink, r.right, r.bottom, rad, rad, paint)
        paint.color = if (down) side else top
        cv.drawRoundRect(r.left + u * 0.05f, r.top + sink, r.right - u * 0.05f, r.bottom - r.height() * 0.13f + sink * 0.5f, rad, rad, paint)
        if (k.label.isEmpty()) return
        text.color = legend
        val cy = (r.top + r.bottom - r.height() * 0.13f) / 2 + sink
        if (k.shiftLabel.isNotEmpty()) {
            text.textSize = u * 0.24f
            cv.drawText(k.shiftLabel, r.centerX(), cy - u * 0.06f, text)
            text.textSize = u * 0.28f
            cv.drawText(k.label, r.centerX(), cy + u * 0.26f, text)
            return
        }
        text.textSize = if (k.label.length == 1) u * 0.38f else minOf(u * 0.22f, r.width() * 0.85f / k.label.length * 1.7f)
        cv.drawText(k.label, r.centerX(), cy + text.textSize * 0.36f, text)
    }

    private fun drawMouse(cv: Canvas) {
        val m = mouse
        val w = m.width()
        // the lead back to the keyboard
        stroke.color = 0xFF9E9B94.toInt(); stroke.strokeWidth = w * 0.03f
        cv.drawLine(m.centerX(), m.top, m.centerX() - w * 0.4f, board.bottom, stroke)
        paint.color = 0x66000000
        cv.drawRoundRect(m.left + w * 0.03f, m.top + w * 0.04f, m.right + w * 0.03f, m.bottom + w * 0.04f, w * 0.35f, w * 0.35f, paint)
        paint.color = 0xFF000000.toInt()
        paint.shader = LinearGradient(0f, m.top, 0f, m.bottom, 0xFFDDDBD4.toInt(), 0xFFB8B5AD.toInt(), Shader.TileMode.CLAMP)
        cv.drawRoundRect(m, w * 0.35f, w * 0.35f, paint)
        paint.shader = null
        val names = arrayOf("SELECT", "MENU", "ADJUST")
        for (i in 0..2) {
            val r = mouseButtons[i]
            val on = buttons and (1 shl i) != 0
            paint.color = if (on) 0xFF7C7A74.toInt() else 0xFF9F9D97.toInt()
            cv.drawRoundRect(r, w * 0.06f, w * 0.06f, paint)
            text.color = 0xFF2F2E2B.toInt(); text.textSize = minOf(r.width() * 0.22f, u * 0.2f)
            cv.drawText(names[i], r.centerX(), r.bottom + text.textSize * 1.3f, text)
        }
        text.color = 0xFF6F6C65.toInt(); text.textSize = u * 0.22f
        cv.drawText("drag on the screen", m.centerX() - w * 1.2f, m.centerY(), text)
        cv.drawText("to move, tap to click", m.centerX() - w * 1.2f, m.centerY() + text.textSize * 1.3f, text)
    }

    // ---- keys ----
    private fun isLatch(k: Key) = k.code == SHIFT_L || k.code == SHIFT_R || k.code == CTRL || k.code == ALT

    private fun keyAt(x: Float, y: Float): Key? {
        if (!board.contains(x, y)) return null
        for (k in keys) if (k.rect.contains(x, y)) return k
        var best: Key? = null; var bd = u * u * 0.15f
        for (k in keys) {
            val ex = maxOf(0f, abs(x - k.rect.centerX()) - k.rect.width() / 2)
            val ey = maxOf(0f, abs(y - k.rect.centerY()) - k.rect.height() / 2)
            val d = ex * ex + ey * ey
            if (d < bd) { bd = d; best = k }
        }
        return best
    }

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
        if (latched.isNotEmpty()) {
            for (m in latched) onKey?.invoke(m.code, false)
            latched.clear()
        }
    }

    private fun setButtons(b: Int) {
        if (b != buttons) { buttons = b; onMouseButtons?.invoke(b); invalidate() }
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                val id = e.getPointerId(i)
                val x = e.getX(i); val y = e.getY(i)
                if (e.actionMasked == MotionEvent.ACTION_DOWN) {
                    if (RectF(menuPlate).apply { inset(-u * 0.3f, -u * 0.3f) }.contains(x, y)) { onMenu?.invoke(); return true }
                    if (RectF(floppy).apply { inset(-u * 0.6f, -u * 0.6f) }.contains(x, y)) { onDisks?.invoke(); return true }
                }
                for (bi in 0..2) if (RectF(mouseButtons[bi]).apply { inset(-u * 0.15f, -u * 0.15f) }.contains(x, y)) {
                    performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                    buttonIds[id] = bi
                    setButtons(buttons or (1 shl bi))
                    return true
                }
                if (padId < 0 && RectF(screen).apply { inset(-u * 0.3f, -u * 0.3f) }.contains(x, y)) {
                    padId = id; padX = x; padY = y; padMoved = false; padDownAt = e.eventTime
                    return true
                }
                val k = keyAt(x, y) ?: return true
                performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                pointers[id] = k
                press(k)
            }
            MotionEvent.ACTION_MOVE -> {
                if (padId >= 0) {
                    val i = e.findPointerIndex(padId)
                    if (i >= 0) {
                        val dx = e.getX(i) - padX; val dy = e.getY(i) - padY
                        if (!padMoved && hypot(dx, dy) > u * 0.25f) padMoved = true
                        if (padMoved) {
                            // screen pixels of the emulated display
                            val s = W / screen.width()
                            onMouseMove?.invoke(dx * s, dy * s)
                            padX = e.getX(i); padY = e.getY(i)
                        }
                    }
                }
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val id = e.getPointerId(e.actionIndex)
                buttonIds.remove(id)?.let { setButtons(buttons and (1 shl it).inv()); return true }
                if (id == padId) {
                    padId = -1
                    if (!padMoved && e.eventTime - padDownAt < 400) {
                        // a tap on the screen: click SELECT
                        setButtons(buttons or 1)
                        postDelayed({ setButtons(buttons and 1.inv()) }, 90)
                    }
                    return true
                }
                pointers.remove(id)?.let { release(it) }
            }
            MotionEvent.ACTION_CANCEL -> {
                for (k in pointers.values) release(k)
                pointers.clear(); buttonIds.clear(); padId = -1
                setButtons(0)
            }
        }
        invalidate()
        return true
    }
}
