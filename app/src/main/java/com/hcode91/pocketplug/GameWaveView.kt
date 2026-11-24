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
import kotlin.math.hypot
import kotlin.math.min

/**
 * Input bits for the Game Wave core (see gamewave.h): bit n is remote key code n
 * (digits 0-9, Up 10, Down 11, Right 12, Left 13, Select 14, DVD menu 15, A-D 16-19,
 * Game menu 20); bits 24..26 say which coloured remote is pressing.
 */
object GwBits {
    const val UP = 1 shl 10; const val DOWN = 1 shl 11; const val RIGHT = 1 shl 12; const val LEFT = 1 shl 13
    const val SEL = 1 shl 14; const val SETUP = 1 shl 15
    const val A = 1 shl 16; const val B = 1 shl 17; const val C = 1 shl 18; const val D = 1 shl 19
    const val MENU = 1 shl 20
    fun digit(n: Int) = 1 shl n
    const val REMOTE_SHIFT = 24
    val REMOTE_COLORS = intArrayOf(0xFFE0302C.toInt(), 0xFFF2CC1E.toInt(), 0xFF2D78E0.toInt(), 0xFF35B850.toInt(), 0xFF9A48D0.toInt(), 0xFFF08A1C.toInt())
    val REMOTE_NAMES = arrayOf("RED", "YELLOW", "BLUE", "GREEN", "PURPLE", "ORANGE")

    fun keyBit(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> UP
        KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> DOWN
        KeyEvent.KEYCODE_DPAD_LEFT -> LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT -> RIGHT
        KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_SPACE -> SEL
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_A -> A
        KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_B -> B
        KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_C -> C
        KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_D -> D
        KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_M -> MENU
        KeyEvent.KEYCODE_TAB -> SETUP
        in KeyEvent.KEYCODE_0..KeyEvent.KEYCODE_9 -> digit(code - KeyEvent.KEYCODE_0)
        in KeyEvent.KEYCODE_NUMPAD_0..KeyEvent.KEYCODE_NUMPAD_9 -> digit(code - KeyEvent.KEYCODE_NUMPAD_0)
        else -> 0
    }
}

/**
 * The remote: a black TV-style remote with grey keys. Along the top, a ring in the remote's
 * colour holds A B C D; below it the arrow keys around SEL sit in a coloured surround; then the
 * number keys with their letters, Setup and Menu. A row of six dots above picks which
 * remote you are holding (each player has their own colour).
 */
class GameWaveRemoteView(ctx: Context) : View(ctx) {
    var onInput: ((Int) -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }
    var remote = 0
        private set

