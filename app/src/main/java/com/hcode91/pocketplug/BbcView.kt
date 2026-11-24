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
 * Early-80s British school computers: a monitor on top, the cream wedge with
 * dark keys, a row of red function keys f0-f9 and BREAK, the cursor keys and
 * COPY on the right (after photos). The Master and Compact styles add the
 * numeric keypad. The monitor stands on a twin disc drive (tap: discs and
 * tapes); a pad for games is in front. No maker's name or logo is drawn.
 *
 * Keys report the keyboard matrix code (row << 4 | column, see bbc.h); BREAK
 * is 0xff. SHIFT and CTRL latch for the next key when tapped on their own.
 * The pad sends [setInputs] bits: 0 left, 1 right, 2 up, 3 down, 4 RETURN,
 * 5 SPACE.
 */
class BbcView(ctx: Context, private val model: Int) : View(ctx) {

    var onKey: ((Int, Boolean) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var onDisks: (() -> Unit)? = null
    var onPad: ((Int) -> Unit)? = null
    /** drive light */
    var indicators = 0
        set(v) { if (v != field) { field = v; postInvalidate() } }
    var mediaLabel: String? = null
        set(v) { field = v; postInvalidate() }
    /** what the pad's directions press, printed on it */
    var padLabels = arrayOf("Z", "X", ":", "/")
        set(v) { field = v; postInvalidate() }
    /** pad bits from a real gamepad, shown on the pad */
    var externalMask = 0
        set(v) { if (v != field) { field = v; postInvalidate() } }

    companion object {
        const val W = 704
        const val H = 552
        const val MODEL_MICRO = 0
        const val MODEL_MASTER = 1
        const val MODEL_COMPACT = 2
        const val SHIFT = 0x00
        const val CTRL = 0x01
        const val BREAK = 0xff
        const val LEFT = 1
        const val RIGHT = 2
        const val UP = 4
        const val DOWN = 8
        const val RETURN = 16
        const val SPACE = 32
    }

    private val compact = model == MODEL_COMPACT
    /** the Masters and the Compact have the numeric keypad */
    private val master = model == MODEL_MASTER || compact

    private enum class Kind { CHAR, WIDE, FUNC, CURSOR, PAD }

    private class Key(val label: String, val code: Int, val kind: Kind, val shiftLabel: String) {
        val rect = RectF()
        var down = false
    }

    private data class K(val label: String, val code: Int, val units: Float = 1f, val kind: Kind = Kind.CHAR, val shift: String = "")

    private val w = Kind.WIDE
    private val f = Kind.FUNC
    private val cu = Kind.CURSOR

    private val rows: List<List<K>> = listOf(
        listOf(K("", -1, 5.2f), K("f0", 0x20, 1f, f), K("f1", 0x71, 1f, f), K("f2", 0x72, 1f, f), K("f3", 0x73, 1f, f),
            K("f4", 0x14, 1f, f), K("f5", 0x74, 1f, f), K("f6", 0x75, 1f, f), K("f7", 0x16, 1f, f), K("f8", 0x76, 1f, f),
            K("f9", 0x77, 1f, f), K("BREAK", BREAK, 1.4f, w)),
        listOf(K("ESCAPE", 0x70, 1.1f, w), K("1", 0x30, shift = "!"), K("2", 0x31, shift = "\""), K("3", 0x11, shift = "#"),
            K("4", 0x12, shift = "$"), K("5", 0x13, shift = "%"), K("6", 0x34, shift = "&"), K("7", 0x24, shift = "'"),
            K("8", 0x15, shift = "("), K("9", 0x26, shift = ")"), K("0", 0x27), K("-", 0x17, shift = "="),
            K("^", 0x18, shift = "~"), K("\\", 0x78, shift = "|"), K("←", 0x19, 1f, cu), K("→", 0x79, 1f, cu)),
        listOf(K("TAB", 0x60, 1.6f, w), K("Q", 0x10), K("W", 0x21), K("E", 0x22), K("R", 0x33), K("T", 0x23), K("Y", 0x44),
            K("U", 0x35), K("I", 0x25), K("O", 0x36), K("P", 0x37), K("@", 0x47), K("[", 0x38, shift = "{"),
            K("_", 0x28, shift = "£"), K("↑", 0x39, 1f, cu), K("↓", 0x29, 1f, cu)),
        listOf(K("CAPS\nLOCK", 0x40, 1f, w), K("CTRL", CTRL, 1f, w), K("A", 0x41), K("S", 0x51), K("D", 0x32), K("F", 0x43),
            K("G", 0x53), K("H", 0x54), K("J", 0x45), K("K", 0x46), K("L", 0x56), K(";", 0x57, shift = "+"),
            K(":", 0x48, shift = "*"), K("]", 0x58, shift = "}"), K("RETURN", 0x49, 2.1f, w)),
        listOf(K("SHIFT\nLOCK", 0x50, 1f, w), K("SHIFT", SHIFT, 1.6f, w), K("Z", 0x61), K("X", 0x42), K("C", 0x52), K("V", 0x63),
            K("B", 0x64), K("N", 0x55), K("M", 0x65), K(",", 0x66, shift = "<"), K(".", 0x67, shift = ">"),
            K("/", 0x68, shift = "?"), K("SHIFT", SHIFT, 1.5f, w), K("DELETE", 0x59, 1f, w), K("COPY", 0x69, 1f, cu)),
        listOf(K("", 0x62, 8f, w)),
    )
    // the Master's numeric keypad, 4 x 5, right of the main block
    private val keypad: List<List<K>> = if (!master) emptyList() else listOf(
        listOf(K("+", 0x3a, 1f, Kind.PAD), K("-", 0x3b, 1f, Kind.PAD), K("/", 0x4a, 1f, Kind.PAD), K("*", 0x5b, 1f, Kind.PAD)),
        listOf(K("7", 0x1b, 1f, Kind.PAD), K("8", 0x2a, 1f, Kind.PAD), K("9", 0x2b, 1f, Kind.PAD), K("#", 0x5a, 1f, Kind.PAD)),
        listOf(K("4", 0x7a, 1f, Kind.PAD), K("5", 0x7b, 1f, Kind.PAD), K("6", 0x1a, 1f, Kind.PAD), K("DEL", 0x4b, 1f, Kind.PAD)),
        listOf(K("1", 0x6b, 1f, Kind.PAD), K("2", 0x7c, 1f, Kind.PAD), K("3", 0x6c, 1f, Kind.PAD), K(",", 0x5c, 1f, Kind.PAD)),
        listOf(K("0", 0x6a, 1f, Kind.PAD), K(".", 0x4c, 1f, Kind.PAD), K("RETURN", 0x3c, 2f, Kind.PAD)),
    )

    private val keys = ArrayList<Key>()
    private val keyByRow = ArrayList<List<Key>>()
    private val padRows = ArrayList<List<Key>>()

    init {
        for (r in rows) {
            val list = r.map { Key(it.label, it.code, it.kind, it.shift) }
            keyByRow += list
            keys += list.filter { it.code >= 0 }
        }
        for (r in keypad) {
            val list = r.map { Key(it.label, it.code, it.kind, it.shift) }
            padRows += list
            keys += list
        }
    }

    private val frame = Bitmap.createBitmap(W, H, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(W * H)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER }
    private val pixelPaint = Paint(Paint.FILTER_BITMAP_FLAG)

    private val monitor = RectF()
    private val screen = RectF()
    private val case = RectF()
    private val keyWell = RectF()
    private val padWell = RectF()
    private val menuPlate = RectF()
    private val ledPos = PointF()
    private val dpad = RectF()
    private val btnA = RectF()
    private val btnB = RectF()
    private val drive = RectF()
    private var unit = 1f

    // colours (from photos): cream case, black key plate, black keys with white legends,
    // red f0-f9; the Model B's cursor keys and COPY are a brown-grey, the Masters' black
    private val bodyTop = 0xFFEAE4D0.toInt()
    private val bodyBottom = 0xFFD6CEB4.toInt()
    private val wellColor = if (compact) 0xFF2A2420.toInt() else 0xFF1A1816.toInt()
    private val keyTop = 0xFF2C2A28.toInt()
    private val keySide = 0xFF121110.toInt()
    private val keyInk = 0xFFF4F2EC.toInt()
    private val wideTop = keyTop
    private val wideSide = keySide
    private val wideInk = keyInk
    private val curTop = if (master) keyTop else 0xFF5E5644.toInt()
    private val curSide = if (master) keySide else 0xFF3A3428.toInt()
    private val driveUnit = RectF()
    private val plate = RectF()
    private val funcTop = 0xFFC8392C.toInt()
    private val funcSide = 0xFF8C2219.toInt()

    private val pointers = HashMap<Int, Key>()
    private val latched = HashSet<Key>()
    private val usedWhileHeld = HashSet<Key>()
    private var padId = -1
    private var padDir = 0
    private val btnIds = HashMap<Int, Int>()

    fun updateFrame(px: IntArray) {
        if (px.size < W * H) return
        System.arraycopy(px, 0, framePx, 0, W * H)
        postInvalidate()
    }

    override fun onSizeChanged(vw: Int, vh: Int, ow: Int, oh: Int) {
        val wf = vw.toFloat()
        // monitor: a 4:3 tube in a cream (grey for the Compact) housing
        val mw = wf * 0.96f
        val sw = mw * 0.88f
        val sh = sw * 0.75f
        monitor.set((wf - mw) / 2, vh * 0.01f, (wf + mw) / 2, vh * 0.01f + sh + mw * 0.1f)
        screen.set(wf / 2 - sw / 2, monitor.top + mw * 0.05f, wf / 2 + sw / 2, monitor.top + mw * 0.05f + sh)

        // keyboard
        // the twin disc drive the monitor stands on
        driveUnit.set(monitor.left + mw * 0.04f, monitor.bottom, monitor.right - mw * 0.04f, monitor.bottom + mw * 0.1f)
        drive.set(driveUnit)
        val cw = wf * 0.99f
        val mainUnits = rows.maxOf { r -> r.sumOf { it.units.toDouble() }.toFloat() }
        val padUnits = if (master) 4.3f else 0f
        unit = cw * 0.96f / (mainUnits + padUnits)
        val kh = unit * 1.2f
        val topH = unit * 0.7f
        val caseH = topH + kh * rows.size + unit * 0.55f
        case.set((wf - cw) / 2, driveUnit.bottom + vh * 0.01f, (wf + cw) / 2, driveUnit.bottom + vh * 0.01f + caseH)
        val kbW = unit * (mainUnits + padUnits)
        val left = case.centerX() - kbW / 2
        var y = case.top + topH
        for ((ri, r) in rows.withIndex()) {
            val total = r.sumOf { it.units.toDouble() }.toFloat()
            var x = if (r.size == 1) left + (mainUnits - total) * unit / 2 else left
            for ((ki, k) in r.withIndex()) {
                keyByRow[ri][ki].rect.set(x + unit * 0.04f, y + kh * 0.05f, x + k.units * unit - unit * 0.04f, y + kh * 0.95f)
                x += k.units * unit
            }
            y += kh
        }
        keyWell.set(left - unit * 0.15f, case.top + topH - unit * 0.1f, left + mainUnits * unit + unit * 0.15f, y + unit * 0.1f)
        if (master) {
            val px0 = left + (mainUnits + 0.3f) * unit
            val ph = kh * rows.size / keypad.size
            var py = case.top + topH
            for ((ri, r) in keypad.withIndex()) {
                var x = px0
                for ((ki, k) in r.withIndex()) {
                    padRows[ri][ki].rect.set(x + unit * 0.04f, py + ph * 0.06f, x + k.units * unit - unit * 0.04f, py + ph * 0.94f)
                    x += k.units * unit
                }
                py += ph
            }
            padWell.set(px0 - unit * 0.15f, keyWell.top, px0 + 4f * unit + unit * 0.15f, keyWell.bottom)
        }
        // the menu plate at the top left, above ESCAPE; the drive light at the top right
        menuPlate.set(left, case.top + topH + kh * 0.12f, left + unit * 1.45f, case.top + topH + kh * 0.88f)
        keyByRow[0][0].rect.setEmpty()
        ledPos.set(case.right - unit * 0.8f, case.top + topH * 0.5f)
        // the black plate the keys sit in
        plate.set(case.left + unit * 0.25f, case.top + topH * 0.55f, case.right - unit * 0.25f, case.bottom - unit * 0.2f)

        // the game pad in front of the keyboard: cross on the left, RETURN / SPACE on the right
        val below = vh - case.bottom
        val zoneTop = case.bottom + below * 0.06f
        val zoneH = below * 0.88f
        val ds = minOf(zoneH, wf * 0.42f)
        dpad.set(wf * 0.05f, zoneTop + (zoneH - ds) / 2, wf * 0.05f + ds, zoneTop + (zoneH + ds) / 2)
        val bs = minOf(ds * 0.48f, wf * 0.2f)
        btnA.set(wf * 0.95f - bs * 2.15f, dpad.centerY() - bs * 0.2f, wf * 0.95f - bs * 1.15f, dpad.centerY() + bs * 0.8f)
        btnB.set(wf * 0.95f - bs, dpad.centerY() - bs * 0.85f, wf * 0.95f, dpad.centerY() + bs * 0.15f)
    }

    override fun onDraw(cv: Canvas) {
        cv.drawColor(0xFF1C1A18.toInt())
        drawMonitor(cv)
        drawDrive(cv)
        drawKeyboard(cv)
        drawPad(cv)
    }

    private fun drawMonitor(cv: Canvas) {
        val mw = monitor.width()
        paint.color = 0x66000000
        cv.drawRoundRect(monitor.left + mw * 0.01f, monitor.top + mw * 0.012f, monitor.right + mw * 0.01f, monitor.bottom + mw * 0.012f, mw * 0.035f, mw * 0.035f, paint)
        paint.shader = LinearGradient(0f, monitor.top, 0f, monitor.bottom, bodyTop, bodyBottom, Shader.TileMode.CLAMP)
        cv.drawRoundRect(monitor, mw * 0.035f, mw * 0.035f, paint)
        paint.shader = null
        // the dark brown bezel around the tube
        paint.color = 0xFF3A2E28.toInt()
        cv.drawRoundRect(screen.left - mw * 0.035f, screen.top - mw * 0.03f, screen.right + mw * 0.035f, screen.bottom + mw * 0.04f, mw * 0.045f, mw * 0.045f, paint)
        paint.color = 0xFF221A16.toInt()
        cv.drawRoundRect(screen.left - mw * 0.012f, screen.top - mw * 0.012f, screen.right + mw * 0.012f, screen.bottom + mw * 0.012f, mw * 0.03f, mw * 0.03f, paint)
        paint.color = 0xFF050505.toInt()
        cv.drawRect(screen, paint)
        frame.setPixels(framePx, 0, W, 0, 0, W, H)
        cv.drawBitmap(frame, null, screen, pixelPaint)
        paint.shader = RadialGradient(screen.centerX(), screen.centerY(), screen.width() * 0.75f, 0x00000000, 0x38000000, Shader.TileMode.CLAMP)
        cv.drawRect(screen, paint)
        paint.shader = null
        // red power light on the bezel, bottom right
        val by = screen.bottom + mw * 0.026f
        paint.color = 0xFFE8302A.toInt()
        cv.drawCircle(screen.right - mw * 0.02f, by, mw * 0.007f, paint)
    }

    private fun drawKeyboard(cv: Canvas) {
        val r = unit * 0.35f
        paint.color = 0x77000000
        cv.drawRoundRect(case.left + unit * 0.1f, case.top + unit * 0.15f, case.right + unit * 0.1f, case.bottom + unit * 0.15f, r, r, paint)
        paint.shader = LinearGradient(0f, case.top, 0f, case.bottom, bodyTop, bodyBottom, Shader.TileMode.CLAMP)
        cv.drawRoundRect(case, r, r, paint)
        paint.shader = null
        if (master) {
            // ribs / vents across the back edge
            stroke.color = 0xFFC4BBA0.toInt(); stroke.strokeWidth = unit * 0.06f
            var gx = case.left + unit * 0.5f
            while (gx < case.right - unit * 0.5f) {
                cv.drawLine(gx, case.top + unit * 0.06f, gx, plate.top - unit * 0.06f, stroke)
                gx += unit * 0.2f
            }
        }
        paint.color = wellColor
        cv.drawRoundRect(plate, unit * 0.1f, unit * 0.1f, paint)
        if (!master) {
            // the glass strip along the top of the plate (no badge drawn)
            val f0 = keyByRow[0][1].rect
            paint.color = 0xFF3A3A3C.toInt()
            cv.drawRect(f0.left, plate.top + unit * 0.08f, plate.right - unit * 0.3f, f0.top - unit * 0.05f, paint)
        }
        // caps lock / shift lock lights at the bottom left of the plate
        for (i in 0..2) {
            paint.color = 0xFF4A1A16.toInt()
            cv.drawCircle(plate.left + unit * (0.35f + i * 0.4f), plate.bottom - unit * 0.18f, unit * 0.07f, paint)
        }
        for (k in keys) drawKey(cv, k)

        // MENU plate, where the blank before f0 is
        paint.color = 0xFF3E3A36.toInt()
        cv.drawRoundRect(menuPlate, unit * 0.08f, unit * 0.08f, paint)
        text.color = 0xFFE6E0D0.toInt(); text.textSize = minOf(menuPlate.height() * 0.38f, menuPlate.width() * 0.26f)
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
        cv.drawText("MENU", menuPlate.centerX(), menuPlate.centerY() + text.textSize * 0.36f, text)
    }

    private fun drawKey(cv: Canvas, k: Key) {
        val r = k.rect
        val down = k.down || latched.contains(k)
        val (top, side, ink) = when (k.kind) {
            Kind.FUNC -> Triple(funcTop, funcSide, 0xFFFFF2E8.toInt())
            Kind.CURSOR -> Triple(curTop, curSide, keyInk)
            Kind.WIDE -> Triple(wideTop, wideSide, wideInk)
            else -> Triple(keyTop, keySide, keyInk)
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
            text.textSize = unit * 0.26f
            cv.drawText(k.shiftLabel, r.centerX(), cy - unit * 0.06f, text)
            text.textSize = unit * 0.3f
            cv.drawText(k.label, r.centerX(), cy + unit * 0.27f, text)
            return
        }
        val lines = k.label.split('\n')
        if (lines.size > 1) {
            text.textSize = minOf(unit * 0.17f, r.width() * 0.9f / lines.maxOf { it.length } * 1.7f)
            cv.drawText(lines[0], r.centerX(), cy - text.textSize * 0.25f, text)
            cv.drawText(lines[1], r.centerX(), cy + text.textSize * 0.95f, text)
            return
        }
        text.textSize = when {
            k.label.length == 1 -> unit * 0.4f
            k.kind == Kind.FUNC -> unit * 0.28f
            else -> minOf(unit * 0.2f, r.width() * 0.9f / maxOf(1, k.label.length) * 1.6f)
        }
        cv.drawText(k.label, r.centerX(), cy + text.textSize * 0.36f, text)
    }

    private fun drawPad(cv: Canvas) {
        val d = dpad
        val s = d.width()
        val dir = padDir or (externalMask and 0x0f)
        // a dark rounded base with a cross
        paint.color = 0x77000000
        cv.drawRoundRect(d.left + s * 0.02f, d.top + s * 0.03f, d.right + s * 0.02f, d.bottom + s * 0.03f, s * 0.18f, s * 0.18f, paint)
        paint.color = 0xFF2E2A26.toInt()
        cv.drawRoundRect(d, s * 0.18f, s * 0.18f, paint)
        val a = s * 0.3f
        val cx = d.centerX(); val cy = d.centerY()
        paint.color = 0xFF141210.toInt()
        cv.drawRoundRect(cx - a / 2, d.top + s * 0.08f, cx + a / 2, d.bottom - s * 0.08f, s * 0.05f, s * 0.05f, paint)
        cv.drawRoundRect(d.left + s * 0.08f, cy - a / 2, d.right - s * 0.08f, cy + a / 2, s * 0.05f, s * 0.05f, paint)
        // pressed arms light up; each arm shows the key it presses
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
        text.textSize = s * 0.12f
        val arms = listOf(
            Triple(LEFT, RectF(d.left + s * 0.08f, cy - a / 2, cx - a / 2, cy + a / 2), padLabels[0]),
            Triple(RIGHT, RectF(cx + a / 2, cy - a / 2, d.right - s * 0.08f, cy + a / 2), padLabels[1]),
            Triple(UP, RectF(cx - a / 2, d.top + s * 0.08f, cx + a / 2, cy - a / 2), padLabels[2]),
            Triple(DOWN, RectF(cx - a / 2, cy + a / 2, cx + a / 2, d.bottom - s * 0.08f), padLabels[3]),
        )
        for ((bit, rr, label) in arms) {
            if (dir and bit != 0) { paint.color = 0xFF4A443E.toInt(); cv.drawRect(rr, paint) }
            text.color = 0xFFCFC6B4.toInt()
            cv.drawText(label, rr.centerX(), rr.centerY() + text.textSize * 0.36f, text)
        }
        drawButton(cv, btnA, "RETURN", RETURN)
        drawButton(cv, btnB, "SPACE", SPACE)
    }

    private fun drawButton(cv: Canvas, b: RectF, label: String, bit: Int) {
        val on = btnIds.values.contains(bit) || externalMask and bit != 0
        paint.color = 0xFF3A0D0A.toInt()
        cv.drawOval(b.left, b.top + b.height() * 0.06f, b.right, b.bottom + b.height() * 0.06f, paint)
        paint.color = if (on) 0xFFA0281E.toInt() else 0xFFD03A2C.toInt()
        cv.drawOval(b.left, b.top + (if (on) b.height() * 0.05f else 0f), b.right, b.bottom + (if (on) b.height() * 0.05f else 0f), paint)
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
        text.color = 0xFFF5E6DA.toInt(); text.textSize = b.width() * 0.17f
        cv.drawText(label, b.centerX(), b.centerY() + text.textSize * 0.36f, text)
    }

    private fun drawDrive(cv: Canvas) {
        // cream twin 5.25" unit under the monitor: two dark drive fronts with slots, levers and lights
        val d = drive
        val dw = d.width()
        paint.shader = LinearGradient(0f, d.top, 0f, d.bottom, bodyTop, bodyBottom, Shader.TileMode.CLAMP)
        cv.drawRect(d, paint)
        paint.shader = null
        for (i in 0..1) {
            val fr = RectF(d.left + dw * (0.03f + i * 0.49f), d.top + d.height() * 0.15f, d.left + dw * (0.48f + i * 0.49f), d.bottom - d.height() * 0.15f)
            paint.color = 0xFF2A2624.toInt()
            cv.drawRoundRect(fr, dw * 0.01f, dw * 0.01f, paint)
            paint.color = 0xFF0C0B0A.toInt()
            val sy = fr.centerY()
            cv.drawRoundRect(fr.left + fr.width() * 0.2f, sy - fr.height() * 0.1f, fr.right - fr.width() * 0.06f, sy + fr.height() * 0.1f, fr.height() * 0.1f, fr.height() * 0.1f, paint)
            paint.color = 0xFF55504A.toInt()
            cv.drawRect(fr.left + fr.width() * 0.45f, sy + fr.height() * 0.14f, fr.left + fr.width() * 0.6f, sy + fr.height() * 0.3f, paint)
            paint.color = if (i == 0 && indicators != 0) 0xFFFF3B2E.toInt() else 0xFF4A2A22.toInt()
            cv.drawCircle(fr.left + fr.width() * 0.08f, sy, fr.height() * 0.1f, paint)
        }
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL)
        text.color = 0xFFCFC6B4.toInt(); text.textSize = d.height() * 0.16f
        cv.drawText(mediaLabel ?: "tap: discs & tapes", d.centerX(), d.bottom - d.height() * 0.22f, text)
    }

