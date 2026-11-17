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
import android.view.MotionEvent
import android.view.View
import kotlin.math.hypot
import kotlin.math.min

private fun Paint.text(size: Float, color: Int, bold: Boolean = true) = apply {
    textSize = size; this.color = color; textAlign = Paint.Align.CENTER
    typeface = Typeface.create(Typeface.SANS_SERIF, if (bold) Typeface.BOLD else Typeface.NORMAL)
}

/**
 * Top panel of the 1292 APVS console: black body with a brushed-metal strip,
 * the RESET, START and GAME SELECT push buttons and the power light.
 */
class ApvsConsoleView(ctx: Context, private val badge: String = "1292") : View(ctx) {
    var onInput: ((Int) -> Unit)? = null
    var onReset: (() -> Unit)? = null

    private class Btn(val label: String, val bit: Int) { val r = RectF() }
    private val buttons = arrayOf(Btn("RESET", 0), Btn("START", ApvsBits.START), Btn("GAME\nSELECT", ApvsBits.SELECT))
    private var pressed = -1
    private val body = RectF()
    private val strip = RectF()
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG)

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val pad = w * 0.04f
        body.set(pad, h * 0.08f, w - pad, h * 0.92f)
        strip.set(body.left + body.width() * 0.03f, body.top + body.height() * 0.16f, body.right - body.width() * 0.03f, body.bottom - body.height() * 0.16f)
        val bw = strip.width() * 0.2f
        val bh = strip.height() * 0.62f
        val x0 = strip.left + strip.width() * 0.32f
        for ((i, b) in buttons.withIndex()) {
            val x = x0 + i * (bw + strip.width() * 0.035f)
            b.r.set(x, strip.centerY() - bh / 2, x + bw, strip.centerY() + bh / 2)
        }
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0814.toInt())
        p.shader = LinearGradient(0f, body.top, 0f, body.bottom, 0xFF2A2A2E.toInt(), 0xFF0E0E10.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, body.height() * 0.18f, body.height() * 0.18f, p)
        p.shader = LinearGradient(0f, strip.top, 0f, strip.bottom,
            intArrayOf(0xFFD9DADF.toInt(), 0xFF9C9EA6.toInt(), 0xFFC9CBD1.toInt()), floatArrayOf(0f, 0.55f, 1f), Shader.TileMode.CLAMP)
        c.drawRoundRect(strip, strip.height() * 0.12f, strip.height() * 0.12f, p)
        p.shader = null

        // badge + power light
        t.text(strip.height() * 0.2f, 0xFF1A1A1E.toInt())
        c.drawText(badge, strip.left + strip.width() * 0.14f, strip.centerY() - strip.height() * 0.02f, t)
        t.text(strip.height() * 0.11f, 0xFF3A3A40.toInt())
        c.drawText("ADVANCED PROGRAMMABLE", strip.left + strip.width() * 0.14f, strip.centerY() + strip.height() * 0.17f, t)
        c.drawText("VIDEO SYSTEM", strip.left + strip.width() * 0.14f, strip.centerY() + strip.height() * 0.31f, t)
        p.color = 0xFFFF3B30.toInt()
        c.drawCircle(strip.left + strip.width() * 0.275f, strip.centerY(), strip.height() * 0.06f, p)

        for ((i, b) in buttons.withIndex()) {
            val down = i == pressed
            val r = RectF(b.r).apply { if (down) offset(0f, b.r.height() * 0.05f) }
            p.color = 0xFF000000.toInt()
            c.drawRoundRect(b.r.left, b.r.top + b.r.height() * 0.08f, b.r.right, b.r.bottom + b.r.height() * 0.08f, 10f, 10f, p)
            p.shader = LinearGradient(0f, r.top, 0f, r.bottom,
                if (i == 0) 0xFFB8302A.toInt() else 0xFF3A3B42.toInt(),
                if (i == 0) 0xFF7A1A16.toInt() else 0xFF1C1D22.toInt(), Shader.TileMode.CLAMP)
            c.drawRoundRect(r, 10f, 10f, p)
            p.shader = null
            val lines = b.label.split("\n")
            t.text(b.r.height() * (if (lines.size > 1) 0.25f else 0.3f), 0xFFF2F2F2.toInt())
            lines.forEachIndexed { k, s ->
                val y = r.centerY() + t.textSize * 0.35f + (k - (lines.size - 1) / 2f) * t.textSize * 1.05f
                c.drawText(s, r.centerX(), y, t)
            }
        }
    }

    /** Called on press with the button's bit: the core holds it until the game reacts. */
    var onTap: ((Int) -> Unit)? = null

    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val i = buttons.indexOfFirst { RectF(it.r).apply { inset(-12f, -16f) }.contains(e.x, e.y) }
                if (i >= 0) {
                    pressed = i
                    performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
                    if (i == 0) onReset?.invoke()
                    else {
                        // A tap counts as exactly one press however briefly the
                        // screen is touched; keeping a finger on the button also
                        // holds it down, so it auto-repeats like the real console.
                        onTap?.invoke(buttons[i].bit)
                        onInput?.invoke(buttons[i].bit)
                    }
                    invalidate()
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                if (pressed >= 0) { pressed = -1; onInput?.invoke(0); invalidate() }
            }
        }
        return true
    }
}

