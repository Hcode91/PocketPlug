package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.PointF
import android.graphics.RadialGradient
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.Shader
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import kotlin.math.hypot
import kotlin.math.min

/** Button bits for the Zodiac core (see zodiac.h). */
object ZodBits {
    const val UP = 0x001; const val DOWN = 0x002; const val LEFT = 0x004; const val RIGHT = 0x008
    const val A = 0x010; const val B = 0x020; const val C = 0x040; const val D = 0x080
    const val L = 0x100; const val R = 0x200
    const val FUNCTION = 0x400; const val HOME = 0x800; const val POWER = 0x1000

    fun keyBit(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> UP
        KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> DOWN
        KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> RIGHT
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_K -> A
        KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_L -> B
        KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_J -> C
        KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_I -> D
        KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_Q -> L
        KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_E -> R
        KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_TAB -> FUNCTION
        KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_ENTER -> HOME
        else -> 0
    }
}

/**
 * A landscape handheld after the Zodiac's layout: a rounded black body with the 480 x 320
 * touch screen in the middle; down the left side the power key, the analog stick, the round
 * silver Function button and the Home key; on the right four silver wedge buttons in a cross
 * with coloured dots (blue top, yellow right, green bottom, red left); shoulder triggers on
 * the top corners. Touching the screen is the stylus.
 */
class ZodiacView(ctx: Context) : View(ctx) {
    var onInput: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    /** stick 0: analog stick (-1..1); stick 2: stylus (0..1 on the screen, x < 0 = lifted) */
    var onAnalog: ((Int, Float, Float) -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    private val frame = Bitmap.createBitmap(480, 320, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(480 * 320)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeCap = Paint.Cap.ROUND }
    private val smooth = Paint(Paint.FILTER_BITMAP_FLAG)
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; color = 0xFF8E9298.toInt() }

    private val body = android.graphics.Path()
    private val bodyBox = RectF()
    private val screen = RectF()
    private val stick = PointF(); private var stickR = 0f
    private val power = PointF(); private val func = PointF(); private val home = PointF(); private var keyR = 0f
    private val cross = PointF(); private var crossR = 0f
    private val action = Array(4) { PointF() }
    private val shoulder = arrayOf(RectF(), RectF())
    private val menu = PointF(); private var menuR = 0f
    private val actionBits = intArrayOf(ZodBits.D, ZodBits.A, ZodBits.B, ZodBits.C)   // top, right, bottom, left
    private val dotColours = intArrayOf(0xFF1E4FC8.toInt(), 0xFFE0B21E.toInt(), 0xFF22A043.toInt(), 0xFFD0302B.toInt())

    private var stickPointer = -1
    private var stickX = 0f; private var stickY = 0f
    private var penPointer = -1
    private val buttonPointers = HashMap<Int, Int>()
    private var mask = 0

