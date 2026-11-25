package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.hypot

/**
 * The small upright 2010s NES-on-a-chip handheld (BittBoy type), from a photo
 * without the name: a white pocket body, the colour screen in a lavender frame,
 * a red cross D-pad, four red round keys in a diamond (A on the right and top,
 * B at the bottom and left: the extra pair are the turbo keys, sent here as
 * plain A / B), two small red pills under the pad (SELECT, START), a small
 * red key between the pad and the buttons (menu here) and speaker slots.
 *
 * Input bits as the NES pad: A 0x01, B 0x02, Select 0x04, Start 0x08,
 * Up 0x10, Down 0x20, Left 0x40, Right 0x80.
 */
class MiniFcView(ctx: Context) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    companion object {
        const val A = 0x01; const val B = 0x02; const val SELECT = 0x04; const val START = 0x08
        const val UP = 0x10; const val DOWN = 0x20; const val LEFT = 0x40; const val RIGHT = 0x80
    }

    private val frame = Bitmap.createBitmap(256, 240, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(256 * 240)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val smooth = Paint(Paint.FILTER_BITMAP_FLAG)

    private val body = RectF(); private val frameR = RectF(); private val screen = RectF()
    private val dpad = PointF(); private var dpadR = 0f
    private val keys = arrayOf(PointF(), PointF(), PointF(), PointF())   // right A, top A, bottom B, left B
    private val keyBits = intArrayOf(A, A, B, B)
    private var keyR = 0f
    private val sel = RectF(); private val start = RectF()
    private val menuKey = PointF(); private var menuR = 0f

    private val pointers = HashMap<Int, Int>()
    private var touchMask = 0

    fun updateFrame(px: IntArray) {
        if (px.size < framePx.size) return
        System.arraycopy(px, 0, framePx, 0, framePx.size)
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        var bw = w * 0.94f
        var bh = bw * 1.62f
        if (bh > h * 0.97f) { bh = h * 0.97f; bw = bh / 1.62f }
        val l = (w - bw) / 2; val t = (h - bh) / 2
        body.set(l, t, l + bw, t + bh)
        frameR.set(l + bw * 0.08f, t + bh * 0.06f, l + bw * 0.92f, t + bh * 0.47f)
        val sw = bw * 0.68f; val sh = sw * 240f / 256f * 0.86f
        screen.set(body.centerX() - sw / 2, frameR.top + bh * 0.035f, body.centerX() + sw / 2, frameR.top + bh * 0.035f + sh)
        dpadR = bw * 0.17f; dpad.set(l + bw * 0.27f, t + bh * 0.64f)
        keyR = bw * 0.065f
        val kc = PointF(l + bw * 0.72f, t + bh * 0.62f); val kd = bw * 0.13f
        keys[0].set(kc.x + kd, kc.y); keys[1].set(kc.x, kc.y - kd); keys[2].set(kc.x, kc.y + kd); keys[3].set(kc.x - kd, kc.y)
        val pw = bw * 0.1f; val ph = bw * 0.045f
        sel.set(l + bw * 0.2f - pw / 2, t + bh * 0.82f, l + bw * 0.2f + pw / 2, t + bh * 0.82f + ph)
        start.set(l + bw * 0.36f - pw / 2, t + bh * 0.82f, l + bw * 0.36f + pw / 2, t + bh * 0.82f + ph)
        menuR = bw * 0.03f; menuKey.set(body.centerX(), t + bh * 0.53f)
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF15161A.toInt())
        val bw = body.width(); val bh = body.height()
        val r = bw * 0.07f
        paint.color = 0x55000000
        c.drawRoundRect(body.left + bw * 0.015f, body.top + bw * 0.02f, body.right + bw * 0.015f, body.bottom + bw * 0.02f, r, r, paint)
        paint.shader = LinearGradient(0f, body.top, 0f, body.bottom, 0xFFFBFBFA.toInt(), 0xFFE2E2E0.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, r, r, paint); paint.shader = null
        stroke.color = 0xFFCDCDCB.toInt(); stroke.strokeWidth = bw * 0.006f
        c.drawRoundRect(body, r, r, stroke)
        // the lavender frame (bottom right corner rounded off as on the photo) and the screen
        val fr = bw * 0.03f
        val path = Path().apply {
            addRoundRect(frameR, floatArrayOf(fr, fr, fr, fr, bw * 0.12f, bw * 0.12f, fr, fr), Path.Direction.CW)
        }
        paint.color = 0xFFC9BEE6.toInt(); c.drawPath(path, paint)
        paint.color = 0xFF0C0C0E.toInt()
        c.drawRect(screen.left - bw * 0.01f, screen.top - bw * 0.01f, screen.right + bw * 0.01f, screen.bottom + bw * 0.01f, paint)
        frame.setPixels(framePx, 0, 256, 0, 0, 256, 240)
        c.drawBitmap(frame, null, screen, smooth)
        // power light on the frame's left edge
        paint.color = 0xFFE03A2A.toInt(); c.drawCircle(frameR.left + bw * 0.035f, screen.centerY(), bw * 0.008f, paint)

        val held = touchMask or externalMask
        drawCross(c, held)
        for (i in 0 until 4) drawRed(c, keys[i].x, keys[i].y, keyR, held and keyBits[i] != 0)
        drawRed(c, menuKey.x, menuKey.y, menuR, false)
        drawPill(c, sel, held and SELECT != 0)
        drawPill(c, start, held and START != 0)
        // speaker slots, bottom right
        stroke.color = 0xFFB8B8B6.toInt(); stroke.strokeWidth = bw * 0.012f
        for (i in 0 until 3) {
            val x = body.left + bw * (0.78f + i * 0.04f)
            c.drawLine(x, body.top + bh * 0.84f, x + bw * 0.05f, body.top + bh * 0.9f, stroke)
        }
    }

    private fun drawRed(c: Canvas, x: Float, y: Float, rad: Float, down: Boolean) {
        paint.color = 0xFFD0D0CE.toInt(); c.drawCircle(x, y, rad * 1.14f, paint)
        paint.shader = RadialGradient(x - rad * 0.3f, y - rad * 0.3f, rad * 1.3f, if (down) 0xFF7A0E14.toInt() else 0xFFB82030.toInt(), 0xFF6A0A10.toInt(), Shader.TileMode.CLAMP)
        c.drawCircle(x, y + (if (down) rad * 0.06f else 0f), rad, paint); paint.shader = null
    }

    private fun drawPill(c: Canvas, r: RectF, down: Boolean) {
        paint.color = if (down) 0xFF7A0E14.toInt() else 0xFFA81C2A.toInt()
        c.drawRoundRect(r, r.height() * 0.3f, r.height() * 0.3f, paint)
    }

    private fun drawCross(c: Canvas, held: Int) {
        val k = dpadR * 0.36f
        paint.color = 0x33000000
        c.drawRect(dpad.x - k, dpad.y - dpadR + k * 0.2f, dpad.x + k, dpad.y + dpadR + k * 0.2f, paint)
        c.drawRect(dpad.x - dpadR, dpad.y - k + k * 0.2f, dpad.x + dpadR, dpad.y + k + k * 0.2f, paint)
        paint.color = 0xFFA81C2A.toInt()
        c.drawRoundRect(dpad.x - k, dpad.y - dpadR, dpad.x + k, dpad.y + dpadR, k * 0.2f, k * 0.2f, paint)
        c.drawRoundRect(dpad.x - dpadR, dpad.y - k, dpad.x + dpadR, dpad.y + k, k * 0.2f, k * 0.2f, paint)
        paint.color = 0xFF6A0A10.toInt()
        val arms = listOf(Triple(0f, -1f, UP), Triple(0f, 1f, DOWN), Triple(-1f, 0f, LEFT), Triple(1f, 0f, RIGHT))
        for ((dx, dy, bit) in arms) if (held and bit != 0) {
            val cx = dpad.x + dx * dpadR * 0.62f; val cy = dpad.y + dy * dpadR * 0.62f
            c.drawRect(cx - k, cy - k, cx + k, cy + k, paint)
        }
        paint.color = 0xFF8E1622.toInt(); c.drawCircle(dpad.x, dpad.y, k * 0.45f, paint)
    }

    private fun hit(x: Float, y: Float): Int {
        val dx = x - dpad.x; val dy = y - dpad.y
        if (hypot(dx, dy) < dpadR * 1.35f) {
            var b = 0
            if (dx > dpadR * 0.25f) b = b or RIGHT
            if (dx < -dpadR * 0.25f) b = b or LEFT
            if (dy > dpadR * 0.25f) b = b or DOWN
            if (dy < -dpadR * 0.25f) b = b or UP
            return b
        }
        for (i in 0 until 4) if (hypot(x - keys[i].x, y - keys[i].y) < keyR * 1.45f) return keyBits[i]
        val pad = body.width() * 0.03f
        if (x > sel.left - pad && x < sel.right + pad && y > sel.top - pad && y < sel.bottom + pad) return SELECT
        if (x > start.left - pad && x < start.right + pad && y > start.top - pad && y < start.bottom + pad) return START
        return 0
    }

    private fun update() {
        var m = 0
        for (v in pointers.values) m = m or v
        if (m != touchMask) {
            if (m and touchMask.inv() != 0) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            touchMask = m
            onInput?.invoke(m)
            invalidate()
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                val x = e.getX(i); val y = e.getY(i)
                if (e.actionMasked == MotionEvent.ACTION_DOWN &&
                    (hypot(x - menuKey.x, y - menuKey.y) < menuR * 2.5f || frameR.contains(x, y))) { onMenu?.invoke(); return true }
                pointers[e.getPointerId(i)] = hit(x, y)
                update()
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until e.pointerCount) {
                    val id = e.getPointerId(i)
                    if (pointers.containsKey(id)) pointers[id] = hit(e.getX(i), e.getY(i))
                }
                update()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> { pointers.remove(e.getPointerId(e.actionIndex)); update() }
            MotionEvent.ACTION_CANCEL -> { pointers.clear(); update() }
        }
        return true
    }
}
