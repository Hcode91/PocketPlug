package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View

/**
 * Early-80s single-board CP/M computer at a desk (Big Board family), in three
 * looks from photos (see the styles): a terminal monitor with a white-phosphor
 * screen, the drives under it and an ASCII keyboard. No names or logos.
 *
 * Keys send ASCII codes (down, then up); SHIFT and CTRL latch for the next key
 * when tapped on their own, CAPS LOCK starts on (CP/M likes capitals). Tap the
 * drive cabinet for the disk menu and the monitor's power rocker for the menu.
 */
@SuppressLint("ViewConstructor")
class CpmTerminalView(ctx: Context, private val fw: Int, private val fh: Int, private val style: Int = BOARD) : View(ctx), DesktopSkin {

    companion object {
        /** the bare blue board with two 8" drives beside it (Big Board, MK-82, Moj mikro) */
        const val BOARD = 0
        /** the brushed-metal case with two drives, a key switch and three buttons (Big Board II) */
        const val CASE = 1
        /** cream office monitor, drive unit with a black front and a cream keyboard with dark keys (Xerox 820) */
        const val OFFICE = 2
        /** a tall graphics terminal: the portrait monitor straight over the keyboard, no drives (BitGraph) */
        const val PORTRAIT = 3
        fun styleFor(id: String) = when (id) { "bigbord2" -> CASE; "x820" -> OFFICE; "bitgrpha", "bitgrphb" -> PORTRAIT; else -> BOARD }
    }

    override var onKey: ((Int, Boolean) -> Unit)? = null
    override var onMenu: (() -> Unit)? = null
    override var onDisks: (() -> Unit)? = null
    override var indicators = 0
        set(v) { if (v != field) { field = v; postInvalidate() } }
    override var floppyLabel: String? = null
        set(v) { field = v; postInvalidate() }

    private enum class Kind { CHAR, FUNC, SHIFT, CTRL, LOCK, SPACE }
    private data class K(val label: String, val code: Int, val units: Float = 1f, val kind: Kind = Kind.CHAR, val shifted: Int = 0)
    private class Key(val k: K) { val rect = RectF(); var down = false }

    private fun c(ch: Char, sh: Char) = K(ch.toString(), ch.code, shifted = sh.code)
    private val rows: List<List<K>> = listOf(
        listOf(K("ESC", 0x1b, 1.2f, Kind.FUNC), c('1', '!'), c('2', '@'), c('3', '#'), c('4', '$'), c('5', '%'), c('6', '^'),
            c('7', '&'), c('8', '*'), c('9', '('), c('0', ')'), c('-', '_'), c('=', '+'), K("BS", 0x08, 1.3f, Kind.FUNC)),
        listOf(K("TAB", 0x09, 1.5f, Kind.FUNC), c('q', 'Q'), c('w', 'W'), c('e', 'E'), c('r', 'R'), c('t', 'T'), c('y', 'Y'),
            c('u', 'U'), c('i', 'I'), c('o', 'O'), c('p', 'P'), c('[', '{'), c(']', '}'), K("DEL", 0x7f, 1.0f, Kind.FUNC)),
        listOf(K("CTRL", 0, 1.75f, Kind.CTRL), c('a', 'A'), c('s', 'S'), c('d', 'D'), c('f', 'F'), c('g', 'G'), c('h', 'H'),
            c('j', 'J'), c('k', 'K'), c('l', 'L'), c(';', ':'), c('\'', '"'), K("RETURN", 0x0d, 1.75f, Kind.FUNC)),
        listOf(K("SHIFT", 0, 2.2f, Kind.SHIFT), c('z', 'Z'), c('x', 'X'), c('c', 'C'), c('v', 'V'), c('b', 'B'), c('n', 'N'),
            c('m', 'M'), c(',', '<'), c('.', '>'), c('/', '?'), K("SHIFT", 0, 2.3f, Kind.SHIFT)),
        listOf(K("CAPS\nLOCK", 0, 1.8f, Kind.LOCK), c('\\', '|'), c('`', '~'), K("", 0x20, 7.4f, Kind.SPACE), K("LF", 0x0a, 1.4f, Kind.FUNC),
            K("BREAK", 0x03, 1.6f, Kind.FUNC)),
    )
    private val keys = rows.map { r -> r.map { Key(it) } }
    private val allKeys = keys.flatten()

