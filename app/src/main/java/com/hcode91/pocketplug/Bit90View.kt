package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.hypot

/**
 * The 1983 ColecoVision-compatible BASIC computer (Bit90), from a photo
 * without the name: a champagne-silver wedge with the speaker slots top right
 * and a dark recessed keyboard of green keys (a white RST key top right, the
 * arrow cluster on the right, the tall RETURN), with a TV above it and a
 * strip with the game controller (stick, two fire buttons, keypad) for
 * cartridges in between.
 *
 * Keys send keyEvent(row * 8 + bit, down) of MAME's ROW0-ROW7; SHIFT, CTRL,
 * FCTN and the meta key latch for the next key when tapped. The controller
 * goes through onInput (up 1, down 2, left 4, right 8, fires 0x10 / 0x20,
 * keypad 0-9, #, * in bits 8-19). Tap the TV for the menu.
 */
@SuppressLint("ViewConstructor")
class Bit90View(ctx: Context) : View(ctx) {

    var onKey: ((Int, Boolean) -> Unit)? = null
    var onInput: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var onReset: (() -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    private class K(val label: String, val code: Int, val units: Float = 1f, val top: String = "", val tall: Boolean = false) {
        val rect = RectF(); var down = false
    }
    private val mods = setOf(56, 61, 63, 57)
    private val rst = -2

    private val rows: List<List<K>> = listOf(
        listOf(K("1", 5, top = "!"), K("2", 13, top = "\""), K("3", 2, top = "#"), K("4", 10, top = "$"), K("5", 6, top = "%"), K("6", 14, top = "&"),
            K("7", 1, top = "'"), K("8", 9, top = "("), K("9", 7, top = ")"), K("0", 15), K("-", 0, top = "="), K("^", 8, top = "~"), K("\\", 3, top = "¦"), K("BS", 11, 1.1f)),
        listOf(K("ESC", 21, 1.3f), K("Q", 29), K("W", 18), K("E", 26), K("R", 22), K("T", 30), K("Y", 17), K("U", 25), K("I", 23), K("O", 31), K("P", 16),
            K("[", 24, top = "{"), K("]", 19, top = "}"), K("RUB\nOUT", 27, 1.1f)),
        listOf(K("CAPS\nLOWER", 37, 1.5f), K("A", 45), K("S", 34), K("D", 42), K("F", 38), K("G", 46), K("H", 33), K("J", 41), K("K", 39), K("L", 47),
            K(";", 32, top = "+"), K(":", 40, top = "*"), K("@", 35, top = "_"), K("RETURN", 43, 1.2f, tall = true)),
        listOf(K("SHIFT", 56, 1.9f), K("Z", 53), K("X", 50), K("C", 54), K("V", 49), K("B", 55), K("N", 48), K("M", 51), K(",", 52, top = "<"),
            K(".", 44, top = ">"), K("/", 36, top = "?"), K("SHIFT", 56, 1.8f)),
        listOf(K("CTRL", 61, 1.6f), K("BASIC", 58, 1.6f), K("SPACE", 62, 6.4f), K("◐", 57, 1.2f), K("FCTN", 63, 1.5f)),
    )
    private val side = listOf(K("RST", rst), K("DEL", 12), K("INS", 4), K("▲", 20), K("◀", 59), K("▶", 28), K("▼", 60))
    private val all = rows.flatten() + side
    private val latched = HashSet<Int>()

    private val frame = Bitmap.createBitmap(272, 208, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(272 * 208)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }
    private val smooth = Paint(Paint.FILTER_BITMAP_FLAG)

    private val tv = RectF(); private val screen = RectF()
    private val strip = RectF(); private val stick = PointF(); private var stickR = 0f
    private val fire1 = PointF(); private val fire2 = PointF(); private var fireR = 0f
    private val pad = Array(12) { RectF() }
    private val padLabels = arrayOf("1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#")
    private val padBits = intArrayOf(1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 0, 10).map { 1 shl (8 + it) }
    private val body = RectF(); private val well = RectF()
    private val keyPointers = HashMap<Int, K>()
    private val padPointers = HashMap<Int, Int>()
    private var padMask = 0

    fun updateFrame(px: IntArray) {
        if (px.size < framePx.size) return
        System.arraycopy(px, 0, framePx, 0, framePx.size)
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        val tw = w * 0.86f; val th = tw * 208f / 272f * 0.82f
        tv.set((w - tw) / 2, h * 0.012f, (w + tw) / 2, h * 0.012f + th + tw * 0.08f)
        screen.set(tv.left + tw * 0.06f, tv.top + tw * 0.04f, tv.right - tw * 0.06f, tv.top + tw * 0.04f + th)
        strip.set(w * 0.03f, tv.bottom + h * 0.01f, w * 0.97f, tv.bottom + h * 0.01f + h * 0.12f)
        stickR = strip.height() * 0.36f; stick.set(strip.left + strip.width() * 0.1f, strip.centerY())
        fireR = strip.height() * 0.17f
        fire1.set(strip.left + strip.width() * 0.25f, strip.centerY() - strip.height() * 0.2f)
        fire2.set(strip.left + strip.width() * 0.25f, strip.centerY() + strip.height() * 0.2f)
        val pl = strip.left + strip.width() * 0.36f; val pw = strip.width() * 0.6f / 6; val ph = strip.height() * 0.4f
        for (i in 0 until 12) {
            val c = i % 6; val r = i / 6
            pad[i].set(pl + c * pw + pw * 0.08f, strip.top + strip.height() * 0.08f + r * ph * 1.1f, pl + (c + 1) * pw - pw * 0.08f, strip.top + strip.height() * 0.08f + r * ph * 1.1f + ph)
        }
        body.set(w * 0.01f, strip.bottom + h * 0.012f, w * 0.99f, h * 0.995f)
        well.set(body.left + body.width() * 0.025f, body.top + body.height() * 0.16f, body.right - body.width() * 0.025f, body.bottom - body.height() * 0.05f)
        // main block: 15.1 units wide; the side block 2.2 units to its right
        val unit = well.width() / 18.2f
        val rowH = well.height() / 5.3f
        var y = well.top + rowH * 0.15f
        for ((ri, r) in rows.withIndex()) {
            var x = well.left + unit * 0.25f + (if (ri == 1 || ri == 2) 0f else 0f)
            for (k in r) {
                k.rect.set(x + unit * 0.06f, y + rowH * 0.06f, x + k.units * unit - unit * 0.06f, y + rowH * (if (k.tall) 1.94f else 0.94f))
                x += k.units * unit
            }
            y += rowH
        }
        val sx = well.left + unit * 15.8f
        fun at(k: K, cx: Float, row: Float) { k.rect.set(sx + cx * unit + unit * 0.06f, well.top + rowH * (0.15f + row) + rowH * 0.06f, sx + (cx + 1f) * unit - unit * 0.06f, well.top + rowH * (0.15f + row) + rowH * 0.94f) }
        at(side[0], 1.2f, 0f); at(side[1], 0.1f, 1f); at(side[2], 1.2f, 1f)
        at(side[3], 0.65f, 2f); at(side[4], 0.1f, 3f); at(side[5], 1.2f, 3f); at(side[6], 0.65f, 4f)
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF1A1714.toInt())
        // the TV
        paint.color = 0xFF2A2A2C.toInt(); c.drawRoundRect(tv, tv.width() * 0.04f, tv.width() * 0.04f, paint)
        paint.color = 0xFF0A0A0A.toInt(); c.drawRoundRect(screen.left - 6f, screen.top - 6f, screen.right + 6f, screen.bottom + 6f, 18f, 18f, paint)
        frame.setPixels(framePx, 0, 272, 0, 0, 272, 208)
        c.drawBitmap(frame, null, screen, smooth)
        paint.color = 0xFF46464A.toInt(); c.drawCircle(tv.right - tv.width() * 0.06f, tv.bottom - tv.width() * 0.035f, tv.width() * 0.012f, paint)

        // the controller strip
        val held = padMask or externalMask
        paint.color = 0xFF1E1E20.toInt(); c.drawRoundRect(strip, strip.height() * 0.2f, strip.height() * 0.2f, paint)
        paint.color = 0xFF3A3A3E.toInt(); c.drawCircle(stick.x, stick.y, stickR, paint)
        val sdx = (if (held and 8 != 0) 1 else 0) - (if (held and 4 != 0) 1 else 0)
        val sdy = (if (held and 2 != 0) 1 else 0) - (if (held and 1 != 0) 1 else 0)
        paint.color = 0xFF101010.toInt(); c.drawCircle(stick.x + sdx * stickR * 0.35f, stick.y + sdy * stickR * 0.35f, stickR * 0.45f, paint)
        for ((p, bit) in listOf(fire1 to 0x10, fire2 to 0x20)) {
            paint.color = if (held and bit != 0) 0xFF8A1010.toInt() else 0xFFD8302A.toInt(); c.drawCircle(p.x, p.y, fireR, paint)
        }
        for (i in 0 until 12) {
            paint.color = if (held and padBits[i] != 0) 0xFF5A5A5E.toInt() else 0xFF8E8E92.toInt()
            c.drawRoundRect(pad[i], 6f, 6f, paint)
            text.color = 0xFF151515.toInt(); text.textSize = pad[i].height() * 0.55f
            c.drawText(padLabels[i], pad[i].centerX(), pad[i].centerY() + text.textSize * 0.36f, text)
        }

        // the computer: champagne body with the raised back and speaker slots
        val bw = body.width(); val bh = body.height()
        paint.shader = LinearGradient(0f, body.top, 0f, body.bottom, intArrayOf(0xFFB9B3A6.toInt(), 0xFFDCD7CC.toInt(), 0xFFCFC9BC.toInt()), floatArrayOf(0f, 0.18f, 1f), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, bw * 0.02f, bw * 0.02f, paint); paint.shader = null
        stroke.color = 0xFF8E897E.toInt(); stroke.strokeWidth = 2f
        c.drawLine(body.left + bw * 0.04f, body.top + bh * 0.12f, body.right - bw * 0.04f, body.top + bh * 0.12f, stroke)
        c.drawLine(body.left + bw * 0.04f, body.top + bh * 0.13f, body.right - bw * 0.04f, body.top + bh * 0.13f, stroke)
        paint.color = 0xFF8E897E.toInt()
        for (i in 0 until 16) c.drawRect(body.right - bw * (0.22f - i * 0.011f), body.top + bh * 0.02f, body.right - bw * (0.216f - i * 0.011f), body.top + bh * 0.06f, paint)
        paint.color = 0xFF1C2220.toInt(); c.drawRect(well, paint)
        for (k in all) drawKey(c, k)
    }

