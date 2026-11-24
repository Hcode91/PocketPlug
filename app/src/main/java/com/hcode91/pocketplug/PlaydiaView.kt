package com.hcode91.pocketplug

import android.content.Context
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RadialGradient
import android.graphics.RectF
import android.graphics.Shader
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import kotlin.math.abs
import kotlin.math.min

/** Button bits for the Playdia core (see playdia.h). B is the disc's "pick" button. */
object PdBits {
    const val UP = 1 shl 0; const val DOWN = 1 shl 1; const val LEFT = 1 shl 2; const val RIGHT = 1 shl 3
    const val A = 1 shl 4; const val B = 1 shl 5; const val START = 1 shl 6; const val SELECT = 1 shl 7

    fun keyBit(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> UP
        KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> DOWN
        KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> RIGHT
        // a pad's confirm button picks, as B does on the Playdia
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_K, KeyEvent.KEYCODE_X -> B
        KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_L, KeyEvent.KEYCODE_Z -> A
        KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_ENTER -> START
        KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_TAB -> SELECT
        else -> 0
    }
}

/**
 * The infrared pad: a rounded indigo-and-white body with a dark IR window along its top edge,
 * a cross pad on the left, round B and A keys on the right, and Select / Start between them.
 */
class PlaydiaPadView(ctx: Context) : View(ctx) {
    var onInput: ((Int) -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    private val body = RectF()
    private val cross = RectF()
    private val keyB = RectF(); private val keyA = RectF()
    private val sel = RectF(); private val start = RectF()
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; isFakeBoldText = true }
    private val down = HashMap<Int, Int>()
    private var mask = 0

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val bw = min(w * 0.96f, h * 2.1f)
        body.set((w - bw) / 2, h * 0.08f, (w + bw) / 2, h * 0.94f)
        val bh = body.height()
        val cs = bh * 0.56f
        val cx = body.left + bw * 0.24f; val cy = body.top + bh * 0.56f
        cross.set(cx - cs / 2, cy - cs / 2, cx + cs / 2, cy + cs / 2)
        val kr = bh * 0.13f
        val rx = body.left + bw * 0.76f
        keyB.set(rx - bw * 0.07f - kr, cy + bh * 0.06f - kr, rx - bw * 0.07f + kr, cy + bh * 0.06f + kr)
        keyA.set(rx + bw * 0.07f - kr, cy - bh * 0.1f - kr, rx + bw * 0.07f + kr, cy - bh * 0.1f + kr)
        val mw = bw * 0.1f; val mh = bh * 0.08f; val my = body.top + bh * 0.72f
        sel.set(body.centerX() - mw * 1.15f, my, body.centerX() - mw * 0.15f, my + mh)
        start.set(body.centerX() + mw * 0.15f, my, body.centerX() + mw * 1.15f, my + mh)
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0D0E1A.toInt())
        val lit = mask or externalMask
        val rad = body.height() * 0.42f
        // white lower shell, indigo top, dark IR window
        p.shader = LinearGradient(0f, body.top, 0f, body.bottom, 0xFFF4F4F2.toInt(), 0xFFC9CBD2.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, rad, rad, p)
        p.shader = LinearGradient(0f, body.top, 0f, body.top + body.height() * 0.3f, 0xFF4A4FC8.toInt(), 0xFF2B2E8E.toInt(), Shader.TileMode.CLAMP)
        c.save(); c.clipRect(body.left, body.top, body.right, body.top + body.height() * 0.26f)
        c.drawRoundRect(body, rad, rad, p); c.restore()
        p.shader = null
        p.color = 0xFF3A1016.toInt()
        val ir = RectF(body.centerX() - body.width() * 0.14f, body.top + body.height() * 0.05f, body.centerX() + body.width() * 0.14f, body.top + body.height() * 0.17f)
        c.drawRoundRect(ir, ir.height() / 2, ir.height() / 2, p)
        p.color = 0x55FF4050; c.drawRoundRect(RectF(ir.left + ir.width() * 0.1f, ir.top + ir.height() * 0.2f, ir.centerX(), ir.top + ir.height() * 0.45f), 8f, 8f, p)
        // cross pad in a recess
        p.color = 0xFFB4B7C2.toInt(); c.drawCircle(cross.centerX(), cross.centerY(), cross.width() * 0.6f, p)
        val arm = cross.width() / 3
        val path = Path().apply {
            addRect(cross.left, cross.top + arm, cross.right, cross.bottom - arm, Path.Direction.CW)
            addRect(cross.left + arm, cross.top, cross.right - arm, cross.bottom, Path.Direction.CW)
        }
        p.color = 0xFF23264F.toInt(); c.drawPath(path, p)
        p.color = 0xFF6A70E8.toInt()
        val half = arm / 2
        if (lit and PdBits.UP != 0) c.drawRect(cross.left + arm, cross.top, cross.right - arm, cross.top + arm, p)
        if (lit and PdBits.DOWN != 0) c.drawRect(cross.left + arm, cross.bottom - arm, cross.right - arm, cross.bottom, p)
        if (lit and PdBits.LEFT != 0) c.drawRect(cross.left, cross.top + arm, cross.left + arm, cross.bottom - arm, p)
        if (lit and PdBits.RIGHT != 0) c.drawRect(cross.right - arm, cross.top + arm, cross.right, cross.bottom - arm, p)
        p.color = 0xFF181A3A.toInt(); c.drawCircle(cross.centerX(), cross.centerY(), half * 0.5f, p)
        // round keys: B pink-red, A yellow
        drawKey(c, keyB, 0xFFE2365A.toInt(), "B", lit and PdBits.B != 0)
        drawKey(c, keyA, 0xFFF2BE1E.toInt(), "A", lit and PdBits.A != 0)
        drawPill(c, sel, "SELECT", lit and PdBits.SELECT != 0)
        drawPill(c, start, "START", lit and PdBits.START != 0)
    }

    private fun drawKey(c: Canvas, r: RectF, color: Int, label: String, on: Boolean) {
        p.color = 0x33000000; c.drawCircle(r.centerX(), r.centerY() + r.height() * 0.06f, r.width() / 2, p)
        p.shader = RadialGradient(r.centerX() - r.width() * 0.15f, r.centerY() - r.height() * 0.18f, r.width() * 0.7f,
            if (on) 0xFFFFFFFF.toInt() else lighten(color), color, Shader.TileMode.CLAMP)
        c.drawCircle(r.centerX(), r.centerY(), r.width() / 2, p)
        p.shader = null
        t.color = 0xFFFFFFFF.toInt(); t.textSize = r.height() * 0.45f
        c.drawText(label, r.centerX(), r.centerY() + t.textSize * 0.36f, t)
    }

    private fun drawPill(c: Canvas, r: RectF, label: String, on: Boolean) {
        p.color = if (on) 0xFF7A80F0.toInt() else 0xFF3B3F9E.toInt()
        c.drawRoundRect(r, r.height() / 2, r.height() / 2, p)
        t.color = 0xFF3B3F9E.toInt(); t.textSize = r.height() * 0.6f
        c.drawText(label, r.centerX(), r.bottom + t.textSize * 1.15f, t)
    }

    private fun lighten(c: Int): Int {
        val r = min(255, ((c shr 16) and 255) + 60); val g = min(255, ((c shr 8) and 255) + 60); val b = min(255, (c and 255) + 60)
        return (0xFF shl 24) or (r shl 16) or (g shl 8) or b
    }

    private fun hit(x: Float, y: Float): Int {
        val grow = { r: RectF, f: Float -> RectF(r).apply { inset(-r.width() * f, -r.height() * f) } }
        if (grow(keyB, 0.3f).contains(x, y)) return PdBits.B
        if (grow(keyA, 0.3f).contains(x, y)) return PdBits.A
        if (grow(sel, 0.4f).contains(x, y)) return PdBits.SELECT
        if (grow(start, 0.4f).contains(x, y)) return PdBits.START
        if (grow(cross, 0.25f).contains(x, y)) {
            val dx = x - cross.centerX(); val dy = y - cross.centerY()
            val dead = cross.width() * 0.1f
            var m = 0
            if (abs(dx) > dead && abs(dx) > abs(dy) * 0.5f) m = m or (if (dx < 0) PdBits.LEFT else PdBits.RIGHT)
            if (abs(dy) > dead && abs(dy) > abs(dx) * 0.5f) m = m or (if (dy < 0) PdBits.UP else PdBits.DOWN)
            return m
        }
        return 0
    }

    private fun publish() {
        val m = down.values.fold(0) { a, b -> a or b }
        if (m != mask) { mask = m; onInput?.invoke(m); invalidate() }
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        val i = e.actionIndex
        val id = e.getPointerId(i)
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> { down[id] = hit(e.getX(i), e.getY(i)); publish() }
            MotionEvent.ACTION_MOVE -> {
                for (k in 0 until e.pointerCount) {
                    val pid = e.getPointerId(k)
                    if (down.containsKey(pid)) down[pid] = hit(e.getX(k), e.getY(k))
                }
                publish()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                down.remove(id)
                if (e.actionMasked == MotionEvent.ACTION_CANCEL) down.clear()
                publish()
            }
        }
        return true
    }
}

