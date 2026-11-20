package com.hcode91.pocketplug

import android.content.Context
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RadialGradient
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import kotlin.math.abs
import kotlin.math.min

/** Input bits of the arcade3d core (spg2xx board 3). */
object A3dBits {
    const val UP = 0x01; const val DOWN = 0x02; const val LEFT = 0x04; const val RIGHT = 0x08
    const val A = 0x10; const val B = 0x20; const val START = 0x40; const val C = 0x80; const val X = 0x100

    fun keyBit(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> UP
        KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> DOWN
        KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> RIGHT
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_Z -> A
        KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_X -> B
        KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_C -> C
        KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_V -> X
        KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_ENTER -> START
        else -> 0
    }
}

private fun Paint.lbl(size: Float, color: Int) = apply {
    textSize = size; this.color = color; textAlign = Paint.Align.CENTER
    typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
}

/**
 * The console, from the photo: a black 16-bit-style deck with a vented left
 * side, two slider switches, the round dish in the middle with the cartridge
 * tab, and a row of green read-outs along the front.
 */
class Arcade3dConsoleView(ctx: Context) : View(ctx) {
    var onReset: (() -> Unit)? = null
    var onPower: (() -> Unit)? = null
    private val body = RectF()
    private val power = RectF()
    private val reset = RectF()
    private var pressed = 0
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG)

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        body.set(w * 0.04f, h * 0.06f, w * 0.96f, h * 0.94f)
        val sw = body.width() * 0.05f; val sh = body.height() * 0.32f
        power.set(body.left + body.width() * 0.12f, body.top + body.height() * 0.2f, body.left + body.width() * 0.12f + sw, body.top + body.height() * 0.2f + sh)
        reset.set(power.right + sw * 0.8f, power.top + sh * 0.35f, power.right + sw * 1.8f + sw, power.top + sh * 0.35f + sh * 0.35f)
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0814.toInt())
        val r = body.height() * 0.08f
        p.shader = LinearGradient(0f, body.top, 0f, body.bottom, 0xFF2B2C30.toInt(), 0xFF101012.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, r, r, p)
        p.shader = null
        // vents on the left
        p.color = 0xFF08080A.toInt()
        for (i in 0 until 8) {
            val x = body.left + body.width() * (0.015f + i * 0.011f)
            c.drawRect(x, body.top + body.height() * 0.1f, x + body.width() * 0.005f, body.bottom - body.height() * 0.32f, p)
        }
        // slider switches
        p.color = 0xFF050506.toInt()
        c.drawRoundRect(power, 4f, 4f, p)
        c.drawRoundRect(reset, 4f, 4f, p)
        p.color = if (pressed == 2) 0xFF9A9CA2.toInt() else 0xFF55575D.toInt()
        c.drawRect(power.left + 3, power.top + power.height() * 0.1f, power.right - 3, power.top + power.height() * 0.42f, p)
        p.color = if (pressed == 1) 0xFF9A9CA2.toInt() else 0xFF55575D.toInt()
        c.drawRect(reset.left + reset.width() * 0.15f, reset.top + 3, reset.left + reset.width() * 0.55f, reset.bottom - 3, p)
        t.lbl(body.height() * 0.07f, 0xFF9A9CA4.toInt())
        c.drawText("POWER", power.centerX(), power.bottom + t.textSize * 1.2f, t)
        c.drawText("RESET", reset.centerX(), reset.bottom + t.textSize * 1.2f, t)
        // the dish
        val cx = body.centerX() + body.width() * 0.06f; val cy = body.top + body.height() * 0.4f
        val dr = body.height() * 0.33f
        p.shader = RadialGradient(cx, cy - dr * 0.3f, dr * 1.4f, 0xFF35363B.toInt(), 0xFF0C0C0E.toInt(), Shader.TileMode.CLAMP)
        c.drawOval(cx - dr * 1.9f, cy - dr, cx + dr * 1.9f, cy + dr, p)
        p.shader = null
        p.style = Paint.Style.STROKE; p.strokeWidth = 2f; p.color = 0xFF46474D.toInt()
        c.drawOval(cx - dr * 1.9f, cy - dr, cx + dr * 1.9f, cy + dr, p)
        c.drawOval(cx - dr * 1.3f, cy - dr * 0.68f, cx + dr * 1.3f, cy + dr * 0.68f, p)
        p.style = Paint.Style.FILL
        // cartridge slot and tab
        p.color = 0xFF050506.toInt()
        c.drawRoundRect(cx - dr * 1.2f, cy - dr * 0.95f, cx + dr * 1.2f, cy - dr * 0.75f, 4f, 4f, p)
        val tab = RectF(cx - dr * 0.45f, cy - dr * 0.25f, cx + dr * 0.45f, cy + dr * 0.55f)
        p.color = 0xFF1C1C1F.toInt(); c.drawRoundRect(tab, 6f, 6f, p)
        p.color = 0xFF2E2F33.toInt(); c.drawRoundRect(RectF(tab).apply { inset(tab.width() * 0.1f, tab.height() * 0.12f) }, 4f, 4f, p)
        p.color = 0xFFE23A2E.toInt(); c.drawCircle(cx, tab.bottom - tab.height() * 0.08f, body.height() * 0.012f, p)
        // green read-outs along the front
        val fy = body.bottom - body.height() * 0.2f
        val fw = body.width() * 0.13f
        for (i in 0 until 6) {
            val x = body.left + body.width() * (0.08f + i * 0.145f)
            p.color = 0xFF050806.toInt(); c.drawRect(x, fy, x + fw, fy + body.height() * 0.1f, p)
            p.color = 0xFF2C6E33.toInt()
            for (d in 0 until 5) c.drawRect(x + fw * (0.08f + d * 0.18f), fy + body.height() * 0.025f, x + fw * (0.2f + d * 0.18f), fy + body.height() * 0.075f, p)
        }
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val hit = { b: RectF -> RectF(b).apply { inset(-b.width() * 0.8f, -b.height() * 0.3f) }.contains(e.x, e.y) }
                pressed = if (hit(reset)) 1 else if (hit(power)) 2 else 0
                if (pressed != 0) {
                    performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
                    if (pressed == 1) onReset?.invoke() else onPower?.invoke()
                    invalidate()
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> if (pressed != 0) { pressed = 0; invalidate() }
        }
        return true
    }
}