    private fun drawKey(c: Canvas, k: K) {
        val r = k.rect
        val down = k.down || (k.code in latched)
        val rst = k.code == rst
        paint.color = 0xFF0C100E.toInt(); c.drawRoundRect(r.left, r.top + r.height() * 0.05f, r.right, r.bottom + r.height() * 0.04f, 6f, 6f, paint)
        val face = if (rst) 0xFFE8EDE6.toInt() else 0xFF2F7A4A.toInt()
        paint.color = if (down) (if (rst) 0xFFB8BDB6.toInt() else 0xFF1E5534.toInt()) else face
        val off = if (down) r.height() * 0.04f else 0f
        c.drawRoundRect(r.left, r.top + off, r.right, r.bottom - r.height() * 0.04f + off, 6f, 6f, paint)
        if (!rst) { paint.color = 0x22FFFFFF; c.drawRoundRect(r.left + 3f, r.top + 3f + off, r.right - 3f, r.top + r.height() * 0.35f + off, 4f, 4f, paint) }
        text.color = if (rst) 0xFF2F9A5A.toInt() else 0xFFF2F2F0.toInt()
        val lines = k.label.split("\n")
        val h = minOf(r.height(), if (k.tall) r.height() / 2 else r.height())
        if (k.label == "RETURN") {
            text.textSize = r.width() * 0.42f
            val step = (r.height() * 0.8f) / 6f
            for ((i, ch) in k.label.withIndex()) c.drawText(ch.toString(), r.centerX(), r.top + r.height() * 0.15f + step * i + text.textSize * 0.4f + off, text)
            return
        }
        if (k.top.isNotEmpty()) {
            text.textSize = h * 0.28f
            c.drawText(k.top, r.left + r.width() * 0.3f, r.top + h * 0.38f + off, text)
            text.textSize = h * 0.42f
            c.drawText(k.label, r.centerX(), r.top + h * 0.82f + off, text)
        } else {
            text.textSize = h * (if (lines.size > 1) 0.24f else if (k.label.length > 2) 0.3f else 0.44f)
            for ((i, l) in lines.withIndex())
                c.drawText(l, r.centerX(), r.centerY() + off + text.textSize * (0.36f + (i - (lines.size - 1) / 2f) * 1.1f), text)
        }
    }