    private var caps = true
    private var shiftLatched = false
    private var ctrlLatched = false

    private val frame = Bitmap.createBitmap(fw, fh, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(fw * fh)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }
    private val smooth = Paint(Paint.FILTER_BITMAP_FLAG)

    private val monitor = RectF(); private val screen = RectF(); private val glass = RectF()
    private val power = RectF(); private val cabinet = RectF(); private val keyboard = RectF()
    private val slots = arrayOf(RectF(), RectF())
    private val pointers = HashMap<Int, Key>()

    override fun updateFrame(px: IntArray) {
        if (px.size < fw * fh) return
        System.arraycopy(px, 0, framePx, 0, fw * fh)
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        var mw = w * 0.97f
        var sw = mw * 0.88f
        var sh = sw * fh / fw * (if (fh > fw) 1f else 1.25f)   // text cells are taller than wide on the tube
        if (sh > h * 0.6f) { val k = h * 0.6f / sh; sh *= k; sw *= k; mw *= k }
        val ml = (w - mw) / 2; val mt = h * 0.012f
        monitor.set(ml, mt, ml + mw, mt + sh + mw * 0.14f)
        screen.set(w / 2f - sw / 2, mt + mw * 0.05f, w / 2f + sw / 2, mt + mw * 0.05f + sh)
        glass.set(screen.left - mw * 0.03f, screen.top - mw * 0.03f, screen.right + mw * 0.03f, screen.bottom + mw * 0.03f)
        power.set(monitor.right - mw * 0.11f, monitor.bottom - mw * 0.06f, monitor.right - mw * 0.04f, monitor.bottom - mw * 0.025f)
        val ct = monitor.bottom + h * 0.008f
        if (style == PORTRAIT) cabinet.set(ml, ct, ml + mw, ct) else
        cabinet.set(ml + mw * 0.02f, ct, ml + mw * 0.98f, ct + mw * 0.17f)
        for (i in 0..1) {
            val l = cabinet.left + cabinet.width() * (0.05f + i * 0.48f)
            slots[i].set(l, cabinet.top + cabinet.height() * 0.42f, l + cabinet.width() * 0.42f, cabinet.top + cabinet.height() * 0.56f)
        }
        keyboard.set(w * 0.01f, cabinet.bottom + h * 0.012f, w * 0.99f, h * 0.995f)
        val maxUnits = rows.maxOf { r -> r.sumOf { it.units.toDouble() }.toFloat() }
        val unit = keyboard.width() / (maxUnits + 0.4f)
        val rowH = keyboard.height() / (rows.size + 0.4f)
        var y = keyboard.top + rowH * 0.2f
        for ((ri, r) in rows.withIndex()) {
            val total = r.sumOf { it.units.toDouble() }.toFloat()
            var x = keyboard.centerX() - total * unit / 2
            for ((ki, k) in r.withIndex()) {
                keys[ri][ki].rect.set(x + unit * 0.05f, y + rowH * 0.06f, x + k.units * unit - unit * 0.05f, y + rowH * 0.94f)
                x += k.units * unit
            }
            y += rowH
        }
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF17181B.toInt())
        val mw = monitor.width()
        // the monitor: light grey with a dark surround
        paint.color = 0x66000000
        c.drawRoundRect(monitor.left + mw * 0.01f, monitor.top + mw * 0.015f, monitor.right + mw * 0.01f, monitor.bottom + mw * 0.015f, mw * 0.035f, mw * 0.035f, paint)
        val office = style == OFFICE
        paint.shader = LinearGradient(0f, monitor.top, 0f, monitor.bottom, if (office) 0xFFEAE4D4.toInt() else 0xFFD6D4CE.toInt(),
            if (office) 0xFFC8C0AC.toInt() else 0xFFAEABA3.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(monitor, mw * 0.035f, mw * 0.035f, paint); paint.shader = null
        stroke.color = 0xFF85827A.toInt(); stroke.strokeWidth = mw * 0.004f
        c.drawRoundRect(monitor, mw * 0.035f, mw * 0.035f, stroke)
        paint.color = if (office) 0xFFB8B0A0.toInt() else 0xFF2E2F32.toInt(); c.drawRoundRect(glass, mw * 0.025f, mw * 0.025f, paint)
        paint.color = 0xFF060707.toInt()
        c.drawRoundRect(screen.left - mw * 0.012f, screen.top - mw * 0.012f, screen.right + mw * 0.012f, screen.bottom + mw * 0.012f, mw * 0.02f, mw * 0.02f, paint)
        frame.setPixels(framePx, 0, fw, 0, 0, fw, fh)
        c.drawBitmap(frame, null, screen, smooth)
        paint.shader = RadialGradient(screen.centerX(), screen.centerY(), screen.width() * 0.7f, 0x00FFFFFF, 0x26000000, Shader.TileMode.CLAMP)
        c.drawRect(screen, paint)
        paint.shader = LinearGradient(screen.left, screen.top, screen.left + screen.width() * 0.5f, screen.bottom, 0x12FFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP)
        c.drawRect(screen, paint); paint.shader = null
        for (i in 0..1) {
            val kx = monitor.left + mw * (0.06f + i * 0.05f); val ky = power.centerY()
            paint.color = 0xFF5A5852.toInt(); c.drawCircle(kx, ky, mw * 0.015f, paint)
            paint.color = 0xFF8C8981.toInt(); c.drawCircle(kx, ky, mw * 0.009f, paint)
        }
        paint.color = 0xFF46443F.toInt(); c.drawRoundRect(power, power.height() * 0.3f, power.height() * 0.3f, paint)
        paint.color = 0xFFE8A030.toInt(); c.drawCircle(power.left - mw * 0.025f, power.centerY(), mw * 0.007f, paint)

        // the drives (tap for the disk menu), in the style's housing
        if (style != PORTRAIT) when (style) {
            BOARD -> {
                // the blue board with rows of black chips, the two drives on its right
                paint.color = 0xFF3A86B8.toInt(); c.drawRoundRect(cabinet, mw * 0.01f, mw * 0.01f, paint)
                paint.color = 0xFF1C1C1E.toInt()
                val cw = cabinet.width() * 0.024f; val ch = cabinet.height() * 0.16f
                for (row in 0 until 4) for (col in 0 until 11) {
                    val x = cabinet.left + cabinet.width() * 0.03f + col * cw * 1.45f
                    val y = cabinet.top + cabinet.height() * 0.1f + row * ch * 1.35f
                    c.drawRect(x, y, x + cw, y + ch, paint)
                }
                paint.color = 0xFFE8E0A0.toInt()
                c.drawRect(cabinet.left + cabinet.width() * 0.32f, cabinet.top + cabinet.height() * 0.55f, cabinet.left + cabinet.width() * 0.36f, cabinet.top + cabinet.height() * 0.75f, paint)
            }
            CASE -> {
                paint.shader = LinearGradient(cabinet.left, 0f, cabinet.right, 0f, intArrayOf(0xFFC9CBCC.toInt(), 0xFFE4E6E7.toInt(), 0xFFBFC1C2.toInt()), null, Shader.TileMode.CLAMP)
                c.drawRoundRect(cabinet, mw * 0.008f, mw * 0.008f, paint); paint.shader = null
                // the key switch and three square buttons on the right, a blank plate above them
                val rx = cabinet.left + cabinet.width() * 0.88f; val ry = cabinet.top + cabinet.height() * 0.55f
                paint.color = 0xFFF4F4F2.toInt(); c.drawRect(rx - mw * 0.06f, cabinet.top + cabinet.height() * 0.12f, rx + mw * 0.06f, cabinet.top + cabinet.height() * 0.3f, paint)
                paint.color = 0xFF2A2A2A.toInt(); c.drawCircle(rx - mw * 0.035f, ry - cabinet.height() * 0.08f, mw * 0.012f, paint)
                paint.color = 0xFFB0A060.toInt(); c.drawRect(rx - mw * 0.037f, ry - cabinet.height() * 0.13f, rx - mw * 0.033f, ry - cabinet.height() * 0.03f, paint)
                for (k in 0 until 3) { paint.color = 0xFF1A1A1A.toInt(); c.drawRect(rx - mw * 0.05f + k * mw * 0.035f, ry + cabinet.height() * 0.08f, rx - mw * 0.03f + k * mw * 0.035f, ry + cabinet.height() * 0.2f, paint) }
            }
            else -> {
                paint.shader = LinearGradient(0f, cabinet.top, 0f, cabinet.bottom, 0xFFEAE4D4.toInt(), 0xFFCFC7B2.toInt(), Shader.TileMode.CLAMP)
                c.drawRoundRect(cabinet, mw * 0.015f, mw * 0.015f, paint); paint.shader = null
                paint.color = 0xFF161616.toInt()
                c.drawRoundRect(cabinet.left + cabinet.width() * 0.03f, cabinet.top + cabinet.height() * 0.1f, cabinet.right - cabinet.width() * 0.03f, cabinet.bottom - cabinet.height() * 0.1f, mw * 0.008f, mw * 0.008f, paint)
            }
        }
        if (style != PORTRAIT) for (i in 0..1) {
            val s = slots[i]
            val face = RectF(s.left - mw * 0.01f, cabinet.top + cabinet.height() * 0.12f, s.right + mw * 0.01f, cabinet.bottom - cabinet.height() * 0.12f)
            if (style == BOARD) {      // the drives sit on the right of the board, smaller
                val l = cabinet.left + cabinet.width() * 0.66f; val t0 = cabinet.top + cabinet.height() * (0.06f + i * 0.47f)
                face.set(l, t0, cabinet.right - cabinet.width() * 0.02f, t0 + cabinet.height() * 0.42f)
                s.set(face.left + face.width() * 0.08f, face.top + face.height() * 0.42f, face.right - face.width() * 0.08f, face.top + face.height() * 0.58f)
            } else if (style == CASE && i == 1) {
                face.right = cabinet.left + cabinet.width() * 0.78f
                s.right = face.right - mw * 0.01f
            }
            paint.color = when (style) { BOARD -> 0xFFCFCCC4.toInt(); CASE -> 0xFF1E1E1E.toInt(); else -> 0xFF222222.toInt() }
            c.drawRoundRect(face, mw * 0.008f, mw * 0.008f, paint)
            paint.color = if (style == BOARD) 0xFF151515.toInt() else 0xFF050505.toInt()
            c.drawRoundRect(s, s.height() / 2, s.height() / 2, paint)
            val disk = if (i == 0) floppyLabel != null else false
            if (disk) {
                paint.color = 0xFF2A2A30.toInt()
                c.drawRect(s.left + s.width() * 0.05f, s.top + s.height() * 0.25f, s.right - s.width() * 0.05f, s.bottom - s.height() * 0.25f, paint)
            }
            paint.color = if (style == BOARD) 0xFF3A3A3A.toInt() else 0xFF5A5A5A.toInt()
            c.drawRoundRect(s.centerX() - s.width() * 0.08f, s.bottom + face.height() * 0.08f, s.centerX() + s.width() * 0.08f, s.bottom + face.height() * 0.2f, 4f, 4f, paint)
            paint.color = if (i == 0 && indicators and 1 != 0) 0xFFFF3A2A.toInt() else 0xFF5A1A14.toInt()
            c.drawCircle(face.right - mw * 0.02f, face.top + face.height() * 0.18f, mw * 0.007f, paint)
            text.color = if (style == BOARD) 0xFF3A3A3A.toInt() else 0xFFBDBDBD.toInt(); text.textSize = face.height() * 0.16f
            c.drawText(if (i == 0) "A" else "B", face.left + mw * 0.02f, face.top + face.height() * 0.24f, text)
        }
        if (style != PORTRAIT) floppyLabel?.let {
            text.color = 0xFFEDEBE5.toInt(); text.textSize = cabinet.height() * 0.13f
            c.drawText(it.take(28), slots[0].centerX(), cabinet.bottom - cabinet.height() * 0.02f, text)
        }

        // the keyboard
        paint.color = if (office) 0xFFE4DCC6.toInt() else 0xFF2C2D31.toInt(); c.drawRoundRect(keyboard, mw * 0.02f, mw * 0.02f, paint)
        for (k in allKeys) {
            val d = k.down || (k.k.kind == Kind.SHIFT && shiftLatched) || (k.k.kind == Kind.CTRL && ctrlLatched) || (k.k.kind == Kind.LOCK && caps)
            val r = k.rect
            val base = if (office) (when (k.k.kind) { Kind.CHAR, Kind.SPACE -> 0xFF3A3A3C.toInt(); else -> 0xFF5A5A5E.toInt() })
                else when (k.k.kind) { Kind.CHAR, Kind.SPACE -> 0xFFE6E2D8.toInt(); else -> 0xFF8C8A84.toInt() }
            paint.color = 0xFF111111.toInt(); c.drawRoundRect(r.left, r.top + r.height() * 0.06f, r.right, r.bottom + r.height() * 0.04f, r.height() * 0.12f, r.height() * 0.12f, paint)
            paint.color = if (d) darken(base) else base
            c.drawRoundRect(r.left, r.top + (if (d) r.height() * 0.05f else 0f), r.right, r.bottom - r.height() * 0.04f + (if (d) r.height() * 0.05f else 0f), r.height() * 0.12f, r.height() * 0.12f, paint)
            if (k.k.kind == Kind.LOCK && caps) { paint.color = 0xFFFF3A2A.toInt(); c.drawCircle(r.right - r.width() * 0.15f, r.top + r.height() * 0.2f, r.height() * 0.06f, paint) }
            text.color = if (office) 0xFFEDEDED.toInt() else 0xFF1C1C1C.toInt()
            val lines = k.k.label.split("\n")
            text.textSize = if (k.k.kind == Kind.CHAR) r.height() * 0.38f else r.height() * (if (lines.size > 1) 0.2f else 0.24f)
            val label = if (k.k.kind == Kind.CHAR && k.k.label[0].isLetter()) k.k.label.uppercase() else k.k.label
            if (k.k.kind == Kind.CHAR && !k.k.label[0].isLetter()) {
                text.textSize = r.height() * 0.28f
                c.drawText(k.k.shifted.toChar().toString(), r.centerX(), r.top + r.height() * 0.38f, text)
                c.drawText(label, r.centerX(), r.top + r.height() * 0.78f, text)
            } else for ((i, l) in lines.withIndex())
                c.drawText(l, r.centerX(), r.centerY() + text.textSize * (0.35f + (i - (lines.size - 1) / 2f) * 1.1f), text)
        }
    }

