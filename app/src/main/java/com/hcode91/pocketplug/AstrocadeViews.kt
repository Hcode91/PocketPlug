package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RadialGradient
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import kotlin.math.PI
import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.min

/** Input bits of the Astrocade core (astrocde.h). */
object AstroBits {
    const val UP = 0x01; const val DOWN = 0x02; const val LEFT = 0x04; const val RIGHT = 0x08
    const val TRIGGER = 0x10
    /** keypad key in [col] (0 = left) and [row] (0 = top) */
    fun key(col: Int, row: Int) = 1 shl (8 + (3 - col) * 6 + row)

    fun keyBit(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> UP
        KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> DOWN
        KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> RIGHT
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_SPACE -> TRIGGER
        KeyEvent.KEYCODE_1 -> key(0, 4); KeyEvent.KEYCODE_2 -> key(1, 4); KeyEvent.KEYCODE_3 -> key(2, 4)
        KeyEvent.KEYCODE_4 -> key(0, 3); KeyEvent.KEYCODE_5 -> key(1, 3); KeyEvent.KEYCODE_6 -> key(2, 3)
        KeyEvent.KEYCODE_7 -> key(0, 2); KeyEvent.KEYCODE_8 -> key(1, 2); KeyEvent.KEYCODE_9 -> key(2, 2)
        KeyEvent.KEYCODE_0 -> key(1, 5)
        KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_BUTTON_START -> key(3, 5)   // =
        else -> 0
    }

    /** calculator legends, top row first */
    val LABELS = arrayOf(
        arrayOf("C", "↑", "↓", "%"),
        arrayOf("MR", "MS", "CH", "÷"),
        arrayOf("7", "8", "9", "×"),
        arrayOf("4", "5", "6", "−"),
        arrayOf("1", "2", "3", "+"),
        arrayOf("CE", "0", ".", "="),
    )
}

private fun Paint.label(size: Float, color: Int) = apply {
    textSize = size; this.color = color; textAlign = Paint.Align.CENTER
    typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
}

/**
 * The Astrocade console's front, from the photo: dark brown case with the gold trim line,
 * the cream 24-key keypad (calculator legends), the cartridge slot with the cartridge
 * playing, and the RESET and EJECT keys. No maker's name or logo is drawn.
 */
