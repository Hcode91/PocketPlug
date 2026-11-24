package com.hcode91.pocketplug

import android.content.Context
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RadialGradient
import android.graphics.RectF
import android.graphics.Shader
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import kotlin.math.hypot
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
 * The infrared pad: a royal-blue slab with a black IR window on its top edge, four round
 * direction keys in a diamond on the left (purple up, blue left, green right, red down) and the
 * yellow A and white B keys on the right. The real pad has no Start or Select.
 */
class PlaydiaPadView(ctx: Context) : View(ctx) {
    var onInput: ((Int) -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    private class Key(val bit: Int, val color: Int, val label: String) { var x = 0f; var y = 0f; var r = 0f }
    private val keys = listOf(
        Key(PdBits.UP, 0xFF8B3FC8.toInt(), ""), Key(PdBits.LEFT, 0xFF1F86E0.toInt(), ""),
        Key(PdBits.RIGHT, 0xFF18A860.toInt(), ""), Key(PdBits.DOWN, 0xFFE0243A.toInt(), ""),
        Key(PdBits.A, 0xFFF5D21C.toInt(), "A"), Key(PdBits.B, 0xFFF4F4F0.toInt(), "B"),
    )
    private val body = RectF(); private val ir = RectF()
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; isFakeBoldText = true }
    private val down = HashMap<Int, Int>()
    private var mask = 0

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val bw = min(w * 0.96f, h * 2.3f)
        body.set((w - bw) / 2, h * 0.1f, (w + bw) / 2, h * 0.92f)
        val bh = body.height()
        ir.set(body.centerX() - bw * 0.09f, body.top - bh * 0.02f, body.centerX() + bw * 0.09f, body.top + bh * 0.1f)
        val cx = body.left + bw * 0.25f; val cy = body.top + bh * 0.56f
        val d = bh * 0.23f; val r = bh * 0.115f
        val pos = listOf(0f to -d, -d to 0f, d to 0f, 0f to d)
        for (k in 0 until 4) { keys[k].x = cx + pos[k].first; keys[k].y = cy + pos[k].second; keys[k].r = r }
        // A sits lower-left of B, as on the pad
        keys[4].x = body.left + bw * 0.66f; keys[4].y = body.top + bh * 0.62f; keys[4].r = bh * 0.14f
        keys[5].x = body.left + bw * 0.84f; keys[5].y = body.top + bh * 0.46f; keys[5].r = bh * 0.14f
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0D0E1A.toInt())
        val lit = mask or externalMask
        val rad = body.height() * 0.2f
        // a shade underneath gives the slab its thickness
        p.shader = null; p.color = 0xFF1E2A8C.toInt()
        c.drawRoundRect(RectF(body.left, body.top + body.height() * 0.06f, body.right, body.bottom + body.height() * 0.04f), rad, rad, p)
        p.shader = LinearGradient(0f, body.top, 0f, body.bottom, 0xFF5A72F0.toInt(), 0xFF3448C8.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, rad, rad, p)
        p.shader = null
        p.color = 0x22FFFFFF
        c.drawRoundRect(RectF(body.left + body.width() * 0.02f, body.top + body.height() * 0.03f, body.right - body.width() * 0.02f, body.top + body.height() * 0.12f), rad, rad, p)
        p.color = 0xFF101014.toInt(); c.drawRoundRect(ir, ir.height() * 0.25f, ir.height() * 0.25f, p)
        p.color = 0x44FF3040; c.drawRect(ir.left + ir.width() * 0.1f, ir.top + ir.height() * 0.3f, ir.left + ir.width() * 0.45f, ir.top + ir.height() * 0.5f, p)
        // little arrows beside the direction keys
        t.color = 0xFFD8DEFF.toInt()
        val k0 = keys[0]
        t.textSize = k0.r * 0.7f
        c.drawText("▲", k0.x, k0.y - k0.r * 1.35f, t)
        c.drawText("▼", keys[3].x, keys[3].y + keys[3].r * 2.0f, t)
        c.drawText("◀", keys[1].x - keys[1].r * 1.6f, keys[1].y + t.textSize * 0.35f, t)
        c.drawText("▶", keys[2].x + keys[2].r * 1.6f, keys[2].y + t.textSize * 0.35f, t)
        for (k in keys) {
            val on = lit and k.bit != 0
            p.color = 0x55000000; c.drawCircle(k.x, k.y + k.r * 0.14f, k.r, p)
            p.shader = RadialGradient(k.x - k.r * 0.3f, k.y - k.r * 0.35f, k.r * 1.4f,
                if (on) 0xFFFFFFFF.toInt() else lighten(k.color), if (on) lighten(k.color) else k.color, Shader.TileMode.CLAMP)
            c.drawCircle(k.x, k.y, k.r, p)
            p.shader = null
            if (k.label.isNotEmpty()) {
                t.color = 0xFFF4F4F0.toInt(); t.textSize = k.r * 0.62f
                c.drawText(k.label, k.x, k.y - k.r * 1.3f, t)
            }
        }
    }

    private fun lighten(c: Int): Int {
        val r = min(255, ((c shr 16) and 255) + 60); val g = min(255, ((c shr 8) and 255) + 60); val b = min(255, (c and 255) + 60)
        return (0xFF shl 24) or (r shl 16) or (g shl 8) or b
    }

    private fun hit(x: Float, y: Float): Int {
        var best = 0; var bestD = Float.MAX_VALUE
        for (k in keys) {
            val d = hypot(x - k.x, y - k.y)
            if (d < k.r * 1.6f && d < bestD) { bestD = d; best = k.bit }
        }
        return best
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
 * The console as a strip above the pad: a royal-blue top on a green base, the big round disc lid
 * with its ridges, two green keys (Reset, Power) on the left with a red power light, the yellow
 * round Open button on the right, and the front hollow where the pad rests, with its black IR eye.
 */
class PlaydiaConsoleView(ctx: Context) : View(ctx) {
    var onReset: (() -> Unit)? = null
    var onPower: (() -> Unit)? = null
    private val body = RectF(); private val top = RectF(); private val dock = RectF()
    private val reset = RectF(); private val power = RectF()
    private var lidX = 0f; private var lidY = 0f; private var lidR = 0f
    private var openX = 0f; private var openY = 0f; private var openR = 0f
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; color = 0xFFDDE3FF.toInt(); isFakeBoldText = true }
    private var pressed = 0

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val bw = min(w * 0.96f, h * 4.2f)
        body.set((w - bw) / 2, h * 0.08f, (w + bw) / 2, h * 0.94f)
        val bh = body.height()
        top.set(body.left, body.top, body.right, body.top + bh * 0.8f)
        lidR = bh * 0.34f; lidX = body.left + bw * 0.55f; lidY = body.top + bh * 0.38f
        val kw = bw * 0.085f; val kh = bh * 0.13f
        reset.set(body.left + bw * 0.24f, body.top + bh * 0.14f, body.left + bw * 0.24f + kw, body.top + bh * 0.14f + kh)
        power.set(body.left + bw * 0.2f, body.top + bh * 0.36f, body.left + bw * 0.2f + kw, body.top + bh * 0.36f + kh)
        dock.set(body.left + bw * 0.12f, body.top + bh * 0.56f, body.left + bw * 0.46f, body.top + bh * 0.76f)
        openR = bh * 0.1f; openX = body.left + bw * 0.86f; openY = body.top + bh * 0.58f
        t.textSize = kh * 0.55f
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0D0E1A.toInt())
        val rad = body.height() * 0.14f
        p.shader = null
        p.color = 0xFF22A85C.toInt(); c.drawRoundRect(body, rad, rad, p)                  // green base
        p.color = 0xFF1A8048.toInt(); c.drawRect(body.left + rad, body.bottom - body.height() * 0.06f, body.right - rad, body.bottom - body.height() * 0.03f, p)
        p.shader = LinearGradient(0f, top.top, 0f, top.bottom, 0xFF5470EE.toInt(), 0xFF3047C6.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(top, rad, rad, p)
        p.shader = null
        // disc lid: a raised disc with curved ridges
        p.color = 0xFF3A52D0.toInt(); c.drawCircle(lidX, lidY, lidR, p)
        p.style = Paint.Style.STROKE; p.strokeWidth = body.height() * 0.012f
        p.color = 0xFF2A3CA8.toInt(); c.drawCircle(lidX, lidY, lidR, p)
        p.color = 0x55FFFFFF
        for (k in 0 until 3) {
            val off = lidR * (0.1f + k * 0.16f)
            c.drawArc(RectF(lidX - lidR * 0.55f, lidY - off - lidR * 0.25f, lidX + lidR * 0.55f, lidY - off + lidR * 0.25f), 200f, 140f, false, p)
        }
        p.style = Paint.Style.FILL
        // the hollow where the pad sits, with the receiver's black eye at its back
        p.color = 0xFF2638A0.toInt(); c.drawRoundRect(dock, dock.height() * 0.3f, dock.height() * 0.3f, p)
        p.color = 0xFF101014.toInt()
        c.drawRect(dock.centerX() + dock.width() * 0.12f, dock.top + dock.height() * 0.1f, dock.centerX() + dock.width() * 0.22f, dock.bottom - dock.height() * 0.25f, p)
        // green keys with labels, red light
        for ((r, label, bit) in listOf(Triple(reset, "RESET", 1), Triple(power, "POWER", 2))) {
            p.color = if (pressed and bit != 0) 0xFF8CFFC0.toInt() else 0xFF2FC878.toInt()
            c.drawRoundRect(r, r.height() * 0.3f, r.height() * 0.3f, p)
            p.color = 0x33000000; c.drawRect(r.left, r.bottom - r.height() * 0.25f, r.right, r.bottom, p)
            c.drawText(label, r.right + t.textSize * 2.1f, r.centerY() + t.textSize * 0.35f, t)
        }
        p.color = 0xFFFF3A2C.toInt(); c.drawCircle(power.left - power.width() * 0.35f, power.centerY(), body.height() * 0.025f, p)
        // yellow Open button
        p.shader = RadialGradient(openX - openR * 0.3f, openY - openR * 0.35f, openR * 1.4f, 0xFFFFF07A.toInt(), 0xFFE8B800.toInt(), Shader.TileMode.CLAMP)
        c.drawCircle(openX, openY, openR, p)
        p.shader = null
        t.textSize = openR * 0.5f; c.drawText("OPEN", openX, openY + openR * 1.75f, t)
        t.textSize = reset.height() * 0.55f
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        fun near(r: RectF) = RectF(r).apply { inset(-r.width() * 0.4f, -r.height() * 0.6f) }.contains(e.x, e.y)
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
