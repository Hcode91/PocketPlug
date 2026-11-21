package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.abs
import kotlin.math.min

/**
 * The Backgammon Challenger: a walnut-framed board with dark-brown and cream
 * points on the left, and on the right a brown panel with a red LED window
 * ("I WIN" / "I LOSE" lamps over four digits labelled FROM, DBL and TO) above a
 * 4x4 keypad of cream keys (RE GM CL EN / PB 7 8 9 / PV 4 5 6 / 0 1 2 3). The
 * board has no sensors: as on the real thing you move the checkers yourself,
 * by dragging them. Long-press the empty walnut frame to set the checkers up
 * again; the frame's top edge (over the panel) opens the emulator menu.
 * Drawn for the phone held sideways; no maker's name is shown.
 */
class BackgammonView(ctx: Context) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    private val digits = IntArray(32)
    private var lampWin = 0
    private var lampLose = 0

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }

    private val frame = RectF()
    private val board = RectF()
    private val panel = RectF()
    private val window = RectF()
    private val bar = RectF()
    private val offTray = RectF()
    private var pointW = 0f
    private var checker = 0f
    private val keys = Array(16) { RectF() }

    // key labels in panel order and their input bit (row * 4 + col of IN.0-IN.3)
    private val keyLabel = arrayOf("RE", "GM", "CL", "EN", "PB", "7", "8", "9", "PV", "4", "5", "6", "0", "1", "2", "3")
    private val keyBit = intArrayOf(12, 8, 4, 0, 13, 9, 5, 1, 14, 10, 6, 2, 15, 11, 7, 3)

    // checkers: count per point 1..24 (positive = cream, negative = brown), bar and borne off
    private val points = IntArray(25)
    private var barCream = 0; private var barBrown = 0
    private var offCream = 0; private var offBrown = 0
    private var dragFrom = -1           // 1..24 point, 25 bar, 26 tray
    private var dragCream = true
    private val dragPos = PointF()

    private val pointers = HashMap<Int, Int>()
    private var touchMask = 0
    private var downTime = 0L

    init { setUp() }

    private fun setUp() {
        points.fill(0)
        // cream moves 24 -> 1, brown 1 -> 24 (positions as on the box)
        points[24] = 2; points[13] = 5; points[8] = 3; points[6] = 5
        points[1] = -2; points[12] = -5; points[17] = -3; points[19] = -5
        barCream = 0; barBrown = 0; offCream = 0; offBrown = 0
    }

    fun updateFrame(px: IntArray) {
        if (px.size < 172) return
        for (i in 0 until 32) digits[i] = px[130 + i]
        lampWin = px[170]; lampLose = px[171]
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        var fw = w * 0.97f; var fh = fw / 1.75f
        if (fh > h * 0.95f) { fh = h * 0.95f; fw = fh * 1.75f }
        frame.set((w - fw) / 2, (h - fh) / 2, (w + fw) / 2, (h + fh) / 2)
        val m = fh * 0.05f
        val bw = fw * 0.66f
        board.set(frame.left + m, frame.top + m, frame.left + bw, frame.bottom - m)
        panel.set(board.right + m * 0.8f, frame.top + m, frame.right - m, frame.bottom - m)
        offTray.set(board.right - board.width() * 0.07f, board.top, board.right, board.bottom)
        val play = RectF(board.left, board.top, offTray.left, board.bottom)
        pointW = play.width() / 13f
        bar.set(play.left + pointW * 6, play.top, play.left + pointW * 7, play.bottom)
        checker = min(pointW * 0.92f, board.height() * 0.085f)
        window.set(panel.left + panel.width() * 0.08f, panel.top + panel.height() * 0.14f,
            panel.right - panel.width() * 0.08f, panel.top + panel.height() * 0.4f)
        val kx0 = panel.left + panel.width() * 0.08f
        val kw = (panel.width() * 0.84f) / 4f
        val ky0 = panel.top + panel.height() * 0.47f
        val kh = (panel.bottom - panel.height() * 0.05f - ky0) / 4f
        for (i in 0 until 16) {
            val c = i % 4; val r = i / 4
            keys[i].set(kx0 + c * kw + kw * 0.1f, ky0 + r * kh + kh * 0.14f, kx0 + (c + 1) * kw - kw * 0.1f, ky0 + (r + 1) * kh - kh * 0.14f)
        }
    }

    // geometry of point p (1..24): x centre and whether it hangs from the top
    private fun pointX(p: Int): Float {
        val play = board.left
        return if (p <= 12) {
            val slot = if (p <= 6) 13 - p else 12 - p   // 1..6 right of the bar, 7..12 left
            play + pointW * (slot + 0.5f)
        } else {
            val slot = if (p >= 19) p - 12 else p - 13
            play + pointW * (slot + 0.5f)
        }
    }
    private fun pointTop(p: Int) = p <= 12

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF1A1512.toInt())
        val fh = frame.height()
        // walnut frame
        paint.shader = LinearGradient(frame.left, frame.top, frame.right, frame.bottom,
            intArrayOf(0xFF6B4528.toInt(), 0xFF4E301B.toInt(), 0xFF6E4629.toInt(), 0xFF45291A.toInt()), null, Shader.TileMode.CLAMP)
        c.drawRoundRect(frame, fh * 0.03f, fh * 0.03f, paint)
        paint.shader = null
        stroke.color = 0x33000000; stroke.strokeWidth = fh * 0.002f
        var gy = frame.top + fh * 0.01f
        while (gy < frame.bottom) { c.drawLine(frame.left, gy, frame.right, gy + fh * 0.004f, stroke); gy += fh * 0.013f }

        // board surface: dark brown with a cream inlay line
        paint.color = 0xFF3A2416.toInt(); c.drawRect(board, paint)
        stroke.color = 0xFFD9C79A.toInt(); stroke.strokeWidth = fh * 0.004f
        c.drawRect(board.left + fh * 0.008f, board.top + fh * 0.008f, offTray.left - fh * 0.004f, board.bottom - fh * 0.008f, stroke)
        paint.color = 0xFF2C1A10.toInt(); c.drawRect(offTray, paint)
        paint.color = 0xFF4A2E1C.toInt(); c.drawRect(bar, paint)
        stroke.color = 0xFFD9C79A.toInt(); c.drawLine(bar.centerX(), bar.top, bar.centerX(), bar.bottom, stroke)

        // points: alternating cream and dark brown with a cream outline
        val ph = board.height() * 0.42f
        text.textSize = fh * 0.028f; text.color = 0xFFE6D6AE.toInt()
        for (p in 1..24) {
            val x = pointX(p); val top = pointTop(p)
            val base = if (top) board.top + fh * 0.02f else board.bottom - fh * 0.02f
            val tip = if (top) base + ph else base - ph
            val path = Path().apply {
                moveTo(x - pointW * 0.44f, base); lineTo(x + pointW * 0.44f, base); lineTo(x, tip); close()
            }
            paint.color = if (p % 2 == 0) 0xFFE8DAB2.toInt() else 0xFF241309.toInt()
            c.drawPath(path, paint)
            stroke.color = 0xFFD9C79A.toInt(); stroke.strokeWidth = fh * 0.003f
            c.drawPath(path, stroke)
            val ly = if (top) board.centerY() - fh * 0.012f else board.centerY() + fh * 0.034f
            c.drawText(p.toString(), x, ly, text)
        }
        c.drawText("0", bar.centerX(), board.centerY() + fh * 0.01f, text)

        // checkers
        for (p in 1..24) {
            var n = abs(points[p]); val cream = points[p] > 0
            if (dragFrom == p) n--
            drawStack(c, pointX(p), pointTop(p), n, cream)
        }
        drawBar(c)
        drawTray(c)
        if (dragFrom >= 0) drawChecker(c, dragPos.x, dragPos.y, dragCream)

        drawPanel(c)
    }

    private fun drawStack(c: Canvas, x: Float, top: Boolean, n: Int, cream: Boolean) {
        val fh = frame.height()
        val step = min(checker * 0.78f, (board.height() * 0.44f - checker) / 5f)
        for (i in 0 until n) {
            val k = if (i < 6) i else 5
            val y = if (top) board.top + fh * 0.02f + checker / 2 + k * step else board.bottom - fh * 0.02f - checker / 2 - k * step
            drawChecker(c, x, y, cream)
        }
        if (n > 6) {
            val y = if (top) board.top + fh * 0.02f + checker / 2 + 5 * step else board.bottom - fh * 0.02f - checker / 2 - 5 * step
            text.textSize = checker * 0.5f; text.color = if (cream) 0xFF5A3A20.toInt() else 0xFFE8DAB2.toInt()
            c.drawText(n.toString(), x, y + text.textSize * 0.35f, text)
        }
    }

    private fun drawChecker(c: Canvas, x: Float, y: Float, cream: Boolean) {
        val r = RectF(x - checker / 2, y - checker * 0.42f, x + checker / 2, y + checker * 0.42f)
        paint.color = 0x55000000
        c.drawRoundRect(r.left + checker * 0.05f, r.top + checker * 0.07f, r.right + checker * 0.05f, r.bottom + checker * 0.07f, checker * 0.12f, checker * 0.12f, paint)
        paint.shader = LinearGradient(r.left, r.top, r.right, r.bottom,
            if (cream) 0xFFFBF3DC.toInt() else 0xFF6A3E22.toInt(), if (cream) 0xFFD8C9A0.toInt() else 0xFF2E180B.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(r, checker * 0.12f, checker * 0.12f, paint)
        paint.shader = null
        stroke.color = if (cream) 0xFFB9A57A.toInt() else 0xFF1C0E06.toInt(); stroke.strokeWidth = checker * 0.04f
        c.drawRoundRect(r, checker * 0.12f, checker * 0.12f, stroke)
    }

    private fun drawBar(c: Canvas) {
        var nb = barBrown; var nc = barCream
        if (dragFrom == 25) { if (dragCream) nc-- else nb-- }
        for (i in 0 until nb) drawChecker(c, bar.centerX(), board.centerY() - checker * (0.9f + i * 0.5f), false)
        for (i in 0 until nc) drawChecker(c, bar.centerX(), board.centerY() + checker * (0.9f + i * 0.5f), true)
    }

    private fun drawTray(c: Canvas) {
        var nb = offBrown; var nc = offCream
        if (dragFrom == 26) { if (dragCream) nc-- else nb-- }
        val t = checker * 0.22f
        paint.color = 0xFF2E180B.toInt()
        for (i in 0 until nb) c.drawRoundRect(offTray.left + offTray.width() * 0.12f, offTray.top + checker * 0.3f + i * t * 1.15f,
            offTray.right - offTray.width() * 0.12f, offTray.top + checker * 0.3f + i * t * 1.15f + t, t * 0.3f, t * 0.3f, paint)
        paint.color = 0xFFE8DAB2.toInt()
        for (i in 0 until nc) c.drawRoundRect(offTray.left + offTray.width() * 0.12f, offTray.bottom - checker * 0.3f - (i + 1) * t * 1.15f,
            offTray.right - offTray.width() * 0.12f, offTray.bottom - checker * 0.3f - (i + 1) * t * 1.15f + t, t * 0.3f, t * 0.3f, paint)
    }

    private fun drawPanel(c: Canvas) {
        val fh = frame.height()
        paint.shader = LinearGradient(panel.left, panel.top, panel.right, panel.bottom, 0xFF4B2E1B.toInt(), 0xFF35200F.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(panel, fh * 0.015f, fh * 0.015f, paint)
        paint.shader = null
        stroke.color = 0xFFC9A96A.toInt(); stroke.strokeWidth = fh * 0.003f
        c.drawRoundRect(panel.left + fh * 0.01f, panel.top + fh * 0.01f, panel.right - fh * 0.01f, panel.bottom - fh * 0.01f, fh * 0.012f, fh * 0.012f, stroke)
        // the cube slot along the top
        val slot = RectF(panel.left + panel.width() * 0.08f, panel.top + panel.height() * 0.04f, panel.right - panel.width() * 0.08f, panel.top + panel.height() * 0.085f)
        paint.color = 0xFF2A170B.toInt(); c.drawRoundRect(slot, slot.height() / 2, slot.height() / 2, paint)
        stroke.color = 0xFFC9A96A.toInt(); c.drawRoundRect(slot, slot.height() / 2, slot.height() / 2, stroke)

        // LED window
        paint.color = 0xFF2C1A10.toInt()
        c.drawRoundRect(window.left - fh * 0.01f, window.top - fh * 0.01f, window.right + fh * 0.01f, window.bottom + fh * 0.01f, fh * 0.015f, fh * 0.015f, paint)
        stroke.color = 0xFFC9A96A.toInt(); c.drawRoundRect(window.left - fh * 0.01f, window.top - fh * 0.01f, window.right + fh * 0.01f, window.bottom + fh * 0.01f, fh * 0.015f, fh * 0.015f, stroke)
        val wh = window.height()
        text.textSize = wh * 0.13f; text.color = 0xFFD9C79A.toInt()
        val lampY = window.top + wh * 0.14f
        drawLamp(c, window.left + window.width() * 0.1f, lampY, lampWin > 64)
        c.drawText("I WIN", window.left + window.width() * 0.28f, lampY + text.textSize * 0.35f, text)
        drawLamp(c, window.left + window.width() * 0.58f, lampY, lampLose > 64)
        c.drawText("I LOSE", window.left + window.width() * 0.78f, lampY + text.textSize * 0.35f, text)
        val glass = RectF(window.left + window.width() * 0.04f, window.top + wh * 0.28f, window.right - window.width() * 0.04f, window.top + wh * 0.78f)
        paint.color = 0xFF1A0204.toInt(); c.drawRect(glass, paint)
        val dw = glass.width() / 4.6f
        for (d in 0 until 4) {
            val x = glass.left + glass.width() * 0.05f + d * dw + (if (d >= 2) dw * 0.5f else 0f)
            drawDigit(c, RectF(x + dw * 0.12f, glass.top + glass.height() * 0.12f, x + dw * 0.88f, glass.bottom - glass.height() * 0.12f), d)
        }
        text.textSize = wh * 0.12f; text.color = 0xFFD9C79A.toInt()
        val ly = window.bottom - wh * 0.04f
        c.drawText("FROM", glass.left + glass.width() * 0.22f, ly, text)
        c.drawText("DBL", glass.centerX(), ly, text)
        c.drawText("TO", glass.left + glass.width() * 0.78f, ly, text)

        // keys
        val held = touchMask or externalMask
        for (i in 0 until 16) {
            val r = keys[i]; val down = held and (1 shl keyBit[i]) != 0
            paint.color = 0xFF2A170B.toInt()
            c.drawRoundRect(r.left - fh * 0.006f, r.top - fh * 0.006f, r.right + fh * 0.006f, r.bottom + fh * 0.006f, fh * 0.012f, fh * 0.012f, paint)
            paint.shader = LinearGradient(0f, r.top, 0f, r.bottom,
                if (down) 0xFFC8B588.toInt() else 0xFFF0E3BE.toInt(), if (down) 0xFFA8956A.toInt() else 0xFFCDB98C.toInt(), Shader.TileMode.CLAMP)
            c.drawRoundRect(r, fh * 0.01f, fh * 0.01f, paint)
            paint.shader = null
            text.textSize = r.height() * 0.5f; text.color = 0xFF4A2E1B.toInt()
            c.drawText(keyLabel[i], r.centerX(), r.centerY() + text.textSize * 0.36f, text)
        }
    }

    private fun drawLamp(c: Canvas, x: Float, y: Float, on: Boolean) {
        val r = window.height() * 0.05f
        paint.color = if (on) 0xFFFF2A30.toInt() else 0xFF3A0608.toInt()
        c.drawCircle(x, y, r, paint)
        if (on) { paint.color = 0x55FF2A30; c.drawCircle(x, y, r * 2f, paint) }
    }

    private fun drawDigit(c: Canvas, r: RectF, d: Int) {
        val on = 0xFFFF2A30.toInt(); val off = 0xFF3A0608.toInt()
        val t = r.width() * 0.16f
        val sk = r.width() * 0.12f   // slant
        fun seg(s: Int) = digits[d * 8 + s] > 64
        fun bar(x0: Float, y0: Float, x1: Float, y1: Float, lit: Boolean) {
            paint.color = if (lit) on else off
            paint.strokeWidth = t; paint.strokeCap = Paint.Cap.ROUND; paint.style = Paint.Style.STROKE
            c.drawLine(x0, y0, x1, y1, paint)
            paint.style = Paint.Style.FILL
        }
        val l = r.left + t; val rr = r.right - t; val top = r.top + t * 0.6f; val mid = r.centerY(); val bot = r.bottom - t * 0.6f
        fun sx(x: Float, y: Float) = x + sk * (mid - y) / (mid - top)
        bar(sx(l, top) + t * 0.6f, top, sx(rr, top) - t * 0.6f, top, seg(0))
        bar(sx(rr, top), top + t * 0.6f, sx(rr, mid), mid - t * 0.6f, seg(1))
        bar(sx(rr, mid), mid + t * 0.6f, sx(rr, bot), bot - t * 0.6f, seg(2))
        bar(sx(l, bot) + t * 0.6f, bot, sx(rr, bot) - t * 0.6f, bot, seg(3))
        bar(sx(l, mid), mid + t * 0.6f, sx(l, bot), bot - t * 0.6f, seg(4))
        bar(sx(l, top), top + t * 0.6f, sx(l, mid), mid - t * 0.6f, seg(5))
        bar(sx(l, mid) + t * 0.6f, mid, sx(rr, mid) - t * 0.6f, mid, seg(6))
        paint.color = if (seg(7)) on else off
        c.drawCircle(r.right + t * 0.2f, bot, t * 0.55f, paint)
    }

    // ---- touch ----

    private fun keyAt(x: Float, y: Float): Int {
        for (i in 0 until 16) {
            val r = keys[i]
            if (x >= r.left - r.width() * 0.08f && x <= r.right + r.width() * 0.08f && y >= r.top - r.height() * 0.15f && y <= r.bottom + r.height() * 0.15f)
                return 1 shl keyBit[i]
        }
        return 0
    }

    private fun pointAt(x: Float, y: Float): Int {
        if (bar.contains(x, y)) return 25
        if (offTray.contains(x, y)) return 26
        if (!board.contains(x, y)) return -1
        var best = -1; var bd = Float.MAX_VALUE
        val top = y < board.centerY()
        for (p in 1..24) {
            if (pointTop(p) != top) continue
            val d = abs(pointX(p) - x)
            if (d < bd) { bd = d; best = p }
        }
        return if (bd < pointW * 0.6f) best else -1
    }

    private fun takeFrom(p: Int, cream: Boolean): Boolean {
        when (p) {
            in 1..24 -> { if (points[p] == 0) return false; dragCream = points[p] > 0; return true }
            25 -> { if (barCream == 0 && barBrown == 0) return false; dragCream = if (cream) barCream > 0 else barBrown == 0; return true }
            26 -> { if (offCream == 0 && offBrown == 0) return false; dragCream = if (cream) offCream > 0 else offBrown == 0; return true }
        }
        return false
    }

    private fun remove(p: Int, cream: Boolean) {
        when (p) {
            in 1..24 -> points[p] += if (cream) -1 else 1
            25 -> if (cream) barCream-- else barBrown--
            26 -> if (cream) offCream-- else offBrown--
        }
    }

    private fun place(p: Int, cream: Boolean) {
        when (p) {
            in 1..24 -> {
                val v = points[p]
                if (v != 0 && (v > 0) != cream && abs(v) == 1) {     // a blot is hit: it goes to the bar
                    if (v > 0) barCream++ else barBrown++
                    points[p] = 0
                }
                if (points[p] == 0 || (points[p] > 0) == cream) points[p] += if (cream) 1 else -1
                else { place(dragFrom, cream); return }              // blocked: put it back
            }
            25 -> if (cream) barCream++ else barBrown++
            26 -> if (cream) offCream++ else offBrown++
        }
    }

    private fun updateKeys() {
        var m = 0
        for (v in pointers.values) m = m or v
        if (m != touchMask) {
            if (m and touchMask.inv() != 0) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            touchMask = m
            onInput?.invoke(m)
            invalidate()
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        val i = e.actionIndex
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val x = e.getX(i); val y = e.getY(i)
                val k = keyAt(x, y)
                if (k != 0) { pointers[e.getPointerId(i)] = k; updateKeys(); return true }
                if (e.actionMasked == MotionEvent.ACTION_DOWN) {
                    if (y < panel.top + panel.height() * 0.1f && x > panel.left) { onMenu?.invoke(); return true }
                    val p = pointAt(x, y)
                    if (p > 0 && takeFrom(p, y > board.centerY())) {
                        dragFrom = p; dragPos.set(x, y)    // drawn one short until it's dropped
                        invalidate()
                    } else if (p < 0 && frame.contains(x, y)) {
                        downTime = e.eventTime
                    }
                }
            }
            MotionEvent.ACTION_MOVE -> {
                if (dragFrom >= 0) { dragPos.set(e.x, e.y); invalidate() }
                for (j in 0 until e.pointerCount) {
                    val id = e.getPointerId(j)
                    if (pointers.containsKey(id)) pointers[id] = keyAt(e.getX(j), e.getY(j))
                }
                updateKeys()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                pointers.remove(e.getPointerId(i)); updateKeys()
                if (e.actionMasked == MotionEvent.ACTION_UP) {
                    if (dragFrom >= 0) {
                        val to = pointAt(e.x, e.y)
                        val from = dragFrom
                        remove(from, dragCream)
                        if (to > 0 && to != from) place(to, dragCream) else place(from, dragCream)
                        dragFrom = -1
                        performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                        invalidate()
                    } else if (downTime != 0L && e.eventTime - downTime > 700) {
                        setUp(); invalidate()
                    }
                    downTime = 0L
                }
            }
            MotionEvent.ACTION_CANCEL -> { pointers.clear(); updateKeys(); dragFrom = -1; invalidate() }
        }
        return true
    }
}
