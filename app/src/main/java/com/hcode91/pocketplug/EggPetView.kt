package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.MotionEvent
import android.view.View
import kotlin.math.*

/**
 * The 1996 egg-shaped LCD pet on a key chain: translucent teal shell, a square
 * window with a printed backdrop (yellow bands with flowers and hearts, a pastel
 * diamond pattern in the middle), the 32x16 dot-matrix LCD with its eight icon
 * segments, and three yellow buttons (A / B / C, left to right).
 *
 * Frames come from the E0C6S46 core: 32x17, rows 0-15 = dots, row 16 = icons.
 * Tapping the key ring opens the emulator menu.
 */
class EggPetView(ctx: Context) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    /** Shown over the LCD while the clock is fast-forwarded (e.g. "Catching up 3h"). */
    @Volatile var status: String? = null
        set(v) { field = v; postInvalidate() }
    /** Buttons held from a gamepad/keyboard (lights the buttons). */
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    private val dots = BooleanArray(32 * 16)
    private val icons = BooleanArray(8)
    private var touchMask = 0
    private val pointerBtn = HashMap<Int, Int>()

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }

    // geometry (computed in onSizeChanged)
    private val egg = Path()
    private val eggBounds = RectF()
    private val lcd = RectF()
    private val ring = PointF(); private var ringR = 0f
    private val btn = Array(3) { PointF() }; private var btnR = 0f
    private var backdrop: Bitmap? = null

    fun updateFrame(px: IntArray) {
        if (px.size < 32 * 17) return
        var changed = false
        for (i in 0 until 32 * 16) { val on = px[i] != 0; if (dots[i] != on) { dots[i] = on; changed = true } }
        for (i in 0 until 8) { val on = px[32 * 16 + i] != 0; if (icons[i] != on) { icons[i] = on; changed = true } }
        if (changed) postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        // egg body: ~ 0.78 x 0.92 aspect; key ring above it
        val ew = min(w * 0.84f, h * 0.64f / 1.2f)
        val eh = ew * 1.2f
        val cx = w / 2f
        val top = h - eh - h * 0.03f
        eggBounds.set(cx - ew / 2, top, cx + ew / 2, top + eh)
        egg.reset()
        // egg: narrower top, fuller bottom (superellipse-ish via cubic curves)
        val l = eggBounds.left; val r = eggBounds.right; val t = eggBounds.top; val b = eggBounds.bottom
        val my = t + eh * 0.6f
        egg.moveTo(cx, t)
        egg.cubicTo(cx + ew * 0.27f, t, r - ew * 0.03f, my - eh * 0.45f, r, my)
        egg.cubicTo(r + ew * 0.005f, my + eh * 0.32f, cx + ew * 0.34f, b, cx, b)
        egg.cubicTo(cx - ew * 0.34f, b, l - ew * 0.005f, my + eh * 0.32f, l, my)
        egg.cubicTo(l + ew * 0.03f, my - eh * 0.45f, cx - ew * 0.27f, t, cx, t)
        egg.close()

        val s = ew * 0.54f
        lcd.set(cx - s / 2, t + eh * 0.29f, cx + s / 2, t + eh * 0.29f + s)
        btnR = ew * 0.058f
        val by = t + eh * 0.85f
        btn[0].set(cx - ew * 0.19f, by - eh * 0.012f)
        btn[1].set(cx, by + eh * 0.018f)
        btn[2].set(cx + ew * 0.19f, by - eh * 0.012f)
        ringR = min(ew * 0.22f, (top - h * 0.02f) * 0.30f).coerceAtLeast(ew * 0.08f)
        ring.set(cx + ew * 0.05f, max(ringR * 1.15f, top * 0.30f))
        backdrop = makeBackdrop(s.toInt().coerceAtLeast(16))
    }

    // ---- printed backdrop behind the LCD glass ----
    private fun makeBackdrop(size: Int): Bitmap {
        val bm = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
        val c = Canvas(bm)
        val p = Paint(Paint.ANTI_ALIAS_FLAG)
        val band = size * 0.24f
        val yellow = 0xFFE6D96A.toInt(); val pink = 0xFFE7B9C4.toInt(); val blue = 0xFF9EBFCB.toInt()
        // diamond (argyle) middle: a checkerboard turned by 45 degrees
        c.drawColor(0xFFD3D0C6.toInt())
        c.save()
        c.clipRect(0f, band, size.toFloat(), size - band)
        c.rotate(45f, size / 2f, size / 2f)
        val d = size / 9.5f
        val g = d * 0.06f
        var i = -12
        while (i < 12) {
            var j = -12
            while (j < 12) {
                p.color = if (((i + j) and 1) == 0) blue else pink
                val x = size / 2f + i * d; val y = size / 2f + j * d
                c.drawRect(x + g, y + g, x + d - g, y + d - g, p)
                j++
            }
            i++
        }
        c.restore()
        // yellow bands with flowers and hearts
        for (bt in floatArrayOf(0f, size - band)) {
            p.color = yellow
            c.drawRect(0f, bt, size.toFloat(), bt + band, p)
            for (i in 0 until 4) {
                val x = size * (0.13f + i * 0.25f) + (if (bt > 0) size * 0.06f else 0f)
                val yy = bt + band * 0.5f
                if ((i + (if (bt > 0) 1 else 0)) % 2 == 0) flower(c, p, x, yy, band * 0.36f, pink)
                else heart(c, p, x, yy, band * 0.40f, blue)
            }
        }
        // a little soft print grain
        p.color = 0x10000000
        val rnd = java.util.Random(7)
        repeat(size * 3) { c.drawCircle(rnd.nextFloat() * size, rnd.nextFloat() * size, 0.6f, p) }
        return bm
    }

    private fun flower(c: Canvas, p: Paint, x: Float, y: Float, r: Float, col: Int) {
        p.color = col
        for (k in 0 until 5) {
            val a = k * 2 * PI / 5 - PI / 2
            c.drawCircle(x + (cos(a) * r * 0.55f).toFloat(), y + (sin(a) * r * 0.55f).toFloat(), r * 0.42f, p)
        }
        p.color = 0xFFD99AAA.toInt(); c.drawCircle(x, y, r * 0.25f, p)
    }

    private fun heart(c: Canvas, p: Paint, x: Float, y: Float, r: Float, col: Int) {
        p.color = col
        val path = Path()
        path.moveTo(x, y + r * 0.9f)
        path.cubicTo(x - r * 1.4f, y - r * 0.1f, x - r * 0.7f, y - r * 1.1f, x, y - r * 0.35f)
        path.cubicTo(x + r * 0.7f, y - r * 1.1f, x + r * 1.4f, y - r * 0.1f, x, y + r * 0.9f)
        c.drawPath(path, p)
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0814.toInt())
        drawKeyChain(c)
        drawShell(c)
        drawLcd(c)
        drawButtons(c)
    }

    private fun drawKeyChain(c: Canvas) {
        val metal = LinearGradient(0f, ring.y - ringR, 0f, ring.y + ringR,
            intArrayOf(0xFFF2F2F2.toInt(), 0xFF8E9096.toInt(), 0xFFE0E0E0.toInt(), 0xFF6C6E74.toInt()), null, Shader.TileMode.CLAMP)
        // split ring
        stroke.color = Color.WHITE
        stroke.shader = metal
        stroke.strokeWidth = ringR * 0.16f
        c.drawCircle(ring.x, ring.y, ringR, stroke)
        stroke.shader = null
        stroke.color = 0x55000000; stroke.strokeWidth = ringR * 0.03f
        for (k in 0 until 28) {
            val a = k * 2 * PI / 28
            val ri = ringR * 0.93f; val ro = ringR * 1.07f
            c.drawLine(ring.x + (cos(a) * ri).toFloat(), ring.y + (sin(a) * ri).toFloat(),
                ring.x + (cos(a + 0.12) * ro).toFloat(), ring.y + (sin(a + 0.12) * ro).toFloat(), stroke)
        }
        // chain from the ring to the egg's top loop
        val x0 = ring.x - ringR * 0.25f; val y0 = ring.y + ringR * 1.02f
        val x1 = eggBounds.centerX(); val y1 = eggBounds.top + eggBounds.height() * 0.01f
        val n = max(3, ((y1 - y0) / (ringR * 0.32f)).toInt())
        for (i in 0 until n) {
            val f = (i + 0.5f) / n
            val x = x0 + (x1 - x0) * f + sin(f * PI).toFloat() * ringR * 0.25f
            val y = y0 + (y1 - y0) * f
            val lw = ringR * 0.17f; val lh = (y1 - y0) / n * 0.75f
            stroke.color = Color.WHITE
            stroke.shader = LinearGradient(x - lw, y, x + lw, y, 0xFFE8E8E8.toInt(), 0xFF707278.toInt(), Shader.TileMode.CLAMP)
            stroke.strokeWidth = ringR * 0.06f
            if (i % 2 == 0) c.drawOval(x - lw * 0.55f, y - lh * 0.75f, x + lw * 0.55f, y + lh * 0.75f, stroke)
            else c.drawOval(x - lw * 0.18f, y - lh * 0.75f, x + lw * 0.18f, y + lh * 0.75f, stroke)
        }
        stroke.shader = null
    }

    private fun drawShell(c: Canvas) {
        val b = eggBounds
        // translucent teal plastic
        paint.color = Color.WHITE
        paint.shader = RadialGradient(b.centerX() - b.width() * 0.15f, b.top + b.height() * 0.35f, b.height() * 0.8f,
            intArrayOf(0xFF3FD0D0.toInt(), 0xFF14A6AE.toInt(), 0xFF0A6F7A.toInt()), floatArrayOf(0f, 0.6f, 1f), Shader.TileMode.CLAMP)
        c.drawPath(egg, paint)
        paint.shader = null
        // rim
        stroke.color = 0xFF0B5E68.toInt(); stroke.strokeWidth = b.width() * 0.012f
        c.drawPath(egg, stroke)
        // inner moulding (the frame around the screen, seen through the plastic)
        val f = RectF(lcd.left - b.width() * 0.13f, lcd.top - b.height() * 0.085f, lcd.right + b.width() * 0.13f, lcd.bottom + b.height() * 0.07f)
        paint.color = 0x3306404A
        c.drawRoundRect(f, b.width() * 0.04f, b.width() * 0.04f, paint)
        stroke.color = 0x6629E0E0; stroke.strokeWidth = b.width() * 0.006f
        c.drawRoundRect(f, b.width() * 0.04f, b.width() * 0.04f, stroke)
        stroke.color = 0x44063A44
        for (k in 1..3) {
            val x = f.left + f.width() * k / 4
            c.drawLine(x, f.top, x, f.top + f.height() * 0.06f, stroke)
            c.drawLine(x, f.bottom - f.height() * 0.06f, x, f.bottom, stroke)
        }
        // screw posts
        paint.color = 0x55053C46
        for ((px, py) in listOf(0.2f to 0.24f, 0.8f to 0.24f, 0.13f to 0.72f, 0.87f to 0.72f)) {
            c.drawCircle(b.left + b.width() * px, b.top + b.height() * py, b.width() * 0.028f, paint)
        }
        // battery-door text moulding
        paint.color = 0x33063A44
        c.drawRoundRect(b.centerX() - b.width() * 0.17f, b.top + b.height() * 0.12f, b.centerX() + b.width() * 0.17f,
            b.top + b.height() * 0.145f, 6f, 6f, paint)
        // top loop
        stroke.color = 0xFF11909A.toInt(); stroke.strokeWidth = b.width() * 0.025f
        c.drawArc(b.centerX() - b.width() * 0.05f, b.top - b.width() * 0.03f, b.centerX() + b.width() * 0.05f, b.top + b.width() * 0.05f, 180f, 180f, false, stroke)
        // button base (green moulding)
        paint.color = 0xAA2E8F3C.toInt()
        c.drawRoundRect(b.left + b.width() * 0.2f, btn[0].y - btnR * 1.6f, b.right - b.width() * 0.2f, btn[1].y + btnR * 1.5f, btnR, btnR, paint)
        // glossy highlights
        paint.color = Color.WHITE
        paint.shader = LinearGradient(b.left, b.top, b.left + b.width() * 0.4f, b.top + b.height() * 0.5f,
            0x66FFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP)
        c.save(); c.clipPath(egg)
        c.drawOval(b.left + b.width() * 0.04f, b.top + b.height() * 0.05f, b.left + b.width() * 0.45f, b.top + b.height() * 0.6f, paint)
        c.restore()
        paint.shader = null
    }

    private fun drawLcd(c: Canvas) {
        val s = lcd.width()
        // bezel
        paint.color = 0xFF9AA39C.toInt()
        val bz = s * 0.035f
        c.drawRect(lcd.left - bz, lcd.top - bz, lcd.right + bz, lcd.bottom + bz, paint)
        backdrop?.let { c.drawBitmap(it, null, lcd, null) }

        // dot matrix in the middle band
        val mw = s * 0.92f
        val pitch = mw / 32f
        val mx = lcd.centerX() - mw / 2
        val my = lcd.centerY() - pitch * 8
        val gap = pitch * 0.12f
        for (y in 0 until 16) for (x in 0 until 32) {
            paint.color = if (dots[y * 32 + x]) 0xF0141414.toInt() else 0x0C000000
            c.drawRect(mx + x * pitch + gap / 2, my + y * pitch + gap / 2, mx + (x + 1) * pitch - gap / 2, my + (y + 1) * pitch - gap / 2, paint)
        }
        // icon segments: 4 in the top band, 4 in the bottom band
        val band = s * 0.24f
        val isz = band * 0.62f
        for (i in 0 until 8) {
            val cx = lcd.left + s * (0.125f + (i % 4) * 0.25f)
            val cy = if (i < 4) lcd.top + band * 0.5f else lcd.bottom - band * 0.5f
            drawIcon(c, i, cx, cy, isz, if (icons[i]) 0xF0141414.toInt() else 0x10000000)
        }
        // glass sheen
        paint.color = Color.WHITE
        paint.shader = LinearGradient(lcd.left, lcd.top, lcd.right, lcd.bottom,
            intArrayOf(0x22FFFFFF, 0x00FFFFFF, 0x10FFFFFF), null, Shader.TileMode.CLAMP)
        c.drawRect(lcd, paint); paint.shader = null

        status?.let { msg ->
            paint.color = 0xCC0B2A30.toInt()
            c.drawRect(lcd.left, lcd.centerY() - s * 0.12f, lcd.right, lcd.centerY() + s * 0.12f, paint)
            paint.color = Color.WHITE; paint.textAlign = Paint.Align.CENTER; paint.textSize = s * 0.075f
            val lines = msg.split('\n')
            lines.forEachIndexed { k, line -> c.drawText(line, lcd.centerX(), lcd.centerY() + paint.textSize * (k - (lines.size - 1) / 2f + 0.35f), paint) }
        }
    }

    /** Simple pictograms for the eight LCD icons (meal, light, play, medicine / bathroom, meter, discipline, attention). */
    private fun drawIcon(c: Canvas, i: Int, x: Float, y: Float, sz: Float, col: Int) {
        val h = sz / 2
        paint.color = col; stroke.color = col; stroke.strokeWidth = sz * 0.1f; stroke.shader = null
        when (i) {
            0 -> { // fork and knife
                c.drawLine(x - h * 0.4f, y - h, x - h * 0.4f, y + h, stroke)
                for (k in -1..1) c.drawLine(x - h * 0.4f + k * h * 0.22f, y - h, x - h * 0.4f + k * h * 0.22f, y - h * 0.35f, stroke)
                c.drawOval(x + h * 0.2f, y - h, x + h * 0.6f, y + h * 0.1f, paint)
                c.drawLine(x + h * 0.4f, y, x + h * 0.4f, y + h, stroke)
            }
            1 -> { // bulb
                c.drawCircle(x, y - h * 0.25f, h * 0.55f, paint)
                c.drawRect(x - h * 0.28f, y + h * 0.2f, x + h * 0.28f, y + h * 0.75f, paint)
            }
            2 -> { // ball and bat
                c.drawCircle(x - h * 0.45f, y + h * 0.45f, h * 0.35f, paint)
                c.drawLine(x - h * 0.1f, y + h * 0.3f, x + h * 0.8f, y - h * 0.8f, stroke)
            }
            3 -> { // syringe
                c.save(); c.rotate(-45f, x, y)
                c.drawRect(x - h * 0.6f, y - h * 0.22f, x + h * 0.4f, y + h * 0.22f, paint)
                c.drawLine(x + h * 0.4f, y, x + h, y, stroke)
                c.drawLine(x - h * 0.9f, y - h * 0.3f, x - h * 0.9f, y + h * 0.3f, stroke)
                c.restore()
            }
            4 -> { // duck
                c.drawOval(x - h * 0.8f, y - h * 0.05f, x + h * 0.6f, y + h * 0.7f, paint)
                c.drawCircle(x + h * 0.3f, y - h * 0.35f, h * 0.35f, paint)
                c.drawRect(x + h * 0.6f, y - h * 0.42f, x + h * 0.95f, y - h * 0.25f, paint)
            }
            5 -> { // meter / scale
                c.drawArc(x - h, y - h * 0.6f, x + h, y + h * 1.2f, 200f, 140f, false, stroke)
                c.drawLine(x, y + h * 0.4f, x + h * 0.4f, y - h * 0.4f, stroke)
                c.drawRect(x - h * 0.7f, y + h * 0.55f, x + h * 0.7f, y + h * 0.8f, paint)
            }
            6 -> { // discipline: a face with a raised hand
                c.drawCircle(x - h * 0.2f, y + h * 0.1f, h * 0.55f, stroke)
                c.drawRect(x + h * 0.45f, y - h * 0.9f, x + h * 0.85f, y - h * 0.1f, paint)
            }
            7 -> { // attention: speech burst with "!"
                c.drawCircle(x, y, h * 0.85f, stroke)
                c.drawLine(x, y - h * 0.5f, x, y + h * 0.15f, stroke)
                c.drawCircle(x, y + h * 0.45f, sz * 0.06f, paint)
            }
        }
    }

    private fun drawButtons(c: Canvas) {
        val held = touchMask or externalMask
        for (i in 0 until 3) {
            val p = btn[i]; val down = held and (1 shl i) != 0
            paint.color = 0x55000000
            c.drawCircle(p.x, p.y + btnR * 0.12f, btnR * 1.08f, paint)
            paint.color = Color.WHITE
            paint.shader = RadialGradient(p.x - btnR * 0.3f, p.y - btnR * 0.3f, btnR * 1.3f,
                if (down) intArrayOf(0xFFD8C21C.toInt(), 0xFFB09A0A.toInt()) else intArrayOf(0xFFFFF27A.toInt(), 0xFFF0D21E.toInt()),
                null, Shader.TileMode.CLAMP)
            c.drawCircle(p.x, p.y + (if (down) btnR * 0.08f else 0f), btnR * (if (down) 0.95f else 1f), paint)
            paint.shader = null
        }
    }

    // ---- touch ----
    private fun hitButton(x: Float, y: Float): Int {
        for (i in 0 until 3) if (hypot(x - btn[i].x, y - btn[i].y) < btnR * 1.9f) return i
        return -1
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        val idx = e.actionIndex
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val x = e.getX(idx); val y = e.getY(idx)
                if (hypot(x - ring.x, y - ring.y) < ringR * 1.3f || y < eggBounds.top) {
                    if (e.actionMasked == MotionEvent.ACTION_DOWN) { onMenu?.invoke(); return true }
                }
                val b = hitButton(x, y)
                if (b >= 0) {
                    pointerBtn[e.getPointerId(idx)] = b
                    performHapticFeedback(android.view.HapticFeedbackConstants.VIRTUAL_KEY)
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> pointerBtn.remove(e.getPointerId(idx))
            MotionEvent.ACTION_CANCEL -> pointerBtn.clear()
        }
        val m = pointerBtn.values.fold(0) { a, b -> a or (1 shl b) }
        if (m != touchMask) { touchMask = m; onInput?.invoke(m); invalidate() }
        return true
    }
}
