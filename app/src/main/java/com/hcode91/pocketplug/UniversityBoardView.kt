package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View

/**
 * A late-70s single-board teaching computer in the style of the TM 990/189:
 * a green circuit board with its chips, the user LEDs, the LOAD switch and
 * the IDLE / FWD lights; the ten-digit red LED display in a smoked window and
 * the 45-key calculator keyboard under it (letters, digits, + - @ > $ , and
 * SHIFT / SPACE / RETURN, with the shifted signs printed above). With the
 * video board a colour monitor sits above the board and two joystick fire
 * buttons sit beside it. No maker's logo is drawn.
 *
 * Frame: 256 x 193; rows 0-191 the video board picture, row 192 the board's
 * digits (pixels 0-9, segment bits a-g + point) and LEDs (pixel 10).
 */
class UniversityBoardView(ctx: Context, private val video: Boolean) : View(ctx) {

    var onKey: ((Int, Boolean) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    /** video board joystick buttons: bit 0 = joystick 1, bit 1 = joystick 2 */
    var onJoy: ((Int) -> Unit)? = null

    companion object {
        const val LOAD = 0x50
    }

    private data class K(val label: String, val shift: String, val code: Int)
    private val rows = listOf(
        listOf(K("V", "←D", 0x40), K("W", "", 0x41), K("X", "", 0x42), K("Y", "", 0x43), K("Z", "D→", 0x44)),
        listOf(K("Q", "", 0x38), K("R", "", 0x39), K("S", "", 0x3a), K("T", "", 0x3b), K("U", "", 0x3c)),
        listOf(K("L", "", 0x30), K("M", "", 0x31), K("N", "", 0x32), K("O", "", 0x33), K("P", "", 0x34)),
        listOf(K("G", "", 0x28), K("H", "", 0x29), K("I", "", 0x2a), K("J", "", 0x2b), K("K", "", 0x2c)),
        listOf(K("B", "", 0x20), K("C", "", 0x21), K("D", "", 0x22), K("E", "", 0x23), K("F", "", 0x24)),
        listOf(K("6", "_", 0x18), K("7", "\"", 0x19), K("8", "#", 0x1a), K("9", "", 0x1b), K("A", "", 0x1c)),
        listOf(K("1", ".", 0x10), K("2", ";", 0x11), K("3", ":", 0x12), K("4", "?", 0x13), K("5", "!", 0x14)),
        listOf(K("+", "(", 0x08), K("-", ")", 0x09), K("@", "/", 0x0a), K(">", "%", 0x0b), K("0", "^", 0x0c)),
        listOf(K("Shift", "", 0x00), K("Sp", "*", 0x01), K("Ret", "'", 0x02), K("$", "=", 0x03), K(",", "<", 0x04)),
    )
    private val keyRects = rows.map { r -> r.map { RectF() } }
    private val keyDown = HashSet<Int>()

    private val frame = Bitmap.createBitmap(256, 192, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(256 * 193)
    private val digits = IntArray(10)
    private var leds = 0

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }
    private val smooth = Paint(Paint.FILTER_BITMAP_FLAG)

    private val monitor = RectF()
    private val tube = RectF()
    private val joy1 = PointF(); private val joy2 = PointF(); private var joyR = 0f
    private val board = RectF()
    private val keyPanel = RectF()
    private val display = RectF()
    private val loadSw = RectF()
    private val menuChip = RectF()
    private var unit = 1f

    private val pointers = HashMap<Int, Int>()   // pointer -> key code, or -2/-3 for joystick buttons
    private var joyMask = 0

    fun updateFrame(px: IntArray) {
        if (px.size < 256 * 193) return
        System.arraycopy(px, 0, framePx, 0, framePx.size)
        for (i in 0..9) digits[i] = px[192 * 256 + i] and 0xff
        leds = px[192 * 256 + 10] and 0x7f
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        var top = h * 0.01f
        if (video) {
            val mw = w * 0.9f
            val tw = mw * 0.7f
            tube.set(w / 2f - tw / 2, top + mw * 0.04f, w / 2f + tw / 2, top + mw * 0.04f + tw * 192f / 256f)
            monitor.set((w - mw) / 2, top, (w + mw) / 2, tube.bottom + mw * 0.12f)
            joyR = mw * 0.04f
            joy1.set(monitor.left + mw * 0.08f, monitor.bottom - mw * 0.06f)
            joy2.set(monitor.right - mw * 0.08f, monitor.bottom - mw * 0.06f)
            top = monitor.bottom + h * 0.008f
        }
        val bw = w * 0.98f
        val boardH = if (video) w * 0.17f else w * 0.42f
        board.set((w - bw) / 2, top, (w + bw) / 2, top + boardH)
        // the keyboard panel (black) with the display window at its top
        keyPanel.set(w * 0.04f, board.bottom - w * 0.015f, w * 0.96f, h * 0.995f)
        display.set(keyPanel.left + w * 0.03f, keyPanel.top + w * 0.03f, keyPanel.right - w * 0.03f, keyPanel.top + w * 0.03f + w * 0.13f)
        val kTop = display.bottom + w * 0.04f
        val kh = (keyPanel.bottom - kTop - w * 0.02f) / rows.size
        val kw = (keyPanel.width() - w * 0.08f) / 5
        unit = minOf(kw, kh)
        for ((ri, r) in rows.withIndex()) for ((ci, _) in r.withIndex()) {
            val x = keyPanel.left + w * 0.04f + ci * kw
            val y = kTop + ri * kh
            keyRects[ri][ci].set(x + kw * 0.1f, y + kh * 0.2f, x + kw * 0.9f, y + kh * 0.92f)
        }
        loadSw.set(board.left + bw * 0.7f, board.top + boardH * (if (video) 0.22f else 0.5f), board.left + bw * 0.76f, board.top + boardH * (if (video) 0.52f else 0.68f))
        menuChip.set(board.left + bw * 0.06f, board.top + boardH * 0.1f, board.left + bw * 0.28f, board.top + boardH * (if (video) 0.4f else 0.26f))
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF161A17.toInt())
        if (video) drawMonitor(c)
        drawBoard(c)
        drawKeyPanel(c)
    }