    private class Key(val bit: Int, val label: String, val sub: String = "") { var x = 0f; var y = 0f; var r = 0f }
    private val abcd = listOf(Key(GwBits.A, "A"), Key(GwBits.B, "B"), Key(GwBits.C, "C"), Key(GwBits.D, "D"))
    private val arrows = listOf(Key(GwBits.UP, "▲"), Key(GwBits.LEFT, "◀"), Key(GwBits.RIGHT, "▶"), Key(GwBits.DOWN, "▼"))
    private val sel = Key(GwBits.SEL, "SEL")
    private val pad = listOf(
        Key(GwBits.digit(1), "1"), Key(GwBits.digit(2), "2", "ABC"), Key(GwBits.digit(3), "3", "DEF"),
        Key(GwBits.digit(4), "4", "GHI"), Key(GwBits.digit(5), "5", "JKL"), Key(GwBits.digit(6), "6", "MNO"),
        Key(GwBits.digit(7), "7", "PQRS"), Key(GwBits.digit(8), "8", "TUV"), Key(GwBits.digit(9), "9", "WXYZ"),
        Key(GwBits.SETUP, "SETUP"), Key(GwBits.digit(0), "0"), Key(GwBits.MENU, "MENU"),
    )
    private val all = abcd + arrows + listOf(sel) + pad
    private val dots = Array(6) { RectF() }
    private val body = RectF()
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; isFakeBoldText = true }
    private val down = HashMap<Int, Int>()
    private var mask = 0

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val dotR = min(w * 0.035f, h * 0.035f)
        for (i in 0 until 6) {
            val cx = w / 2f + (i - 2.5f) * dotR * 3.2f
            dots[i].set(cx - dotR, h * 0.04f, cx + dotR, h * 0.04f + dotR * 2)
        }
        val bw = min(w * 0.9f, h * 0.62f)
        body.set((w - bw) / 2, h * 0.04f + dotR * 3f, (w + bw) / 2, h * 0.99f)
        val bh = body.height(); val cx = body.centerX()
        // A B C D on an arc
        val r1 = bw * 0.075f
        for (i in 0 until 4) {
            abcd[i].x = cx + (i - 1.5f) * bw * 0.21f
            abcd[i].y = body.top + bh * 0.1f + (if (i == 0 || i == 3) bh * 0.035f else 0f)
            abcd[i].r = r1
        }
        // arrows around SEL
        val dy = body.top + bh * 0.33f; val d = bw * 0.17f
        val ar = bw * 0.075f
        arrows[0].apply { x = cx; y = dy - d; r = ar }
        arrows[1].apply { x = cx - d; y = dy; r = ar }
        arrows[2].apply { x = cx + d; y = dy; r = ar }
        arrows[3].apply { x = cx; y = dy + d; r = ar }
        sel.apply { x = cx; y = dy; r = ar * 0.95f }
        // number pad
        val top = body.top + bh * 0.56f
        val kr = bw * 0.07f
        for (i in pad.indices) {
            val col = i % 3; val row = i / 3
            pad[i].x = cx + (col - 1) * bw * 0.25f
            pad[i].y = top + row * bh * 0.1f
            pad[i].r = kr
        }
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF101114.toInt())
        val accent = GwBits.REMOTE_COLORS[remote]
        // remote picker
        for (i in 0 until 6) {
            p.color = GwBits.REMOTE_COLORS[i]
            c.drawOval(dots[i], p)
            if (i == remote) {
                p.style = Paint.Style.STROKE; p.strokeWidth = dots[i].width() * 0.18f; p.color = 0xFFFFFFFF.toInt()
                c.drawOval(RectF(dots[i]).apply { inset(-width() * 0.22f, -height() * 0.22f) }, p)
                p.style = Paint.Style.FILL
            }
        }
        // body: rounded, waisted black remote
        val path = Path()
        val bw = body.width(); val l = body.left; val r = body.right; val tp = body.top; val bt = body.bottom
        path.moveTo(l + bw * 0.12f, tp)
        path.lineTo(r - bw * 0.12f, tp)
        path.quadTo(r, tp, r, tp + bw * 0.14f)
        path.cubicTo(r, tp + body.height() * 0.4f, r - bw * 0.1f, tp + body.height() * 0.55f, r - bw * 0.06f, bt - bw * 0.14f)
        path.quadTo(r - bw * 0.06f, bt, r - bw * 0.2f, bt)
        path.lineTo(l + bw * 0.2f, bt)
        path.quadTo(l + bw * 0.06f, bt, l + bw * 0.06f, bt - bw * 0.14f)
        path.cubicTo(l + bw * 0.1f, tp + body.height() * 0.55f, l, tp + body.height() * 0.4f, l, tp + bw * 0.14f)
        path.quadTo(l, tp, l + bw * 0.12f, tp)
        path.close()
        p.shader = LinearGradient(l, 0f, r, 0f, intArrayOf(0xFF1C1D21.toInt(), 0xFF2C2E33.toInt(), 0xFF17181B.toInt()), null, Shader.TileMode.CLAMP)
        c.drawPath(path, p)
        p.shader = null
        // coloured ring under A-D and around the arrows
        p.style = Paint.Style.STROKE; p.color = accent; p.strokeWidth = bw * 0.018f
        c.drawArc(RectF(abcd[0].x - abcd[0].r * 1.6f, abcd[1].y - abcd[0].r * 1.7f, abcd[3].x + abcd[0].r * 1.6f, abcd[1].y + bw * 0.5f), 200f, 140f, false, p)
        p.style = Paint.Style.FILL
        val d = arrows[2].x - sel.x
        val diamond = Path().apply {
            moveTo(sel.x, sel.y - d * 1.55f); lineTo(sel.x + d * 1.55f, sel.y); lineTo(sel.x, sel.y + d * 1.55f); lineTo(sel.x - d * 1.55f, sel.y); close()
        }
        p.color = accent; c.drawPath(diamond, p)
        val inner = Path().apply {
            moveTo(sel.x, sel.y - d * 1.38f); lineTo(sel.x + d * 1.38f, sel.y); lineTo(sel.x, sel.y + d * 1.38f); lineTo(sel.x - d * 1.38f, sel.y); close()
        }
        p.color = 0xFF202125.toInt(); c.drawPath(inner, p)
        val lit = mask or externalMask
        for (k in all) {
            val on = lit and k.bit != 0
            p.color = 0x66000000; c.drawCircle(k.x, k.y + k.r * 0.12f, k.r, p)
            p.shader = RadialGradient(k.x - k.r * 0.3f, k.y - k.r * 0.35f, k.r * 1.5f,
                if (on) 0xFFFFFFFF.toInt() else 0xFFC8CACE.toInt(), if (on) 0xFFB8BBC2.toInt() else 0xFF8A8D93.toInt(), Shader.TileMode.CLAMP)
            c.drawCircle(k.x, k.y, k.r, p)
            p.shader = null
            t.color = 0xFF2A2B2F.toInt()
            t.textSize = k.r * (if (k.label.length > 2) 0.42f else 0.85f)
            c.drawText(k.label, k.x, k.y + t.textSize * 0.36f, t)
            if (k.sub.isNotEmpty()) {
                t.color = 0xFFB8BBC2.toInt(); t.textSize = k.r * 0.42f
                c.drawText(k.sub, k.x, k.y + k.r * 1.55f, t)
            }
        }
        t.color = 0xFFDDE0E6.toInt(); t.textSize = bw * 0.045f
        c.drawText(GwBits.REMOTE_NAMES[remote], body.centerX(), body.bottom - bw * 0.07f, t)
    }

    private fun hit(x: Float, y: Float): Int {
        var best = 0; var bestD = Float.MAX_VALUE
        for (k in all) {
            val dd = hypot(x - k.x, y - k.y)
            if (dd < k.r * 1.45f && dd < bestD) { bestD = dd; best = k.bit }
        }
        return best
    }

    private fun publish() {
        val m = down.values.fold(0) { a, b -> a or b }
        mask = m
        onInput?.invoke(m or (remote shl GwBits.REMOTE_SHIFT))
        invalidate()
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        val i = e.actionIndex
        val id = e.getPointerId(i)
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val x = e.getX(i); val y = e.getY(i)
                val dot = dots.indexOfFirst { RectF(it).apply { inset(-width() * 0.6f, -height() * 0.8f) }.contains(x, y) }
                if (dot >= 0) { remote = dot; publish(); return true }
                down[id] = hit(x, y); publish()
            }
            MotionEvent.ACTION_MOVE -> {
                var changed = false
                for (k in 0 until e.pointerCount) {
                    val pid = e.getPointerId(k)
                    if (down.containsKey(pid)) { val b = hit(e.getX(k), e.getY(k)); if (down[pid] != b) { down[pid] = b; changed = true } }
                }
                if (changed) publish()
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
 * The console as a strip: a silver DVD player with wavy vent grooves on top, the Power key,
 * a dark display window, the Open/Close key and the disc tray along the front.
 */
class GameWaveConsoleView(ctx: Context) : View(ctx) {
    var onReset: (() -> Unit)? = null
    var onPower: (() -> Unit)? = null
    private val body = RectF(); private val front = RectF(); private val display = RectF(); private val tray = RectF()
    private var powX = 0f; private var powY = 0f; private var openX = 0f; private var openY = 0f; private var keyR = 0f
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; color = 0xFF4A4D52.toInt(); isFakeBoldText = true }
    private var pressed = 0

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val bw = min(w * 0.96f, h * 4.4f)
        body.set((w - bw) / 2, h * 0.06f, (w + bw) / 2, h * 0.94f)
        val bh = body.height()
        front.set(body.left, body.top + bh * 0.52f, body.right, body.bottom)
        keyR = bh * 0.09f
        powX = body.left + bw * 0.07f; powY = front.top + front.height() * 0.38f
        display.set(body.left + bw * 0.13f, front.top + front.height() * 0.14f, body.left + bw * 0.26f, front.top + front.height() * 0.64f)
        openX = body.left + bw * 0.31f; openY = powY
        tray.set(body.left + bw * 0.37f, front.top + front.height() * 0.24f, body.left + bw * 0.78f, front.top + front.height() * 0.5f)
        t.textSize = bh * 0.075f
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF101114.toInt())
        val rad = body.height() * 0.12f
        // top shell, then the front face
        p.shader = LinearGradient(0f, body.top, 0f, front.top, 0xFFE4E6E9.toInt(), 0xFFB9BCC1.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, rad, rad, p)
        p.shader = LinearGradient(0f, front.top, 0f, front.bottom, 0xFFC9CCD0.toInt(), 0xFF9EA2A8.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(front, rad, rad, p)
        p.shader = null
        // wavy vent grooves on the top
        p.style = Paint.Style.STROKE; p.strokeWidth = body.height() * 0.02f; p.color = 0xFF9DA1A7.toInt()
        val vx0 = body.left + body.width() * 0.42f; val vw = body.width() * 0.32f
        for (k in 0 until 5) {
            val y = body.top + body.height() * (0.08f + k * 0.085f)
            val path = Path().apply { moveTo(vx0, y); cubicTo(vx0 + vw * 0.33f, y - body.height() * 0.05f, vx0 + vw * 0.66f, y + body.height() * 0.05f, vx0 + vw, y) }
            c.drawPath(path, p)
        }
        p.style = Paint.Style.FILL
        // display window and tray
        p.color = 0xFF141518.toInt(); c.drawRoundRect(display, rad * 0.2f, rad * 0.2f, p)
        p.color = 0x5532E0A0; c.drawRect(display.left + display.width() * 0.1f, display.centerY() - display.height() * 0.08f, display.left + display.width() * 0.6f, display.centerY() + display.height() * 0.08f, p)
        p.color = 0xFFB2B5BA.toInt(); c.drawRoundRect(tray, rad * 0.3f, rad * 0.3f, p)
        p.style = Paint.Style.STROKE; p.strokeWidth = body.height() * 0.012f; p.color = 0xFF878B91.toInt()
        c.drawRoundRect(tray, rad * 0.3f, rad * 0.3f, p)
        p.style = Paint.Style.FILL
        p.color = 0xFF2B78FF.toInt(); c.drawCircle(powX + keyR * 1.7f, powY - keyR * 0.3f, keyR * 0.2f, p)
        for ((pair, label) in listOf(Triple(powX, powY, 2) to "POWER", Triple(openX, openY, 1) to "OPEN/CLOSE")) {
            val (x, y, bit) = pair
            p.shader = RadialGradient(x - keyR * 0.3f, y - keyR * 0.3f, keyR * 1.4f,
                if (pressed and bit != 0) 0xFFFFFFFF.toInt() else 0xFFF0F1F3.toInt(), 0xFF9CA0A6.toInt(), Shader.TileMode.CLAMP)
            c.drawCircle(x, y, keyR, p)
            p.shader = null
            c.drawText(label, x, y + keyR * 2.1f, t)
        }
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        val bit = when {
            hypot(e.x - powX, e.y - powY) < keyR * 2.5f -> 2
            hypot(e.x - openX, e.y - openY) < keyR * 2.5f -> 1
            else -> 0
        }
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> { pressed = bit; invalidate() }
            MotionEvent.ACTION_UP -> {
                // Open/Close re-reads the disc: the game starts over
                if (bit != 0 && bit == pressed) { if (bit == 1) onReset?.invoke() else onPower?.invoke() }
                pressed = 0; invalidate()
            }
            MotionEvent.ACTION_CANCEL -> { pressed = 0; invalidate() }
        }
        return true
    }
}
