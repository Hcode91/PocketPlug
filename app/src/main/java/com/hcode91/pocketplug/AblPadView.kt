package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import kotlin.math.atan2
import kotlin.math.hypot

/**
 * The controllers of the 2000s plug-into-the-TV sports and joystick games
 * (the TV above shows the game). One view, drawn per [style]:
 *
 *  - "pinball":  a pinball front panel: two flipper buttons on the sides, a
 *                spring plunger in the middle (hold to pull it back), NUDGE,
 *                START and SELECT. NES pad bits.
 *  - "pingpong": a table-tennis bat; tap the rubber to swing (and to choose),
 *                arrows and cancel on the handle. NES pad bits.
 *  - "minijoy":  two teal-and-white mini joysticks with grey-green ball tops,
 *                one per player (player 2 in bits 8-15).
 *  - "tennis":   a racket (tap or swipe the strings to swing, the handle button
 *                tosses the ball to serve) and the receiver's own buttons for the menus.
 *  - "kickbox":  two gloves (punch), two foot pads (kick), a guard button and
 *                the receiver's buttons for the menus.
 *  - "fourplay": four pads for the 4 Player System, two by two; player 1's
 *                has START and SELECT.
 *
 * No maker's name or logo is drawn.
 */
class AblPadView(ctx: Context, private val style: String) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    companion object {
        // NES-style pad (VT cores)
        const val A = 0x01; const val B = 0x02; const val SELECT = 0x04; const val START = 0x08
        const val UP = 0x10; const val DOWN = 0x20; const val LEFT = 0x40; const val RIGHT = 0x80
        // SPG2xx boards: receiver pad, then the wireless sensors
        const val S_UP = 0x01; const val S_DOWN = 0x02; const val S_LEFT = 0x04; const val S_RIGHT = 0x08
        const val S_OK = 0x10; const val S_BACK = 0x20
        const val SWING = 0x80; const val RACKET_BUTTON = 0x100
        // 4 Player System: player 1 in bits 0-7, players 2-4 in bits 8-13 / 16-21 / 24-29
        const val F_START = 0x01; const val F_SELECT = 0x02; const val F_UP = 0x04; const val F_DOWN = 0x08
        const val F_LEFT = 0x10; const val F_RIGHT = 0x20; const val F_B1 = 0x40; const val F_B2 = 0x80
        const val L_PUNCH = 0x80; const val R_PUNCH = 0x100; const val L_KICK = 0x200; const val R_KICK = 0x400; const val GUARD = 0x800

