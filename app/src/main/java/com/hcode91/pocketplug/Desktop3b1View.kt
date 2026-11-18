package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.os.SystemClock
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View

/**
 * Mid-80s desktop in the style of the PC 7300 / 3B1: a beige monitor with a
 * green phosphor tube sitting on the low system unit (floppy drive on the
 * right of its front, a "20 MB" capacity badge), a three-button mouse in
 * front of it and the long keyboard with its rows of office function keys. Nothing
 * here reproduces a maker's logo or name.
 *
 * The keyboard is folded for a phone held upright: the right-hand function
 * cluster and the left-hand editing cluster become the two rows above the
 * soft keys, and the cursor keys a row below the space bar. Keys report the
 * keyboard's own scan codes (see freebee/keyboard.c). SHIFT and CTRL latch
 * for the next key when tapped on their own.
 *
 * The screen is the mouse's desk: drag to move the pointer, tap to click the
 * left button. The mouse itself has all three buttons to hold down. Tap the
 * floppy drive for the disk menu and the power switch for the emulator menu.
 */
class Desktop3b1View(ctx: Context) : View(ctx), DesktopSkin {

    override var onKey: ((Int, Boolean) -> Unit)? = null
    override var onMenu: (() -> Unit)? = null
    override var onDisks: (() -> Unit)? = null
    /** relative pointer motion in screen pixels */
    var onMouseMove: ((Float, Float) -> Unit)? = null
    /** bit 0 right, bit 1 middle, bit 2 left */
    var onMouseButtons: ((Int) -> Unit)? = null
    override var indicators = 0
        set(v) { if (v != field) { field = v; postInvalidate() } }
    override var floppyLabel: String? = null
        set(v) { field = v; postInvalidate() }

    companion object {
        const val W = 720
        const val H = 348
        const val BUTTON_RIGHT = 1
        const val BUTTON_MIDDLE = 2
        const val BUTTON_LEFT = 4
    }

    private enum class Kind { CHAR, FUNC, MOD, WIDE }

    private class Key(val label: String, val code: Int, val kind: Kind, val shiftLabel: String) {
        val rect = RectF()
        var down = false
    }

    private data class K(val label: String, val code: Int, val units: Float = 1f, val kind: Kind = Kind.CHAR, val shift: String = "")

