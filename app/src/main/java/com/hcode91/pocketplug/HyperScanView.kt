package com.hcode91.pocketplug

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.PointF
import android.graphics.RadialGradient
import android.graphics.RectF
import android.graphics.Shader
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import kotlin.math.hypot
import kotlin.math.min

/** Button bits for the HyperScan core (see hyperscan.h). Directions push the analog stick fully. */
object HsBits {
    const val UP = 1 shl 0; const val DOWN = 1 shl 1; const val LEFT = 1 shl 2; const val RIGHT = 1 shl 3
    const val GREEN = 1 shl 4; const val RED = 1 shl 5; const val YELLOW = 1 shl 6; const val BLUE = 1 shl 7
    const val START = 1 shl 8; const val SELECT = 1 shl 9
    const val L_SHOULDER = 1 shl 10; const val R_SHOULDER = 1 shl 11; const val L_TRIGGER = 1 shl 12; const val R_TRIGGER = 1 shl 13

    fun keyBit(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> UP
        KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> DOWN
        KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> RIGHT
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_K -> GREEN
        KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_L -> RED
        KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_I -> YELLOW
        KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_J -> BLUE
        KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_ENTER -> START
        KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_TAB -> SELECT
        KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_Q -> L_SHOULDER
        KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_E -> R_SHOULDER
        KeyEvent.KEYCODE_BUTTON_L2, KeyEvent.KEYCODE_1 -> L_TRIGGER
        KeyEvent.KEYCODE_BUTTON_R2, KeyEvent.KEYCODE_3 -> R_TRIGGER
        else -> 0
    }
}

/**
 * The console's controller: a silver shell over black grips; a red-capped analog stick on the
 * left, four round buttons in a diamond on the right (red top, blue right, yellow bottom, green
 * left), two small red keys (Select over Start) on a black block in the middle, and shoulder
 * buttons with triggers behind them along the top.
 */