        /** Gamepad / keyboard mapping for each style. */
        fun keyBit(style: String, code: Int): Int {
            if (style == "fourplay") return when (code) {
                KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> F_UP
                KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> F_DOWN
                KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> F_LEFT
                KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> F_RIGHT
                KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_Z -> F_B1
                KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_X -> F_B2
                KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_ENTER -> F_START
                KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_SHIFT_RIGHT -> F_SELECT
                else -> 0
            }
            val spg = style == "tennis" || style == "kickbox"
            val dir = when (code) {
                KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> if (spg) S_UP else UP
                KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> if (spg) S_DOWN else DOWN
                KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> if (spg) S_LEFT else LEFT
                KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> if (spg) S_RIGHT else RIGHT
                else -> 0
            }
            if (dir != 0) return dir
            return when (style) {
                "tennis" -> when (code) {
                    KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_Z -> SWING
                    KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_X -> RACKET_BUTTON
                    KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_ENTER -> S_OK
                    KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_ESCAPE -> S_BACK
                    else -> 0
                }
                "kickbox" -> when (code) {
                    KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_Q -> L_PUNCH
                    KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_E -> R_PUNCH
                    KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_Z -> L_KICK
                    KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_C -> R_KICK
                    KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_SPACE -> GUARD
                    KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_ENTER -> S_OK
                    KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_ESCAPE -> S_BACK
                    else -> 0
                }
                "pinball" -> when (code) {
                    KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_Z -> B
                    KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_SLASH -> A
                    KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_SPACE -> DOWN
                    KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_N -> UP
                    KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_ENTER -> START
                    KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_SHIFT_RIGHT -> SELECT
                    else -> 0
                }
                else -> when (code) {
                    KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_X -> A
                    KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_Z -> B
                    KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_SHIFT_RIGHT -> SELECT
                    KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_ENTER -> START
                    else -> 0
                }
            }
        }
    }

    /** A touch target: a circle (r > 0) or a rectangle, or a stick. */
    private class Ctl(val bit: Int, val label: String = "") {
        val rect = RectF()
        var cx = 0f; var cy = 0f; var r = 0f
        var stick = false
        var stickBits = intArrayOf(0, 0, 0, 0)     // up, down, left, right
        var knobDx = 0f; var knobDy = 0f
        fun hit(x: Float, y: Float): Boolean =
            if (r > 0f) hypot(x - cx, y - cy) < r * (if (stick) 1.7f else 1.3f) else RectF(rect).apply { inset(-rect.width() * 0.08f, -rect.height() * 0.15f) }.contains(x, y)
    }

    private val ctls = mutableListOf<Ctl>()
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }
    private val pointers = HashMap<Int, Ctl?>()
    private val stickOf = HashMap<Int, Int>()   // pointer -> stick bits
    private var mask = 0
    private val body = RectF()
    private val path = Path()

    private fun circle(bit: Int, label: String, x: Float, y: Float, r: Float) = Ctl(bit, label).also { it.cx = x; it.cy = y; it.r = r; ctls += it }
    private fun rect(bit: Int, label: String, l: Float, t: Float, rr: Float, b: Float) = Ctl(bit, label).also { it.rect.set(l, t, rr, b); ctls += it }
    private fun stick(x: Float, y: Float, r: Float, shift: Int, bits: IntArray = intArrayOf(UP, DOWN, LEFT, RIGHT)) = Ctl(0).also {
        it.cx = x; it.cy = y; it.r = r; it.stick = true
        it.stickBits = IntArray(4) { i -> bits[i] shl shift }; ctls += it
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        ctls.clear()
        val bw = w * 0.94f
        val bh = minOf(h * 0.9f, bw * 0.62f)
        val l = (w - bw) / 2; val t = (h - bh) / 2
        body.set(l, t, l + bw, t + bh)
        when (style) {
            "pinball" -> {
                circle(B, "", l + bw * 0.035f, t + bh * 0.5f, bh * 0.13f)
                circle(A, "", l + bw * 0.965f, t + bh * 0.5f, bh * 0.13f)
                rect(DOWN, "PULL", l + bw * 0.42f, t + bh * 0.3f, l + bw * 0.58f, t + bh * 0.88f)
                circle(UP, "NUDGE", l + bw * 0.22f, t + bh * 0.68f, bh * 0.085f)
                circle(START, "START", l + bw * 0.78f, t + bh * 0.68f, bh * 0.085f)
                circle(SELECT, "SELECT", l + bw * 0.78f, t + bh * 0.33f, bh * 0.06f)
            }
            "pingpong" -> {
                // the bat lies across: blade on the right, handle to the left
                circle(A, "", l + bw * 0.66f, t + bh * 0.5f, bh * 0.42f)
                circle(UP, "▲", l + bw * 0.13f, t + bh * 0.5f, bh * 0.07f)
                circle(DOWN, "▼", l + bw * 0.24f, t + bh * 0.5f, bh * 0.07f)
                circle(B, "✕", l + bw * 0.35f, t + bh * 0.5f, bh * 0.07f)
            }
            "minijoy" -> {
                for (p in 0..1) {
                    val ox = l + bw * (if (p == 0) 0f else 0.5f)
                    val s = p * 8
                    stick(ox + bw * 0.14f, t + bh * 0.48f, bh * 0.15f, s)
                    circle(B shl s, "B", ox + bw * 0.32f, t + bh * 0.58f, bh * 0.065f)
                    circle(A shl s, "A", ox + bw * 0.41f, t + bh * 0.44f, bh * 0.065f)
                    rect(SELECT shl s, "SEL", ox + bw * 0.17f, t + bh * 0.8f, ox + bw * 0.25f, t + bh * 0.86f)
                    rect(START shl s, "START", ox + bw * 0.28f, t + bh * 0.8f, ox + bw * 0.36f, t + bh * 0.86f)
                }
            }
            "tennis" -> {
                circle(SWING, "", l + bw * 0.62f, t + bh * 0.4f, bh * 0.36f)
                circle(RACKET_BUTTON, "", l + bw * 0.315f, t + bh * 0.78f, bh * 0.055f)
                receiverPad(l, t, bw, bh)
            }
            "fourplay" -> {
                // the pads sit two by two, filling the space under the TV
                val gw = w * 0.47f; val gh = h * 0.46f
                for (p in 0..3) {
                    val ox = w * 0.02f + (p % 2) * (gw + w * 0.02f)
                    val oy = h * 0.03f + (p / 2) * (gh + h * 0.03f)
                    val r = minOf(gw, gh) * 0.2f
                    if (p == 0) {
                        stick(ox + gw * 0.25f, oy + gh * 0.5f, r, 0, intArrayOf(F_UP, F_DOWN, F_LEFT, F_RIGHT))
                        circle(F_B1, "1", ox + gw * 0.66f, oy + gh * 0.58f, r * 0.45f)
                        circle(F_B2, "2", ox + gw * 0.84f, oy + gh * 0.42f, r * 0.45f)
                        rect(F_SELECT, "SELECT", ox + gw * 0.4f, oy + gh * 0.16f, ox + gw * 0.52f, oy + gh * 0.23f)
                        rect(F_START, "START", ox + gw * 0.56f, oy + gh * 0.16f, ox + gw * 0.68f, oy + gh * 0.23f)
                    } else {
                        val s = 8 * p
                        stick(ox + gw * 0.25f, oy + gh * 0.5f, r, s, intArrayOf(0x01, 0x02, 0x04, 0x08))
                        circle(0x10 shl s, "1", ox + gw * 0.66f, oy + gh * 0.58f, r * 0.45f)
                        circle(0x20 shl s, "2", ox + gw * 0.84f, oy + gh * 0.42f, r * 0.45f)
                    }
                }
            }
            "kickbox" -> {
                circle(L_PUNCH, "", l + bw * 0.36f, t + bh * 0.25f, bh * 0.2f)
                circle(R_PUNCH, "", l + bw * 0.64f, t + bh * 0.25f, bh * 0.2f)
                rect(L_KICK, "", l + bw * 0.28f, t + bh * 0.58f, l + bw * 0.44f, t + bh * 0.95f)
                rect(R_KICK, "", l + bw * 0.56f, t + bh * 0.58f, l + bw * 0.72f, t + bh * 0.95f)
                circle(GUARD, "GUARD", l + bw * 0.5f, t + bh * 0.52f, bh * 0.075f)
                receiverPad(l, t, bw, bh)
            }
        }
    }

    /** The receiver box's own buttons on the left (menus). */
    private fun receiverPad(l: Float, t: Float, bw: Float, bh: Float) {
        val cx = l + bw * 0.1f; val cy = t + bh * 0.3f; val d = bh * 0.085f; val r = bh * 0.05f
        circle(S_UP, "▲", cx, cy - d, r); circle(S_DOWN, "▼", cx, cy + d, r)
        circle(S_LEFT, "◀", cx - d, cy, r); circle(S_RIGHT, "▶", cx + d, cy, r)
        circle(S_OK, "OK", l + bw * 0.06f, t + bh * 0.58f, bh * 0.055f)
        circle(S_BACK, "✕", l + bw * 0.15f, t + bh * 0.58f, bh * 0.055f)
    }

    // ---- drawing ----
    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0814.toInt())
        val held = mask or externalMask
        val bw = body.width(); val bh = body.height()
        // cable up to the TV
        stroke.color = 0xFF202020.toInt(); stroke.strokeWidth = bw * 0.01f
        c.drawLine(body.centerX(), body.top + bh * 0.1f, body.centerX(), 0f, stroke)
        when (style) {
            "pinball" -> drawPinball(c, held)
            "pingpong" -> drawPingPong(c, held)
            "minijoy" -> drawMiniJoy(c, held)
            "tennis" -> { drawReceiver(c, held); drawRacket(c, held) }
            "kickbox" -> { drawReceiver(c, held); drawKickBox(c, held) }
            "fourplay" -> drawFourPlay(c, held)
        }
    }

    private fun shade(c: Canvas, x: Float, y: Float, r: Float, light: Int, dark: Int, down: Boolean) {
        paint.color = 0x55000000; c.drawCircle(x, y + r * 0.12f, r * 1.04f, paint)
        paint.color = Color.WHITE
        paint.shader = RadialGradient(x - r * 0.3f, y - r * 0.35f, r * 1.3f, if (down) dark else light, dark, Shader.TileMode.CLAMP)
        c.drawCircle(x, y + (if (down) r * 0.06f else 0f), r, paint)
        paint.shader = null
    }

    private fun label(c: Canvas, s: String, x: Float, y: Float, size: Float, color: Int) {
        text.color = color; text.textSize = size
        c.drawText(s, x, y + size * 0.36f, text)
    }

    private fun drawButtons(c: Canvas, held: Int, light: Int, dark: Int, ink: Int, skip: Set<Int> = emptySet()) {
        for (k in ctls) {
            if (k.stick || k.bit in skip) continue
            val down = held and k.bit != 0
            if (k.r > 0f) {
                shade(c, k.cx, k.cy, k.r, light, dark, down)
                if (k.label.isNotEmpty()) label(c, k.label, k.cx, k.cy, k.r * (if (k.label.length > 2) 0.42f else 0.8f), ink)
            } else {
                paint.color = if (down) dark else light
                c.drawRoundRect(k.rect, k.rect.height() / 2, k.rect.height() / 2, paint)
                if (k.label.isNotEmpty()) label(c, k.label, k.rect.centerX(), k.rect.bottom + k.rect.height() * 0.9f, k.rect.height() * 0.7f, ink)
            }
        }
    }

    private fun drawPinball(c: Canvas, held: Int) {
        val bw = body.width(); val bh = body.height()
        // front panel: a slanted cabinet face in deep blue with chrome trim
        path.reset()
        path.moveTo(body.left + bw * 0.06f, body.top + bh * 0.08f); path.lineTo(body.right - bw * 0.06f, body.top + bh * 0.08f)
        path.lineTo(body.right, body.bottom); path.lineTo(body.left, body.bottom); path.close()
        paint.shader = LinearGradient(0f, body.top, 0f, body.bottom, 0xFF1F3C9A.toInt(), 0xFF0E1C52.toInt(), Shader.TileMode.CLAMP)
        c.drawPath(path, paint); paint.shader = null
        stroke.color = 0xFFC9CFD8.toInt(); stroke.strokeWidth = bw * 0.012f; c.drawPath(path, stroke)
        // star field decoration
        paint.color = 0x33FFE07A
        for (i in 0 until 14) c.drawCircle(body.left + bw * (0.12f + (i * 37 % 76) / 100f), body.top + bh * (0.15f + (i * 53 % 60) / 100f), bw * 0.006f, paint)
        // flipper buttons: red domes on the side walls
        for (k in ctls.filter { it.bit == A || it.bit == B }) shade(c, k.cx, k.cy, k.r, 0xFFFF5A4A.toInt(), 0xFFA0140E.toInt(), held and k.bit != 0)
        // plunger: chrome rod with a knob, pulled down while held
        val p = ctls.first { it.bit == DOWN }.rect
        val pull = if (held and DOWN != 0) p.height() * 0.28f else 0f
        paint.color = 0xFF0A1236.toInt(); c.drawRoundRect(p, p.width() * 0.2f, p.width() * 0.2f, paint)
        paint.shader = LinearGradient(p.left, 0f, p.right, 0f, intArrayOf(0xFF8C939E.toInt(), 0xFFF2F4F7.toInt(), 0xFF7A808A.toInt()), null, Shader.TileMode.CLAMP)
        c.drawRect(p.centerX() - p.width() * 0.08f, p.top + p.height() * 0.05f, p.centerX() + p.width() * 0.08f, p.top + p.height() * 0.62f + pull, paint)
        paint.shader = null
        // spring coils
        stroke.color = 0xFFB0B6C0.toInt(); stroke.strokeWidth = p.width() * 0.04f
        val coils = 6; val top = p.top + p.height() * 0.12f; val bot = p.top + p.height() * 0.55f + pull * 0.6f
        for (i in 0 until coils) { val y = top + (bot - top) * i / (coils - 1); c.drawLine(p.centerX() - p.width() * 0.2f, y, p.centerX() + p.width() * 0.2f, y + p.height() * 0.02f, stroke) }
        shade(c, p.centerX(), p.top + p.height() * 0.72f + pull, p.width() * 0.32f, 0xFFF0F0F0.toInt(), 0xFF8A8A8A.toInt(), false)
        label(c, "PULL", p.centerX(), p.bottom + bh * 0.06f, bh * 0.05f, 0xFFC9D4FF.toInt())
        drawButtons(c, held, 0xFFFFD34A.toInt(), 0xFFB07A06.toInt(), 0xFF3A2400.toInt(), setOf(A, B, DOWN))
        label(c, "FLIPPER", body.left + bw * 0.1f, body.top + bh * 0.28f, bh * 0.045f, 0xFFC9D4FF.toInt())
        label(c, "FLIPPER", body.right - bw * 0.1f, body.top + bh * 0.28f, bh * 0.045f, 0xFFC9D4FF.toInt())
    }

    private fun drawPingPong(c: Canvas, held: Int) {
        val bw = body.width(); val bh = body.height()
        val blade = ctls.first { it.bit == A }
        // wooden handle with flared grip
        val hr = RectF(body.left + bw * 0.04f, body.top + bh * 0.4f, blade.cx - blade.r * 0.7f, body.top + bh * 0.6f)
        paint.shader = LinearGradient(0f, hr.top, 0f, hr.bottom, intArrayOf(0xFFE2B57A.toInt(), 0xFFB98444.toInt(), 0xFF8A5A26.toInt()), null, Shader.TileMode.CLAMP)
        c.drawRoundRect(hr, hr.height() * 0.4f, hr.height() * 0.4f, paint); paint.shader = null
        stroke.color = 0xFF6E4318.toInt(); stroke.strokeWidth = bw * 0.004f
        c.drawRoundRect(hr, hr.height() * 0.4f, hr.height() * 0.4f, stroke)
        // blade: wooden edge then red rubber with pimples
        val down = held and A != 0
        val r = blade.r * (if (down) 0.96f else 1f)
        paint.color = 0xFFB98444.toInt(); c.drawOval(blade.cx - r * 1.05f, blade.cy - r, blade.cx + r * 1.05f, blade.cy + r, paint)
        paint.shader = RadialGradient(blade.cx - r * 0.3f, blade.cy - r * 0.3f, r * 1.4f, 0xFFE8342E.toInt(), 0xFF8E0F0C.toInt(), Shader.TileMode.CLAMP)
        c.drawOval(blade.cx - r * 0.98f, blade.cy - r * 0.93f, blade.cx + r * 0.98f, blade.cy + r * 0.93f, paint); paint.shader = null
        paint.color = 0x22000000
        var y = blade.cy - r * 0.8f
        while (y < blade.cy + r * 0.8f) { var x = blade.cx - r * 0.85f; while (x < blade.cx + r * 0.85f) { if (hypot((x - blade.cx) / 0.98f, (y - blade.cy) / 0.93f) < r * 0.85f) c.drawCircle(x, y, r * 0.018f, paint); x += r * 0.09f }; y += r * 0.09f }
        label(c, "TAP TO SWING / OK", blade.cx, blade.cy + r * 0.05f, bh * 0.055f, 0x99FFFFFF.toInt())
        drawButtons(c, held, 0xFF3A3A3A.toInt(), 0xFF101010.toInt(), 0xFFF2F2F2.toInt(), setOf(A))
    }

    private fun drawMiniJoy(c: Canvas, held: Int) {
        val bw = body.width(); val bh = body.height()
        for (p in 0..1) {
            val ox = body.left + bw * (if (p == 0) 0f else 0.5f)
            val r = RectF(ox + bw * 0.02f, body.top + bh * 0.12f, ox + bw * 0.48f, body.bottom - bh * 0.02f)
            // teal base, white top plate
            paint.color = 0x55000000; c.drawRoundRect(r.left + bw * 0.006f, r.top + bw * 0.01f, r.right + bw * 0.006f, r.bottom + bw * 0.01f, bh * 0.12f, bh * 0.12f, paint)
            paint.shader = LinearGradient(0f, r.top, 0f, r.bottom, 0xFF22B3B0.toInt(), 0xFF0E7B7A.toInt(), Shader.TileMode.CLAMP)
            c.drawRoundRect(r, bh * 0.12f, bh * 0.12f, paint); paint.shader = null
            val top = RectF(r).apply { inset(bw * 0.018f, bh * 0.05f); bottom -= bh * 0.2f }
            paint.color = 0xFFF4F6F6.toInt(); c.drawRoundRect(top, bh * 0.09f, bh * 0.09f, paint)
            stroke.color = 0xFFBFC9C9.toInt(); stroke.strokeWidth = bw * 0.003f; c.drawRoundRect(top, bh * 0.09f, bh * 0.09f, stroke)
            label(c, if (p == 0) "1P" else "2P", r.right - bw * 0.04f, r.bottom - bh * 0.1f, bh * 0.06f, 0xFFE8FFFF.toInt())
        }
        // sticks: teal collar, black shaft, grey-green ball top
        for (k in ctls.filter { it.stick }) {
            paint.color = 0xFF0E7B7A.toInt(); c.drawCircle(k.cx, k.cy, k.r, paint)
            paint.color = 0xFF16999A.toInt(); c.drawCircle(k.cx, k.cy, k.r * 0.72f, paint)
            val kx = k.cx + k.knobDx * k.r * 0.45f; val ky = k.cy + k.knobDy * k.r * 0.45f
            stroke.color = 0xFF1A1A1A.toInt(); stroke.strokeWidth = k.r * 0.24f; stroke.strokeCap = Paint.Cap.ROUND
            c.drawLine(k.cx, k.cy, kx, ky, stroke); stroke.strokeCap = Paint.Cap.BUTT
            paint.shader = RadialGradient(kx - k.r * 0.15f, ky - k.r * 0.18f, k.r * 0.6f, 0xFF6F8579.toInt(), 0xFF2B3A33.toInt(), Shader.TileMode.CLAMP)
            c.drawCircle(kx, ky, k.r * 0.44f, paint); paint.shader = null
        }
        drawButtons(c, held, 0xFF2CC7C4.toInt(), 0xFF0B6E6C.toInt(), 0xFFFFFFFF.toInt())
    }

    private val playerColours = intArrayOf(0xFF2F6FE0.toInt(), 0xFFE0402F.toInt(), 0xFF2FA84A.toInt(), 0xFFE0B02F.toInt())

    private fun drawFourPlay(c: Canvas, held: Int) {
        val sticks = ctls.filter { it.stick }
        for ((p, k) in sticks.withIndex()) {
            // pad body: a rounded grey controller with a coloured player stripe
            val gw = width * 0.47f; val gh = height * 0.46f
            val ox = width * 0.02f + (p % 2) * (gw + width * 0.02f)
            val oy = height * 0.03f + (p / 2) * (gh + height * 0.03f)
            val r = RectF(ox, oy + gh * 0.08f, ox + gw, oy + gh * 0.95f)
            paint.color = 0x55000000; c.drawRoundRect(r.left + 4f, r.top + 6f, r.right + 4f, r.bottom + 6f, gh * 0.3f, gh * 0.3f, paint)
            paint.shader = LinearGradient(0f, r.top, 0f, r.bottom, 0xFFD9DCE2.toInt(), 0xFF9A9FA8.toInt(), Shader.TileMode.CLAMP)
            c.drawRoundRect(r, gh * 0.3f, gh * 0.3f, paint); paint.shader = null
            paint.color = playerColours[p]
            c.drawRoundRect(r.centerX() - gw * 0.12f, r.bottom - gh * 0.16f, r.centerX() + gw * 0.12f, r.bottom - gh * 0.07f, gh * 0.04f, gh * 0.04f, paint)
            label(c, "${p + 1}P", r.centerX(), r.bottom - gh * 0.115f, gh * 0.07f, 0xFFFFFFFF.toInt())
            // D-pad cross that tilts toward the thumb
            val a = k.r * 0.95f; val t = k.r * 0.34f
            paint.color = 0xFF2A2D33.toInt()
            c.drawRoundRect(k.cx - a, k.cy - t, k.cx + a, k.cy + t, t * 0.3f, t * 0.3f, paint)
            c.drawRoundRect(k.cx - t, k.cy - a, k.cx + t, k.cy + a, t * 0.3f, t * 0.3f, paint)
            paint.color = 0xFF4A4E57.toInt()
            val bits = k.stickBits
            val dx = (if (held and bits[3] != 0) 1 else 0) - (if (held and bits[2] != 0) 1 else 0)
            val dy = (if (held and bits[1] != 0) 1 else 0) - (if (held and bits[0] != 0) 1 else 0)
            c.drawCircle(k.cx + dx * a * 0.6f, k.cy + dy * a * 0.6f, t * 0.55f, paint)
        }
        drawButtons(c, held, 0xFFE8473A.toInt(), 0xFF8E1A12.toInt(), 0xFFFFFFFF.toInt())
    }

    private fun drawReceiver(c: Canvas, held: Int) {
        val bw = body.width(); val bh = body.height()
        val r = RectF(body.left, body.top + bh * 0.1f, body.left + bw * 0.21f, body.top + bh * 0.72f)
        paint.shader = LinearGradient(0f, r.top, 0f, r.bottom, 0xFF3A3F49.toInt(), 0xFF1C1F25.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(r, bh * 0.05f, bh * 0.05f, paint); paint.shader = null
        // IR window
        paint.color = 0xFF5A0A12.toInt(); c.drawRoundRect(r.left + bw * 0.03f, r.top + bh * 0.03f, r.right - bw * 0.03f, r.top + bh * 0.08f, bh * 0.02f, bh * 0.02f, paint)
        for (k in ctls.filter { it.bit in setOf(S_UP, S_DOWN, S_LEFT, S_RIGHT, S_OK, S_BACK) })
            shade(c, k.cx, k.cy, k.r, 0xFF9AA3B2.toInt(), 0xFF4A5160.toInt(), held and k.bit != 0)
        for (k in ctls.filter { it.bit in setOf(S_UP, S_DOWN, S_LEFT, S_RIGHT, S_OK, S_BACK) })
            label(c, k.label, k.cx, k.cy, k.r * (if (k.label.length > 1) 0.6f else 0.8f), 0xFF1A1C22.toInt())
    }

    private fun drawRacket(c: Canvas, held: Int) {
        val bh = body.height()
        val head = ctls.first { it.bit == SWING }
        val swing = held and SWING != 0
        c.save()
        c.rotate(if (swing) -28f else -12f, head.cx - head.r * 1.4f, head.cy + head.r * 1.3f)
        // handle: white grip with a blue band and the button
        val btn = ctls.first { it.bit == RACKET_BUTTON }
        stroke.color = 0xFFE9EDF2.toInt(); stroke.strokeWidth = head.r * 0.22f; stroke.strokeCap = Paint.Cap.ROUND
        c.drawLine(head.cx - head.r * 0.7f, head.cy + head.r * 0.7f, btn.cx - head.r * 0.15f, btn.cy + head.r * 0.12f, stroke)
        stroke.color = 0xFF2B57C8.toInt(); stroke.strokeWidth = head.r * 0.24f
        c.drawLine(btn.cx + head.r * 0.12f, btn.cy - head.r * 0.08f, btn.cx - head.r * 0.4f, btn.cy + head.r * 0.32f, stroke)
        stroke.strokeCap = Paint.Cap.BUTT
        // frame and strings
        stroke.color = 0xFF2B57C8.toInt(); stroke.strokeWidth = head.r * 0.12f
        c.drawOval(head.cx - head.r * 0.8f, head.cy - head.r, head.cx + head.r * 0.8f, head.cy + head.r, stroke)
        stroke.color = 0xFFFFFFFF.toInt(); stroke.strokeWidth = head.r * 0.03f
        c.drawOval(head.cx - head.r * 0.72f, head.cy - head.r * 0.92f, head.cx + head.r * 0.72f, head.cy + head.r * 0.92f, stroke)
        stroke.color = 0xCCE6E6E6.toInt(); stroke.strokeWidth = head.r * 0.012f
        c.save()
        path.reset(); path.addOval(head.cx - head.r * 0.72f, head.cy - head.r * 0.92f, head.cx + head.r * 0.72f, head.cy + head.r * 0.92f, Path.Direction.CW)
        c.clipPath(path)
        var x = head.cx - head.r; while (x < head.cx + head.r) { c.drawLine(x, head.cy - head.r, x, head.cy + head.r, stroke); x += head.r * 0.12f }
        var y = head.cy - head.r; while (y < head.cy + head.r) { c.drawLine(head.cx - head.r, y, head.cx + head.r, y, stroke); y += head.r * 0.12f }
        c.restore()
        c.restore()
        shade(c, btn.cx, btn.cy, btn.r, 0xFFFFD34A.toInt(), 0xFFB07A06.toInt(), held and RACKET_BUTTON != 0)
        label(c, "TAP / SWIPE TO SWING", head.cx, head.cy + head.r * 1.18f, bh * 0.05f, 0xFFB8C4D8.toInt())
        label(c, "SERVE", btn.cx, btn.cy + btn.r * 1.9f, bh * 0.045f, 0xFFB8C4D8.toInt())
    }

    private fun drawKickBox(c: Canvas, held: Int) {
        // gloves
        for (k in ctls.filter { it.bit == L_PUNCH || it.bit == R_PUNCH }) {
            val down = held and k.bit != 0
            val r = k.r * (if (down) 1.12f else 1f)
            val dir = if (k.bit == L_PUNCH) -1 else 1
            paint.color = 0x55000000; c.drawCircle(k.cx, k.cy + r * 0.1f, r, paint)
            paint.shader = RadialGradient(k.cx - r * 0.3f, k.cy - r * 0.35f, r * 1.4f, 0xFFF0453A.toInt(), 0xFF8C0E0A.toInt(), Shader.TileMode.CLAMP)
            c.drawOval(k.cx - r, k.cy - r * 0.9f, k.cx + r, k.cy + r * 0.95f, paint)
            // thumb and white cuff
            c.drawOval(k.cx - dir * r * 1.05f - r * 0.32f, k.cy - r * 0.1f, k.cx - dir * r * 1.05f + r * 0.32f, k.cy + r * 0.6f, paint)
            paint.shader = null
            paint.color = 0xFFF4F4F4.toInt(); c.drawRoundRect(k.cx - r * 0.62f, k.cy + r * 0.8f, k.cx + r * 0.62f, k.cy + r * 1.22f, r * 0.12f, r * 0.12f, paint)
            label(c, "PUNCH", k.cx, k.cy, r * 0.28f, 0xCCFFFFFF.toInt())
        }
        // foot pads: dark soles with a red strap
        for (k in ctls.filter { it.bit == L_KICK || it.bit == R_KICK }) {
            val down = held and k.bit != 0
            val r = RectF(k.rect).apply { if (down) inset(width() * 0.04f, height() * 0.03f) }
            paint.color = if (down) 0xFF14161A.toInt() else 0xFF2A2E36.toInt()
            c.drawRoundRect(r, r.width() * 0.45f, r.width() * 0.45f, paint)
            paint.color = 0xFFD8231C.toInt(); c.drawRect(r.left, r.top + r.height() * 0.42f, r.right, r.top + r.height() * 0.55f, paint)
            label(c, "KICK", r.centerX(), r.top + r.height() * 0.25f, r.width() * 0.22f, 0xFFC0C6D0.toInt())
        }
        val g = ctls.first { it.bit == GUARD }
        shade(c, g.cx, g.cy, g.r, 0xFF5D8BFF.toInt(), 0xFF1D3C9A.toInt(), held and GUARD != 0)
        label(c, "GUARD", g.cx, g.cy, g.r * 0.4f, 0xFFFFFFFF.toInt())
    }

    // ---- touch ----
    private fun stickBits(k: Ctl, x: Float, y: Float): Int {
        val dx = x - k.cx; val dy = y - k.cy
        k.knobDx = (dx / k.r).coerceIn(-1f, 1f); k.knobDy = (dy / k.r).coerceIn(-1f, 1f)
        if (hypot(dx, dy) < k.r * 0.25f) return 0
        val a = Math.toDegrees(atan2(dy.toDouble(), dx.toDouble()))
        var b = 0
        if (a > -67.5 && a < 67.5) b = b or k.stickBits[3]
        if (a > 112.5 || a < -112.5) b = b or k.stickBits[2]
        if (a > 22.5 && a < 157.5) b = b or k.stickBits[1]
        if (a < -22.5 && a > -157.5) b = b or k.stickBits[0]
        return b
    }

    private fun at(x: Float, y: Float): Ctl? = ctls.firstOrNull { it.hit(x, y) }

    private fun update() {
        var m = 0
        for ((id, k) in pointers) m = m or if (k == null) 0 else if (k.stick) (stickOf[id] ?: 0) else k.bit
        if (m != mask) {
            if (m and mask.inv() != 0) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            mask = m
            onInput?.invoke(m)
        }
        invalidate()
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex; val id = e.getPointerId(i)
                val k = at(e.getX(i), e.getY(i))
                pointers[id] = k
                if (k?.stick == true) stickOf[id] = stickBits(k, e.getX(i), e.getY(i))
                update()
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until e.pointerCount) {
                    val id = e.getPointerId(i)
                    val k = pointers[id]
                    if (k?.stick == true) stickOf[id] = stickBits(k, e.getX(i), e.getY(i))
                    else if (pointers.containsKey(id)) {
                        // a finger sliding onto the racket head counts as a swing too
                        val n = at(e.getX(i), e.getY(i))
                        if (n?.stick != true) pointers[id] = n
                    }
                }
                update()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val id = e.getPointerId(e.actionIndex)
                pointers.remove(id)?.let { if (it.stick) { it.knobDx = 0f; it.knobDy = 0f } }
                stickOf.remove(id)
                update()
            }
            MotionEvent.ACTION_CANCEL -> {
                pointers.clear(); stickOf.clear(); ctls.forEach { it.knobDx = 0f; it.knobDy = 0f }; update()
            }
        }
        return true
    }
}
