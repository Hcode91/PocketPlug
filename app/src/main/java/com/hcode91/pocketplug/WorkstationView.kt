package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View

/**
 * Early-80s office workstation (NGEN type): a warm-grey CRT monitor with a
 * green phosphor screen, the disk module's 5.25" floppy slot underneath it,
 * and the CTOS keyboard with its GO / CANCEL / FINISH / MARK / BOUND keys.
 * The design is original and carries no logos.
 *
 * Keys report the keyboard's own key numbers (see ngen.cpp). SHIFT and CODE
 * latch for the next key when tapped on their own. Tap the floppy slot for
 * the disk menu and the monitor's power switch for the emulator menu.
 */
class WorkstationView(ctx: Context) : View(ctx) {

    var onKey: ((Int, Boolean) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var onDisks: (() -> Unit)? = null
    /** bit 0 floppy busy, bit 1 hard disk busy, bits 8-15 keyboard LEDs */
    var indicators = 0
        set(v) { if (v != field) { field = v; postInvalidate() } }
    var floppyLabel: String? = null
        set(v) { field = v; postInvalidate() }

    companion object {
        const val W = 720
        const val H = 348
        private const val LED_LOCK = 1 shl (8 + 6)
        private const val LED_OVERTYPE = 1 shl (8 + 7)
    }

    private enum class Kind { CHAR, FUNC, MOD, GO, WIDE }

    private class Key(val label: String, val code: Int, val kind: Kind, val shiftLabel: String = "") {
        val rect = RectF()
        var down = false
    }

    // rows: label, key number, kind, width in units, shifted legend
    private data class K(val label: String, val code: Int, val units: Float = 1f, val kind: Kind = Kind.CHAR, val shift: String = "")

    private val rows: List<List<K>> = listOf(
        listOf(K("ACTION", 0x43, 1.85f, Kind.FUNC), K("FINISH", 0x04, 1.85f, Kind.FUNC), K("PREV\nPAGE", 0x05, 1.85f, Kind.FUNC),
            K("NEXT\nPAGE", 0x0c, 1.85f, Kind.FUNC), K("SCROLL\nUP", 0x13, 1.85f, Kind.FUNC), K("SCROLL\nDOWN", 0x11, 1.85f, Kind.FUNC),
            K("HELP", 0x00, 1.85f, Kind.FUNC), K("CANCEL", 0x07, 1.85f, Kind.GO)),
        listOf(K("f1", 0x15, 1.48f, Kind.FUNC), K("f2", 0x16, 1.48f, Kind.FUNC), K("f3", 0x17, 1.48f, Kind.FUNC), K("f4", 0x18, 1.48f, Kind.FUNC),
            K("f5", 0x19, 1.48f, Kind.FUNC), K("f6", 0x1a, 1.48f, Kind.FUNC), K("f7", 0x1c, 1.48f, Kind.FUNC), K("f8", 0x1d, 1.48f, Kind.FUNC),
            K("f9", 0x1e, 1.48f, Kind.FUNC), K("f10", 0x1f, 1.48f, Kind.FUNC)),
        listOf(K("DELETE", 0x7f, 1.3f, Kind.FUNC), K("1", 0x31, shift = "!"), K("2", 0x32, shift = "@"), K("3", 0x33, shift = "#"),
            K("4", 0x34, shift = "$"), K("5", 0x35, shift = "%"), K("6", 0x36, shift = "^"), K("7", 0x37, shift = "&"),
            K("8", 0x38, shift = "*"), K("9", 0x39, shift = "("), K("0", 0x30, shift = ")"), K("-", 0x2d, shift = "_"),
            K("=", 0x2b, shift = "+"), K("BACK\nSPACE", 0x08, 1.5f, Kind.FUNC)),
        listOf(K("TAB", 0x09, 1.5f, Kind.FUNC), K("Q", 0x71), K("W", 0x77), K("E", 0x65), K("R", 0x72), K("T", 0x74), K("Y", 0x79),
            K("U", 0x75), K("I", 0x69), K("O", 0x6f), K("P", 0x70), K("[", 0x5b, shift = "<"), K("]", 0x5d, shift = ">"),
            K("RETURN", 0x0a, 1.3f, Kind.FUNC)),
        listOf(K("LOCK", 0x45, 1.75f, Kind.MOD), K("A", 0x61), K("S", 0x73), K("D", 0x64), K("F", 0x66), K("G", 0x67), K("H", 0x68),
            K("J", 0x6a), K("K", 0x6b), K("L", 0x6c), K(";", 0x3b, shift = ":"), K("'", 0x27, shift = "\""),
            K("NEXT", 0x0d, 2.05f, Kind.FUNC)),
        listOf(K("SHIFT", 0x48, 2.25f, Kind.MOD), K("Z", 0x7a), K("X", 0x78), K("C", 0x63), K("V", 0x76), K("B", 0x62), K("N", 0x6e),
            K("M", 0x6d), K(",", 0x2c, shift = ","), K(".", 0x2e, shift = "."), K("/", 0x2f, shift = "?"),
            K("SHIFT", 0x49, 2.55f, Kind.MOD)),
        listOf(K("CODE", 0x4c, 1.6f, Kind.MOD), K("OVER\nTYPE", 0x44, 1.3f, Kind.FUNC), K("", 0x20, 7.0f, Kind.WIDE),
            K("CODE", 0x4d, 1.6f, Kind.MOD), K("GO", 0x1b, 2.3f, Kind.GO)),
        listOf(K("MARK", 0x02, 1.64f, Kind.FUNC), K("BOUND", 0x03, 1.64f, Kind.FUNC), K("MOVE", 0x12, 1.64f, Kind.FUNC), K("COPY", 0x14, 1.64f, Kind.FUNC),
            K("DEL\nCHAR", 0x22, 1.64f, Kind.FUNC), K("◀", 0x0e, 1.64f, Kind.FUNC), K("▲", 0x01, 1.64f, Kind.FUNC), K("▼", 0x0b, 1.64f, Kind.FUNC),
            K("▶", 0x0f, 1.64f, Kind.FUNC)),
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
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }
    private val smooth = Paint(Paint.FILTER_BITMAP_FLAG)

    private val monitor = RectF()
    private val screen = RectF()
    private val glass = RectF()
    private val power = RectF()
    private val module = RectF()
    private val slot = RectF()
    private val keyboard = RectF()
    private var unit = 1f

    // pointer id -> key
    private val pointers = HashMap<Int, Key>()
    // SHIFT / CODE tapped on their own stay down until the next key is released
    private val latched = HashSet<Key>()

    fun updateFrame(px: IntArray) {
        if (px.size < W * H) return
        System.arraycopy(px, 0, framePx, 0, W * H)
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        val mw = w * 0.97f
        val sw = mw * 0.86f
        val sh = sw * H / W
        val ml = (w - mw) / 2
        val mt = h * 0.012f
        monitor.set(ml, mt, ml + mw, mt + sh + mw * 0.15f)
        screen.set(w / 2f - sw / 2, mt + mw * 0.06f, w / 2f + sw / 2, mt + mw * 0.06f + sh)
        glass.set(screen.left - mw * 0.03f, screen.top - mw * 0.03f, screen.right + mw * 0.03f, screen.bottom + mw * 0.03f)
        power.set(monitor.right - mw * 0.11f, monitor.bottom - mw * 0.06f, monitor.right - mw * 0.04f, monitor.bottom - mw * 0.025f)
        val modTop = monitor.bottom + h * 0.008f
        module.set(ml + mw * 0.02f, modTop, ml + mw * 0.98f, modTop + mw * 0.12f)
        slot.set(module.left + module.width() * 0.22f, module.top + module.height() * 0.38f,
            module.right - module.width() * 0.22f, module.top + module.height() * 0.58f)
        keyboard.set(w * 0.01f, module.bottom + h * 0.012f, w * 0.99f, h * 0.995f)

        // keyboard rows: widest row is 15.1 units
        val maxUnits = rows.maxOf { r -> r.sumOf { it.units.toDouble() }.toFloat() }
        unit = keyboard.width() / (maxUnits + 0.4f)
        val gapRows = 0.35f
        val rowH = keyboard.height() / (rows.size + gapRows * 2 + 0.3f)
        var y = keyboard.top + rowH * 0.15f
        for ((ri, r) in rows.withIndex()) {
            if (ri == 2 || ri == 7) y += rowH * gapRows   // gaps between the key groups
            val total = r.sumOf { it.units.toDouble() }.toFloat()
            var x = keyboard.centerX() - total * unit / 2
            for ((ki, k) in r.withIndex()) {
                val key = keyByRow[ri][ki]
                key.rect.set(x + unit * 0.05f, y + rowH * 0.06f, x + k.units * unit - unit * 0.05f, y + rowH * 0.94f)
                x += k.units * unit
            }
            y += rowH
        }
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF1A1714.toInt())
        val mw = monitor.width()

        // monitor housing: warm grey with a darker screen surround
        paint.color = 0x66000000
        c.drawRoundRect(monitor.left + mw * 0.01f, monitor.top + mw * 0.015f, monitor.right + mw * 0.01f, monitor.bottom + mw * 0.015f, mw * 0.04f, mw * 0.04f, paint)
        paint.color = Color.WHITE
        paint.shader = LinearGradient(0f, monitor.top, 0f, monitor.bottom, 0xFFCFC6B6.toInt(), 0xFFAFA493.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(monitor, mw * 0.04f, mw * 0.04f, paint)
        paint.shader = null
        stroke.color = 0xFF8C826F.toInt(); stroke.strokeWidth = mw * 0.004f
        c.drawRoundRect(monitor, mw * 0.04f, mw * 0.04f, stroke)
        paint.color = 0xFF3B3833.toInt()
        c.drawRoundRect(glass, mw * 0.025f, mw * 0.025f, paint)
        // picture with a soft phosphor glow
        paint.color = 0xFF050A06.toInt()
        c.drawRoundRect(screen.left - mw * 0.012f, screen.top - mw * 0.012f, screen.right + mw * 0.012f, screen.bottom + mw * 0.012f, mw * 0.02f, mw * 0.02f, paint)
        frame.setPixels(framePx, 0, W, 0, 0, W, H)
        c.drawBitmap(frame, null, screen, smooth)
        paint.color = Color.WHITE
        paint.shader = RadialGradient(screen.centerX(), screen.centerY(), screen.width() * 0.7f, 0x0018FF60, 0x22000000, Shader.TileMode.CLAMP)
        c.drawRect(screen, paint)
        paint.shader = LinearGradient(screen.left, screen.top, screen.left + screen.width() * 0.5f, screen.bottom, 0x14FFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP)
        c.drawRect(screen, paint)
        paint.shader = null
        // brightness / contrast knobs and the power rocker (menu)
        for (i in 0..1) {
            val kx = monitor.left + mw * (0.06f + i * 0.05f); val ky = power.centerY()
            paint.color = 0xFF5E574C.toInt(); c.drawCircle(kx, ky, mw * 0.015f, paint)
            paint.color = 0xFF8A8172.toInt(); c.drawCircle(kx, ky, mw * 0.009f, paint)
        }
        paint.color = 0xFF4A443B.toInt()
        c.drawRoundRect(power, power.height() * 0.3f, power.height() * 0.3f, paint)
        paint.color = 0xFF3AE070.toInt()
        c.drawCircle(power.left - mw * 0.025f, power.centerY(), mw * 0.007f, paint)

        // disk module with the floppy slot
        paint.color = Color.WHITE
        paint.shader = LinearGradient(0f, module.top, 0f, module.bottom, 0xFFC4BBAA.toInt(), 0xFF9E9483.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(module, mw * 0.015f, mw * 0.015f, paint)
        paint.shader = null
        stroke.color = 0xFF7D7462.toInt(); stroke.strokeWidth = mw * 0.003f
        c.drawRoundRect(module, mw * 0.015f, mw * 0.015f, stroke)
        paint.color = 0xFF2A2621.toInt()
        val bezel = RectF(slot.left - mw * 0.03f, module.top + module.height() * 0.18f, slot.right + mw * 0.03f, module.bottom - module.height() * 0.18f)
        c.drawRoundRect(bezel, mw * 0.01f, mw * 0.01f, paint)
        paint.color = 0xFF0C0B0A.toInt()
        c.drawRoundRect(slot, slot.height() / 2, slot.height() / 2, paint)
        // inserted disk: the label edge shows in the slot
        if (floppyLabel != null) {
            paint.color = 0xFF2B2B30.toInt()
            c.drawRect(slot.left + slot.width() * 0.06f, slot.top + slot.height() * 0.25f, slot.right - slot.width() * 0.06f, slot.bottom - slot.height() * 0.25f, paint)
        }
        // door latch
        paint.color = 0xFF5B544A.toInt()
        c.drawRoundRect(slot.centerX() - mw * 0.02f, bezel.top + mw * 0.004f, slot.centerX() + mw * 0.02f, slot.top - mw * 0.004f, mw * 0.004f, mw * 0.004f, paint)
        // activity lights: floppy (left of the slot) and hard disk (right of the module)
        drawLed(c, bezel.left + mw * 0.015f, bezel.centerY(), indicators and 1 != 0, 0xFFFF4A3A.toInt())
        drawLed(c, module.right - mw * 0.06f, module.centerY(), indicators and 2 != 0, 0xFFFFB13A.toInt())
        text.textSize = mw * 0.022f
        text.color = 0xFF5B544A.toInt()
        c.drawText("DISK", module.right - mw * 0.06f, module.centerY() + mw * 0.035f, text)
        val label = floppyLabel ?: "no disk  (tap to insert)"
        text.textSize = mw * 0.021f
        c.drawText(label, slot.centerX(), bezel.bottom + mw * 0.022f, text)

        // keyboard base
        paint.color = 0x55000000
        c.drawRoundRect(keyboard.left, keyboard.top + unit * 0.12f, keyboard.right, keyboard.bottom + unit * 0.05f, unit * 0.4f, unit * 0.4f, paint)
        paint.color = Color.WHITE
        paint.shader = LinearGradient(0f, keyboard.top, 0f, keyboard.bottom, 0xFFB9AF9D.toInt(), 0xFF968C7B.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(keyboard, unit * 0.4f, unit * 0.4f, paint)
        paint.shader = null
        for (k in keys) drawKey(c, k)
    }

    private fun drawLed(c: Canvas, x: Float, y: Float, on: Boolean, color: Int) {
        val r = monitor.width() * 0.008f
        paint.color = 0xFF2A2621.toInt()
        c.drawCircle(x, y, r * 1.5f, paint)
        paint.color = if (on) color else 0xFF4A2A26.toInt()
        c.drawCircle(x, y, r, paint)
    }

    private fun drawKey(c: Canvas, k: Key) {
        val r = k.rect
        val down = k.down || latched.contains(k)
        val (top, side, ink) = when (k.kind) {
            Kind.CHAR, Kind.WIDE -> Triple(0xFFEDE6D6.toInt(), 0xFFC2B9A6.toInt(), 0xFF2E2A24.toInt())
            Kind.FUNC -> Triple(0xFF8F8676.toInt(), 0xFF6D6557.toInt(), 0xFFF4EFE4.toInt())
            Kind.MOD -> Triple(0xFF7A7264.toInt(), 0xFF5A5347.toInt(), 0xFFF4EFE4.toInt())
            Kind.GO -> Triple(0xFFD9A441.toInt(), 0xFFA97A28.toInt(), 0xFF2E2A24.toInt())
        }
        val rad = unit * 0.12f
        val sink = if (down) r.height() * 0.06f else 0f
        paint.color = side
        c.drawRoundRect(r.left, r.top + sink, r.right, r.bottom, rad, rad, paint)
        paint.color = if (down) side else top
        c.drawRoundRect(r.left + unit * 0.06f, r.top + sink, r.right - unit * 0.06f, r.bottom - r.height() * 0.14f + sink * 0.5f, rad, rad, paint)
        // lighted keys
        val led = when (k.code) { 0x45 -> LED_LOCK; 0x44 -> LED_OVERTYPE; else -> 0 }
        if (led != 0) {
            paint.color = if (indicators and led != 0) 0xFF5DFF7A.toInt() else 0xFF3E4A3C.toInt()
            c.drawCircle(r.left + unit * 0.2f, r.top + unit * 0.2f + sink, unit * 0.07f, paint)
        }
        text.color = ink
        val lines = k.label.split('\n')
        val big = k.kind == Kind.CHAR && k.label.length == 1
        text.textSize = if (big) unit * 0.42f else unit * (if (lines.size > 1) 0.2f else 0.22f)
        if (k.label.length > 6 && lines.size == 1) text.textSize = unit * 0.19f
        val cy = (r.top + r.bottom - r.height() * 0.14f) / 2 + sink
        if (k.shiftLabel.isNotEmpty() && k.shiftLabel != k.label) {
            text.textSize = unit * 0.3f
            c.drawText(k.shiftLabel, r.centerX(), cy - unit * 0.04f, text)
            c.drawText(k.label, r.centerX(), cy + unit * 0.3f, text)
            return
        }
        val lh = text.textSize * 1.1f
        var y = cy - lh * (lines.size - 1) / 2 + text.textSize * 0.36f
        for (l in lines) { c.drawText(l, r.centerX(), y, text); y += lh }
    }

    // ---- touch ----
    private fun keyAt(x: Float, y: Float): Key? {
        for (k in keys) if (k.rect.contains(x, y)) return k
        // forgiving: the nearest key within half a key
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

    // a modifier held by a finger while another key was typed is not latched
    private val usedWhileHeld = HashSet<Key>()

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
            // tapped alone: keep it held for the next key
            if (!latched.remove(k)) { latched.add(k); return }
            onKey?.invoke(k.code, false)
            return
        }
        onKey?.invoke(k.code, false)
        // a normal key ends any latched SHIFT / CODE
        if (latched.isNotEmpty()) {
            for (m in latched) onKey?.invoke(m.code, false)
            latched.clear()
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                val x = e.getX(i); val y = e.getY(i)
                if (e.actionMasked == MotionEvent.ACTION_DOWN) {
                    if (RectF(power).apply { inset(-power.width() * 0.6f, -power.height() * 1.5f) }.contains(x, y)) { onMenu?.invoke(); return true }
                    if (module.contains(x, y)) { onDisks?.invoke(); return true }
                }
                val k = keyAt(x, y) ?: return true
                performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                pointers[e.getPointerId(i)] = k
                press(k)
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                pointers.remove(e.getPointerId(e.actionIndex))?.let { release(it) }
            }
            MotionEvent.ACTION_CANCEL -> {
                for (k in pointers.values) release(k)
                pointers.clear()
            }
        }
        invalidate()
        return true
    }
}