class HyperScanPadView(ctx: Context) : View(ctx) {
    var onInput: ((Int) -> Unit)? = null
    /** stick 0: the analog stick, -1..1 each way (up and left negative) */
    var onAnalog: ((Int, Float, Float) -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    private class Btn(val bit: Int, val color: Int, val label: String) { val r = RectF(); var round = true }
    private val buttons = listOf(
        Btn(HsBits.RED, 0xFFD8302A.toInt(), ""), Btn(HsBits.BLUE, 0xFF2D6FD6.toInt(), ""),
        Btn(HsBits.YELLOW, 0xFFF2C230.toInt(), ""), Btn(HsBits.GREEN, 0xFF3BB04A.toInt(), ""),
        Btn(HsBits.SELECT, 0xFFC8231E.toInt(), "SELECT").apply { round = false },
        Btn(HsBits.START, 0xFFC8231E.toInt(), "START").apply { round = false },
        Btn(HsBits.L_TRIGGER, 0xFF2A2C30.toInt(), "L2").apply { round = false },
        Btn(HsBits.L_SHOULDER, 0xFF34373C.toInt(), "L1").apply { round = false },
        Btn(HsBits.R_SHOULDER, 0xFF34373C.toInt(), "R1").apply { round = false },
        Btn(HsBits.R_TRIGGER, 0xFF2A2C30.toInt(), "R2").apply { round = false },
    )
    private val body = RectF()
    private val block = RectF()
    private val stick = PointF(); private var stickR = 0f
    private var stickX = 0f; private var stickY = 0f; private var stickPointer = -1
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; isFakeBoldText = true }
    private val down = HashMap<Int, Int>()          // pointer -> bits
    private var mask = 0

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val bw = min(w * 0.96f, h * 2.2f)
        body.set((w - bw) / 2, h * 0.2f, (w + bw) / 2, h * 0.96f)
        val bh = body.height()
        // shoulders and triggers on top
        val sw = bw * 0.16f; val sh = h * 0.07f
        buttons[6].r.set(body.left + bw * 0.04f, h * 0.02f, body.left + bw * 0.04f + sw, h * 0.02f + sh)
        buttons[7].r.set(body.left + bw * 0.04f, h * 0.1f, body.left + bw * 0.04f + sw * 1.15f, h * 0.1f + sh)
        buttons[8].r.set(body.right - bw * 0.04f - sw * 1.15f, h * 0.1f, body.right - bw * 0.04f, h * 0.1f + sh)
        buttons[9].r.set(body.right - bw * 0.04f - sw, h * 0.02f, body.right - bw * 0.04f, h * 0.02f + sh)
        stickR = bh * 0.22f
        stick.set(body.left + bw * 0.22f, body.top + bh * 0.48f)
        val cx = body.left + bw * 0.76f; val cy = body.top + bh * 0.48f
        val br = bh * 0.1f; val d = bh * 0.21f
        buttons[0].r.set(cx - br, cy - d - br, cx + br, cy - d + br)
        buttons[1].r.set(cx + d - br, cy - br, cx + d + br, cy + br)
        buttons[2].r.set(cx - br, cy + d - br, cx + br, cy + d + br)
        buttons[3].r.set(cx - d - br, cy - br, cx - d + br, cy + br)
        // Select and Start: two small red keys, one above the other, on a black block at the top middle
        val mw = bw * 0.1f; val mh = bh * 0.11f; val mx = body.centerX() + bw * 0.08f
        buttons[4].r.set(mx - mw / 2, body.top + bh * 0.14f, mx + mw / 2, body.top + bh * 0.14f + mh)
        buttons[5].r.set(mx - mw / 2, body.top + bh * 0.3f, mx + mw / 2, body.top + bh * 0.3f + mh)
        block.set(mx - mw * 0.75f, body.top + bh * 0.08f, mx + mw * 0.75f, body.top + bh * 0.47f)
        t.textSize = mh * 0.55f
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0B0E.toInt())
        val lit = mask or externalMask
        // shoulders behind the body
        for (b in buttons.subList(6, 10)) drawButton(c, b, lit)
        p.shader = null
        val rad = body.height() * 0.45f
        // black grips under a silver top shell
        p.color = 0xFF151618.toInt()
        c.drawRoundRect(RectF(body.left, body.top + body.height() * 0.35f, body.left + body.width() * 0.3f, body.bottom), rad, rad, p)
        c.drawRoundRect(RectF(body.right - body.width() * 0.3f, body.top + body.height() * 0.35f, body.right, body.bottom), rad, rad, p)
        val shell = RectF(body.left + body.width() * 0.05f, body.top, body.right - body.width() * 0.05f, body.top + body.height() * 0.78f)
        p.shader = android.graphics.LinearGradient(0f, shell.top, 0f, shell.bottom, 0xFFD9DCE0.toInt(), 0xFF8E9399.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(shell, rad, rad, p)
        p.shader = null
        p.color = 0xFF1E1F22.toInt(); c.drawRoundRect(block, block.width() * 0.2f, block.width() * 0.2f, p)
        // stick: black collar, red cap
        p.color = 0xFF141517.toInt(); c.drawCircle(stick.x, stick.y, stickR * 1.1f, p)
        val kx = stick.x + stickX * stickR * 0.45f; val ky = stick.y + stickY * stickR * 0.45f
        p.color = 0xFF2A2B2E.toInt(); c.drawCircle(kx, ky, stickR * 0.7f, p)
        p.shader = RadialGradient(kx - stickR * 0.15f, ky - stickR * 0.2f, stickR * 0.5f, 0xFFFF6A5E.toInt(), 0xFF9E1712.toInt(), Shader.TileMode.CLAMP)
        c.drawCircle(kx, ky, stickR * 0.45f, p)
        p.shader = null
        // the buttons sit in a black round pad
        p.color = 0xFF1A1B1E.toInt()
        c.drawCircle(buttons[0].r.centerX(), buttons[1].r.centerY(), (buttons[1].r.right - buttons[3].r.left) * 0.62f, p)
        for (b in buttons.subList(0, 6)) drawButton(c, b, lit)
    }

    private fun drawButton(c: Canvas, b: Btn, lit: Int) {
        val on = lit and b.bit != 0
        p.shader = null
        p.color = if (on) brighten(b.color) else b.color
        if (b.round) {
            c.drawCircle(b.r.centerX(), b.r.centerY(), b.r.width() / 2, p)
            p.color = 0x33FFFFFF
            c.drawCircle(b.r.centerX() - b.r.width() * 0.12f, b.r.centerY() - b.r.width() * 0.12f, b.r.width() * 0.22f, p)
        } else {
            c.drawRoundRect(b.r, b.r.height() * 0.45f, b.r.height() * 0.45f, p)
            t.color = if (on || b.bit == HsBits.START || b.bit == HsBits.SELECT) 0xFFFFFFFF.toInt() else 0xFFB8BCC4.toInt()
            t.textSize = b.r.height() * 0.42f
            c.drawText(b.label, b.r.centerX(), b.r.centerY() + t.textSize * 0.35f, t)
        }
    }

    private fun brighten(c: Int): Int {
        val r = min(255, ((c shr 16) and 255) + 70); val g = min(255, ((c shr 8) and 255) + 70); val bl = min(255, (c and 255) + 70)
        return (0xFF shl 24) or (r shl 16) or (g shl 8) or bl
    }

    private fun hit(x: Float, y: Float): Int {
        for (b in buttons) {
            val r = RectF(b.r); r.inset(-b.r.width() * 0.25f, -b.r.height() * 0.25f)
            if (r.contains(x, y)) return b.bit
        }
        return 0
    }

    private fun updateStick(x: Float, y: Float) {
        var dx = (x - stick.x) / stickR; var dy = (y - stick.y) / stickR
        val len = hypot(dx, dy)
        if (len > 1f) { dx /= len; dy /= len }
        stickX = dx; stickY = dy
        onAnalog?.invoke(0, dx, dy)
        invalidate()
    }

    private fun publish() {
        val m = down.values.fold(0) { a, b -> a or b }
        if (m != mask) { mask = m; onInput?.invoke(m); invalidate() }
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        val i = e.actionIndex
        val id = e.getPointerId(i)
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val x = e.getX(i); val y = e.getY(i)
                if (stickPointer < 0 && hypot(x - stick.x, y - stick.y) < stickR * 1.5f) { stickPointer = id; updateStick(x, y) }
                else { down[id] = hit(x, y); publish() }
            }
            MotionEvent.ACTION_MOVE -> {
                for (k in 0 until e.pointerCount) {
                    val pid = e.getPointerId(k)
                    if (pid == stickPointer) updateStick(e.getX(k), e.getY(k))
                    else if (down.containsKey(pid)) down[pid] = hit(e.getX(k), e.getY(k))
                }
                publish()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                if (id == stickPointer || e.actionMasked == MotionEvent.ACTION_CANCEL) {
                    stickPointer = -1; stickX = 0f; stickY = 0f; onAnalog?.invoke(0, 0f, 0f); invalidate()
                }
                down.remove(id)
                if (e.actionMasked == MotionEvent.ACTION_CANCEL) down.clear()
                publish()
            }
        }
        return true
    }
}