/** The console's black pad: d-pad, START, and the A B C buttons with a small X above. */
class Arcade3dPadView(ctx: Context) : View(ctx) {
    var onInput: ((Int) -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; invalidate() }
    private class Btn(val label: String, val bit: Int) { var x = 0f; var y = 0f; var r = 0f }
    private val btns = listOf(Btn("A", A3dBits.A), Btn("B", A3dBits.B), Btn("C", A3dBits.C), Btn("X", A3dBits.X))
    private val body = RectF()
    private val start = RectF()
    private var dcx = 0f; private var dcy = 0f; private var dr = 0f
    private var mask = 0
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG)

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val bw = min(w * 0.96f, h * 2.1f)
        body.set((w - bw) / 2, h * 0.08f, (w + bw) / 2, h * 0.92f)
        dr = body.height() * 0.28f
        dcx = body.left + body.width() * 0.22f; dcy = body.centerY()
        val br = body.height() * 0.11f
        val bx = body.left + body.width() * 0.66f; val by = body.centerY() + body.height() * 0.08f
        btns[0].apply { x = bx; y = by + br * 0.6f; r = br }
        btns[1].apply { x = bx + br * 2.5f; y = by; r = br }
        btns[2].apply { x = bx + br * 5f; y = by - br * 0.6f; r = br }
        btns[3].apply { x = bx + br * 2.5f; y = by - br * 2.6f; r = br * 0.6f }
        start.set(body.centerX() - body.width() * 0.06f, body.centerY() + body.height() * 0.05f, body.centerX() + body.width() * 0.06f, body.centerY() + body.height() * 0.15f)
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0814.toInt())
        val lit = mask or externalMask
        // the pad's shape: two rounded grips joined by the middle
        val path = Path().apply {
            addCircle(body.left + body.height() * 0.5f, body.centerY(), body.height() * 0.5f, Path.Direction.CW)
            addCircle(body.right - body.height() * 0.5f, body.centerY(), body.height() * 0.5f, Path.Direction.CW)
            addRoundRect(RectF(body.left + body.height() * 0.4f, body.top + body.height() * 0.04f, body.right - body.height() * 0.4f, body.bottom - body.height() * 0.1f), 30f, 30f, Path.Direction.CW)
        }
        p.shader = LinearGradient(0f, body.top, 0f, body.bottom, 0xFF2D2E33.toInt(), 0xFF0E0E10.toInt(), Shader.TileMode.CLAMP)
        c.drawPath(path, p)
        p.shader = null
        // d-pad
        p.color = 0xFF0A0A0C.toInt()
        c.drawCircle(dcx, dcy, dr * 1.12f, p)
        val arm = dr * 0.36f
        for ((i, bit) in listOf(A3dBits.UP, A3dBits.RIGHT, A3dBits.DOWN, A3dBits.LEFT).withIndex()) {
            val on = (lit and bit) != 0
            p.color = if (on) 0xFF5A5C64.toInt() else 0xFF26272C.toInt()
            when (i) {
                0 -> c.drawRect(dcx - arm, dcy - dr, dcx + arm, dcy, p)
                1 -> c.drawRect(dcx, dcy - arm, dcx + dr, dcy + arm, p)
                2 -> c.drawRect(dcx - arm, dcy, dcx + arm, dcy + dr, p)
                else -> c.drawRect(dcx - dr, dcy - arm, dcx, dcy + arm, p)
            }
        }
        // start
        val sOn = (lit and A3dBits.START) != 0
        p.color = if (sOn) 0xFF6A6C74.toInt() else 0xFF2E2F34.toInt()
        c.drawRoundRect(start, start.height() / 2, start.height() / 2, p)
        t.lbl(start.height() * 0.6f, 0xFF8E9098.toInt())
        c.drawText("START", start.centerX(), start.bottom + t.textSize * 1.1f, t)
        // buttons
        for (b in btns) {
            val on = (lit and b.bit) != 0
            p.shader = RadialGradient(b.x - b.r * 0.3f, b.y - b.r * 0.3f, b.r * 1.3f,
                if (on) 0xFF7A7C84.toInt() else 0xFF45464C.toInt(), 0xFF111113.toInt(), Shader.TileMode.CLAMP)
            c.drawCircle(b.x, b.y, b.r, p)
            p.shader = null
            t.lbl(b.r * 0.8f, 0xFF9EA0A8.toInt())
            c.drawText(b.label, b.x, b.y + b.r + t.textSize * 1.05f, t)
        }
    }

    private fun bitsAt(x: Float, y: Float): Int {
        val dx = x - dcx; val dy = y - dcy
        if (dx * dx + dy * dy < dr * dr * 1.6f) {
            var m = 0
            if (abs(dx) < dr * 0.2f && abs(dy) < dr * 0.2f) return 0
            if (dy < 0 && abs(dy) > abs(dx) * 0.42f) m = m or A3dBits.UP
            if (dy > 0 && abs(dy) > abs(dx) * 0.42f) m = m or A3dBits.DOWN
            if (dx < 0 && abs(dx) > abs(dy) * 0.42f) m = m or A3dBits.LEFT
            if (dx > 0 && abs(dx) > abs(dy) * 0.42f) m = m or A3dBits.RIGHT
            return m
        }
        if (RectF(start).apply { inset(-12f, -14f) }.contains(x, y)) return A3dBits.START
        for (b in btns) { val ex = x - b.x; val ey = y - b.y; if (ex * ex + ey * ey < b.r * b.r * 1.7f) return b.bit }
        return 0
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        val up = when (e.actionMasked) {
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> e.getPointerId(e.actionIndex)
            MotionEvent.ACTION_CANCEL -> -2
            else -> -1
        }
        var m = 0
        if (up != -2) for (i in 0 until e.pointerCount) if (e.getPointerId(i) != up) m = m or bitsAt(e.getX(i), e.getY(i))
        if (m != mask) {
            if ((m and mask.inv()) != 0) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
            mask = m
            onInput?.invoke(m)
            invalidate()
        }
        return true
    }
}
