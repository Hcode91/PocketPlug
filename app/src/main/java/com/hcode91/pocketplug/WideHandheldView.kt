package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.hypot

/**
 * The white sideways handheld of the 2020s n-in-1 kind: a rounded white body
 * held in both hands, the square colour screen in a grey frame in the middle,
 * a black cross-shaped D-pad of four separate keys on the left, big black A
 * and B on the right and two small round keys up top (SELECT left, START
 * right). Drawn for the phone held sideways; no maker's name is shown - the
 * plain strip under the screen opens the menu.
 */
class WideHandheldView(ctx: Context) : View(ctx) {

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
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }
    private val smooth = Paint(Paint.FILTER_BITMAP_FLAG)

    private val body = RectF()
    private val bezel = RectF()
    private val screen = RectF()
    private val menuStrip = RectF()
    private val dpad = PointF(); private var dpadR = 0f
    private val btnA = PointF(); private val btnB = PointF(); private var abR = 0f
    private val btnSel = PointF(); private val btnStart = PointF(); private var smallR = 0f

    private val pointers = HashMap<Int, Int>()
    private var touchMask = 0

    fun updateFrame(px: IntArray) {
        if (px.size < framePx.size) return
        System.arraycopy(px, 0, framePx, 0, framePx.size)
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        // fit a 2:1 body in the view
        var bw = w * 0.98f; var bh = bw * 0.5f
        if (bh > h * 0.96f) { bh = h * 0.96f; bw = bh * 2f }
        val l = (w - bw) / 2; val t = (h - bh) / 2
        body.set(l, t, l + bw, t + bh)
        val sh = bh * 0.7f; val sw = sh * 256f / 240f
        screen.set(body.centerX() - sw / 2, t + bh * 0.11f, body.centerX() + sw / 2, t + bh * 0.11f + sh)
        bezel.set(screen.left - bh * 0.05f, screen.top - bh * 0.05f, screen.right + bh * 0.05f, screen.bottom + bh * 0.11f)
        menuStrip.set(bezel.left, screen.bottom + bh * 0.015f, bezel.right, bezel.bottom - bh * 0.015f)
        dpadR = bh * 0.2f; dpad.set(l + bw * 0.14f, t + bh * 0.6f)
        abR = bh * 0.085f
        btnB.set(l + bw * 0.885f, t + bh * 0.45f); btnA.set(l + bw * 0.81f, t + bh * 0.68f)
        smallR = bh * 0.045f
        btnSel.set(l + bw * 0.115f, t + bh * 0.2f); btnStart.set(l + bw * 0.885f, t + bh * 0.2f)
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF15161A.toInt())
        val bh = body.height()
        val r = bh * 0.42f
        paint.color = 0x55000000
        c.drawRoundRect(body.left + bh * 0.02f, body.top + bh * 0.03f, body.right + bh * 0.02f, body.bottom + bh * 0.03f, r, r, paint)
        paint.color = Color.WHITE
        paint.shader = LinearGradient(0f, body.top, 0f, body.bottom, 0xFFFAFAFA.toInt(), 0xFFDCDDE0.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, r, r, paint)
        paint.shader = null
        stroke.color = 0xFFC5C7CC.toInt(); stroke.strokeWidth = bh * 0.008f
        c.drawRoundRect(body, r, r, stroke)

        // screen frame and picture
        paint.color = 0xFFE9EAEC.toInt()
        c.drawRoundRect(bezel.left - bh * 0.02f, bezel.top - bh * 0.02f, bezel.right + bh * 0.02f, bezel.bottom + bh * 0.02f, bh * 0.06f, bh * 0.06f, paint)
        stroke.color = 0xFFCFD1D5.toInt(); stroke.strokeWidth = bh * 0.006f
        c.drawRoundRect(bezel.left - bh * 0.02f, bezel.top - bh * 0.02f, bezel.right + bh * 0.02f, bezel.bottom + bh * 0.02f, bh * 0.06f, bh * 0.06f, stroke)
        paint.color = 0xFF0C0C0E.toInt()
        c.drawRect(screen.left - bh * 0.008f, screen.top - bh * 0.008f, screen.right + bh * 0.008f, screen.bottom + bh * 0.008f, paint)
        frame.setPixels(framePx, 0, 256, 0, 0, 256, 240)
        c.drawBitmap(frame, null, screen, smooth)
        text.color = 0xFFB0B3B8.toInt(); text.textSize = menuStrip.height() * 0.5f
        c.drawText("MENU", menuStrip.centerX(), menuStrip.centerY() + text.textSize * 0.36f, text)

        val held = touchMask or externalMask
        drawDpad(c, held)
        drawRound(c, btnB, abR, held and B != 0, "B")
        drawRound(c, btnA, abR, held and A != 0, "A")
        drawRound(c, btnSel, smallR, held and SELECT != 0, "")
        drawRound(c, btnStart, smallR, held and START != 0, "")
        text.color = 0xFF9A9DA3.toInt(); text.textSize = smallR * 0.75f
        c.drawText("SELECT", btnSel.x, btnSel.y + smallR * 2.1f, text)
        c.drawText("START", btnStart.x, btnStart.y + smallR * 2.1f, text)
    }

    private fun drawRound(c: Canvas, p: PointF, rad: Float, down: Boolean, label: String) {
        paint.color = 0xFFC9CBD0.toInt(); c.drawCircle(p.x, p.y, rad * 1.18f, paint)
        paint.color = Color.WHITE
        paint.shader = RadialGradient(p.x - rad * 0.3f, p.y - rad * 0.3f, rad * 1.3f, if (down) 0xFF1A1A1A.toInt() else 0xFF4A4A4E.toInt(), 0xFF0A0A0B.toInt(), Shader.TileMode.CLAMP)
        c.drawCircle(p.x, p.y + (if (down) rad * 0.05f else 0f), rad, paint)
        paint.shader = null
        if (label.isNotEmpty()) {
            text.color = 0xFF6E7076.toInt(); text.textSize = rad * 0.75f
            c.drawText(label, p.x, p.y + text.textSize * 0.36f, text)
        }
    }

    private fun drawDpad(c: Canvas, held: Int) {
        // four separate wedge keys round a centre, as on the photo's pad
        val k = dpadR * 0.42f
        val dirs = listOf(Triple(0f, -1f, UP), Triple(0f, 1f, DOWN), Triple(-1f, 0f, LEFT), Triple(1f, 0f, RIGHT))
        paint.color = 0xFFC9CBD0.toInt(); c.drawCircle(dpad.x, dpad.y, dpadR * 1.05f, paint)
        for ((dx, dy, bit) in dirs) {
            val cx = dpad.x + dx * dpadR * 0.55f; val cy = dpad.y + dy * dpadR * 0.55f
            val rr = RectF(cx - k, cy - k, cx + k, cy + k)
            paint.color = if (held and bit != 0) 0xFF0E0E10.toInt() else 0xFF2C2C30.toInt()
            c.drawRoundRect(rr, k * 0.35f, k * 0.35f, paint)
            // arrow notch
            paint.color = 0xFF55575C.toInt()
            val path = Path()
            path.moveTo(cx + dx * k * 0.45f, cy + dy * k * 0.45f)
            path.lineTo(cx - dy * k * 0.3f - dx * k * 0.1f, cy - dx * k * 0.3f - dy * k * 0.1f)
            path.lineTo(cx + dy * k * 0.3f - dx * k * 0.1f, cy + dx * k * 0.3f - dy * k * 0.1f)
            path.close()
            c.drawPath(path, paint)
        }
    }

    private fun hit(x: Float, y: Float): Int {
        var b = 0
        val dx = x - dpad.x; val dy = y - dpad.y
        if (hypot(dx, dy) < dpadR * 1.4f && hypot(dx, dy) > dpadR * 0.15f) {
            if (dx > dpadR * 0.25f) b = b or RIGHT
            if (dx < -dpadR * 0.25f) b = b or LEFT
            if (dy > dpadR * 0.25f) b = b or DOWN
            if (dy < -dpadR * 0.25f) b = b or UP
            return b
        }
        if (hypot(x - btnA.x, y - btnA.y) < abR * 1.5f) return A
        if (hypot(x - btnB.x, y - btnB.y) < abR * 1.5f) return B
        if (hypot(x - btnSel.x, y - btnSel.y) < smallR * 2.2f) return SELECT
        if (hypot(x - btnStart.x, y - btnStart.y) < smallR * 2.2f) return START
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
                if (e.actionMasked == MotionEvent.ACTION_DOWN && menuStrip.contains(x, y)) { onMenu?.invoke(); return true }
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