/**
 * The console itself, as a strip above the controller: a black slab with a red rim, the disc lid
 * on the left, the glowing red card scanner on the right, and two round chrome keys (Reset,
 * Power) on the front edge between them.
 */
class HyperScanConsoleView(ctx: Context) : View(ctx) {
    var onReset: (() -> Unit)? = null
    var onPower: (() -> Unit)? = null
    private val body = RectF(); private val lid = RectF()
    private val reset = PointF(); private val power = PointF(); private var keyR = 0f
    private val scan = PointF(); private var scanR = 0f
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; color = 0xFFB8BCC4.toInt() }
    private var pressed = 0

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val bw = min(w * 0.96f, h * 4.2f)
        body.set((w - bw) / 2, h * 0.12f, (w + bw) / 2, h * 0.9f)
        lid.set(body.left + bw * 0.04f, body.top + body.height() * 0.12f, body.left + bw * 0.44f, body.bottom - body.height() * 0.12f)
        scanR = body.height() * 0.38f
        scan.set(body.right - bw * 0.2f, body.centerY())
        keyR = body.height() * 0.12f
        reset.set(body.left + bw * 0.52f, body.bottom - body.height() * 0.28f)
        power.set(body.left + bw * 0.6f, body.bottom - body.height() * 0.28f)
        t.textSize = keyR * 0.8f
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0B0E.toInt())
        val rad = body.height() * 0.18f
        p.color = 0xFF17181B.toInt(); c.drawRoundRect(body, rad, rad, p)
        p.style = Paint.Style.STROKE; p.strokeWidth = body.height() * 0.035f; p.color = 0xFFB5231F.toInt()
        c.drawRoundRect(body, rad, rad, p)
        p.strokeWidth = body.height() * 0.015f; p.color = 0xFF2E3034.toInt()
        c.drawRoundRect(lid, rad * 0.7f, rad * 0.7f, p)
        // scanner: red glow and rings
        p.style = Paint.Style.FILL
        p.shader = RadialGradient(scan.x, scan.y, scanR * 1.3f, 0xAAFF2A20.toInt(), 0x00FF2A20, Shader.TileMode.CLAMP)
        c.drawCircle(scan.x, scan.y, scanR * 1.3f, p)
        p.shader = null
        p.style = Paint.Style.STROKE
        for (k in 1..3) { p.strokeWidth = scanR * 0.08f; p.color = 0xFFD8302A.toInt(); c.drawCircle(scan.x, scan.y, scanR * k / 3.2f, p) }
        p.style = Paint.Style.FILL
        p.color = 0xFFFF5A4E.toInt(); c.drawCircle(scan.x, scan.y, scanR * 0.12f, p)
        for ((pt, label, bit) in listOf(Triple(reset, "RESET", 1), Triple(power, "POWER", 2))) {
            p.shader = RadialGradient(pt.x - keyR * 0.3f, pt.y - keyR * 0.3f, keyR * 1.2f,
                if (pressed and bit != 0) 0xFFFFFFFF.toInt() else 0xFFE8D2C8.toInt(), 0xFF8A5A50.toInt(), Shader.TileMode.CLAMP)
            c.drawCircle(pt.x, pt.y, keyR, p)
            p.shader = null
            c.drawText(label, pt.x, pt.y - keyR * 1.4f, t)
        }
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        val bit = when {
            hypot(e.x - reset.x, e.y - reset.y) < keyR * 2f -> 1
            hypot(e.x - power.x, e.y - power.y) < keyR * 2f -> 2
            else -> 0
        }
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