    // ---- keyboard ----
    private fun keyAt(x: Float, y: Float): Key? {
        if (!case.contains(x, y)) return null
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

    private fun isLatch(k: Key) = k.code == SHIFT || k.code == CTRL

    private fun press(k: Key) {
        if (isLatch(k) && latched.contains(k)) return
        if (!isLatch(k)) for (m in keys) if (m.down && isLatch(m)) usedWhileHeld.add(m)
        k.down = true
        onKey?.invoke(k.code, true)
        invalidate()
    }

    private fun release(k: Key) {
        k.down = false
        invalidate()
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

    // ---- pad ----
    private fun dirFor(x: Float, y: Float): Int {
        val dx = x - dpad.centerX(); val dy = y - dpad.centerY()
        if (hypot(dx, dy) < dpad.width() * 0.1f) return 0
        val a = Math.toDegrees(atan2(dy.toDouble(), dx.toDouble()))
        val sector = (((a + 360 + 22.5) % 360) / 45).toInt()
        return intArrayOf(RIGHT, RIGHT or DOWN, DOWN, DOWN or LEFT, LEFT, LEFT or UP, UP, UP or RIGHT)[sector]
    }

    private fun sendPad() {
        var bits = padDir
        for (b in btnIds.values) bits = bits or b
        onPad?.invoke(bits)
        invalidate()
    }

    private fun buttonAt(x: Float, y: Float): Int {
        val grow = btnA.width() * 0.15f
        if (RectF(btnA).apply { inset(-grow, -grow) }.contains(x, y)) return RETURN
        if (RectF(btnB).apply { inset(-grow, -grow) }.contains(x, y)) return SPACE
        return 0
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                val id = e.getPointerId(i)
                val x = e.getX(i); val y = e.getY(i)
                if (e.actionMasked == MotionEvent.ACTION_DOWN) {
                    if (RectF(menuPlate).apply { inset(-unit * 0.1f, -unit * 0.1f) }.contains(x, y)) { onMenu?.invoke(); return true }
                    if (drive.contains(x, y)) { onDisks?.invoke(); return true }
                }
                val b = buttonAt(x, y)
                if (b != 0) {
                    performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                    btnIds[id] = b; sendPad(); return true
                }
                if (padId < 0 && RectF(dpad).apply { inset(-dpad.width() * 0.08f, -dpad.width() * 0.08f) }.contains(x, y)) {
                    padId = id
                    padDir = dirFor(x, y); sendPad(); return true
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
                        val d = dirFor(e.getX(i), e.getY(i))
                        if (d != padDir) { padDir = d; sendPad() }
                    }
                }
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val id = e.getPointerId(e.actionIndex)
                if (btnIds.remove(id) != null) { sendPad(); return true }
                if (id == padId) { padId = -1; padDir = 0; sendPad(); return true }
                pointers.remove(id)?.let { release(it) }
            }
            MotionEvent.ACTION_CANCEL -> {
                for (k in pointers.values) release(k)
                pointers.clear()
                btnIds.clear(); padId = -1; padDir = 0; sendPad()
            }
        }
        return true
    }
}