class AstrocadeConsoleView(ctx: Context, private val cartTitle: String) : View(ctx) {
    var onKeys: ((Int) -> Unit)? = null
    var onReset: (() -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG)
    private val body = RectF(); private val pad = RectF(); private val slot = RectF()
    private val reset = RectF(); private val eject = RectF()
    private val keys = Array(24) { RectF() }
    private val pointers = HashMap<Int, Int>()
    private var mask = 0
    private var resetDown = false

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        body.set(w * 0.02f, h * 0.03f, w * 0.98f, h * 0.97f)
        pad.set(body.left + w * 0.04f, body.top + h * 0.07f, body.left + w * 0.04f + min(w * 0.46f, h * 0.62f), body.bottom - h * 0.07f)
        val kw = pad.width() / 4f; val kh = pad.height() / 6f
        for (r in 0 until 6) for (c in 0 until 4) {
            val cx = pad.left + kw * (c + 0.5f); val cy = pad.top + kh * (r + 0.5f)
            keys[r * 4 + c].set(cx - kw * 0.38f, cy - kh * 0.32f, cx + kw * 0.38f, cy + kh * 0.32f)
        }
        val rx = pad.right + w * 0.04f
        slot.set(rx, body.top + h * 0.12f, body.right - w * 0.04f, body.top + h * 0.58f)
        val bw = slot.width() * 0.28f
        reset.set(slot.left + slot.width() * 0.1f, slot.bottom + h * 0.1f, slot.left + slot.width() * 0.1f + bw, slot.bottom + h * 0.24f)
        eject.set(reset.right + slot.width() * 0.12f, reset.top, reset.right + slot.width() * 0.12f + bw, reset.bottom)
    }

    override fun onDraw(c: Canvas) {
        p.shader = LinearGradient(0f, body.top, 0f, body.bottom, 0xFF3A2418.toInt(), 0xFF1C110A.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, 18f, 18f, p); p.shader = null
        // the gold trim line
        p.style = Paint.Style.STROKE; p.strokeWidth = 4f; p.color = 0xFFC8A060.toInt()
        c.drawRoundRect(body.left + 10f, body.top + 10f, body.right - 10f, body.bottom - 10f, 12f, 12f, p)
        p.style = Paint.Style.FILL
        // keypad plate
        p.color = 0xFF1A1008.toInt(); c.drawRoundRect(pad.left - 10f, pad.top - 10f, pad.right + 10f, pad.bottom + 10f, 10f, 10f, p)
        p.color = 0xFFE8DCC0.toInt(); c.drawRoundRect(pad, 8f, 8f, p)
        val m = mask or externalMask
        for (r in 0 until 6) for (col in 0 until 4) {
            val k = keys[r * 4 + col]
            val down = m and AstroBits.key(col, r) != 0
            p.color = 0xFF8A7A60.toInt(); c.drawRoundRect(k.left, k.top + 3f, k.right, k.bottom + 3f, 6f, 6f, p)
            p.color = if (down) 0xFF5A4A34.toInt() else if (col == 3 || r == 0) 0xFF7A4A2A.toInt() else 0xFF3A2A1E.toInt()
            val o = if (down) 3f else 0f
            c.drawRoundRect(k.left, k.top + o, k.right, k.bottom + o, 6f, 6f, p)
            t.label(k.height() * 0.55f, 0xFFF2E8D0.toInt())
            c.drawText(AstroBits.LABELS[r][col], k.centerX(), k.centerY() + o + t.textSize * 0.36f, t)
        }
        // cartridge slot with the cartridge's label
        p.color = 0xFF0A0604.toInt(); c.drawRoundRect(slot, 8f, 8f, p)
        val cart = RectF(slot.left + slot.width() * 0.08f, slot.top + slot.height() * 0.3f, slot.right - slot.width() * 0.08f, slot.bottom - slot.height() * 0.12f)
        if (cartTitle.isNotEmpty()) {
            p.color = 0xFF202020.toInt(); c.drawRoundRect(cart, 6f, 6f, p)
            p.color = 0xFFF0ECE4.toInt(); c.drawRect(cart.left + 6f, cart.top + 6f, cart.right - 6f, cart.bottom - 6f, p)
            t.label(cart.height() * 0.28f, 0xFF2A2A2A.toInt())
            c.drawText(cartTitle.take(22), cart.centerX(), cart.centerY() + t.textSize * 0.36f, t)
        } else {
            p.color = 0xFF2A1A10.toInt(); c.drawRect(cart.left, cart.centerY() - 4f, cart.right, cart.centerY() + 4f, p)
        }
        for ((r, s) in listOf(reset to "RESET", eject to "EJECT")) {
            val down = r === reset && resetDown
            p.color = 0xFF0A0604.toInt(); c.drawRoundRect(r.left - 4f, r.top - 4f, r.right + 4f, r.bottom + 6f, 8f, 8f, p)
            p.color = if (down) 0xFF2A2018.toInt() else 0xFF4A3A2A.toInt(); c.drawRoundRect(r, 6f, 6f, p)
            t.label(r.height() * 0.42f, 0xFFE8D8B8.toInt())
            c.drawText(s, r.centerX(), r.centerY() + t.textSize * 0.36f, t)
        }
    }

    private fun keyAt(x: Float, y: Float): Int {
        for (r in 0 until 6) for (col in 0 until 4) {
            val k = RectF(keys[r * 4 + col]).apply { inset(-width() * 0.12f, -height() * 0.2f) }
            if (k.contains(x, y)) return AstroBits.key(col, r)
        }
        return 0
    }

    private fun update() {
        var m = 0
        for (v in pointers.values) m = m or v
        if (m != mask) {
            if (m and mask.inv() != 0) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            mask = m; onKeys?.invoke(m); invalidate()
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex; val x = e.getX(i); val y = e.getY(i)
                val k = keyAt(x, y)
                if (k != 0) { pointers[e.getPointerId(i)] = k; update() }
                else if (RectF(reset).apply { inset(-20f, -20f) }.contains(x, y)) {
                    resetDown = true; performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); invalidate(); onReset?.invoke()
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                pointers.remove(e.getPointerId(e.actionIndex)); resetDown = false; update(); invalidate()
            }
            MotionEvent.ACTION_CANCEL -> { pointers.clear(); resetDown = false; update(); invalidate() }
        }
        return true
    }
}

/**
 * The pistol-grip hand controller seen from above: the knob on top is both an 8-way
 * stick (drag inside it) and a paddle (drag round the ridged ring to turn it; it stays
 * where it is left, as the real one does), and the trigger on the grip.
 */
