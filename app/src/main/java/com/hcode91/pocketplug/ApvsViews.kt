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
 * The 1292 console as seen from the player's side: light grey body, a black
 * panel with four red switches (OFF/ON, RESET, SELECT, START) and the model
 * badge, "Programmable Video System" lettering, and the cartridge standing in
 * its slot on the right, labelled with the current game.
 */
class ApvsConsoleView(ctx: Context, private val badge: String = "1292", private val cartTitle: String = "") : View(ctx) {
    var onInput: ((Int) -> Unit)? = null
    /** Called on press with the button's bit: the core holds it until the game reacts. */
    var onTap: ((Int) -> Unit)? = null
    var onReset: (() -> Unit)? = null
    var onPower: (() -> Unit)? = null

    private enum class Action { POWER, RESET, BIT }
    private class Btn(val label: String, val action: Action, val bit: Int = 0) { val r = RectF() }
    private val buttons = arrayOf(
        Btn("OFF ON", Action.POWER), Btn("RESET", Action.RESET),
        Btn("SELECT", Action.BIT, ApvsBits.SELECT), Btn("START", Action.BIT, ApvsBits.START),
    )
    private var pressed = -1
    private val body = RectF()
    private val panel = RectF()
    private val slot = RectF()
    private val cart = RectF()
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG)

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val pad = w * 0.04f
        body.set(pad, h * 0.1f, w - pad, h * 0.95f)
        panel.set(body.left + body.width() * 0.04f, body.top + body.height() * 0.1f,
            body.left + body.width() * 0.58f, body.top + body.height() * 0.66f)
        slot.set(body.left + body.width() * 0.64f, body.top + body.height() * 0.08f,
            body.right - body.width() * 0.04f, body.bottom - body.height() * 0.08f)
        cart.set(slot.left + slot.width() * 0.08f, slot.top + slot.height() * 0.06f,
            slot.right - slot.width() * 0.08f, slot.bottom - slot.height() * 0.1f)
        val bw = panel.width() * 0.13f
        val bh = panel.height() * 0.42f
        val step = panel.width() * 0.17f
        val x0 = panel.left + panel.width() * 0.25f
        for ((i, b) in buttons.withIndex()) {
            val cx = x0 + i * step
            b.r.set(cx - bw / 2, panel.bottom - panel.height() * 0.12f - bh, cx + bw / 2, panel.bottom - panel.height() * 0.12f)
        }
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0814.toInt())
        val r = body.height() * 0.07f
        p.color = 0x88000000.toInt()
        c.drawRoundRect(body.left + 6, body.top + 10, body.right + 6, body.bottom + 10, r, r, p)
        // light grey plastic body
        p.shader = LinearGradient(0f, body.top, 0f, body.bottom, 0xFFE1E2E4.toInt(), 0xFFB5B7BB.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, r, r, p)
        p.shader = null
        p.style = Paint.Style.STROKE; p.strokeWidth = 2f; p.color = 0xFF8D9096.toInt()
        c.drawRoundRect(body, r, r, p)
        p.style = Paint.Style.FILL

        // black control panel with the badge and red switches
        p.color = 0xFF141416.toInt()
        c.drawRoundRect(panel, 8f, 8f, p)
        t.text(panel.height() * 0.2f, 0xFFE8E8EA.toInt())
        t.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD_ITALIC)
        t.textAlign = Paint.Align.LEFT
        c.drawText(badge, panel.left + panel.width() * 0.04f, panel.top + panel.height() * 0.26f, t)
        t.textAlign = Paint.Align.CENTER
        for ((i, b) in buttons.withIndex()) {
            val down = i == pressed
            t.text(panel.height() * 0.1f, 0xFFDADADD.toInt())
            c.drawText(b.label, b.r.centerX(), b.r.top - panel.height() * 0.06f, t)
            p.color = 0xFF000000.toInt()
            c.drawRoundRect(b.r.left - 3, b.r.top - 3, b.r.right + 3, b.r.bottom + 3, 5f, 5f, p)
            val k = RectF(b.r).apply { if (down) offset(0f, b.r.height() * 0.08f) }
            p.shader = LinearGradient(0f, k.top, 0f, k.bottom,
                if (down) 0xFFA0161A.toInt() else 0xFFF0353A.toInt(), 0xFF7C0E12.toInt(), Shader.TileMode.CLAMP)
            c.drawRoundRect(k, 5f, 5f, p)
            p.shader = null
        }

        // lettering on the grey below the panel
        t.text(body.height() * 0.11f, 0xFF2A2B30.toInt(), bold = false)
        t.typeface = Typeface.create(Typeface.SERIF, Typeface.NORMAL)
        c.drawText("Programmable Video System", panel.centerX(), body.bottom - body.height() * 0.12f, t)

        // cartridge slot with the game's cartridge in it
        p.color = 0xFF1A1A1D.toInt()
        c.drawRoundRect(slot, 6f, 6f, p)
        p.color = 0xFF2B2C30.toInt()
        c.drawRect(cart, p)
        val label = RectF(cart.left + cart.width() * 0.06f, cart.top + cart.height() * 0.08f, cart.right - cart.width() * 0.06f, cart.bottom - cart.height() * 0.08f)
        // bright striped label like the original game cartridges
        val stripes = intArrayOf(0xFFF7D21E.toInt(), 0xFFE5322D.toInt(), 0xFF2E9E46.toInt(), 0xFF2D63C8.toInt())
        val sh = label.height() * 0.1f
        for ((i, col) in stripes.withIndex()) {
            p.color = col
            c.drawRect(label.left, label.top + i * sh, label.right, label.top + (i + 1) * sh, p)
        }
        p.color = 0xFFFFF6D8.toInt()
        c.drawRect(label.left, label.top + 4 * sh, label.right, label.bottom, p)
        t.text(label.height() * 0.24f, 0xFF1A1A1A.toInt())
        val title = cartTitle.uppercase()
        while (t.measureText(title) > label.width() * 0.9f && t.textSize > 6f) t.textSize *= 0.92f
        c.drawText(title, label.centerX(), label.top + 4 * sh + (label.height() - 4 * sh) / 2 + t.textSize * 0.36f, t)
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val i = buttons.indexOfFirst { RectF(it.r).apply { inset(-it.r.width() * 0.3f, -it.r.height() * 0.4f) }.contains(e.x, e.y) }
                if (i >= 0) {
                    pressed = i
                    performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
                    val b = buttons[i]
                    when (b.action) {
                        Action.POWER -> onPower?.invoke()
                        Action.RESET -> onReset?.invoke()
                        // A tap counts as exactly one press however briefly the
                        // screen is touched; keeping a finger on the button also
                        // holds it down, so it auto-repeats like the real console.
                        Action.BIT -> { onTap?.invoke(b.bit); onInput?.invoke(b.bit) }
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
class ApvsControllerView(ctx: Context, private val player: Int, private val greyBody: Boolean = false) : View(ctx) {
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
    private val fireL = RectF()
    private val fireR = RectF()
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
        // two red fire buttons above the keypad, as on the real controller
        val top = well.bottom + body.height() * 0.04f
        val kw = body.width() * 0.24f
        val gap = body.width() * 0.05f
        val avail = body.bottom - body.height() * 0.05f - top
        val kh = (avail - gap * 4) / 4.75f
        val left = body.centerX() - (kw * 3 + gap * 2) / 2
        val fh = kh * 0.75f
        fireL.set(left, top, left + kw, top + fh)
        fireR.set(left + 2 * (kw + gap), top, left + 3 * kw + 2 * gap, top + fh)
        val kTop = top + fh + gap
        keys.forEachIndexed { i, k ->
            val cx = left + (i % 3) * (kw + gap)
            val cy = kTop + (i / 3) * (kh + gap)
            k.r.set(cx, cy, cx + kw, cy + kh)
        }
        knobX = well.centerX(); knobY = well.centerY()
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0814.toInt())
        // body
        p.shader = LinearGradient(body.left, 0f, body.right, 0f,
            if (greyBody) intArrayOf(0xFFB8BBC0.toInt(), 0xFFE2E3E6.toInt(), 0xFFA9ACB2.toInt())
            else intArrayOf(0xFF1C1C20.toInt(), 0xFF2F3036.toInt(), 0xFF17171A.toInt()), floatArrayOf(0f, 0.45f, 1f), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, body.width() * 0.12f, body.width() * 0.12f, p)
        p.shader = null
        t.text(body.width() * 0.07f, if (greyBody) 0xFF4A4C52.toInt() else 0xFF8E9098.toInt())
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
                if (on) 0xFF5A5C66.toInt() else 0xFF34353B.toInt(), if (on) 0xFF3A3C44.toInt() else 0xFF111114.toInt(), Shader.TileMode.CLAMP)
            c.drawRoundRect(k.r, 8f, 8f, p)
            p.shader = null
            p.style = Paint.Style.STROKE; p.strokeWidth = 2f; p.color = if (on) 0xFFFFE14D.toInt() else 0xFF4A4B52.toInt()
            c.drawRoundRect(k.r, 8f, 8f, p)
            p.style = Paint.Style.FILL
            t.text(k.r.height() * (if (k.label.length > 1) 0.4f else 0.55f), if (on) 0xFFFFE14D.toInt() else 0xFFE8E8EA.toInt())
            c.drawText(k.label, k.r.centerX(), k.r.centerY() + t.textSize * 0.36f, t)
        }

        // fire buttons (both wired to keypad 2)
        val fireOn = (lit and ApvsBits.fire(player)) != 0
        for (f in arrayOf(fireL, fireR)) {
            p.color = 0xFF000000.toInt()
            c.drawRoundRect(f.left, f.top + 4, f.right, f.bottom + 4, 8f, 8f, p)
            p.shader = LinearGradient(0f, f.top, 0f, f.bottom,
                if (fireOn) 0xFFFF8A80.toInt() else 0xFFE0352B.toInt(), if (fireOn) 0xFFE0352B.toInt() else 0xFF8E1A14.toInt(), Shader.TileMode.CLAMP)
            c.drawRoundRect(f, 8f, 8f, p)
            p.shader = null
        }
        t.text(fireL.height() * 0.3f, if (greyBody) 0xFF4A4C52.toInt() else 0xFF8E9098.toInt())
        c.drawText("FIRE", body.centerX(), fireL.centerY() + t.textSize * 0.36f, t)
    }

    private fun keyAt(x: Float, y: Float): Int {
        if (RectF(fireL).apply { inset(-10f, -10f) }.contains(x, y) || RectF(fireR).apply { inset(-10f, -10f) }.contains(x, y))
            return ApvsBits.fire(player)
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
