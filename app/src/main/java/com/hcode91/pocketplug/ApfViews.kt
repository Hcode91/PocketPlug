package com.hcode91.pocketplug

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
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.min

/** Input bits of the APF core (apf.h): 16 per controller, player 1 is the left one. */
object ApfBits {
    const val UP = 0x01; const val DOWN = 0x02; const val LEFT = 0x04; const val RIGHT = 0x08
    const val FIRE = 0x10            // the fire button and ENTER are one key
    fun key(n: Int) = 1 shl (5 + n)  // keys 0..9
    const val CLEAR = 0x8000
    fun of(player: Int, bits: Int) = bits shl (16 * player)

    fun keyBit(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> UP
        KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> DOWN
        KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> RIGHT
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_SPACE,
        KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_BUTTON_START -> FIRE
        KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_DEL, KeyEvent.KEYCODE_C -> CLEAR
        in KeyEvent.KEYCODE_0..KeyEvent.KEYCODE_9 -> key(code - KeyEvent.KEYCODE_0)
        KeyEvent.KEYCODE_BUTTON_X -> key(1)
        KeyEvent.KEYCODE_BUTTON_Y -> key(2)
        else -> 0
    }
}

private fun Paint.label(size: Float, color: Int) = apply {
    textSize = size; this.color = color; textAlign = Paint.Align.CENTER
    typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
}

/**
 * The APF MP1000 console seen from above: black body over a woodgrain base, the
 * cartridge slot (with the cartridge playing, if any), the two silver RESET and
 * POWER buttons and the ribbed speaker grille along the front.
 */
class ApfConsoleView(ctx: Context, private val cartTitle: String) : View(ctx) {
    var onReset: (() -> Unit)? = null
    var onPower: (() -> Unit)? = null

