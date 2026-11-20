package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.min

/**
 * A plug-into-the-TV flight stick (the Air-Blaster Joystick): the game is in
 * the stick. The base holds RESET and the ON/OFF switch; the grip has the
 * trigger on its front and, on its head, the hat switch and three buttons.
 * Put a finger on the grip and push it to steer (the whole stick moves the
 * plane, as on the real one); the hat does the same. No maker's name or logo.
 *
 * Input bits (see eu3a05.h): 0 up, 1 down, 2 left, 3 right, 4 pause,
 * 5 start, 6 trigger, 7 missile, 8 reset.
 */
class FlightStickView(ctx: Context) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    companion object {
        const val UP = 0x01; const val DOWN = 0x02; const val LEFT = 0x04; const val RIGHT = 0x08
        const val PAUSE = 0x10; const val START = 0x20; const val TRIGGER = 0x40; const val MISSILE = 0x80
        const val RESET = 0x100

        fun keyBit(code: Int): Int = when (code) {
            KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> UP
            KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> DOWN
            KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> LEFT
            KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> RIGHT
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_Z, KeyEvent.KEYCODE_SPACE, KeyEvent.KEYCODE_BUTTON_R1 -> TRIGGER
            KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_X -> MISSILE
            KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_ENTER -> START
            KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_P -> PAUSE
            else -> 0
        }
    }

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.DEFAULT_BOLD }

    // drawn in a 1000 x 1100 space
    private var sc = 1f
    private var ox = 0f
    private var oy = 0f

    private val hatC = PointF(500f, 250f)
    private val startC = PointF(350f, 205f)
    private val pauseC = PointF(650f, 205f)
    private val missileC = PointF(500f, 115f)
    private val triggerR = RectF(410f, 400f, 590f, 520f)
    private val gripR = RectF(330f, 300f, 670f, 860f)
    private val resetC = PointF(190f, 960f)
    private val switchR = RectF(740f, 935f, 860f, 985f)

    private var mask = 0
    private val pointers = HashMap<Int, Int>()          // pointer -> bit (or -1 grip steering)
    private var steerId = -1
    private val steerStart = PointF()
    private var steerBits = 0
    private var hatId = -1
    private var hatBits = 0

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        sc = min(w / 1000f, h / 1100f)
        ox = (w - 1000f * sc) / 2f
        oy = (h - 1100f * sc) / 2f
    }

    private fun X(v: Float) = ox + v * sc
    private fun Y(v: Float) = oy + v * sc
    private fun rect(l: Float, t: Float, r: Float, b: Float) = RectF(X(l), Y(t), X(r), Y(b))

    private fun dirs() = (mask or externalMask) and 0x0f

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0E0F12.toInt())
        val m = mask or externalMask
        // base
        paint.color = 0xFF202226.toInt()
        val base = Path().apply {
            moveTo(X(80f), Y(1060f)); lineTo(X(920f), Y(1060f)); lineTo(X(860f), Y(880f))
            quadTo(X(500f), Y(830f), X(140f), Y(880f)); close()
        }
        c.drawPath(base, paint)
        paint.color = 0xFF2E3136.toInt()
        c.drawPath(Path().apply { moveTo(X(140f), Y(880f)); quadTo(X(500f), Y(830f), X(860f), Y(880f)); lineTo(X(850f), Y(905f)); quadTo(X(500f), Y(858f), X(150f), Y(905f)); close() }, paint)
        paint.color = 0xFF8C9096.toInt(); c.drawRect(rect(100f, 1020f, 900f, 1030f), paint)
        // reset button
        val rd = m and RESET != 0
        paint.color = 0xFF111214.toInt(); c.drawCircle(X(resetC.x), Y(resetC.y), 44f * sc, paint)
        paint.color = if (rd) 0xFFA02018.toInt() else 0xFFD83020.toInt(); c.drawCircle(X(resetC.x), Y(resetC.y + if (rd) 3f else 0f), 34f * sc, paint)
        text.color = 0xFFD8DCE2.toInt(); text.textSize = 26f * sc
        c.drawText("RESET", X(resetC.x), Y(resetC.y + 76f), text)
        // on/off slide
        paint.color = 0xFF0A0B0C.toInt(); c.drawRoundRect(rect(switchR.left, switchR.top, switchR.right, switchR.bottom), 10f * sc, 10f * sc, paint)
        paint.color = 0xFFB8BCC2.toInt(); c.drawRoundRect(rect(switchR.left + 64f, switchR.top + 6f, switchR.right - 6f, switchR.bottom - 6f), 8f * sc, 8f * sc, paint)
        text.textSize = 22f * sc
        c.drawText("OFF   ON", X(switchR.centerX()), Y(switchR.bottom + 38f), text)
        // the stick leans the way it's pushed
        val d = dirs()
        val lx = (if (d and RIGHT != 0) 34f else 0f) - (if (d and LEFT != 0) 34f else 0f)
        val ly = (if (d and DOWN != 0) 22f else 0f) - (if (d and UP != 0) 22f else 0f)
        // rubber boot
        paint.color = 0xFF141517.toInt()
        c.drawOval(rect(360f, 820f, 640f, 900f), paint)
        stroke.color = 0xFF2A2C30.toInt(); stroke.strokeWidth = 5f * sc
        for (i in 0 until 3) c.drawOval(rect(380f + i * 18f, 812f - i * 14f, 620f - i * 18f, 878f - i * 16f), stroke)
        c.save()
        c.translate(X(lx) - X(0f), Y(ly) - Y(0f))
        c.rotate(lx * 0.35f, X(500f), Y(860f))
        // grip
        val grip = Path().apply {
            moveTo(X(400f), Y(840f))
            cubicTo(X(370f), Y(700f), X(360f), Y(560f), X(390f), Y(330f))
            lineTo(X(610f), Y(330f))
            cubicTo(X(640f), Y(560f), X(630f), Y(700f), X(600f), Y(840f))
            close()
        }
        paint.shader = LinearGradient(X(370f), 0f, X(630f), 0f, intArrayOf(0xFF1C1D20.toInt(), 0xFF4A4D53.toInt(), 0xFF1C1D20.toInt()), null, Shader.TileMode.CLAMP)
        c.drawPath(grip, paint)
        paint.shader = null
        // finger ridges on the back
        stroke.color = 0xFF2C2E33.toInt(); stroke.strokeWidth = 8f * sc
        for (i in 0 until 4) c.drawLine(X(392f), Y(580f + i * 60f), X(608f), Y(580f + i * 60f), stroke)
        // trigger
        val td = m and TRIGGER != 0
        paint.color = 0xFF0C0C0D.toInt(); c.drawRoundRect(rect(triggerR.left - 10f, triggerR.top - 10f, triggerR.right + 10f, triggerR.bottom + 10f), 30f * sc, 30f * sc, paint)
        paint.color = if (td) 0xFFA0201A.toInt() else 0xFFE03828.toInt()
        c.drawRoundRect(rect(triggerR.left, triggerR.top + if (td) 8f else 0f, triggerR.right, triggerR.bottom), 26f * sc, 26f * sc, paint)
        text.color = 0xFFFFF0E0.toInt(); text.textSize = 28f * sc
        c.drawText("TRIGGER", X(triggerR.centerX()), Y(triggerR.centerY() + 10f), text)
        // head
        paint.shader = LinearGradient(0f, Y(60f), 0f, Y(340f), intArrayOf(0xFFB9BEC6.toInt(), 0xFF6E747C.toInt()), null, Shader.TileMode.CLAMP)
        c.drawRoundRect(rect(250f, 60f, 750f, 340f), 90f * sc, 90f * sc, paint)
        paint.shader = null
        paint.color = 0xFF2A2C30.toInt(); c.drawRoundRect(rect(270f, 80f, 730f, 320f), 76f * sc, 76f * sc, paint)
        // hat switch
        val hb = d
        paint.color = 0xFF0E0F11.toInt(); c.drawCircle(X(hatC.x), Y(hatC.y), 62f * sc, paint)
        paint.color = 0xFF5A5E66.toInt()
        val hx = (if (hb and RIGHT != 0) 8f else 0f) - (if (hb and LEFT != 0) 8f else 0f)
        val hy = (if (hb and DOWN != 0) 8f else 0f) - (if (hb and UP != 0) 8f else 0f)
        c.drawRoundRect(rect(hatC.x - 50f + hx, hatC.y - 16f + hy, hatC.x + 50f + hx, hatC.y + 16f + hy), 8f * sc, 8f * sc, paint)
        c.drawRoundRect(rect(hatC.x - 16f + hx, hatC.y - 50f + hy, hatC.x + 16f + hx, hatC.y + 50f + hy), 8f * sc, 8f * sc, paint)
        paint.color = 0xFF7A7F88.toInt(); c.drawCircle(X(hatC.x + hx), Y(hatC.y + hy), 12f * sc, paint)
        // three buttons
        fun button(p: PointF, bit: Int, col: Int, label: String) {
            val dn = m and bit != 0
            paint.color = 0xFF0E0F11.toInt(); c.drawCircle(X(p.x), Y(p.y), 46f * sc, paint)
            paint.color = if (dn) Color.rgb(Color.red(col) * 3 / 4, Color.green(col) * 3 / 4, Color.blue(col) * 3 / 4) else col
            c.drawCircle(X(p.x), Y(p.y + if (dn) 3f else 0f), 36f * sc, paint)
            text.color = 0xFFE8ECF2.toInt(); text.textSize = 22f * sc
            c.drawText(label, X(p.x), Y(p.y + (if (p.y < 150f) -54f else 74f)), text)
        }
        button(missileC, MISSILE, 0xFFF0B020.toInt(), "MISSILE")
        button(startC, START, 0xFF2F7FE0.toInt(), "START")
        button(pauseC, PAUSE, 0xFF2F7FE0.toInt(), "PAUSE")
        c.restore()
    }

    // ---- touch ----
    private fun send() {
        val m = (mask and 0x0f.inv()) or steerBits or hatBits
        if (m != mask) {
            if (m and mask.inv() != 0) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            mask = m
            onInput?.invoke(m)
            invalidate()
        }
    }

    private fun dirBits(dx: Float, dy: Float, dead: Float): Int {
        if (hypot(dx, dy) < dead) return 0
        val a = Math.toDegrees(atan2(dy.toDouble(), dx.toDouble())).toFloat()
        var b = 0
        if (a > -67.5f && a < 67.5f) b = b or RIGHT
        if (a > 112.5f || a < -112.5f) b = b or LEFT
        if (a > 22.5f && a < 157.5f) b = b or DOWN
        if (a < -22.5f && a > -157.5f) b = b or UP
        return b
    }

    private fun near(p: PointF, x: Float, y: Float, r: Float) = hypot(x - p.x, y - p.y) < r

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                val x = (e.getX(i) - ox) / sc; val y = (e.getY(i) - oy) / sc
                val id = e.getPointerId(i)
                val bit = when {
                    near(missileC, x, y, 70f) -> MISSILE
                    near(startC, x, y, 70f) -> START
                    near(pauseC, x, y, 70f) -> PAUSE
                    near(resetC, x, y, 80f) -> RESET
                    RectF(triggerR.left - 30f, triggerR.top - 30f, triggerR.right + 30f, triggerR.bottom + 30f).contains(x, y) -> TRIGGER
                    near(hatC, x, y, 95f) -> -2
                    gripR.contains(x, y) || (y > 300f && y < 900f) -> -1
                    else -> 0
                }
                when (bit) {
                    0 -> {}
                    -1 -> if (steerId < 0) { steerId = id; steerStart.set(x, y); pointers[id] = -1 }
                    -2 -> { hatId = id; pointers[id] = -2; hatBits = dirBits(x - hatC.x, y - hatC.y, 18f); send() }
                    else -> { pointers[id] = bit; mask = mask or bit; onInput?.invoke((mask and 0x0f.inv()) or steerBits or hatBits); performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP); invalidate() }
                }
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until e.pointerCount) {
                    val id = e.getPointerId(i)
                    val x = (e.getX(i) - ox) / sc; val y = (e.getY(i) - oy) / sc
                    if (id == steerId) steerBits = dirBits(x - steerStart.x, y - steerStart.y, 36f)
                    if (id == hatId) hatBits = dirBits(x - hatC.x, y - hatC.y, 18f)
                }
                send()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> release(e.getPointerId(e.actionIndex))
            MotionEvent.ACTION_CANCEL -> for (id in pointers.keys.toList()) release(id)
        }
        return true
    }

    private fun release(id: Int) {
        when (val b = pointers.remove(id)) {
            null -> return
            -1 -> { steerId = -1; steerBits = 0 }
            -2 -> { hatId = -1; hatBits = 0 }
            else -> mask = mask and b.inv()
        }
        mask = mask and 0x0f.inv()
        send()
        onInput?.invoke((mask and 0x0f.inv()) or steerBits or hatBits)
        invalidate()
    }
}
