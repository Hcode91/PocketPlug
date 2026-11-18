package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.atan2
import kotlin.math.hypot

/**
 * Plug-and-play joystick of the early 2000s (the Joystick88 kind): a silver
 * horseshoe body with blue grips, a ball-top stick on the left, four round
 * blue fire buttons on the right (A, B and their turbo twins) and SELECT /
 * START in between. The games are on the chip inside; the TV above shows them.
 * No maker's name or sticker is drawn.
 *
 * Bits as the VT core's pad: A, B, Select, Start, Up, Down, Left, Right.
 */
class JoystickPadView(ctx: Context) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    companion object {
        const val A = 0x01; const val B = 0x02; const val SELECT = 0x04; const val START = 0x08
        const val UP = 0x10; const val DOWN = 0x20; const val LEFT = 0x40; const val RIGHT = 0x80
        private const val TURBO_A = 0x100; private const val TURBO_B = 0x200
    }

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }

    private val body = Path()
    private val bodyBounds = RectF()
    private val stick = PointF(); private var stickR = 0f
    private var knobDx = 0f; private var knobDy = 0f
    private val buttons = arrayOf(PointF(), PointF(), PointF(), PointF())   // turbo B, turbo A, B, A
    private val buttonBits = intArrayOf(TURBO_B, TURBO_A, B, A)
    private var btnR = 0f
    private val selectR = RectF(); private val startR = RectF()

    private val pointers = HashMap<Int, Int>()
    private var stickPointer = -1
    private var mask = 0
    private var turboPhase = false
    private val turbo = object : Runnable {
        override fun run() {
            if (mask and (TURBO_A or TURBO_B) == 0) return
            turboPhase = !turboPhase
            emit()
            postDelayed(this, 50)
        }
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        val bw = w * 0.96f
        val bh = minOf(h * 0.9f, bw * 0.62f)
        val l = (w - bw) / 2; val t = (h - bh) / 2
        bodyBounds.set(l, t, l + bw, t + bh)
        // horseshoe: a wide top bar with two grips hanging down and a notch between them
        body.reset()
        body.moveTo(l + bw * 0.08f, t + bh * 0.12f)
        body.cubicTo(l + bw * 0.3f, t - bh * 0.02f, l + bw * 0.7f, t - bh * 0.02f, l + bw * 0.92f, t + bh * 0.12f)
        body.cubicTo(l + bw * 1.02f, t + bh * 0.22f, l + bw * 1.0f, t + bh * 0.9f, l + bw * 0.86f, t + bh)
        body.cubicTo(l + bw * 0.74f, t + bh * 1.04f, l + bw * 0.66f, t + bh * 0.9f, l + bw * 0.62f, t + bh * 0.72f)
        body.cubicTo(l + bw * 0.56f, t + bh * 0.6f, l + bw * 0.44f, t + bh * 0.6f, l + bw * 0.38f, t + bh * 0.72f)
        body.cubicTo(l + bw * 0.34f, t + bh * 0.9f, l + bw * 0.26f, t + bh * 1.04f, l + bw * 0.14f, t + bh)
        body.cubicTo(l + bw * 0.0f, t + bh * 0.9f, l - bw * 0.02f, t + bh * 0.22f, l + bw * 0.08f, t + bh * 0.12f)
        body.close()
        stick.set(l + bw * 0.2f, t + bh * 0.42f); stickR = bw * 0.11f
        btnR = bw * 0.062f
        val cx = l + bw * 0.79f; val cy = t + bh * 0.4f; val d = bw * 0.085f
        buttons[0].set(cx - d, cy - d); buttons[1].set(cx + d, cy - d)
        buttons[2].set(cx - d, cy + d); buttons[3].set(cx + d, cy + d)
        selectR.set(l + bw * 0.36f, t + bh * 0.3f, l + bw * 0.47f, t + bh * 0.38f)
        startR.set(l + bw * 0.53f, t + bh * 0.3f, l + bw * 0.64f, t + bh * 0.38f)
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0814.toInt())
        val bw = bodyBounds.width()
        // cable going up to the TV
        stroke.color = 0xFF202020.toInt(); stroke.strokeWidth = bw * 0.012f
        c.drawLine(bodyBounds.centerX(), bodyBounds.top + bodyBounds.height() * 0.05f, bodyBounds.centerX(), 0f, stroke)
        // body: silver top, blue grips
        paint.color = 0x66000000
        c.save(); c.translate(bw * 0.01f, bw * 0.015f); c.drawPath(body, paint); c.restore()
        paint.color = Color.WHITE
        paint.shader = LinearGradient(0f, bodyBounds.top, 0f, bodyBounds.bottom, intArrayOf(0xFFE4E7EC.toInt(), 0xFFB5BBC6.toInt(), 0xFF3A4FB0.toInt(), 0xFF26388C.toInt()),
            floatArrayOf(0f, 0.5f, 0.62f, 1f), Shader.TileMode.CLAMP)
        c.drawPath(body, paint)
        paint.shader = null
        stroke.color = 0xFF6E7684.toInt(); stroke.strokeWidth = bw * 0.006f
        c.drawPath(body, stroke)
        val held = mask or externalMask

        // stick: round base, shaft and ball top that leans with the thumb
        paint.color = 0xFF3B4FA8.toInt(); c.drawCircle(stick.x, stick.y, stickR, paint)
        paint.color = 0xFF2A3A82.toInt(); c.drawCircle(stick.x, stick.y, stickR * 0.75f, paint)
        val kx = stick.x + knobDx * stickR * 0.45f; val ky = stick.y + knobDy * stickR * 0.45f
        stroke.color = 0xFF151515.toInt(); stroke.strokeWidth = stickR * 0.28f; stroke.strokeCap = Paint.Cap.ROUND
        c.drawLine(stick.x, stick.y, kx, ky, stroke)
        stroke.strokeCap = Paint.Cap.BUTT
        paint.color = Color.WHITE
        paint.shader = RadialGradient(kx - stickR * 0.15f, ky - stickR * 0.15f, stickR * 0.6f, 0xFF3A3A3A.toInt(), 0xFF080808.toInt(), Shader.TileMode.CLAMP)
        c.drawCircle(kx, ky, stickR * 0.42f, paint)
        paint.shader = null

        // fire buttons
        val labels = arrayOf("TURBO", "TURBO", "B", "A")
        for (i in 0..3) {
            val p = buttons[i]
            val down = held and buttonBits[i] != 0 || (i == 3 && held and A != 0 && mask and TURBO_A == 0) || (i == 2 && held and B != 0 && mask and TURBO_B == 0)
            paint.color = 0xFF1C2A6E.toInt(); c.drawCircle(p.x, p.y + btnR * 0.12f, btnR * 1.05f, paint)
            paint.color = Color.WHITE
            paint.shader = RadialGradient(p.x - btnR * 0.3f, p.y - btnR * 0.3f, btnR * 1.3f,
                if (down) 0xFF3048A8.toInt() else 0xFF5A7BEA.toInt(), 0xFF22358E.toInt(), Shader.TileMode.CLAMP)
            c.drawCircle(p.x, p.y + (if (down) btnR * 0.08f else 0f), btnR, paint)
            paint.shader = null
            // legends on the buttons themselves
            text.color = 0xFFE6ECFF.toInt(); text.textSize = btnR * (if (i < 2) 0.36f else 0.6f)
            c.drawText(labels[i], p.x, p.y + text.textSize * 0.36f + (if (down) btnR * 0.08f else 0f), text)
        }

        // select / start: small dark pills
        for ((r, bit, label) in listOf(Triple(selectR, SELECT, "SELECT"), Triple(startR, START, "START"))) {
            paint.color = if (held and bit != 0) 0xFF1A1A1A.toInt() else 0xFF3A3F4A.toInt()
            c.drawRoundRect(r, r.height() / 2, r.height() / 2, paint)
            text.color = 0xFF2B3240.toInt(); text.textSize = r.height() * 0.6f
            c.drawText(label, r.centerX(), r.bottom + r.height() * 0.9f, text)
        }
    }

    private fun buttonAt(x: Float, y: Float): Int {
        for (i in 0..3) if (hypot(x - buttons[i].x, y - buttons[i].y) < btnR * 1.5f) return buttonBits[i]
        val sel = RectF(selectR).apply { inset(-selectR.width() * 0.3f, -selectR.height() * 0.8f) }
        val st = RectF(startR).apply { inset(-startR.width() * 0.3f, -startR.height() * 0.8f) }
        if (sel.contains(x, y)) return SELECT
        if (st.contains(x, y)) return START
        return 0
    }

    private fun stickBits(x: Float, y: Float): Int {
        val dx = x - stick.x; val dy = y - stick.y
        val dist = hypot(dx, dy)
        knobDx = (dx / stickR).coerceIn(-1f, 1f); knobDy = (dy / stickR).coerceIn(-1f, 1f)
        if (dist < stickR * 0.25f) return 0
        val a = Math.toDegrees(atan2(dy.toDouble(), dx.toDouble()))
        var b = 0
        if (a > -67.5 && a < 67.5) b = b or RIGHT
        if (a > 112.5 || a < -112.5) b = b or LEFT
        if (a > 22.5 && a < 157.5) b = b or DOWN
        if (a < -22.5 && a > -157.5) b = b or UP
        return b
    }

    private fun emit() {
        var out = mask and 0xff
        if (mask and TURBO_A != 0 && turboPhase) out = out or A
        if (mask and TURBO_B != 0 && turboPhase) out = out or B
        onInput?.invoke(out)
        invalidate()
    }

    private fun update() {
        var m = 0
        for (v in pointers.values) m = m or v
        if (m != mask) {
            if (m and mask.inv() != 0) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            val hadTurbo = mask and (TURBO_A or TURBO_B) != 0
            mask = m
            if (!hadTurbo && m and (TURBO_A or TURBO_B) != 0) { turboPhase = true; removeCallbacks(turbo); postDelayed(turbo, 50) }
            emit()
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex; val id = e.getPointerId(i)
                val x = e.getX(i); val y = e.getY(i)
                if (stickPointer < 0 && hypot(x - stick.x, y - stick.y) < stickR * 1.8f) {
                    stickPointer = id; pointers[id] = stickBits(x, y)
                } else pointers[id] = buttonAt(x, y)
                update()
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until e.pointerCount) {
                    val id = e.getPointerId(i)
                    if (id == stickPointer) pointers[id] = stickBits(e.getX(i), e.getY(i))
                    else if (pointers.containsKey(id)) pointers[id] = buttonAt(e.getX(i), e.getY(i))
                }
                update()
                invalidate()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val id = e.getPointerId(e.actionIndex)
                if (id == stickPointer) { stickPointer = -1; knobDx = 0f; knobDy = 0f }
                pointers.remove(id)
                update()
                invalidate()
            }
            MotionEvent.ACTION_CANCEL -> { pointers.clear(); stickPointer = -1; knobDx = 0f; knobDy = 0f; update(); invalidate() }
        }
        return true
    }
}
