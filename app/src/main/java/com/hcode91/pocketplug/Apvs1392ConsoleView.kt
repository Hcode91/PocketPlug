package com.hcode91.pocketplug

import android.content.Context
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View

/**
 * The 1392 console as seen from the player's side: black ridged top with the
 * cartridge standing in its slot (labelled with the current game), a silver
 * front panel with four black switches - OFF/ON, RESET, SELECT, START - and a
 * dark blue badge plate.
 */
class Apvs1392ConsoleView(ctx: Context, private val badge: String, private val cartTitle: String) : View(ctx) {
    var onInput: ((Int) -> Unit)? = null
    var onTap: ((Int) -> Unit)? = null
    var onReset: (() -> Unit)? = null
    var onPower: (() -> Unit)? = null

    private enum class Action { POWER, RESET, BIT }
    private class Switch(val label: String, val action: Action, val bit: Int = 0) { val r = RectF() }
    private val switches = arrayOf(
        Switch("OFF  ON", Action.POWER), Switch("RESET", Action.RESET),
        Switch("SELECT", Action.BIT, ApvsBits.SELECT), Switch("START", Action.BIT, ApvsBits.START),
    )
    private var pressed = -1

    private val body = RectF()
    private val topDeck = RectF()
    private val fascia = RectF()
    private val panel = RectF()
    private val plate = RectF()
    private val slot = RectF()
    private val cart = RectF()
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val pad = w * 0.04f
        body.set(pad, h * 0.24f, w - pad, h * 0.96f)
        topDeck.set(body.left, body.top, body.right, body.top + body.height() * 0.42f)
        fascia.set(body.left, topDeck.bottom, body.right, body.bottom)
        panel.set(fascia.left + fascia.width() * 0.03f, fascia.top + fascia.height() * 0.1f,
            fascia.left + fascia.width() * 0.66f, fascia.bottom - fascia.height() * 0.1f)
        plate.set(panel.right + fascia.width() * 0.02f, fascia.top + fascia.height() * 0.42f,
            fascia.right - fascia.width() * 0.03f, fascia.bottom - fascia.height() * 0.16f)
        slot.set(body.left + body.width() * 0.62f, topDeck.top + topDeck.height() * 0.12f,
            body.right - body.width() * 0.03f, topDeck.bottom)
        cart.set(slot.left + slot.width() * 0.1f, h * 0.02f, slot.right - slot.width() * 0.1f, topDeck.top + topDeck.height() * 0.55f)