class AstrocadeControllerView(ctx: Context) : View(ctx) {
    var onInput: ((Int) -> Unit)? = null
    var onKnob: ((Float) -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG)
    private var cx = 0f; private var cy = 0f; private var ring = 0f; private var head = 0f
    private val trigger = RectF()
    private var dir = 0
    private var trig = false
    private var knob = 0f          // -1 .. 1 (centre 0), about 270 degrees of travel
    private var stickPtr = -1; private var ringPtr = -1; private var trigPtr = -1
    private var ringLast = 0.0

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        ring = min(w * 0.28f, h * 0.42f)
        head = ring * 0.62f
        cx = w * 0.36f; cy = h * 0.5f
        trigger.set(w * 0.7f, h * 0.28f, w * 0.92f, h * 0.72f)
    }

    override fun onDraw(c: Canvas) {
        // the black grip
        p.color = 0xFF141414.toInt()
        c.drawRoundRect(cx - ring * 1.25f, cy - ring * 1.2f, trigger.right + 20f, cy + ring * 1.2f, ring * 0.5f, ring * 0.5f, p)
        p.color = 0xFF1E1E1E.toInt()
        for (i in 0 until 10) c.drawRect(cx + ring * 1.15f, cy - ring + i * ring * 0.2f, trigger.left - 16f, cy - ring + i * ring * 0.2f + ring * 0.08f, p)
        // the ridged ring that turns
        p.color = 0xFF5A2A10.toInt(); c.drawCircle(cx, cy, ring, p)
        p.color = 0xFF3A1A08.toInt()
        val turn = knob * 135.0
        for (i in 0 until 36) {
            val a = Math.toRadians(i * 10.0 + turn)
            c.drawCircle(cx + (ring * 0.9f) * Math.cos(a).toFloat(), cy + (ring * 0.9f) * Math.sin(a).toFloat(), ring * 0.05f, p)
        }
        // pointer mark on the ring
        val ma = Math.toRadians(-90.0 + turn)
        p.color = 0xFFF0E0C0.toInt(); c.drawCircle(cx + ring * 0.78f * Math.cos(ma).toFloat(), cy + ring * 0.78f * Math.sin(ma).toFloat(), ring * 0.06f, p)
        // the knob head, pushed the way of the stick
        val m = dir or (externalMask and 0x0f)
        val dx = (if (m and AstroBits.RIGHT != 0) 1 else 0) - (if (m and AstroBits.LEFT != 0) 1 else 0)
        val dy = (if (m and AstroBits.DOWN != 0) 1 else 0) - (if (m and AstroBits.UP != 0) 1 else 0)
        val hx = cx + dx * head * 0.18f; val hy = cy + dy * head * 0.18f
        p.shader = RadialGradient(hx - head * 0.3f, hy - head * 0.3f, head * 1.3f, 0xFFD8783A.toInt(), 0xFF7A3410.toInt(), Shader.TileMode.CLAMP)
        c.drawCircle(hx, hy, head, p); p.shader = null
        // the trigger
        val down = trig || (externalMask and AstroBits.TRIGGER != 0)
        p.color = 0xFF080808.toInt(); c.drawRoundRect(trigger.left - 6f, trigger.top - 6f, trigger.right + 6f, trigger.bottom + 10f, 30f, 30f, p)
        p.color = if (down) 0xFF2A2A2A.toInt() else 0xFF464646.toInt()
        c.drawRoundRect(trigger.left, trigger.top + (if (down) 6f else 0f), trigger.right, trigger.bottom + (if (down) 6f else 0f), 26f, 26f, p)
        t.label(trigger.width() * 0.2f, 0xFFCCCCCC.toInt())
        c.drawText("TRIGGER", trigger.centerX(), trigger.bottom + t.textSize * 1.4f, t)
        t.label(ring * 0.14f, 0xFF9A9A9A.toInt())
        c.drawText("TURN", cx, cy + ring + t.textSize * 1.3f, t)
    }

    private fun stickBits(x: Float, y: Float): Int {
        val ddx = x - cx; val ddy = y - cy
        if (hypot(ddx, ddy) < head * 0.25f) return 0
        val a = Math.toDegrees(atan2(ddy.toDouble(), ddx.toDouble()))
        var b = 0
        if (a > -157.5 && a < -22.5) b = b or AstroBits.UP
        if (a > 22.5 && a < 157.5) b = b or AstroBits.DOWN
        if (a > 112.5 || a < -112.5) b = b or AstroBits.LEFT
        if (a > -67.5 && a < 67.5) b = b or AstroBits.RIGHT
        return b
    }

    private fun send() { onInput?.invoke(dir or (if (trig) AstroBits.TRIGGER else 0)); invalidate() }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex; val id = e.getPointerId(i); val x = e.getX(i); val y = e.getY(i)
                val d = hypot(x - cx, y - cy)
                when {
                    RectF(trigger).apply { inset(-30f, -30f) }.contains(x, y) -> { trigPtr = id; trig = true; performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); send() }
                    d < head -> { stickPtr = id; dir = stickBits(x, y); send() }
                    d < ring * 1.3f -> { ringPtr = id; ringLast = atan2((y - cy).toDouble(), (x - cx).toDouble()) }
                }
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until e.pointerCount) {
                    val id = e.getPointerId(i); val x = e.getX(i); val y = e.getY(i)
                    if (id == stickPtr) { val b = stickBits(x, y); if (b != dir) { dir = b; send() } }
                    if (id == ringPtr) {
                        val a = atan2((y - cy).toDouble(), (x - cx).toDouble())
                        var da = a - ringLast
                        if (da > PI) da -= 2 * PI
                        if (da < -PI) da += 2 * PI
                        ringLast = a
                        knob = (knob + (da / (0.75 * PI)).toFloat()).coerceIn(-1f, 1f)
                        onKnob?.invoke(knob); invalidate()
                    }
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val id = e.getPointerId(e.actionIndex)
                if (id == stickPtr) { stickPtr = -1; dir = 0; send() }
                if (id == trigPtr) { trigPtr = -1; trig = false; send() }
                if (id == ringPtr) ringPtr = -1
            }
            MotionEvent.ACTION_CANCEL -> { stickPtr = -1; ringPtr = -1; trigPtr = -1; dir = 0; trig = false; send() }
        }
        return true
    }
}
