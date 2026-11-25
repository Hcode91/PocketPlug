package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.hypot

/**
 * Karaoke microphone that plugs into the TV (Karaoke Ranking Party kind), after a photo: a
 * flat-screen TV on top, and under it the white stick microphone lying across the screen - the
 * round grille head on the left, a gold ring, the white handle with its blue keys (a big star in
 * the middle with up / down / left / right keys round it, then two small keys) and the blue
 * end cap. No maker's name or logo; the TV's stand opens the menu.
 *
 * Input bits as HandheldView: up 0x01, down 0x02, left 0x04, right 0x08, star (A) 0x10,
 * back (B) 0x20, row (START) 0x40.
 */
@SuppressLint("ViewConstructor")
class KaraokeMicView(ctx: Context, private val fw: Int = 640, private val fh: Int = 480) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    companion object {
        const val UP = 0x01; const val DOWN = 0x02; const val LEFT = 0x04; const val RIGHT = 0x08
        const val STAR = 0x10; const val BACK = 0x20; const val ROW = 0x40
    }

    private val frame = Bitmap.createBitmap(fw, fh, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(fw * fh)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }
    private val smooth = Paint(Paint.FILTER_BITMAP_FLAG)

    private val tv = RectF(); private val screen = RectF(); private val stand = RectF()
    private val mic = RectF(); private var headR = 0f; private val head = PointF()
    private class Btn(val bit: Int, val label: String) { val c = PointF(); var r = 0f; var pill = false }
    private val btns = listOf(Btn(STAR, "★"), Btn(UP, "▲"), Btn(DOWN, "▼"), Btn(LEFT, "◀"), Btn(RIGHT, "▶"), Btn(BACK, "✦"), Btn(ROW, "»"))
    private val pointers = HashMap<Int, Int>()
    private var touchMask = 0

    fun updateFrame(px: IntArray) {
        if (px.size < framePx.size) return
        System.arraycopy(px, 0, framePx, 0, framePx.size)
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        // TV: 4:3 picture in a thin black frame, on a small stand
        var tw = w * 0.96f
        var th = tw * 0.75f
        if (th > h * 0.52f) { th = h * 0.52f; tw = th / 0.75f }
        val tl = (w - tw) / 2; val tt = h * 0.04f
        tv.set(tl, tt, tl + tw, tt + th)
        val b = tw * 0.02f
        screen.set(tv.left + b, tv.top + b, tv.right - b, tv.bottom - b * 1.6f)
        stand.set(tv.centerX() - tw * 0.12f, tv.bottom, tv.centerX() + tw * 0.12f, tv.bottom + h * 0.03f)

        // the microphone lying across, head on the left
        val mw = w * 0.94f
        val mh = minOf(mw * 0.3f, (h - stand.bottom) * 0.7f)
        val ml = (w - mw) / 2
        val mt = stand.bottom + ((h - stand.bottom) - mh) / 2
        mic.set(ml, mt, ml + mw, mt + mh)
        headR = mh * 0.5f
        head.set(mic.left + headR, mic.centerY())

        // keys on the handle: the cross round the star, then back and row
        val cx = mic.left + mw * 0.46f; val cy = mic.centerY()
        val unit = mh * 0.25f
        fun set(i: Int, x: Float, y: Float, r: Float, pill: Boolean = false) { btns[i].c.set(x, y); btns[i].r = r; btns[i].pill = pill }
        set(0, cx, cy, unit * 0.95f)
        set(1, cx, cy - unit * 1.45f, unit * 0.52f)
        set(2, cx, cy + unit * 1.45f, unit * 0.52f)
        set(3, cx - unit * 1.75f, cy, unit * 0.58f)
        set(4, cx + unit * 1.75f, cy, unit * 0.58f)
        set(5, mic.left + mw * 0.66f, cy - unit * 0.75f, unit * 0.5f, true)
        set(6, mic.left + mw * 0.66f, cy + unit * 0.75f, unit * 0.5f, true)
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF1B1D24.toInt())
        // TV
        paint.color = 0xFF0A0A0C.toInt()
        c.drawRoundRect(tv, tv.width() * 0.01f, tv.width() * 0.01f, paint)
        stroke.color = 0xFF3A3C44.toInt(); stroke.strokeWidth = tv.width() * 0.004f
        c.drawRoundRect(tv, tv.width() * 0.01f, tv.width() * 0.01f, stroke)
        frame.setPixels(framePx, 0, fw, 0, 0, fw, fh)
        c.drawBitmap(frame, null, screen, smooth)
        paint.color = 0xFF2A2C34.toInt()
        c.drawRect(stand.left + stand.width() * 0.35f, stand.top, stand.right - stand.width() * 0.35f, stand.bottom, paint)
        c.drawRoundRect(stand.left, stand.bottom - stand.height() * 0.3f, stand.right, stand.bottom, stand.height() * 0.15f, stand.height() * 0.15f, paint)
        text.color = 0xFF6A6E7A.toInt(); text.textSize = stand.height() * 0.5f
        c.drawText("MENU", stand.centerX() + stand.width() * 0.8f, stand.bottom - stand.height() * 0.2f, text)

        drawMic(c)
    }

    private fun drawMic(c: Canvas) {
        val mh = mic.height()
        val held = touchMask or externalMask
        // handle: a long white body, a little narrower toward the cap
        val hl = head.x + headR * 0.7f
        val capL = mic.right - mh * 0.55f
        val path = Path().apply {
            moveTo(hl, mic.centerY() - mh * 0.36f)
            lineTo(capL, mic.centerY() - mh * 0.3f)
            lineTo(capL, mic.centerY() + mh * 0.3f)
            lineTo(hl, mic.centerY() + mh * 0.36f)
            close()
        }
        paint.color = 0x50000000
        c.save(); c.translate(0f, mh * 0.05f); c.drawPath(path, paint); c.restore()
        paint.shader = LinearGradient(0f, mic.centerY() - mh * 0.36f, 0f, mic.centerY() + mh * 0.36f,
            intArrayOf(0xFFE4E6EA.toInt(), 0xFFFFFFFF.toInt(), 0xFFD0D3D8.toInt()), floatArrayOf(0f, 0.35f, 1f), Shader.TileMode.CLAMP)
        c.drawPath(path, paint); paint.shader = null
        // blue end cap
        val cap = RectF(capL - mh * 0.02f, mic.centerY() - mh * 0.31f, mic.right, mic.centerY() + mh * 0.31f)
        paint.shader = LinearGradient(0f, cap.top, 0f, cap.bottom, intArrayOf(0xFF2FA9D8.toInt(), 0xFF6CCDF0.toInt(), 0xFF1E86B4.toInt()), floatArrayOf(0f, 0.35f, 1f), Shader.TileMode.CLAMP)
        c.drawRoundRect(cap, mh * 0.12f, mh * 0.12f, paint); paint.shader = null
        stroke.color = 0xFF1A6E96.toInt(); stroke.strokeWidth = mh * 0.012f
        for (k in 1..3) { val x = cap.left + cap.width() * k / 4f; c.drawLine(x, cap.top + mh * 0.05f, x, cap.bottom - mh * 0.05f, stroke) }
        // a thin blue band near the cap, as the photo's
        paint.color = 0xFF3DB2E0.toInt()
        c.drawRect(capL - mh * 0.5f, mic.centerY() - mh * 0.305f, capL - mh * 0.44f, mic.centerY() + mh * 0.305f, paint)

        // gold ring between head and handle
        val ring = RectF(head.x + headR * 0.62f, mic.centerY() - mh * 0.38f, head.x + headR * 0.95f, mic.centerY() + mh * 0.38f)
        paint.shader = LinearGradient(0f, ring.top, 0f, ring.bottom, intArrayOf(0xFFB89A4A.toInt(), 0xFFF2DE9A.toInt(), 0xFF9C7E36.toInt()), floatArrayOf(0f, 0.4f, 1f), Shader.TileMode.CLAMP)
        c.drawRoundRect(ring, mh * 0.05f, mh * 0.05f, paint); paint.shader = null
        stroke.color = 0x66FFFFFF; stroke.strokeWidth = mh * 0.01f
        for (k in 0..4) { val x = ring.left + ring.width() * (k + 0.5f) / 5f; c.drawLine(x, ring.top + mh * 0.06f, x, ring.bottom - mh * 0.06f, stroke) }

        // grille head: white dome with a fine dot mesh
        paint.color = 0x50000000; c.drawCircle(head.x, head.y + mh * 0.04f, headR, paint)
        paint.shader = RadialGradient(head.x - headR * 0.35f, head.y - headR * 0.4f, headR * 1.4f, 0xFFFFFFFF.toInt(), 0xFFC9CDD3.toInt(), Shader.TileMode.CLAMP)
        c.drawCircle(head.x, head.y, headR, paint); paint.shader = null
        paint.color = 0x22000000
        val step = headR * 0.12f
        var yy = head.y - headR
        while (yy < head.y + headR) {
            var xx = head.x - headR
            while (xx < head.x + headR) {
                if (hypot(xx - head.x, yy - head.y) < headR * 0.9f) c.drawCircle(xx, yy, step * 0.18f, paint)
                xx += step
            }
            yy += step
        }

        // keys
        for (b in btns) {
            val down = held and b.bit != 0
            val dy = if (down) b.r * 0.06f else 0f
            paint.color = 0x40000000
            if (b.pill) c.drawRoundRect(b.c.x - b.r * 1.3f, b.c.y - b.r * 0.62f + b.r * 0.12f, b.c.x + b.r * 1.3f, b.c.y + b.r * 0.62f + b.r * 0.12f, b.r * 0.6f, b.r * 0.6f, paint)
            else c.drawCircle(b.c.x, b.c.y + b.r * 0.1f, b.r, paint)
            paint.color = if (down) 0xFF1E8FC4.toInt() else 0xFF33B5E6.toInt()
            if (b.bit == STAR) {
                drawStar(c, b.c.x, b.c.y + dy, b.r * 1.1f, paint)
            } else if (b.pill) {
                c.drawRoundRect(b.c.x - b.r * 1.3f, b.c.y - b.r * 0.62f + dy, b.c.x + b.r * 1.3f, b.c.y + b.r * 0.62f + dy, b.r * 0.6f, b.r * 0.6f, paint)
            } else c.drawCircle(b.c.x, b.c.y + dy, b.r, paint)
            if (b.bit != STAR) {
                text.color = 0xFFFFFFFF.toInt(); text.textSize = b.r * 0.85f
                c.drawText(b.label, b.c.x, b.c.y + dy + text.textSize * 0.36f, text)
            }
        }
    }

    private fun drawStar(c: Canvas, cx: Float, cy: Float, r: Float, p: Paint) {
        val path = Path()
        for (i in 0 until 10) {
            val a = Math.PI / 2 + i * Math.PI / 5
            val rr = if (i % 2 == 0) r else r * 0.48f
            val x = cx + (rr * Math.cos(a)).toFloat(); val y = cy - (rr * Math.sin(a)).toFloat()
            if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
        }
        path.close()
        val saved = p.color
        p.color = 0x40000000
        c.save(); c.translate(0f, r * 0.08f); c.drawPath(path, p); c.restore()
        p.color = saved
        c.drawPath(path, p)
    }

    private fun hit(x: Float, y: Float): Int {
        var best = 0; var bd = Float.MAX_VALUE
        for (b in btns) {
            val d = hypot(x - b.c.x, y - b.c.y) / (b.r * (if (b.pill) 1.5f else 1.25f))
            if (d < 1f && d < bd) { bd = d; best = b.bit }
        }
        return best
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
                if (e.actionMasked == MotionEvent.ACTION_DOWN && (stand.contains(x, y) || (y > tv.bottom && y < mic.top))) { onMenu?.invoke(); return true }
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