/**
 * One APVS hand controller: an analog joystick on top, the 12-key keypad and
 * the fire button. Multi-touch: steer with one finger while pressing keys or
 * fire with another.
 */
class ApvsControllerView(ctx: Context, private val player: Int) : View(ctx) {
    var onInput: ((Int) -> Unit)? = null
    var onAnalog: ((Float, Float) -> Unit)? = null
    /** Bits pressed from a gamepad / keyboard, shown lit. */
    var externalMask = 0
        set(v) { field = v; invalidate() }

    private class Key(val label: String, val k: Int) { val r = RectF() }
    private val keys = listOf(
        Key("1", ApvsBits.K1), Key("2", ApvsBits.K2), Key("3", ApvsBits.K3),
        Key("4", ApvsBits.K4), Key("5", ApvsBits.K5), Key("6", ApvsBits.K6),
        Key("7", ApvsBits.K7), Key("8", ApvsBits.K8), Key("9", ApvsBits.K9),
        Key("CL", ApvsBits.KCLEAR), Key("0", ApvsBits.K0), Key("EN", ApvsBits.KENTER),
    )
    private val body = RectF()
    private val well = RectF()
    private val fire = RectF()
    private var stickR = 0f
    private var knobX = 0f
    private var knobY = 0f
    private var stickPointer = -1
    private var mask = 0
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG)

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val bw = min(w * 0.92f, h * 0.62f)
        body.set((w - bw) / 2, h * 0.03f, (w + bw) / 2, h * 0.97f)
        val wellSize = body.width() * 0.62f
        well.set(body.centerX() - wellSize / 2, body.top + body.height() * 0.07f, body.centerX() + wellSize / 2, body.top + body.height() * 0.07f + wellSize)
        stickR = wellSize * 0.36f
        // keypad
        val kTop = well.bottom + body.height() * 0.05f
        val kw = body.width() * 0.24f
        val gap = body.width() * 0.05f
        val kh = (body.bottom - body.height() * 0.13f - kTop - gap * 3) / 4
        val left = body.centerX() - (kw * 3 + gap * 2) / 2
        keys.forEachIndexed { i, k ->
            val cx = left + (i % 3) * (kw + gap)
            val cy = kTop + (i / 3) * (kh + gap)
            k.r.set(cx, cy, cx + kw, cy + kh)
        }
        val fh = body.height() * 0.07f
        fire.set(body.left + body.width() * 0.18f, body.bottom - fh * 1.35f, body.right - body.width() * 0.18f, body.bottom - fh * 0.35f)
        knobX = well.centerX(); knobY = well.centerY()
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0814.toInt())
        // body
        p.shader = LinearGradient(body.left, 0f, body.right, 0f,
            intArrayOf(0xFF1C1C20.toInt(), 0xFF2F3036.toInt(), 0xFF17171A.toInt()), floatArrayOf(0f, 0.45f, 1f), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, body.width() * 0.12f, body.width() * 0.12f, p)
        p.shader = null
        t.text(body.width() * 0.07f, 0xFF8E9098.toInt())
        c.drawText(if (player == 0) "LEFT" else "RIGHT", body.centerX(), body.top + body.height() * 0.05f, t)

        // joystick well + stick
        p.shader = RadialGradient(well.centerX(), well.centerY(), well.width() / 2, 0xFF050506.toInt(), 0xFF2A2B30.toInt(), Shader.TileMode.CLAMP)
        c.drawOval(well, p)
        p.shader = null
        p.color = 0xFF0A0A0C.toInt()
        c.drawCircle(well.centerX(), well.centerY(), stickR * 0.35f, p)   // shaft base
        p.strokeWidth = stickR * 0.22f
        p.color = 0xFF55565C.toInt()
        c.drawLine(well.centerX(), well.centerY(), knobX, knobY, p)
        p.shader = RadialGradient(knobX - stickR * 0.15f, knobY - stickR * 0.15f, stickR * 0.5f, 0xFF7B7D85.toInt(), 0xFF1D1E22.toInt(), Shader.TileMode.CLAMP)
        c.drawCircle(knobX, knobY, stickR * 0.42f, p)
        p.shader = null

        // keypad
        val lit = mask or externalMask
        for (k in keys) {
            val on = (lit and ApvsBits.key(player, k.k)) != 0
            p.color = 0xFF000000.toInt()
            c.drawRoundRect(k.r.left, k.r.top + 4, k.r.right, k.r.bottom + 4, 8f, 8f, p)
            p.shader = LinearGradient(0f, k.r.top, 0f, k.r.bottom,
                if (on) 0xFFFFE89A.toInt() else 0xFFF1EFE8.toInt(), if (on) 0xFFE0B830.toInt() else 0xFFC7C4BA.toInt(), Shader.TileMode.CLAMP)
            c.drawRoundRect(k.r, 8f, 8f, p)
            p.shader = null
            t.text(k.r.height() * (if (k.label.length > 1) 0.4f else 0.55f), 0xFF151515.toInt())
            c.drawText(k.label, k.r.centerX(), k.r.centerY() + t.textSize * 0.36f, t)
        }

        // fire button (wired to keypad 2)
        val fireOn = (lit and ApvsBits.fire(player)) != 0
        p.color = if (fireOn) 0xFFFF6B5E.toInt() else 0xFFC0261D.toInt()
        c.drawRoundRect(fire, fire.height() / 2, fire.height() / 2, p)
        t.text(fire.height() * 0.5f, 0xFFFFFFFF.toInt())
        c.drawText("FIRE", fire.centerX(), fire.centerY() + t.textSize * 0.36f, t)
    }

    private fun keyAt(x: Float, y: Float): Int {
        if (RectF(fire).apply { inset(-10f, -10f) }.contains(x, y)) return ApvsBits.fire(player)
        for (k in keys) if (RectF(k.r).apply { inset(-6f, -6f) }.contains(x, y)) return ApvsBits.key(player, k.k)
        return 0
    }

    private fun moveStick(x: Float, y: Float) {
        var dx = x - well.centerX()
        var dy = y - well.centerY()
        val d = hypot(dx, dy)
        if (d > stickR) { dx *= stickR / d; dy *= stickR / d }
        knobX = well.centerX() + dx
        knobY = well.centerY() + dy
        onAnalog?.invoke(dx / stickR, dy / stickR)
    }

    private fun releaseStick() {
        stickPointer = -1
        knobX = well.centerX(); knobY = well.centerY()
        onAnalog?.invoke(0f, 0f)   // the real sticks spring back to the centre
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        val action = e.actionMasked
        val idx = e.actionIndex
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            val x = e.getX(idx); val y = e.getY(idx)
            val inWell = hypot(x - well.centerX(), y - well.centerY()) < well.width() * 0.6f
            if (stickPointer < 0 && inWell) { stickPointer = e.getPointerId(idx); moveStick(x, y) }
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
        if (upId == -2 || (upId >= 0 && upId == stickPointer)) releaseStick()

        var m = 0
        if (upId != -2) {
            for (i in 0 until e.pointerCount) {
                val id = e.getPointerId(i)
                if (id == upId || id == stickPointer) continue
                m = m or keyAt(e.getX(i), e.getY(i))
            }
        }
        if (m != mask) {
            if ((m and mask.inv()) != 0) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
            mask = m
            onInput?.invoke(m)
        }
        invalidate()
        return true
    }
}