    private val f = Kind.FUNC
    private val rows: List<List<K>> = listOf(
        // the right-hand cluster's office keys
        listOf(K("Exit", 0x04, 1.25f, f), K("Msg", 0x06, 1.25f, f), K("Help", 0x2a, 1.25f, f), K("Cancl", 0x07, 1.25f, f),
            K("Suspd", 0x1e, 1.25f, f), K("Rsume", 0x1f, 1.25f, f), K("Opts", 0x13, 1.25f, f), K("Cmd", 0x41, 1.25f, f),
            K("Close\nOpen", 0x42, 1.25f, f), K("Print", 0x43, 1.25f, f), K("Clear\nRfrsh", 0x44, 1.25f, f), K("Rstrt\nRef", 0x03, 1.25f, f)),
        // the left-hand editing cluster
        listOf(K("Clear\nLine", 0x02, 1.154f, f), K("Creat", 0x10, 1.154f, f), K("Save", 0x11, 1.154f, f), K("Undo", 0x21, 1.154f, f),
            K("Redo", 0x22, 1.154f, f), K("Find", 0x23, 1.154f, f), K("Rplac", 0x24, 1.154f, f), K("Move", 0x12, 1.154f, f),
            K("Copy", 0x14, 1.154f, f), K("Dlete", 0x7f, 1.154f, f), K("Dlete\nChar", 0x26, 1.154f, f), K("Slect\nMark", 0x28, 1.154f, f),
            K("Input\nMode", 0x29, 1.154f, f)),
        // soft keys under the screen labels
        listOf(K("F1", 0x15, 1.6f, f), K("F2", 0x16, 1.6f, f), K("F3", 0x17, 1.6f, f), K("F4", 0x18, 1.6f, f),
            K("F5", 0x19, 1.6f, f), K("F6", 0x1a, 1.6f, f), K("F7", 0x1c, 1.6f, f), K("F8", 0x1d, 1.6f, f),
            K("Reset\nBreak", 0x25, 2.2f, f)),
        listOf(K("Esc", 0x1b, 1f, f), K("1", 0x31, shift = "!"), K("2", 0x32, shift = "@"), K("3", 0x33, shift = "#"),
            K("4", 0x34, shift = "$"), K("5", 0x35, shift = "%"), K("6", 0x36, shift = "^"), K("7", 0x37, shift = "&"),
            K("8", 0x38, shift = "*"), K("9", 0x39, shift = "("), K("0", 0x30, shift = ")"), K("-", 0x2d, shift = "_"),
            K("=", 0x3d, shift = "+"), K("`", 0x60, shift = "~"), K("◀\nBksp", 0x08, 1f, f)),
        listOf(K("Tab", 0x09, 1.5f, f), K("Q", 0x71), K("W", 0x77), K("E", 0x65), K("R", 0x72), K("T", 0x74), K("Y", 0x79),
            K("U", 0x75), K("I", 0x69), K("O", 0x6f), K("P", 0x70), K("[", 0x5b, shift = "{"), K("]", 0x5d, shift = "}"),
            K("\\", 0x5c, 1.5f, shift = "|")),
        listOf(K("Caps\nLock", 0x45, 1.75f, Kind.MOD), K("A", 0x61), K("S", 0x73), K("D", 0x64), K("F", 0x66), K("G", 0x67), K("H", 0x68),
            K("J", 0x6a), K("K", 0x6b), K("L", 0x6c), K(";", 0x3b, shift = ":"), K("'", 0x27, shift = "\""),
            K("Return", 0x0d, 2.25f, f)),
        listOf(K("Shift", 0x48, 2.25f, Kind.MOD), K("Z", 0x7a), K("X", 0x78), K("C", 0x63), K("V", 0x76), K("B", 0x62), K("N", 0x6e),
            K("M", 0x6d), K(",", 0x2c, shift = "<"), K(".", 0x2e, shift = ">"), K("/", 0x2f, shift = "?"),
            K("Shift", 0x49, 2.75f, Kind.MOD)),
        listOf(K("Ctrl", 0x4c, 2f, Kind.MOD), K("", 0x20, 9f, Kind.WIDE), K("Ctrl", 0x4d, 2f, Kind.MOD), K("Enter", 0x0a, 2f, f)),
        // the cursor block
        listOf(K("Beg", 0x47, 1.364f, f), K("Home", 0x4a, 1.364f, f), K("End", 0x4b, 1.364f, f), K("Page", 0x46, 1.364f, f),
            K("Prev", 0x05, 1.364f, f), K("Next", 0x0c, 1.364f, f), K("◀", 0x0e, 1.364f, f), K("▲\nRoll", 0x01, 1.364f, f),
            K("▼\nRoll", 0x0b, 1.364f, f), K("▶", 0x0f, 1.364f, f), K("Num\nLock", 0x7e, 1.364f, f)),
    )
    private val keys = ArrayList<Key>()
    private val keyByRow = ArrayList<List<Key>>()

    init {
        for (r in rows) {
            val list = r.map { Key(it.label, it.code, it.kind, it.shift) }
            keyByRow += list
            keys += list
        }
    }