    fun updateFrame(px: IntArray) {
        if (px.size < framePx.size) return
        System.arraycopy(px, 0, framePx, 0, framePx.size)
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        var bw = w * 0.98f; var bh = bw / 1.62f
        if (bh > h * 0.94f) { bh = h * 0.94f; bw = bh * 1.62f }
        val l = (w - bw) / 2; val t = (h - bh) / 2
        bodyBox.set(l, t, l + bw, t + bh)
        // body: two round grips joined by a slightly waisted middle
        body.reset()
        val r = bh * 0.42f
        body.addRoundRect(RectF(l, t + bh * 0.06f, l + bw, t + bh * 0.94f), r, r, android.graphics.Path.Direction.CW)
        body.addCircle(l + bh * 0.42f, t + bh * 0.5f, bh * 0.47f, android.graphics.Path.Direction.CW)
        body.addCircle(l + bw - bh * 0.42f, t + bh * 0.5f, bh * 0.47f, android.graphics.Path.Direction.CW)
        val sh = bh * 0.62f; val sw = sh * 1.5f
        screen.set(l + bw / 2 - sw / 2, t + bh * 0.2f, l + bw / 2 + sw / 2, t + bh * 0.2f + sh)
        val leftX = l + (screen.left - l) * 0.5f
        val rightX = screen.right + (l + bw - screen.right) * 0.5f
        stickR = bh * 0.1f
        stick.set(leftX - bh * 0.02f, t + bh * 0.42f)
        keyR = bh * 0.033f
        power.set(leftX + bh * 0.07f, t + bh * 0.22f)
        func.set(leftX + bh * 0.07f, t + bh * 0.6f)
        home.set(leftX + bh * 0.05f, t + bh * 0.74f)
        crossR = bh * 0.15f
        cross.set(rightX + bh * 0.02f, t + bh * 0.46f)
        val d = crossR * 0.62f
        action[0].set(cross.x, cross.y - d); action[1].set(cross.x + d, cross.y); action[2].set(cross.x, cross.y + d); action[3].set(cross.x - d, cross.y)
        val shW = bh * 0.32f
        shoulder[0].set(l + bh * 0.12f, t + bh * 0.02f, l + bh * 0.12f + shW, t + bh * 0.08f)
        shoulder[1].set(l + bw - bh * 0.12f - shW, t + bh * 0.02f, l + bw - bh * 0.12f, t + bh * 0.08f)
        menuR = bh * 0.03f
        menu.set(l + bw / 2, screen.bottom + (t + bh - screen.bottom) * 0.45f)
        text.textSize = bh * 0.035f
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0E0F11.toInt())
        for ((i, r) in shoulder.withIndex()) {
            paint.color = if (pressed(if (i == 0) ZodBits.L else ZodBits.R)) 0xFF5A5E64.toInt() else 0xFF2A2C30.toInt()
            c.drawRoundRect(r, r.height() / 2, r.height() / 2, paint)
        }
        paint.shader = LinearGradient(0f, bodyBox.top, 0f, bodyBox.bottom, 0xFF2E3034.toInt(), 0xFF17181B.toInt(), Shader.TileMode.CLAMP)
        c.drawPath(body, paint)
        paint.shader = null
        // screen and bezel
        val bez = bodyBox.height() * 0.025f
        paint.color = 0xFF08090A.toInt()
        c.drawRoundRect(screen.left - bez, screen.top - bez, screen.right + bez, screen.bottom + bez, bez, bez, paint)
        frame.setPixels(framePx, 0, 480, 0, 0, 480, 320)
        c.drawBitmap(frame, Rect(0, 0, 480, 320), screen, smooth)
        // power key: dark with an orange power symbol
        paint.color = 0xFF34363A.toInt(); c.drawCircle(power.x, power.y, keyR, paint)
        stroke.color = if (pressed(ZodBits.POWER)) 0xFFFFB070.toInt() else 0xFFE07A3A.toInt(); stroke.strokeWidth = keyR * 0.16f
        c.drawArc(power.x - keyR * 0.5f, power.y - keyR * 0.5f, power.x + keyR * 0.5f, power.y + keyR * 0.5f, -60f, 300f, false, stroke)
        c.drawLine(power.x, power.y - keyR * 0.62f, power.x, power.y - keyR * 0.1f, stroke)
        // analog stick in its well
        paint.color = 0xFF0B0C0D.toInt(); c.drawCircle(stick.x, stick.y, stickR * 1.25f, paint)
        val kx = stick.x + stickX * stickR * 0.4f; val ky = stick.y + stickY * stickR * 0.4f
        paint.shader = RadialGradient(kx - stickR * 0.25f, ky - stickR * 0.3f, stickR, 0xFF45484D.toInt(), 0xFF141517.toInt(), Shader.TileMode.CLAMP)
        c.drawCircle(kx, ky, stickR * 0.85f, paint)
        paint.shader = null
        // Function: round silver button with a dot
        paint.shader = RadialGradient(func.x - keyR * 0.3f, func.y - keyR * 0.4f, keyR * 1.4f,
            if (pressed(ZodBits.FUNCTION)) 0xFFB8BBBF.toInt() else 0xFFE8EAEC.toInt(), 0xFF8C9095.toInt(), Shader.TileMode.CLAMP)
        c.drawCircle(func.x, func.y, keyR * 1.15f, paint); paint.shader = null
        paint.color = 0xFF202224.toInt(); c.drawCircle(func.x, func.y, keyR * 0.12f, paint)
        // Home: dark key with a house outline
        paint.color = if (pressed(ZodBits.HOME)) 0xFF55585D.toInt() else 0xFF34363A.toInt(); c.drawCircle(home.x, home.y, keyR, paint)
        stroke.color = 0xFFB4B8BE.toInt(); stroke.strokeWidth = keyR * 0.12f
        val hs = keyR * 0.5f
        c.drawLine(home.x - hs, home.y, home.x, home.y - hs, stroke); c.drawLine(home.x, home.y - hs, home.x + hs, home.y, stroke)
        c.drawRect(home.x - hs * 0.7f, home.y, home.x + hs * 0.7f, home.y + hs * 0.8f, stroke)
        // speaker grilles
        paint.color = 0xFF0A0A0B.toInt()
        for (side in 0..1) {
            val gx = if (side == 0) bodyBox.left + bodyBox.height() * 0.2f else bodyBox.right - bodyBox.height() * 0.24f
            for (k in 0..2) c.drawRoundRect(gx + k * keyR * 0.45f, bodyBox.top + bodyBox.height() * 0.64f,
                gx + k * keyR * 0.45f + keyR * 0.2f, bodyBox.top + bodyBox.height() * 0.68f, keyR * 0.1f, keyR * 0.1f, paint)
        }
        // action buttons: a dark cross well with four silver wedges
        paint.color = 0xFF111214.toInt(); c.drawCircle(cross.x, cross.y, crossR * 1.05f, paint)
        for (i in 0 until 4) {
            val p = action[i]
            val down = pressed(actionBits[i])
            paint.shader = RadialGradient(p.x - crossR * 0.1f, p.y - crossR * 0.15f, crossR * 0.5f,
                if (down) 0xFFAEB2B7.toInt() else 0xFFE4E6E9.toInt(), 0xFF8A8E94.toInt(), Shader.TileMode.CLAMP)
            val ww = crossR * 0.36f; val wl = crossR * 0.26f
            if (i % 2 == 0) c.drawRoundRect(p.x - ww, p.y - wl, p.x + ww, p.y + wl, wl * 0.8f, wl * 0.8f, paint)
            else c.drawRoundRect(p.x - wl, p.y - ww, p.x + wl, p.y + ww, wl * 0.8f, wl * 0.8f, paint)
            paint.shader = null
            paint.color = dotColours[i]; c.drawCircle(p.x, p.y, crossR * 0.035f, paint)
        }
        // emulator menu
        paint.color = 0xFF26282C.toInt(); c.drawCircle(menu.x, menu.y, menuR, paint)
        c.drawText("≡", menu.x, menu.y + text.textSize * 0.35f, text)
    }

    private fun pressed(bit: Int) = ((mask or externalMask) and bit) != 0

    private fun buttonAt(x: Float, y: Float): Int {
        if (hypot(x - cross.x, y - cross.y) < crossR * 1.2f) {
            val dx = x - cross.x; val dy = y - cross.y
            return if (kotlin.math.abs(dx) > kotlin.math.abs(dy)) (if (dx > 0) ZodBits.A else ZodBits.C) else (if (dy < 0) ZodBits.D else ZodBits.B)
        }
        if (hypot(x - power.x, y - power.y) < keyR * 1.8f) return ZodBits.POWER
        if (hypot(x - func.x, y - func.y) < keyR * 1.8f) return ZodBits.FUNCTION
        if (hypot(x - home.x, y - home.y) < keyR * 1.8f) return ZodBits.HOME
        if (y < bodyBox.top + bodyBox.height() * 0.12f) {
            if (x < screen.left) return ZodBits.L
            if (x > screen.right) return ZodBits.R
        }
        return 0
    }

    private fun updateStick(x: Float, y: Float) {
        var dx = (x - stick.x) / stickR; var dy = (y - stick.y) / stickR
        val d = hypot(dx, dy)
        if (d > 1f) { dx /= d; dy /= d }
        stickX = dx; stickY = dy
        onAnalog?.invoke(0, dx, dy)
    }

    private fun pen(x: Float, y: Float) {
        onAnalog?.invoke(2, ((x - screen.left) / screen.width()).coerceIn(0f, 1f), ((y - screen.top) / screen.height()).coerceIn(0f, 1f))
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        val i = e.actionIndex
        val id = e.getPointerId(i)
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val x = e.getX(i); val y = e.getY(i)
                when {
                    hypot(x - menu.x, y - menu.y) < menuR * 1.8f -> { onMenu?.invoke(); return true }
                    screen.contains(x, y) && penPointer < 0 -> { penPointer = id; pen(x, y) }
                    hypot(x - stick.x, y - stick.y) < stickR * 1.6f && stickPointer < 0 -> { stickPointer = id; updateStick(x, y) }
                    else -> { val b = buttonAt(x, y); if (b != 0) buttonPointers[id] = b }
                }
            }
            MotionEvent.ACTION_MOVE -> for (p in 0 until e.pointerCount) {
                when (e.getPointerId(p)) {
                    penPointer -> pen(e.getX(p), e.getY(p))
                    stickPointer -> updateStick(e.getX(p), e.getY(p))
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                if (e.actionMasked == MotionEvent.ACTION_CANCEL) {
                    buttonPointers.clear()
                    if (penPointer >= 0) { penPointer = -1; onAnalog?.invoke(2, -1f, -1f) }
                    if (stickPointer >= 0) { stickPointer = -1; stickX = 0f; stickY = 0f; onAnalog?.invoke(0, 0f, 0f) }
                } else when (id) {
                    penPointer -> { penPointer = -1; onAnalog?.invoke(2, -1f, -1f) }
                    stickPointer -> { stickPointer = -1; stickX = 0f; stickY = 0f; onAnalog?.invoke(0, 0f, 0f) }
                    else -> buttonPointers.remove(id)
                }
            }
        }
        var m = 0
        for (b in buttonPointers.values) m = m or b
        if (m != mask) { mask = m; onInput?.invoke(m) }
        invalidate()
        return true
    }
}