    private fun drawMonitor(c: Canvas) {
        val mw = monitor.width()
        paint.color = Color.WHITE
        paint.shader = LinearGradient(0f, monitor.top, 0f, monitor.bottom, 0xFF5C5650.toInt(), 0xFF3C3833.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(monitor, mw * 0.03f, mw * 0.03f, paint)
        paint.shader = null
        paint.color = 0xFF111111.toInt()
        c.drawRoundRect(tube.left - mw * 0.02f, tube.top - mw * 0.02f, tube.right + mw * 0.02f, tube.bottom + mw * 0.02f, mw * 0.03f, mw * 0.03f, paint)
        frame.setPixels(framePx, 0, 256, 0, 0, 256, 192)
        c.drawBitmap(frame, null, tube, smooth)
        paint.shader = RadialGradient(tube.centerX(), tube.centerY(), tube.width() * 0.75f, 0x00000000, 0x33000000, Shader.TileMode.CLAMP)
        c.drawRect(tube, paint)
        paint.shader = null
        for ((p, bit, label) in listOf(Triple(joy1, 1, "JOY 1"), Triple(joy2, 2, "JOY 2"))) {
            paint.color = 0xFF222222.toInt(); c.drawCircle(p.x, p.y, joyR * 1.25f, paint)
            paint.color = if (joyMask and bit != 0) 0xFF8A1A14.toInt() else 0xFFD2352B.toInt()
            c.drawCircle(p.x, p.y, joyR, paint)
            text.color = 0xFFCFC8BC.toInt(); text.textSize = joyR * 0.55f
            c.drawText(label, p.x + (if (bit == 1) joyR * 2.6f else -joyR * 2.6f), p.y + text.textSize * 0.35f, text)
        }
    }

    private fun drawBoard(c: Canvas) {
        val bw = board.width(); val bh = board.height()
        paint.color = 0xFF1E6B4A.toInt()
        c.drawRoundRect(board, bw * 0.015f, bw * 0.015f, paint)
        // traces
        stroke.color = 0xFF2E8A62.toInt(); stroke.strokeWidth = bw * 0.003f
        for (i in 0..18) {
            val y = board.top + bh * (0.05f + i * 0.05f)
            if (y > board.bottom - bh * 0.03f) break
            c.drawLine(board.left + bw * 0.02f, y, board.left + bw * (0.3f + (i % 5) * 0.12f), y, stroke)
        }
        for (i in 0..24) {
            val x = board.left + bw * (0.02f + i * 0.04f)
            c.drawLine(x, board.top + bh * 0.03f, x, board.top + bh * (0.2f + (i % 4) * 0.15f), stroke)
        }
        // chips: the big 40-pin CPU and 9901s, smaller DIPs and the ROMs
        val chips = if (video) listOf(
            floatArrayOf(0.33f, 0.12f, 0.26f, 0.22f), floatArrayOf(0.82f, 0.12f, 0.14f, 0.22f)
        ) else listOf(
            floatArrayOf(0.06f, 0.36f, 0.06f, 0.36f), floatArrayOf(0.15f, 0.36f, 0.06f, 0.36f),
            floatArrayOf(0.34f, 0.08f, 0.26f, 0.14f), floatArrayOf(0.78f, 0.08f, 0.18f, 0.12f),
            floatArrayOf(0.28f, 0.36f, 0.05f, 0.22f), floatArrayOf(0.36f, 0.36f, 0.05f, 0.22f), floatArrayOf(0.44f, 0.36f, 0.05f, 0.22f),
            floatArrayOf(0.52f, 0.36f, 0.05f, 0.22f), floatArrayOf(0.60f, 0.36f, 0.05f, 0.22f),
            floatArrayOf(0.84f, 0.36f, 0.06f, 0.36f)
        )
        for (ch in chips) {
            val r = RectF(board.left + bw * ch[0], board.top + bh * ch[1], board.left + bw * (ch[0] + ch[2]), board.top + bh * (ch[1] + ch[3]))
            paint.color = 0xFFB9B9B9.toInt()
            val pins = ((if (r.height() > r.width()) r.height() else r.width()) / (bw * 0.012f)).toInt()
            for (p in 0 until pins) {
                if (r.height() > r.width()) {
                    val y = r.top + (p + 0.5f) * r.height() / pins
                    c.drawRect(r.left - bw * 0.006f, y - bw * 0.002f, r.left, y + bw * 0.002f, paint)
                    c.drawRect(r.right, y - bw * 0.002f, r.right + bw * 0.006f, y + bw * 0.002f, paint)
                } else {
                    val x = r.left + (p + 0.5f) * r.width() / pins
                    c.drawRect(x - bw * 0.002f, r.top - bw * 0.006f, x + bw * 0.002f, r.top, paint)
                    c.drawRect(x - bw * 0.002f, r.bottom, x + bw * 0.002f, r.bottom + bw * 0.006f, paint)
                }
            }
            paint.color = 0xFF16161A.toInt()
            c.drawRect(r, paint)
        }
        // the battery-backed coin cell look and the crystal
        if (!video) {
            paint.color = 0xFFB8A57A.toInt(); c.drawCircle(board.left + bw * 0.68f, board.top + bh * 0.2f, bw * 0.04f, paint)
            paint.color = 0xFFC9C9CC.toInt(); c.drawRoundRect(board.left + bw * 0.24f, board.top + bh * 0.1f, board.left + bw * 0.31f, board.top + bh * 0.15f, bw * 0.01f, bw * 0.01f, paint)
        }
        // user LEDs 1-4 (and the shift light by the keyboard), IDLE and FWD
        val ly = board.top + bh * (if (video) 0.66f else 0.8f)
        for (i in 0..3) drawLed(c, board.left + bw * (0.06f + i * 0.045f), ly, leds and (1 shl i) != 0, "LED${i + 1}")
        drawLed(c, board.left + bw * 0.52f, ly, leds and 0x40 != 0, "IDLE")
        drawLed(c, board.left + bw * 0.58f, ly, leds and 0x20 != 0, "FWD")
        // LOAD toggle (the red lever) and the menu chip (EPROM labels)
        paint.color = 0xFF202020.toInt(); c.drawRoundRect(loadSw, bw * 0.006f, bw * 0.006f, paint)
        paint.color = 0xFFD8352A.toInt()
        c.drawRoundRect(loadSw.left + loadSw.width() * 0.25f, loadSw.top - loadSw.height() * 0.25f, loadSw.right - loadSw.width() * 0.25f, loadSw.centerY(), bw * 0.006f, bw * 0.006f, paint)
        text.color = 0xFFE7E3D3.toInt(); text.textSize = bw * 0.022f
        c.drawText("LOAD", loadSw.centerX(), loadSw.bottom + text.textSize * 1.2f, text)
        paint.color = 0xFFF1EEE4.toInt(); c.drawRoundRect(menuChip, bw * 0.006f, bw * 0.006f, paint)
        text.color = 0xFF333333.toInt(); text.textSize = menuChip.height() * 0.3f
        c.drawText("MENU", menuChip.centerX(), menuChip.centerY() + text.textSize * 0.35f, text)
    }

    private fun drawLed(c: Canvas, x: Float, y: Float, on: Boolean, label: String) {
        val r = board.width() * 0.011f
        paint.color = 0xFF3A0A08.toInt(); c.drawCircle(x, y, r * 1.3f, paint)
        paint.color = if (on) 0xFFFF3B2A.toInt() else 0xFF6A1712.toInt()
        c.drawCircle(x, y, r, paint)
        if (on) { paint.color = 0x55FF5A40; c.drawCircle(x, y, r * 2.2f, paint) }
        text.color = 0xFFE7E3D3.toInt(); text.textSize = r * 1.4f
        c.drawText(label, x, y + r * 3f, text)
    }

    private fun drawKeyPanel(c: Canvas) {
        val w = keyPanel.width()
        paint.color = 0xFF121212.toInt()
        c.drawRoundRect(keyPanel, w * 0.03f, w * 0.03f, paint)
        stroke.color = 0xFF3A3A3A.toInt(); stroke.strokeWidth = w * 0.004f
        c.drawRoundRect(keyPanel, w * 0.03f, w * 0.03f, stroke)
        // display window: smoked red filter
        paint.color = 0xFF1A0505.toInt()
        c.drawRoundRect(display, w * 0.01f, w * 0.01f, paint)
        val dw = display.width() / 10
        for (i in 0..9) drawDigit(c, display.left + dw * i + dw * 0.15f, display.top + display.height() * 0.14f, dw * 0.66f, display.height() * 0.72f, digits[i])
        // shift light by the keyboard
        val sl = PointF(display.left + w * 0.02f, display.bottom + w * 0.022f)
        paint.color = if (leds and 0x10 != 0) 0xFFFF3B2A.toInt() else 0xFF4A110D.toInt()
        c.drawCircle(sl.x, sl.y, w * 0.008f, paint)
        text.color = 0xFF9A9A9A.toInt(); text.textSize = w * 0.022f; text.textAlign = Paint.Align.LEFT
        c.drawText("SHIFT", sl.x + w * 0.018f, sl.y + text.textSize * 0.36f, text)
        text.textAlign = Paint.Align.CENTER
        for ((ri, r) in rows.withIndex()) for ((ci, k) in r.withIndex()) drawKey(c, keyRects[ri][ci], k)
    }

    private fun drawKey(c: Canvas, r: RectF, k: K) {
        val down = keyDown.contains(k.code)
        val rad = r.height() * 0.18f
        paint.color = 0xFF050505.toInt()
        c.drawRoundRect(r.left, r.top + r.height() * 0.05f, r.right, r.bottom + r.height() * 0.05f, rad, rad, paint)
        paint.color = if (down) 0xFF1E1E1E.toInt() else 0xFF2F2F31.toInt()
        c.drawRoundRect(r.left, r.top + (if (down) r.height() * 0.05f else 0f), r.right, r.bottom, rad, rad, paint)
        val blue = k.code == 0x40 || k.code == 0x44
        text.color = if (blue) 0xFF6FA8FF.toInt() else 0xFFF2F2F2.toInt()
        text.textSize = if (k.label.length > 1) r.height() * 0.36f else r.height() * 0.5f
        c.drawText(k.label, r.centerX(), r.centerY() + text.textSize * 0.36f + (if (down) r.height() * 0.05f else 0f), text)
        if (k.shift.isNotEmpty()) {
            text.color = 0xFFE9D48A.toInt(); text.textSize = r.height() * 0.28f
            c.drawText(k.shift, r.right - r.width() * 0.16f, r.top - r.height() * 0.02f, text)
        }
    }

    // a red seven-segment digit; bits a-g, point
    private fun drawDigit(c: Canvas, x: Float, y: Float, w: Float, h: Float, segs: Int) {
        val t = w * 0.16f
        val on = 0xFFFF2A1A.toInt(); val off = 0xFF2C0806.toInt()
        val sk = w * 0.08f   // slant
        fun seg(i: Int, x0: Float, y0: Float, x1: Float, y1: Float) {
            paint.color = if (segs and (1 shl i) != 0) on else off
            stroke.color = paint.color; stroke.strokeWidth = t; stroke.strokeCap = Paint.Cap.ROUND
            c.drawLine(x0, y0, x1, y1, stroke)
        }
        val l = x + t; val r = x + w - t; val tY = y + t / 2; val mY = y + h / 2; val bY = y + h - t / 2
        seg(0, l + sk, tY, r + sk, tY)            // a
        seg(1, r + sk, tY, r, mY)                 // b
        seg(2, r, mY, r - sk, bY)                 // c
        seg(3, l - sk, bY, r - sk, bY)            // d
        seg(4, l, mY, l - sk, bY)                 // e
        seg(5, l + sk, tY, l, mY)                 // f
        seg(6, l, mY, r, mY)                      // g
        stroke.strokeCap = Paint.Cap.BUTT
        paint.color = if (segs and 0x80 != 0) on else off
        c.drawCircle(x + w + t * 0.4f, bY, t * 0.6f, paint)
    }

    // ---- touch ----
    private fun keyAt(x: Float, y: Float): Int {
        for ((ri, r) in rows.withIndex()) for ((ci, k) in r.withIndex()) {
            val rr = keyRects[ri][ci]
            if (x >= rr.left - rr.width() * 0.12f && x <= rr.right + rr.width() * 0.12f && y >= rr.top - rr.height() * 0.25f && y <= rr.bottom + rr.height() * 0.1f) return k.code
        }
        return -1
    }

    private fun setJoy() {
        var m = 0
        for (v in pointers.values) { if (v == -2) m = m or 1; if (v == -3) m = m or 2 }
        if (m != joyMask) { joyMask = m; onJoy?.invoke(m); invalidate() }
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex; val id = e.getPointerId(i)
                val x = e.getX(i); val y = e.getY(i)
                if (e.actionMasked == MotionEvent.ACTION_DOWN && RectF(menuChip).apply { inset(-menuChip.width() * 0.1f, -menuChip.height() * 0.3f) }.contains(x, y)) { onMenu?.invoke(); return true }
                if (RectF(loadSw).apply { inset(-loadSw.width() * 0.6f, -loadSw.height() * 0.5f) }.contains(x, y)) {
                    performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                    onKey?.invoke(LOAD, true); onKey?.invoke(LOAD, false)
                    return true
                }
                if (video) {
                    if (Math.hypot((x - joy1.x).toDouble(), (y - joy1.y).toDouble()) < joyR * 1.8f) { pointers[id] = -2; setJoy(); return true }
                    if (Math.hypot((x - joy2.x).toDouble(), (y - joy2.y).toDouble()) < joyR * 1.8f) { pointers[id] = -3; setJoy(); return true }
                }
                val k = keyAt(x, y)
                if (k < 0) return true
                performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                pointers[id] = k
                keyDown.add(k)
                onKey?.invoke(k, true)
                invalidate()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val v = pointers.remove(e.getPointerId(e.actionIndex))
                if (v != null && v < 0) setJoy()
                else if (v != null) { keyDown.remove(v); onKey?.invoke(v, false); invalidate() }
            }
            MotionEvent.ACTION_CANCEL -> {
                for (v in pointers.values) if (v >= 0) onKey?.invoke(v, false)
                pointers.clear(); keyDown.clear(); setJoy(); invalidate()
            }
        }
        return true
    }
}