        val sw = panel.width() * 0.1f      // tall, narrow rocker switches
        val sh = panel.height() * 0.6f
        val step = panel.width() / 4f
        for ((i, s) in switches.withIndex()) {
            val cx = panel.left + step * (i + 0.5f)
            s.r.set(cx - sw / 2, panel.top + panel.height() * 0.08f, cx + sw / 2, panel.top + panel.height() * 0.08f + sh)
        }
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0814.toInt())
        val r = body.height() * 0.06f

        // shadow + body
        p.color = 0x88000000.toInt()
        c.drawRoundRect(body.left + 6, body.top + 10, body.right + 6, body.bottom + 10, r, r, p)
        p.color = 0xFF151517.toInt()
        c.drawRoundRect(body, r, r, p)

        // ridged black top deck
        p.shader = LinearGradient(0f, topDeck.top, 0f, topDeck.bottom, 0xFF2A2B2F.toInt(), 0xFF141416.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(topDeck, r, r, p)
        p.shader = null
        p.color = 0xFF0A0A0B.toInt()
        val ridges = 7
        for (i in 1..ridges) {
            val y = topDeck.top + topDeck.height() * i / (ridges + 1f)
            c.drawRect(topDeck.left + r, y, slot.left - topDeck.width() * 0.02f, y + 2f, p)
        }

        // silver slot surround
        p.shader = LinearGradient(slot.left, 0f, slot.right, 0f, 0xFFBFC2C8.toInt(), 0xFF8D9097.toInt(), Shader.TileMode.CLAMP)
        c.drawRect(slot, p)
        p.shader = null

        // the cartridge in its slot, with the game's name on the label
        p.color = 0xFF000000.toInt()
        c.drawRect(cart.left + 4, cart.top + 6, cart.right + 4, cart.bottom, p)
        p.shader = LinearGradient(0f, cart.top, 0f, cart.bottom, 0xFFC9CBD0.toInt(), 0xFF9A9DA4.toInt(), Shader.TileMode.CLAMP)
        c.drawRect(cart, p)
        p.shader = null
        val label = RectF(cart.left + cart.width() * 0.06f, cart.top + cart.height() * 0.1f, cart.right - cart.width() * 0.06f, cart.top + cart.height() * 0.62f)
        p.color = 0xFFF2F2EE.toInt()
        c.drawRect(label, p)
        p.color = 0xFF1F3A7A.toInt()
        c.drawRect(label.left, label.top, label.right, label.top + label.height() * 0.24f, p)
        t.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
        t.textSize = label.height() * 0.16f
        t.color = 0xFFFFFFFF.toInt()
        c.drawText("PROGRAMME CARTRIDGE", label.centerX(), label.top + label.height() * 0.18f, t)
        t.color = 0xFF14306E.toInt()
        t.textSize = label.height() * 0.34f
        val title = cartTitle.uppercase()
        while (t.measureText(title) > label.width() * 0.92f && t.textSize > 6f) t.textSize *= 0.92f
        c.drawText(title, label.centerX(), label.top + label.height() * 0.68f, t)

        // silver front panel
        p.shader = LinearGradient(0f, panel.top, 0f, panel.bottom,
            intArrayOf(0xFFF0F1F3.toInt(), 0xFFC5C8CE.toInt(), 0xFFE2E4E8.toInt()), floatArrayOf(0f, 0.6f, 1f), Shader.TileMode.CLAMP)
        c.drawRoundRect(panel, 8f, 8f, p)
        p.shader = null

        // switches
        for ((i, s) in switches.withIndex()) {
            val down = i == pressed
            p.color = 0xFF000000.toInt()
            c.drawRoundRect(s.r.left - 3, s.r.top - 3, s.r.right + 3, s.r.bottom + 3, 6f, 6f, p)   // recess
            val k = RectF(s.r).apply { if (down) offset(0f, s.r.height() * 0.08f) }
            p.shader = LinearGradient(0f, k.top, 0f, k.bottom,
                if (down) 0xFF1C1C1F.toInt() else 0xFF45464C.toInt(), 0xFF0E0E10.toInt(), Shader.TileMode.CLAMP)
            c.drawRoundRect(k, 6f, 6f, p)
            p.shader = null
            p.color = 0x33FFFFFF
            for (g in 1..3) {          // grip lines
                val y = k.top + k.height() * (0.25f + g * 0.14f)
                c.drawRect(k.left + k.width() * 0.2f, y, k.right - k.width() * 0.2f, y + 2f, p)
            }
            t.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
            t.textSize = panel.height() * 0.13f
            t.color = 0xFF202124.toInt()
            c.drawText(s.label, s.r.centerX(), panel.bottom - panel.height() * 0.12f, t)
        }

        // badge plate
        p.shader = LinearGradient(0f, plate.top, 0f, plate.bottom, 0xFF26386E.toInt(), 0xFF141E40.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(plate, 6f, 6f, p)
        p.shader = null
        t.color = 0xFFDDE3F2.toInt()
        t.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD_ITALIC)
        t.textSize = plate.height() * 0.42f
        c.drawText(badge, plate.centerX(), plate.top + plate.height() * 0.48f, t)
        t.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
        t.textSize = plate.height() * 0.15f
        val sub = "PROGRAMMABLE VIDEO SYSTEM"
        while (t.measureText(sub) > plate.width() * 0.92f && t.textSize > 5f) t.textSize *= 0.92f
        c.drawText(sub, plate.centerX(), plate.top + plate.height() * 0.82f, t)
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                val i = switches.indexOfFirst { RectF(it.r).apply { inset(-it.r.width() * 0.4f, -it.r.height() * 0.3f) }.contains(e.x, e.y) }
                if (i >= 0) {
                    pressed = i
                    performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
                    val s = switches[i]
                    when (s.action) {
                        Action.POWER -> onPower?.invoke()
                        Action.RESET -> onReset?.invoke()
                        Action.BIT -> { onTap?.invoke(s.bit); onInput?.invoke(s.bit) }
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