    private val frame = Bitmap.createBitmap(W, H, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(W * H)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL) }
    private val smooth = Paint(Paint.FILTER_BITMAP_FLAG)

    private val monitor = RectF()
    private val bezel = RectF()
    private val screen = RectF()
    private val neck = RectF()
    private val base = RectF()
    private val drive = RectF()
    private val slot = RectF()
    private val power = RectF()
    private val mouse = RectF()
    private val mouseButtons = Array(3) { RectF() }   // left, middle, right
    private val keyboard = RectF()
    private var unit = 1f

    private val pointers = HashMap<Int, Key>()
    private val latched = HashSet<Key>()
    private val usedWhileHeld = HashSet<Key>()

    // mouse: pointer id -> button bit; screen drag tracking
    private val buttonPointers = HashMap<Int, Int>()
    private var buttons = 0
    private var dragId = -1
    private var dragX = 0f
    private var dragY = 0f
    private var dragStartX = 0f
    private var dragStartY = 0f
    private var dragStartT = 0L
    private var dragMoved = false

    override fun updateFrame(px: IntArray) {
        if (px.size < W * H) return
        System.arraycopy(px, 0, framePx, 0, W * H)
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        val mw = w * 0.9f
        val sw = mw * 0.84f
        val sh = sw * 0.72f          // the tube's 4:3-ish face
        val ml = (w - mw) / 2
        val mt = h * 0.008f
        monitor.set(ml, mt, ml + mw, mt + sh + mw * 0.17f)
        screen.set(w / 2f - sw / 2, mt + mw * 0.07f, w / 2f + sw / 2, mt + mw * 0.07f + sh)
        bezel.set(screen.left - mw * 0.035f, screen.top - mw * 0.035f, screen.right + mw * 0.035f, screen.bottom + mw * 0.035f)
        neck.set(w / 2f - mw * 0.18f, monitor.bottom, w / 2f + mw * 0.18f, monitor.bottom + mw * 0.025f)
        // system unit under the monitor; the mouse on the desk in front of it, at the right
        val bw = w * 0.94f
        base.set((w - bw) / 2, neck.bottom, (w + bw) / 2, neck.bottom + bw * 0.13f)
        drive.set(base.left + bw * 0.55f, base.top + base.height() * 0.2f, base.right - bw * 0.04f, base.bottom - base.height() * 0.18f)
        slot.set(drive.left + drive.width() * 0.08f, drive.centerY() - drive.height() * 0.09f,
            drive.right - drive.width() * 0.22f, drive.centerY() + drive.height() * 0.09f)
        power.set(base.left + bw * 0.04f, base.bottom - base.height() * 0.42f, base.left + bw * 0.1f, base.bottom - base.height() * 0.2f)
        mouse.set(w * 0.8f, base.bottom + h * 0.01f, w * 0.97f, base.bottom + h * 0.01f + w * 0.2f)
        val bwid = mouse.width() * 0.82f / 3
        for (i in 0..2) {
            val l = mouse.left + mouse.width() * 0.09f + i * bwid
            mouseButtons[i].set(l + bwid * 0.06f, mouse.top + mouse.height() * 0.1f, l + bwid * 0.94f, mouse.top + mouse.height() * 0.5f)
        }
        keyboard.set(w * 0.01f, mouse.bottom + h * 0.01f, w * 0.99f, h * 0.995f)

        val maxUnits = rows.maxOf { r -> r.sumOf { it.units.toDouble() }.toFloat() }
        unit = keyboard.width() / (maxUnits + 0.4f)
        val gapRows = 0.3f
        val rowH = keyboard.height() / (rows.size + gapRows * 3 + 0.3f)
        var y = keyboard.top + rowH * 0.15f
        for ((ri, r) in rows.withIndex()) {
            if (ri == 2 || ri == 3 || ri == 8) y += rowH * gapRows   // gaps between the key groups
            val total = r.sumOf { it.units.toDouble() }.toFloat()
            var x = keyboard.centerX() - total * unit / 2
            for ((ki, k) in r.withIndex()) {
                keyByRow[ri][ki].rect.set(x + unit * 0.05f, y + rowH * 0.06f, x + k.units * unit - unit * 0.05f, y + rowH * 0.94f)
                x += k.units * unit
            }
            y += rowH
        }
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF2B2722.toInt())
        val mw = monitor.width()
        val beigeTop = 0xFFDCD2BC.toInt()
        val beigeBottom = 0xFFC2B79E.toInt()
        val edge = 0xFF9C9079.toInt()

        // system unit (drawn first: the monitor's foot stands on it)
        paint.color = 0x66000000
        c.drawRoundRect(base.left + mw * 0.01f, base.top + mw * 0.012f, base.right + mw * 0.01f, base.bottom + mw * 0.012f, mw * 0.015f, mw * 0.015f, paint)
        paint.color = Color.WHITE
        paint.shader = LinearGradient(0f, base.top, 0f, base.bottom, beigeTop, beigeBottom, Shader.TileMode.CLAMP)
        c.drawRoundRect(base, mw * 0.015f, mw * 0.015f, paint)
        paint.shader = null
        stroke.color = edge; stroke.strokeWidth = mw * 0.003f
        c.drawRoundRect(base, mw * 0.015f, mw * 0.015f, stroke)
        // top lip and the front's ventilation grooves
        stroke.color = 0x55FFFFFF; stroke.strokeWidth = mw * 0.003f
        c.drawLine(base.left + mw * 0.02f, base.top + base.height() * 0.1f, base.right - mw * 0.02f, base.top + base.height() * 0.1f, stroke)
        stroke.color = 0xFFA89C84.toInt(); stroke.strokeWidth = mw * 0.004f
        for (i in 0..4) {
            val gy = base.top + base.height() * (0.3f + i * 0.09f)
            c.drawLine(base.left + base.width() * 0.16f, gy, base.left + base.width() * 0.44f, gy, stroke)
        }
        // capacity badge
        val badge = RectF(base.left + base.width() * 0.2f, base.bottom - base.height() * 0.3f, base.left + base.width() * 0.36f, base.bottom - base.height() * 0.1f)
        paint.color = 0xFF3A352E.toInt()
        c.drawRoundRect(badge, badge.height() * 0.2f, badge.height() * 0.2f, paint)
        text.color = 0xFFE8DFC9.toInt(); text.textSize = badge.height() * 0.62f
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
        c.drawText("20 MB", badge.centerX(), badge.centerY() + text.textSize * 0.36f, text)
        // power switch with its light (menu)
        paint.color = 0xFF6E6453.toInt()
        c.drawRoundRect(power, power.height() * 0.2f, power.height() * 0.2f, paint)
        paint.color = 0xFF8C826E.toInt()
        c.drawRoundRect(power.left + power.width() * 0.12f, power.top + power.height() * 0.15f, power.centerX(), power.bottom - power.height() * 0.15f, power.height() * 0.15f, power.height() * 0.15f, paint)
        drawLed(c, power.centerX(), power.top - base.height() * 0.16f, true, 0xFF4BE36B.toInt())
        // floppy drive: dark bezel, slot, latch and busy light
        paint.color = 0xFF4A443A.toInt()
        c.drawRoundRect(drive, mw * 0.008f, mw * 0.008f, paint)
        paint.color = 0xFF0E0D0B.toInt()
        c.drawRoundRect(slot, slot.height() / 2, slot.height() / 2, paint)
        if (floppyLabel != null) {
            paint.color = 0xFF26262C.toInt()
            c.drawRect(slot.left + slot.width() * 0.05f, slot.top + slot.height() * 0.25f, slot.right - slot.width() * 0.05f, slot.bottom - slot.height() * 0.25f, paint)
        }
        val lever = RectF(drive.right - drive.width() * 0.17f, drive.top + drive.height() * 0.2f, drive.right - drive.width() * 0.07f, drive.bottom - drive.height() * 0.2f)
        paint.color = 0xFF2A2621.toInt()
        c.drawRoundRect(lever, lever.width() * 0.2f, lever.width() * 0.2f, paint)
        drawLed(c, drive.left + drive.width() * 0.08f, drive.top + drive.height() * 0.2f, indicators and 1 != 0, 0xFFFF4A3A.toInt())
        drawLed(c, drive.left - base.width() * 0.04f, drive.centerY(), indicators and 2 != 0, 0xFFFFB13A.toInt())
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL)
        text.color = 0xFFDCD2BC.toInt(); text.textSize = drive.height() * 0.2f
        c.drawText(floppyLabel ?: "no disk - tap to insert", slot.centerX(), drive.bottom - drive.height() * 0.08f, text)

        // monitor: foot, housing, recessed bezel and the tube
        paint.color = 0xFFB3A78E.toInt()
        c.drawRect(neck, paint)
        paint.color = 0x66000000
        c.drawRoundRect(monitor.left + mw * 0.01f, monitor.top + mw * 0.015f, monitor.right + mw * 0.01f, monitor.bottom + mw * 0.015f, mw * 0.035f, mw * 0.035f, paint)
        paint.color = Color.WHITE
        paint.shader = LinearGradient(0f, monitor.top, 0f, monitor.bottom, beigeTop, beigeBottom, Shader.TileMode.CLAMP)
        c.drawRoundRect(monitor, mw * 0.035f, mw * 0.035f, paint)
        paint.shader = null
        stroke.color = edge; stroke.strokeWidth = mw * 0.004f
        c.drawRoundRect(monitor, mw * 0.035f, mw * 0.035f, stroke)
        paint.shader = LinearGradient(0f, bezel.top, 0f, bezel.bottom, 0xFFA59880.toInt(), 0xFFCFC4AC.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(bezel, mw * 0.03f, mw * 0.03f, paint)
        paint.shader = null
        paint.color = 0xFF070B08.toInt()
        c.drawRoundRect(screen.left - mw * 0.014f, screen.top - mw * 0.014f, screen.right + mw * 0.014f, screen.bottom + mw * 0.014f, mw * 0.03f, mw * 0.03f, paint)
        frame.setPixels(framePx, 0, W, 0, 0, W, H)
        c.drawBitmap(frame, null, screen, smooth)
        paint.color = Color.WHITE
        paint.shader = RadialGradient(screen.centerX(), screen.centerY(), screen.width() * 0.7f, 0x0018FF60, 0x2A000000, Shader.TileMode.CLAMP)
        c.drawRect(screen, paint)
        paint.shader = LinearGradient(screen.left, screen.top, screen.left + screen.width() * 0.5f, screen.bottom, 0x14FFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP)
        c.drawRect(screen, paint)
        paint.shader = null
        // brightness / contrast thumbwheels under the bezel
        for (i in 0..1) {
            val kx = monitor.right - mw * (0.08f + i * 0.05f); val ky = (bezel.bottom + monitor.bottom) / 2
            paint.color = 0xFF8E836E.toInt(); c.drawRoundRect(kx - mw * 0.016f, ky - mw * 0.008f, kx + mw * 0.016f, ky + mw * 0.008f, mw * 0.004f, mw * 0.004f, paint)
        }

        // mouse with its cord running back to the unit
        stroke.color = 0xFF3A352E.toInt(); stroke.strokeWidth = mw * 0.006f
        c.drawLine(mouse.centerX(), mouse.top, mouse.centerX(), base.bottom, stroke)
        text.color = 0xFF9A8F7C.toInt(); text.textSize = mouse.height() * 0.15f
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL)
        val hx = (keyboard.left + mouse.left) / 2
        c.drawText("Drag on the screen to move the pointer,", hx, mouse.centerY() - text.textSize * 0.3f, text)
        c.drawText("tap it to click. Hold the mouse buttons for more.", hx, mouse.centerY() + text.textSize * 1.0f, text)
        paint.color = 0x66000000
        c.drawRoundRect(mouse.left + mw * 0.008f, mouse.top + mw * 0.01f, mouse.right + mw * 0.008f, mouse.bottom + mw * 0.01f, mouse.width() * 0.3f, mouse.width() * 0.3f, paint)
        paint.color = Color.WHITE
        paint.shader = LinearGradient(0f, mouse.top, 0f, mouse.bottom, beigeTop, beigeBottom, Shader.TileMode.CLAMP)
        c.drawRoundRect(mouse, mouse.width() * 0.3f, mouse.width() * 0.3f, paint)
        paint.shader = null
        stroke.color = edge; stroke.strokeWidth = mw * 0.003f
        c.drawRoundRect(mouse, mouse.width() * 0.3f, mouse.width() * 0.3f, stroke)
        val bits = intArrayOf(BUTTON_LEFT, BUTTON_MIDDLE, BUTTON_RIGHT)
        for (i in 0..2) {
            paint.color = if (buttons and bits[i] != 0) 0xFF2A2621.toInt() else 0xFF4E473C.toInt()
            c.drawRoundRect(mouseButtons[i], mouseButtons[i].width() * 0.25f, mouseButtons[i].width() * 0.25f, paint)
        }

        // keyboard
        paint.color = 0x55000000
        c.drawRoundRect(keyboard.left, keyboard.top + unit * 0.12f, keyboard.right, keyboard.bottom + unit * 0.05f, unit * 0.35f, unit * 0.35f, paint)
        paint.color = Color.WHITE
        paint.shader = LinearGradient(0f, keyboard.top, 0f, keyboard.bottom, 0xFFD3C8B1.toInt(), 0xFFB8AC93.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(keyboard, unit * 0.35f, unit * 0.35f, paint)
        paint.shader = null
        for (k in keys) drawKey(c, k)
    }

    private fun drawLed(c: Canvas, x: Float, y: Float, on: Boolean, color: Int) {
        val r = monitor.width() * 0.0075f
        paint.color = 0xFF2A2621.toInt()
        c.drawCircle(x, y, r * 1.5f, paint)
        paint.color = if (on) color else 0xFF4A2A26.toInt()
        c.drawCircle(x, y, r, paint)
    }

    private fun drawKey(c: Canvas, k: Key) {
        val r = k.rect
        val down = k.down || latched.contains(k)
        val (top, side, ink) = when (k.kind) {
            Kind.CHAR, Kind.WIDE -> Triple(0xFFF0E9D9.toInt(), 0xFFC9BFAA.toInt(), 0xFF2E2A24.toInt())
            Kind.FUNC -> Triple(0xFF8A7D69.toInt(), 0xFF685D4D.toInt(), 0xFFF6F0E2.toInt())
            Kind.MOD -> Triple(0xFFB2A58D.toInt(), 0xFF8C806B.toInt(), 0xFF2E2A24.toInt())
        }
        val rad = unit * 0.1f
        val sink = if (down) r.height() * 0.06f else 0f
        paint.color = side
        c.drawRoundRect(r.left, r.top + sink, r.right, r.bottom, rad, rad, paint)
        paint.color = if (down) side else top
        c.drawRoundRect(r.left + unit * 0.06f, r.top + sink, r.right - unit * 0.06f, r.bottom - r.height() * 0.14f + sink * 0.5f, rad, rad, paint)
        text.color = ink
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL)
        val lines = k.label.split('\n')
        val big = k.kind == Kind.CHAR && k.label.length == 1
        text.textSize = if (big) unit * 0.42f else unit * (if (lines.size > 1) 0.21f else 0.24f)
        val cy = (r.top + r.bottom - r.height() * 0.14f) / 2 + sink
        if (k.shiftLabel.isNotEmpty()) {
            text.textSize = unit * 0.3f
            c.drawText(k.shiftLabel, r.centerX(), cy - unit * 0.04f, text)
            c.drawText(k.label, r.centerX(), cy + unit * 0.3f, text)
            return
        }
        val lh = text.textSize * 1.12f
        var y = cy - lh * (lines.size - 1) / 2 + text.textSize * 0.36f
        for (l in lines) { c.drawText(l, r.centerX(), y, text); y += lh }
    }

    // ---- keyboard touch ----
    private fun keyAt(x: Float, y: Float): Key? {
        if (!keyboard.contains(x, y)) return null
        for (k in keys) if (k.rect.contains(x, y)) return k
        var best: Key? = null; var bd = unit * unit * 0.25f
        for (k in keys) {
            val dx = x - k.rect.centerX(); val dy = y - k.rect.centerY()
            val ex = maxOf(0f, kotlin.math.abs(dx) - k.rect.width() / 2); val ey = maxOf(0f, kotlin.math.abs(dy) - k.rect.height() / 2)
            val d = ex * ex + ey * ey
            if (d < bd) { bd = d; best = k }
        }
        return best
    }

    private fun isLatch(k: Key) = k.code == 0x48 || k.code == 0x49 || k.code == 0x4c || k.code == 0x4d

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

    // ---- mouse ----
    private fun setButtons(b: Int) {
        if (b == buttons) return
        buttons = b
        onMouseButtons?.invoke(b)
        invalidate()
    }

    private fun buttonAt(x: Float, y: Float): Int {
        if (!RectF(mouse).apply { inset(-mouse.width() * 0.1f, -mouse.height() * 0.1f) }.contains(x, y)) return 0
        val i = ((x - mouse.left) / mouse.width() * 3).toInt().coerceIn(0, 2)
        return intArrayOf(BUTTON_LEFT, BUTTON_MIDDLE, BUTTON_RIGHT)[i]
    }

    private fun heldButtons(): Int { var b = 0; for (v in buttonPointers.values) b = b or v; return b }

    private val clickUp = Runnable { setButtons(heldButtons()) }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                val id = e.getPointerId(i)
                val x = e.getX(i); val y = e.getY(i)
                if (e.actionMasked == MotionEvent.ACTION_DOWN &&
                    RectF(power).apply { inset(-power.width() * 0.5f, -power.height() * 0.8f) }.contains(x, y)) { onMenu?.invoke(); return true }
                if (e.actionMasked == MotionEvent.ACTION_DOWN && drive.contains(x, y)) { onDisks?.invoke(); return true }
                val b = buttonAt(x, y)
                if (b != 0) {
                    performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                    buttonPointers[id] = b
                    setButtons(heldButtons())
                    return true
                }
                if (dragId < 0 && RectF(bezel).contains(x, y)) {
                    dragId = id; dragX = x; dragY = y; dragStartX = x; dragStartY = y
                    dragStartT = SystemClock.uptimeMillis(); dragMoved = false
                    return true
                }
                val k = keyAt(x, y) ?: return true
                performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                pointers[id] = k
                press(k)
            }
            MotionEvent.ACTION_MOVE -> {
                if (dragId >= 0) {
                    val i = e.findPointerIndex(dragId)
                    if (i >= 0) {
                        val x = e.getX(i); val y = e.getY(i)
                        val scale = W / screen.width() * 1.4f
                        val slop = screen.width() * 0.015f
                        if (!dragMoved && (kotlin.math.abs(x - dragStartX) > slop || kotlin.math.abs(y - dragStartY) > slop)) dragMoved = true
                        if (dragMoved) onMouseMove?.invoke((x - dragX) * scale, (y - dragY) * scale)
                        dragX = x; dragY = y
                    }
                }
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val id = e.getPointerId(e.actionIndex)
                if (id == dragId) {
                    dragId = -1
                    // a quick tap on the screen clicks the left button
                    if (!dragMoved && SystemClock.uptimeMillis() - dragStartT < 300) {
                        removeCallbacks(clickUp)
                        setButtons(heldButtons() or BUTTON_LEFT)
                        postDelayed(clickUp, 90)
                    }
                    return true
                }
                if (buttonPointers.remove(id) != null) { setButtons(heldButtons()); return true }
                pointers.remove(id)?.let { release(it) }
            }
            MotionEvent.ACTION_CANCEL -> {
                for (k in pointers.values) release(k)
                pointers.clear()
                buttonPointers.clear()
                dragId = -1
                setButtons(0)
            }
        }
        invalidate()
        return true
    }
}