    private fun padHit(x: Float, y: Float): Int {
        val dx = x - stick.x; val dy = y - stick.y
        if (hypot(dx, dy) < stickR * 1.3f) {
            var b = 0
            if (dx > stickR * 0.25f) b = b or 8
            if (dx < -stickR * 0.25f) b = b or 4
            if (dy > stickR * 0.25f) b = b or 2
            if (dy < -stickR * 0.25f) b = b or 1
            return b
        }
        if (hypot(x - fire1.x, y - fire1.y) < fireR * 1.4f) return 0x10
        if (hypot(x - fire2.x, y - fire2.y) < fireR * 1.4f) return 0x20
        for (i in 0 until 12) if (pad[i].contains(x, y)) return padBits[i]
        return 0
    }

    private fun updatePad() {
        var m = 0
        for (v in padPointers.values) m = m or v
        if (m != padMask) {
            if (m and padMask.inv() != 0) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            padMask = m; onInput?.invoke(m); invalidate()
        }
    }

    private fun keyDown(k: K) {
        performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
        if (k.code == rst) { onReset?.invoke(); return }
        if (k.code in mods) {
            if (k.code in latched) { latched.remove(k.code); onKey?.invoke(k.code, false) }
            else { latched.add(k.code); onKey?.invoke(k.code, true) }
            return
        }
        onKey?.invoke(k.code, true)
    }