    private val body = RectF()
    private val slot = RectF()
    private val reset = RectF()
    private val power = RectF()
    private val grille = RectF()
    private var pressed = 0
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG)

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val pad = w * 0.04f
        body.set(pad, h * 0.06f, w - pad, h * 0.94f)
        slot.set(body.left + body.width() * 0.3f, body.top + body.height() * 0.07f, body.right - body.width() * 0.3f, body.top + body.height() * 0.3f)
        val bw = body.width() * 0.14f; val bh = body.height() * 0.13f
        val by = body.top + body.height() * 0.44f
        reset.set(body.centerX() - bw * 1.15f, by, body.centerX() - bw * 0.15f, by + bh)
        power.set(body.centerX() + bw * 0.15f, by, body.centerX() + bw * 1.15f, by + bh)
        grille.set(body.left + body.width() * 0.03f, body.top + body.height() * 0.66f, body.right - body.width() * 0.03f, body.top + body.height() * 0.8f)
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0814.toInt())
        val r = body.height() * 0.06f
        // woodgrain base
        val wood = RectF(body.left, body.bottom - body.height() * 0.17f, body.right, body.bottom)
        p.shader = LinearGradient(0f, wood.top, 0f, wood.bottom, 0xFF7A4A28.toInt(), 0xFF4A2A14.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(wood, r, r, p)
        p.shader = null
        p.color = 0x33000000; p.strokeWidth = 1.5f
        for (i in 0 until 9) {
            val y = wood.top + wood.height() * (0.12f + i * 0.1f)
            c.drawLine(wood.left + r, y, wood.right - r, y + (if (i % 2 == 0) 2f else -2f), p)
        }
        // black body
        val top = RectF(body.left, body.top, body.right, wood.top + 2)
        p.shader = LinearGradient(0f, top.top, 0f, top.bottom, 0xFF2A2B2F.toInt(), 0xFF131316.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(top, r, r, p)
        p.shader = null
        // raised deck around the slot
        p.color = 0xFF1A1B1E.toInt()
        c.drawRoundRect(slot.left - slot.width() * 0.08f, slot.top - slot.height() * 0.2f, slot.right + slot.width() * 0.08f, slot.bottom + slot.height() * 0.25f, 8f, 8f, p)
        p.color = 0xFF050506.toInt()
        c.drawRoundRect(slot, 4f, 4f, p)
        if (cartTitle.isNotEmpty()) {
            val cart = RectF(slot.left + slot.width() * 0.06f, slot.top - slot.height() * 0.5f, slot.right - slot.width() * 0.06f, slot.bottom - slot.height() * 0.15f)
            p.color = 0xFF202124.toInt(); c.drawRect(cart, p)
            val lab = RectF(cart.left + cart.width() * 0.08f, cart.top + cart.height() * 0.12f, cart.right - cart.width() * 0.08f, cart.bottom - cart.height() * 0.12f)
            p.color = 0xFFEDE6D2.toInt(); c.drawRect(lab, p)
            t.label(lab.height() * 0.5f, 0xFF1A1A1A.toInt())
            val title = cartTitle.uppercase()
            while (t.measureText(title) > lab.width() * 0.92f && t.textSize > 6f) t.textSize *= 0.92f
            c.drawText(title, lab.centerX(), lab.centerY() + t.textSize * 0.36f, t)
        }
        // RESET and POWER
        for ((i, b) in listOf(reset, power).withIndex()) {
            val down = pressed == i + 1
            p.color = 0xFF000000.toInt()
            c.drawRoundRect(b.left - 3, b.top - 3, b.right + 3, b.bottom + 5, 5f, 5f, p)
            val k = RectF(b).apply { if (down) offset(0f, b.height() * 0.08f) }
            p.shader = LinearGradient(0f, k.top, 0f, k.bottom, if (down) 0xFFB8BAC0.toInt() else 0xFFF2F3F5.toInt(), 0xFF9C9EA6.toInt(), Shader.TileMode.CLAMP)
            c.drawRoundRect(k, 4f, 4f, p)
            p.shader = null
            t.label(b.height() * 0.32f, 0xFFDADBDF.toInt())
            c.drawText(if (i == 0) "Reset" else "Power", b.centerX(), b.top - b.height() * 0.22f, t)
        }
        // speaker grille
        p.color = 0xFF0A0A0C.toInt()
        c.drawRect(grille, p)
        p.color = 0xFF26272B.toInt()
        val n = 7
        for (i in 0 until n) {
            val y = grille.top + grille.height() * (i + 0.5f) / n
            c.drawRect(grille.left + 4, y - 1.5f, grille.right - 4, y + 1.5f, p)
        }
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val hit = { b: RectF -> RectF(b).apply { inset(-b.width() * 0.2f, -b.height() * 0.6f) }.contains(e.x, e.y) }
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

/**
 * One MP1000 hand controller: a black box with the joystick (black shaft, chrome
 * ball on top), the red fire button on its upper edge and the 12-key keypad
 * (7 8 9 0 / 4 5 6 CL / 1 2 3 EN, CL in blue). Multi-touch.
 */
class ApfControllerView(ctx: Context, private val player: Int) : View(ctx) {
    var onInput: ((Int) -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; invalidate() }

    private class Key(val label: String, val bit: Int, val blue: Boolean = false) { val r = RectF() }
    private val keys = listOf(
        Key("7", ApfBits.key(7)), Key("8", ApfBits.key(8)), Key("9", ApfBits.key(9)), Key("0", ApfBits.key(0)),
        Key("4", ApfBits.key(4)), Key("5", ApfBits.key(5)), Key("6", ApfBits.key(6)), Key("CL", ApfBits.CLEAR, blue = true),
        Key("1", ApfBits.key(1)), Key("2", ApfBits.key(2)), Key("3", ApfBits.key(3)), Key("EN", ApfBits.FIRE),
    )
    private val body = RectF()
    private val base = RectF()
    private val fire = RectF()
    private var stickR = 0f
    private var knobX = 0f
    private var knobY = 0f
    private var stickPointer = -1
    private var stickBits = 0
    private var mask = 0
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG)

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val bw = min(w * 0.92f, h * 0.66f)
        body.set((w - bw) / 2, h * 0.04f, (w + bw) / 2, h * 0.97f)
        val bs = body.width() * 0.6f
        base.set(body.centerX() - bs / 2, body.top + body.height() * 0.08f, body.centerX() + bs / 2, body.top + body.height() * 0.08f + bs)
        stickR = bs * 0.36f
        fire.set(body.right - body.width() * 0.2f, body.top - body.height() * 0.01f, body.right - body.width() * 0.04f, body.top + body.height() * 0.05f)
        val kTop = base.bottom + body.height() * 0.06f
        val gap = body.width() * 0.04f
        val kw = (body.width() * 0.86f - gap * 3) / 4
        val kh = min(kw * 0.8f, (body.bottom - body.height() * 0.04f - kTop - gap * 2) / 3)
        val left = body.centerX() - (kw * 4 + gap * 3) / 2
        keys.forEachIndexed { i, k ->
            val x = left + (i % 4) * (kw + gap); val y = kTop + (i / 4) * (kh + gap)
            k.r.set(x, y, x + kw, y + kh)
        }
        knobX = base.centerX(); knobY = base.centerY()
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0814.toInt())
        val lit = ((mask or externalMask) shr (16 * player)) and 0xffff
        // red fire button on the top edge
        val fireOn = (lit and ApfBits.FIRE) != 0
        p.color = if (fireOn) 0xFFFF6B5E.toInt() else 0xFFC9271D.toInt()
        c.drawRoundRect(fire, 6f, 6f, p)
        // body
        p.shader = LinearGradient(body.left, 0f, body.right, 0f, intArrayOf(0xFF17171A.toInt(), 0xFF2C2D32.toInt(), 0xFF141417.toInt()),
            floatArrayOf(0f, 0.45f, 1f), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, body.width() * 0.06f, body.width() * 0.06f, p)
        p.shader = null
        // joystick boot and stick
        p.shader = RadialGradient(base.centerX(), base.centerY(), base.width() / 2, 0xFF050506.toInt(), 0xFF25262A.toInt(), Shader.TileMode.CLAMP)
        c.drawOval(base, p)
        p.shader = null
        p.color = 0xFF0A0A0C.toInt()
        c.drawCircle(base.centerX(), base.centerY(), stickR * 0.4f, p)
        p.strokeWidth = stickR * 0.26f; p.strokeCap = Paint.Cap.ROUND
        p.color = 0xFF111114.toInt()
        c.drawLine(base.centerX(), base.centerY(), knobX, knobY, p)
        // chrome ball top with its black band
        val br = stickR * 0.5f
        p.shader = RadialGradient(knobX - br * 0.35f, knobY - br * 0.35f, br * 1.2f, intArrayOf(0xFFFFFFFF.toInt(), 0xFFB9BCC4.toInt(), 0xFF55575E.toInt()),
            floatArrayOf(0f, 0.45f, 1f), Shader.TileMode.CLAMP)
        c.drawCircle(knobX, knobY, br, p)
        p.shader = null
        p.color = 0xFF1A1A1C.toInt()
        c.drawRect(knobX - br, knobY - br * 0.12f, knobX + br, knobY + br * 0.12f, p)
        // keypad
        for (k in keys) {
            val on = (lit and k.bit) != 0
            p.color = 0xFF000000.toInt()
            c.drawRoundRect(k.r.left, k.r.top + 3, k.r.right, k.r.bottom + 3, 6f, 6f, p)
            val top = if (k.blue) 0xFF5C8FD6.toInt() else 0xFFD7D8DC.toInt()
            val bot = if (k.blue) 0xFF2F5E9E.toInt() else 0xFF9A9CA3.toInt()
            p.shader = LinearGradient(0f, k.r.top, 0f, k.r.bottom, if (on) bot else top, bot, Shader.TileMode.CLAMP)
            c.drawRoundRect(k.r, 6f, 6f, p)
            p.shader = null
            if (on) { p.style = Paint.Style.STROKE; p.strokeWidth = 3f; p.color = 0xFFFFE14D.toInt(); c.drawRoundRect(k.r, 6f, 6f, p); p.style = Paint.Style.FILL }
            t.label(k.r.height() * (if (k.label.length > 1) 0.36f else 0.5f), if (k.blue) 0xFFFFFFFF.toInt() else 0xFF2A2B30.toInt())
            c.drawText(k.label, k.r.centerX(), k.r.centerY() + t.textSize * 0.36f, t)
        }
    }

    private fun keyAt(x: Float, y: Float): Int {
        if (RectF(fire).apply { inset(-16f, -16f) }.contains(x, y)) return ApfBits.FIRE
        for (k in keys) if (RectF(k.r).apply { inset(-5f, -5f) }.contains(x, y)) return k.bit
        return 0
    }

    private fun moveStick(x: Float, y: Float) {
        var dx = x - base.centerX(); var dy = y - base.centerY()
        val d = hypot(dx, dy)
        if (d > stickR) { dx *= stickR / d; dy *= stickR / d }
        knobX = base.centerX() + dx; knobY = base.centerY() + dy
        var b = 0
        val dead = stickR * 0.3f
        if (d > dead) {
            // 8-way: a direction counts once it is within ~67 degrees of the push
            if (dy < 0 && abs(dy) > abs(dx) * 0.42f) b = b or ApfBits.UP
            if (dy > 0 && abs(dy) > abs(dx) * 0.42f) b = b or ApfBits.DOWN
            if (dx < 0 && abs(dx) > abs(dy) * 0.42f) b = b or ApfBits.LEFT
            if (dx > 0 && abs(dx) > abs(dy) * 0.42f) b = b or ApfBits.RIGHT
        }
        stickBits = b
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        val action = e.actionMasked
        val idx = e.actionIndex
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            val x = e.getX(idx); val y = e.getY(idx)
            if (stickPointer < 0 && hypot(x - base.centerX(), y - base.centerY()) < base.width() * 0.62f) {
                stickPointer = e.getPointerId(idx); moveStick(x, y)
            }
        }
        if (action == MotionEvent.ACTION_MOVE && stickPointer >= 0) {
            val i = e.findPointerIndex(stickPointer)
            if (i >= 0) moveStick(e.getX(i), e.getY(i))
        }
        val upId = when (action) {
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> e.getPointerId(idx)
            MotionEvent.ACTION_CANCEL -> -2
            else -> -1
        }
        if (upId == -2 || (upId >= 0 && upId == stickPointer)) {
            stickPointer = -1; stickBits = 0; knobX = base.centerX(); knobY = base.centerY()
        }
        var m = stickBits
        if (upId != -2) for (i in 0 until e.pointerCount) {
            val id = e.getPointerId(i)
            if (id == upId || id == stickPointer) continue
            m = m or keyAt(e.getX(i), e.getY(i))
        }
        val full = ApfBits.of(player, m)
        if (full != mask) {
            if ((full and mask.inv()) != 0) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
            mask = full
            onInput?.invoke(full)
        }
        invalidate()
        return true
    }
}