/**
 * The console as a strip above the pad: an indigo-blue top over a green base, the disc lid on
 * the left, the hollow where the pad rests at the front, and Open / Power keys on the right.
 */
class PlaydiaConsoleView(ctx: Context) : View(ctx) {
    var onReset: (() -> Unit)? = null
    var onPower: (() -> Unit)? = null
    private val body = RectF(); private val lid = RectF(); private val dock = RectF()
    private val reset = RectF(); private val power = RectF()
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; color = 0xFFDDE0FF.toInt(); isFakeBoldText = true }
    private var pressed = 0

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val bw = min(w * 0.96f, h * 4.2f)
        body.set((w - bw) / 2, h * 0.1f, (w + bw) / 2, h * 0.92f)
        val bh = body.height()
        lid.set(body.left + bw * 0.05f, body.top + bh * 0.1f, body.left + bw * 0.5f, body.top + bh * 0.58f)
        dock.set(body.left + bw * 0.12f, body.top + bh * 0.66f, body.left + bw * 0.5f, body.bottom - bh * 0.06f)
        val kw = bw * 0.11f; val kh = bh * 0.2f
        reset.set(body.right - bw * 0.3f, body.top + bh * 0.3f, body.right - bw * 0.3f + kw, body.top + bh * 0.3f + kh)
        power.set(body.right - bw * 0.16f, body.top + bh * 0.3f, body.right - bw * 0.16f + kw, body.top + bh * 0.3f + kh)
        t.textSize = kh * 0.6f
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0D0E1A.toInt())
        val rad = body.height() * 0.16f
        p.shader = null
        p.color = 0xFF2E9C5A.toInt(); c.drawRoundRect(body, rad, rad, p)               // green base
        p.shader = LinearGradient(0f, body.top, 0f, body.top + body.height() * 0.62f, 0xFF5559D6.toInt(), 0xFF2D2F96.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(RectF(body.left, body.top, body.right, body.top + body.height() * 0.62f), rad, rad, p)
        p.shader = null
        p.color = 0xFF24267A.toInt(); c.drawRoundRect(lid, rad * 0.6f, rad * 0.6f, p)
        p.style = Paint.Style.STROKE; p.strokeWidth = body.height() * 0.02f; p.color = 0xFF7A7EEA.toInt()
        c.drawCircle(lid.centerX(), lid.centerY(), lid.height() * 0.32f, p)
        p.style = Paint.Style.FILL
        p.color = 0xFF1F6E3E.toInt(); c.drawRoundRect(dock, dock.height() * 0.4f, dock.height() * 0.4f, p)
        for ((r, label, bit) in listOf(Triple(reset, "RESET", 1), Triple(power, "POWER", 2))) {
            p.color = if (pressed and bit != 0) 0xFFFFFFFF.toInt() else 0xFFE9E6DA.toInt()
            c.drawRoundRect(r, r.height() * 0.3f, r.height() * 0.3f, p)
            c.drawText(label, r.centerX(), r.top - t.textSize * 0.4f, t)
        }
        p.color = 0xFFFF4A3C.toInt(); c.drawCircle(power.centerX(), power.bottom + body.height() * 0.12f, body.height() * 0.035f, p)
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        fun near(r: RectF) = RectF(r).apply { inset(-r.width() * 0.4f, -r.height() * 0.8f) }.contains(e.x, e.y)
        val bit = when { near(reset) -> 1; near(power) -> 2; else -> 0 }
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> { pressed = bit; invalidate() }
            MotionEvent.ACTION_UP -> {
                if (bit != 0 && bit == pressed) { if (bit == 1) onReset?.invoke() else onPower?.invoke() }
                pressed = 0; invalidate()
            }
            MotionEvent.ACTION_CANCEL -> { pressed = 0; invalidate() }
        }
        return true
    }
}