    private fun keyUp(k: K) {
        if (k.code == rst || k.code in mods) return
        onKey?.invoke(k.code, false)
        // a latched modifier lasts for one key
        for (m in latched) onKey?.invoke(m, false)
        latched.clear()
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        val i = e.actionIndex
        val id = e.getPointerId(i)
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val x = e.getX(i); val y = e.getY(i)
                if (tv.contains(x, y)) { if (e.actionMasked == MotionEvent.ACTION_DOWN) onMenu?.invoke(); return true }
                if (strip.contains(x, y)) { padPointers[id] = padHit(x, y); updatePad(); return true }
                val k = all.firstOrNull { it.rect.contains(x, y) } ?: return true
                k.down = true; keyPointers[id] = k; keyDown(k); invalidate()
            }
            MotionEvent.ACTION_MOVE -> {
                var changed = false
                for (p in 0 until e.pointerCount) {
                    val pid = e.getPointerId(p)
                    if (padPointers.containsKey(pid)) { padPointers[pid] = padHit(e.getX(p), e.getY(p)); changed = true }
                }
                if (changed) updatePad()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                if (padPointers.remove(id) != null) updatePad()
                keyPointers.remove(id)?.let { it.down = false; keyUp(it); invalidate() }
                if (e.actionMasked == MotionEvent.ACTION_CANCEL) {
                    padPointers.clear(); updatePad()
                    for (k in keyPointers.values) { k.down = false; keyUp(k) }
                    keyPointers.clear(); invalidate()
                }
            }
        }
        return true
    }
}
