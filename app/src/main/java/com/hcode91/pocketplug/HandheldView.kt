package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.*

/**
 * Vertical multi-game handheld (the 265-in-1 SPG2xx type): a glossy two-tone
 * shell with the 4:3 colour screen at the top, a D-pad, red A and yellow B
 * buttons, a START pill, a power LED and a speaker grille. The design is
 * original. Tap the logo under the screen for the menu.
 *
 * Input bits: Up 0x01, Down 0x02, Left 0x04, Right 0x08, A 0x10, B 0x20, Start 0x40.
 */
class HandheldView(ctx: Context) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    companion object {
        const val UP = 0x01; const val DOWN = 0x02; const val LEFT = 0x04; const val RIGHT = 0x08
        const val A = 0x10; const val B = 0x20; const val START = 0x40
    }

    private val frame = Bitmap.createBitmap(320, 240, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(320 * 240)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }
    private val smooth = Paint(Paint.FILTER_BITMAP_FLAG)

    private val body = RectF()
    private val screen = RectF()
    private val bezel = RectF()
    private val logo = RectF()
    private val dpad = PointF(); private var dpadR = 0f
    private val btnA = PointF(); private val btnB = PointF(); private var abR = 0f
    private val btnStart = RectF()
    private val grille = PointF()

    private val pointers = HashMap<Int, Int>()
    private var touchMask = 0

    fun updateFrame(px: IntArray) {
        if (px.size < 320 * 240) return
        System.arraycopy(px, 0, framePx, 0, 320 * 240)
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        var bw = w * 0.94f
        var bh = bw / 0.56f
        if (bh > h * 0.97f) { bh = h * 0.97f; bw = bh * 0.56f }
        val l = (w - bw) / 2; val t = (h - bh) / 2
        body.set(l, t, l + bw, t + bh)
        val sw = bw * 0.80f
        val sh = sw * 240f / 320f
        screen.set(body.centerX() - sw / 2, t + bw * 0.13f, body.centerX() + sw / 2, t + bw * 0.13f + sh)
        bezel.set(screen.left - bw * 0.05f, screen.top - bw * 0.06f, screen.right + bw * 0.05f, screen.bottom + bw * 0.10f)
        logo.set(bezel.left, screen.bottom + bw * 0.015f, bezel.right, bezel.bottom)
        val cy = bezel.bottom + bw * 0.30f
        dpadR = bw * 0.17f
        dpad.set(l + bw * 0.27f, cy)
        abR = bw * 0.085f
        btnB.set(l + bw * 0.66f, cy + bw * 0.06f)
        btnA.set(l + bw * 0.84f, cy - bw * 0.06f)
        val sy = cy + bw * 0.32f
        btnStart.set(body.centerX() - bw * 0.075f, sy - bw * 0.03f, body.centerX() + bw * 0.075f, sy + bw * 0.03f)
        grille.set(l + bw * 0.80f, body.bottom - bw * 0.16f)
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0814.toInt())
        val bw = body.width()
        val r = bw * 0.14f

        // shell: deep teal-blue with a lighter lower half and a gloss band
        paint.color = 0x99000000.toInt()
        c.drawRoundRect(body.left + bw * 0.015f, body.top + bw * 0.025f, body.right + bw * 0.015f, body.bottom + bw * 0.025f, r, r, paint)
        paint.color = Color.WHITE
        paint.shader = LinearGradient(0f, body.top, 0f, body.bottom,
            intArrayOf(0xFF1F6FB8.toInt(), 0xFF1858A0.toInt(), 0xFF123F7A.toInt()), floatArrayOf(0f, 0.5f, 1f), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, r, r, paint)
        paint.shader = LinearGradient(body.left, 0f, body.right, 0f,
            intArrayOf(0x30FFFFFF, 0x00FFFFFF, 0x00FFFFFF, 0x22FFFFFF), floatArrayOf(0f, 0.2f, 0.85f, 1f), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, r, r, paint)
        paint.shader = null
        stroke.color = 0xFF0C2E5C.toInt(); stroke.strokeWidth = bw * 0.01f
        c.drawRoundRect(body, r, r, stroke)
        // parting line between top and bottom halves
        stroke.color = 0x55FFFFFF; stroke.strokeWidth = bw * 0.004f
        c.drawLine(body.left + r * 0.4f, bezel.bottom + bw * 0.06f, body.right - r * 0.4f, bezel.bottom + bw * 0.06f, stroke)

        // screen bezel (black glass) and picture
        paint.color = 0xFF0A0B10.toInt()
        c.drawRoundRect(bezel, bw * 0.04f, bw * 0.04f, paint)
        stroke.color = 0xFF9FB3C8.toInt(); stroke.strokeWidth = bw * 0.006f
        c.drawRoundRect(bezel, bw * 0.04f, bw * 0.04f, stroke)
        frame.setPixels(framePx, 0, 320, 0, 0, 320, 240)
        c.drawBitmap(frame, null, screen, smooth)
        paint.color = Color.WHITE
        paint.shader = LinearGradient(screen.left, screen.top, screen.left + screen.width() * 0.6f, screen.bottom, 0x1CFFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP)
        c.drawRect(screen, paint); paint.shader = null
        // power LED
        paint.color = 0xFF39FF6A.toInt()
        c.drawCircle(bezel.left + bw * 0.04f, logo.centerY(), bw * 0.012f, paint)
        // logo strip (tap for menu)
        text.textSize = logo.height() * 0.55f
        text.color = Color.WHITE
        text.shader = LinearGradient(0f, logo.top, 0f, logo.bottom, 0xFFFFE680.toInt(), 0xFFFF9A2E.toInt(), Shader.TileMode.CLAMP)
        c.drawText("265 GAMES", logo.centerX(), logo.centerY() + text.textSize * 0.36f, text)
        text.shader = null

        val held = touchMask or externalMask
        drawDpad(c, held)
        // A / B on a darker oval pad
        c.save()
        c.rotate(-20f, (btnA.x + btnB.x) / 2, (btnA.y + btnB.y) / 2)
        val pw = hypot(btnA.x - btnB.x, btnA.y - btnB.y) + abR * 2.9f
        paint.color = 0x40000000
        val pcx = (btnA.x + btnB.x) / 2; val pcy = (btnA.y + btnB.y) / 2
        c.drawRoundRect(pcx - pw / 2, pcy - abR * 1.45f, pcx + pw / 2, pcy + abR * 1.45f, abR * 1.45f, abR * 1.45f, paint)
        c.restore()
        drawRound(c, btnB, abR, held and B != 0, 0xFFFFD23A.toInt(), 0xFFC98F12.toInt())
        drawRound(c, btnA, abR, held and A != 0, 0xFFFF4B4B.toInt(), 0xFFB01E28.toInt())
        text.textSize = abR * 0.9f
        text.color = 0xCCFFFFFF.toInt()
        c.drawText("B", btnB.x, btnB.y + abR * 2.15f, text)
        c.drawText("A", btnA.x, btnA.y + abR * 2.15f, text)

        // START pill
        paint.color = if (held and START != 0) 0xFF5A6575.toInt() else 0xFF8C98A8.toInt()
        c.drawRoundRect(btnStart, btnStart.height() / 2, btnStart.height() / 2, paint)
        text.textSize = bw * 0.035f
        text.color = 0xCCFFFFFF.toInt()
        c.drawText("START", btnStart.centerX(), btnStart.bottom + bw * 0.05f, text)

        // speaker grille
        paint.color = 0xFF0B2448.toInt()
        val gr = bw * 0.011f
        for (row in -2..2) for (col in -2..2) {
            if (row * row + col * col > 5) continue
            c.drawCircle(grille.x + col * gr * 3f, grille.y + row * gr * 3f, gr, paint)
        }
    }

    private fun drawRound(c: Canvas, p: PointF, r: Float, down: Boolean, top: Int, bottom: Int) {
        paint.color = 0x88000000.toInt()
        c.drawCircle(p.x, p.y + r * 0.14f, r * 1.06f, paint)
        paint.color = Color.WHITE
        paint.shader = RadialGradient(p.x - r * 0.3f, p.y - r * 0.35f, r * 1.4f,
            if (down) intArrayOf(bottom, bottom) else intArrayOf(top, bottom), null, Shader.TileMode.CLAMP)
        c.drawCircle(p.x, p.y + (if (down) r * 0.07f else 0f), r * (if (down) 0.95f else 1f), paint)
        paint.shader = null
    }

    private fun drawDpad(c: Canvas, held: Int) {
        val r = dpadR; val arm = r * 0.36f
        paint.color = 0x50000000
        c.drawCircle(dpad.x, dpad.y, r * 1.12f, paint)
        paint.color = 0xFF1B1E24.toInt()
        c.drawRoundRect(dpad.x - arm, dpad.y - r, dpad.x + arm, dpad.y + r, arm * 0.3f, arm * 0.3f, paint)
        c.drawRoundRect(dpad.x - r, dpad.y - arm, dpad.x + r, dpad.y + arm, arm * 0.3f, arm * 0.3f, paint)
        paint.color = 0xFF3A3F48.toInt()
        if (held and UP != 0) c.drawRect(dpad.x - arm, dpad.y - r, dpad.x + arm, dpad.y - arm, paint)
        if (held and DOWN != 0) c.drawRect(dpad.x - arm, dpad.y + arm, dpad.x + arm, dpad.y + r, paint)
        if (held and LEFT != 0) c.drawRect(dpad.x - r, dpad.y - arm, dpad.x - arm, dpad.y + arm, paint)
        if (held and RIGHT != 0) c.drawRect(dpad.x + arm, dpad.y - arm, dpad.x + r, dpad.y + arm, paint)
        paint.color = 0xFF4A505B.toInt()
        val a = arm * 0.45f
        fun tri(x: Float, y: Float, dx: Float, dy: Float) {
            val p = Path(); p.moveTo(x + dx * a, y + dy * a)
            p.lineTo(x - dy * a * 0.8f, y + dx * a * 0.8f); p.lineTo(x + dy * a * 0.8f, y - dx * a * 0.8f); p.close()
            c.drawPath(p, paint)
        }
        tri(dpad.x, dpad.y - r * 0.68f, 0f, -1f); tri(dpad.x, dpad.y + r * 0.68f, 0f, 1f)
        tri(dpad.x - r * 0.68f, dpad.y, -1f, 0f); tri(dpad.x + r * 0.68f, dpad.y, 1f, 0f)
        stroke.color = 0xFF30343C.toInt(); stroke.strokeWidth = arm * 0.12f
        c.drawCircle(dpad.x, dpad.y, arm * 0.55f, stroke)
    }

    // ---- touch ----
    private fun hit(x: Float, y: Float): Int {
        val ddx = x - dpad.x; val ddy = y - dpad.y
        if (hypot(ddx, ddy) < dpadR * 1.3f) {
            if (hypot(ddx, ddy) < dpadR * 0.18f) return 0
            val ang = Math.toDegrees(atan2(ddy.toDouble(), ddx.toDouble()))
            var m = 0
            if (ang > -157.5 && ang < -22.5) m = m or UP
            if (ang > 22.5 && ang < 157.5) m = m or DOWN
            if (ang > 112.5 || ang < -112.5) m = m or LEFT
            if (ang > -67.5 && ang < 67.5) m = m or RIGHT
            return m
        }
        if (hypot(x - btnA.x, y - btnA.y) < abR * 1.5f) return A
        if (hypot(x - btnB.x, y - btnB.y) < abR * 1.5f) return B
        if (RectF(btnStart).apply { inset(-btnStart.width() * 0.3f, -btnStart.height() * 1.2f) }.contains(x, y)) return START
        return 0
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                val x = e.getX(i); val y = e.getY(i)
                if (logo.contains(x, y) && e.actionMasked == MotionEvent.ACTION_DOWN) { onMenu?.invoke(); return true }
                val b = hit(x, y)
                if (b != 0) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
                pointers[e.getPointerId(i)] = b
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until e.pointerCount) {
                    val id = e.getPointerId(i)
                    val old = pointers[id] ?: continue
                    if (old and 0x0f != 0 || old == 0) {
                        val b = hit(e.getX(i), e.getY(i))
                        if (b and 0x0f != 0 || b == 0) pointers[id] = b
                    }
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> pointers.remove(e.getPointerId(e.actionIndex))
            MotionEvent.ACTION_CANCEL -> pointers.clear()
        }
        var m = 0
        for (v in pointers.values) m = m or v
        if (m != touchMask) { touchMask = m; onInput?.invoke(m) }
        invalidate()
        return true
    }
}