    private fun darken(c: Int) = Color.rgb(Color.red(c) * 7 / 10, Color.green(c) * 7 / 10, Color.blue(c) * 7 / 10)

    private fun press(k: Key) {
        when (k.k.kind) {
            Kind.SHIFT -> { shiftLatched = !shiftLatched; return }
            Kind.CTRL -> { ctrlLatched = !ctrlLatched; return }
            Kind.LOCK -> { caps = !caps; return }
            else -> {}
        }
        var code = k.k.code
        if (k.k.kind == Kind.CHAR) {
            val letter = k.k.label[0].isLetter()
            if (shiftLatched || (caps && letter)) code = k.k.shifted
            if (shiftLatched && caps && letter) code = k.k.code
            if (ctrlLatched) code = code and 0x1f
        }
        shiftLatched = false; ctrlLatched = false
        onKey?.invoke(code, true)
        onKey?.invoke(code, false)
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        val i = e.actionIndex
        val id = e.getPointerId(i)
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val x = e.getX(i); val y = e.getY(i)
                if (power.contains(x, y) || (y > power.top - 20 && y < monitor.bottom && x > power.left - 20)) { onMenu?.invoke(); return true }
                if (cabinet.contains(x, y)) { onDisks?.invoke(); return true }
                val k = allKeys.firstOrNull { it.rect.contains(x, y) } ?: return true
                k.down = true; pointers[id] = k
                performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                press(k)
                invalidate()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                pointers.remove(id)?.let { it.down = false; invalidate() }
                if (e.actionMasked == MotionEvent.ACTION_CANCEL) { for (k in pointers.values) k.down = false; pointers.clear() }
            }
        }
        return true
    }
}
