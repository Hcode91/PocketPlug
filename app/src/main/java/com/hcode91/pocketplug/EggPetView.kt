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
 *
 * With [angel] set it is the 1997 angel model instead: pale lavender shell with
 * a white window-pane grid and holographic bubbles, wings beside a star-shaped
 * bezel, icons printed on the LCD, pale blue buttons and a ball chain. Touching
 * a wing works the shake sensor (as does shaking the phone).
 *
 * Styles: "gen1" (teal 1996 egg), "reef" (deep-sea reef shell for the same
 * game), "angel" (1997 angel) and "umino" (clear 1998-style ocean egg with
 * blue waves, used by the built-in ocean pet; tapping the shell works its
 * touch sensor, e.g. to scare off a predator).
 */
class EggPetView(ctx: Context, private val style: String = "gen1") : View(ctx) {
    private val angel = style == "angel"
    private val reef = style == "reef"
    private val umino = style == "umino"
    private val centred = angel || umino

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
    private val wingL = RectF(); private val wingR = RectF()
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
        val ew = if (centred) min(w * 0.80f, h * 0.84f / 1.15f) else min(w * 0.84f, h * 0.64f / 1.2f)
        val eh = if (centred) ew * 1.15f else ew * 1.2f
        val cx = if (angel) w / 2f - ew * 0.04f else if (umino) w / 2f + ew * 0.04f else w / 2f
        val top = if (centred) (h - eh) / 2f + h * 0.02f else h - eh - h * 0.03f
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
        if (centred) {
            val sa = ew * 0.48f
            lcd.set(cx - sa / 2, t + eh * 0.33f, cx + sa / 2, t + eh * 0.33f + sa)
            btnR = ew * 0.065f
            val bya = t + eh * 0.875f
            btn[0].set(cx - ew * 0.2f, bya - eh * 0.01f)
            btn[1].set(cx, bya + eh * 0.025f)
            btn[2].set(cx + ew * 0.2f, bya - eh * 0.01f)
            val wy = lcd.centerY() - sa * 0.05f
            wingL.set(l + ew * 0.02f, wy - sa * 0.42f, lcd.left - ew * 0.075f, wy + sa * 0.22f)
            wingR.set(lcd.right + ew * 0.075f, wy - sa * 0.42f, r - ew * 0.02f, wy + sa * 0.22f)
            ring.set(-1000f, -1000f); ringR = 0f
        }
        if (reef) { ring.set(-1000f, -1000f); ringR = 0f }
        val bs = lcd.width().toInt().coerceAtLeast(16)
        backdrop = when {
            angel -> makeAngelBackdrop(bs)
            reef -> makeSwirlBackdrop(bs)
            umino -> makeWaveBackdrop(bs)
            else -> makeBackdrop(bs)
        }
    }

    /** Yellow and sea-blue marbled swirl (reef shell). */
    private fun makeSwirlBackdrop(size: Int): Bitmap {
        val bm = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
        val c = Canvas(bm)
        val p = Paint(Paint.ANTI_ALIAS_FLAG)
        c.drawColor(0xFFE3D77A.toInt())
        p.style = Paint.Style.STROKE
        p.strokeWidth = size * 0.07f
        p.color = 0xFF7FB2BC.toInt()
        for (k in 0 until 7) {
            val path = Path()
            val y0 = size * (k / 6f)
            path.moveTo(-size * 0.1f, y0)
            var x = -size * 0.1f
            while (x < size * 1.1f) {
                val y = y0 + (sin(x / size * 9.0 + k * 1.3) * size * 0.07).toFloat() + (sin(x / size * 3.0 + k) * size * 0.05).toFloat()
                path.lineTo(x, y); x += size / 40f
            }
            c.drawPath(path, p)
        }
        return bm
    }

    /** Pale aqua with thin white and blue wavy lines (clear ocean egg). */
    private fun makeWaveBackdrop(size: Int): Bitmap {
        val bm = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
        val c = Canvas(bm)
        val p = Paint(Paint.ANTI_ALIAS_FLAG)
        c.drawColor(0xFFBFE2D6.toInt())
        p.style = Paint.Style.STROKE
        for (k in 0 until 9) {
            p.color = if (k % 3 == 0) 0xAA4C7FD0.toInt() else 0xCCF8FFFC.toInt()
            p.strokeWidth = size * (if (k % 3 == 0) 0.006f else 0.012f)
            val path = Path()
            val y0 = size * (0.05f + k * 0.11f)
            var x = 0f
            path.moveTo(0f, y0)
            while (x <= size) {
                path.lineTo(x, y0 + (sin(x / size * 7.0 + k * 2.1) * size * 0.05).toFloat()); x += size / 50f
            }
            c.drawPath(path, p)
        }
        return bm
    }

    private fun makeAngelBackdrop(size: Int): Bitmap {
        val bm = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
        val c = Canvas(bm)
        val p = Paint(Paint.ANTI_ALIAS_FLAG)
        p.shader = LinearGradient(0f, 0f, size.toFloat(), size.toFloat(), 0xFFC7D5DA.toInt(), 0xFFAFC0C8.toInt(), Shader.TileMode.CLAMP)
        c.drawRect(0f, 0f, size.toFloat(), size.toFloat(), p); p.shader = null
        // big printed star outline behind the dots
        val star = Path()
        val cx = size / 2f; val cy = size * 0.54f; val ro = size * 0.40f; val ri = ro * 0.42f
        for (k in 0 until 10) {
            val a = -PI / 2 + k * PI / 5
            val rr = if (k % 2 == 0) ro else ri
            val x = cx + (cos(a) * rr).toFloat(); val y = cy + (sin(a) * rr).toFloat()
            if (k == 0) star.moveTo(x, y) else star.lineTo(x, y)
        }
        star.close()
        p.style = Paint.Style.STROKE; p.strokeJoin = Paint.Join.ROUND
        p.color = 0xFFF4F8FA.toInt(); p.strokeWidth = size * 0.035f
        c.drawPath(star, p)
        p.color = 0x664A86C8; p.strokeWidth = size * 0.006f
        c.drawLine(cx - ro * 0.5f, cy - ro * 0.55f, cx + ro * 0.65f, cy + ro * 0.35f, p)
        c.drawLine(cx - ro * 0.3f, cy + ro * 0.6f, cx + ro * 0.5f, cy - ro * 0.6f, p)
        return bm
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
        when {
            angel -> { drawAngelShell(c); drawBallChain(c, 1) }
            umino -> { drawUminoShell(c); drawBallChain(c, -1) }
            reef -> { drawBallChain(c, 0); drawReefShell(c) }
            else -> { drawKeyChain(c); drawShell(c) }
        }
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

    /** Ball chain: side 1 = over the right shoulder, -1 = left, 0 = a loop straight up. */
    private fun drawBallChain(c: Canvas, side: Int) {
        val b = eggBounds
        val pts = ArrayList<PointF>()
        val x0 = b.centerX() - b.width() * 0.02f * side; val y0 = b.top + b.height() * 0.015f
        val br = b.width() * 0.028f
        val path = Path().apply {
            moveTo(x0, y0)
            if (side == 0) {
                // a long loop up to a clasp above the egg
                val top = max(br * 2, b.top * 0.12f)
                cubicTo(x0 - b.width() * 0.16f, y0 - (y0 - top) * 0.3f, x0 - b.width() * 0.14f, top, x0, top)
                cubicTo(x0 + b.width() * 0.14f, top, x0 + b.width() * 0.16f, y0 - (y0 - top) * 0.3f, x0 + br * 0.5f, y0)
            } else {
                val ex = if (side > 0) b.right else b.left
                cubicTo(ex - b.width() * 0.05f * side, b.top - b.height() * 0.06f, ex + b.width() * 0.10f * side, b.top + b.height() * 0.25f,
                    ex + b.width() * 0.035f * side, b.top + b.height() * 0.78f)
            }
        }
        val pm = PathMeasure(path, false)
        val len = pm.length
        val pos = FloatArray(2)
        var d = br * 0.6f
        while (d < len) { pm.getPosTan(d, pos, null); pts += PointF(pos[0], pos[1]); d += br * 2.15f }
        for ((k, p) in pts.withIndex()) {
            if ((side != 0 && k == pts.size - 3) || (side == 0 && k == pts.size / 2)) {
                // the clasp
                paint.color = Color.WHITE
                paint.shader = LinearGradient(p.x - br, p.y, p.x + br, p.y, 0xFFF0F0F0.toInt(), 0xFF6E7076.toInt(), Shader.TileMode.CLAMP)
                c.drawRoundRect(p.x - br * 0.6f, p.y - br * 3f, p.x + br * 0.6f, p.y + br * 3f, br * 0.5f, br * 0.5f, paint)
                paint.shader = null
                continue
            }
            paint.color = Color.WHITE
            paint.shader = RadialGradient(p.x - br * 0.35f, p.y - br * 0.35f, br * 1.2f,
                intArrayOf(0xFFFFFFFF.toInt(), 0xFFB5B8BE.toInt(), 0xFF5D6066.toInt()), floatArrayOf(0f, 0.5f, 1f), Shader.TileMode.CLAMP)
            c.drawCircle(p.x, p.y, br, paint)
            paint.shader = null
        }
    }

    private fun drawAngelShell(c: Canvas) {
        val b = eggBounds
        paint.color = Color.WHITE
        paint.shader = RadialGradient(b.centerX() - b.width() * 0.1f, b.top + b.height() * 0.4f, b.height() * 0.75f,
            intArrayOf(0xFFCBD7F7.toInt(), 0xFFAFC1F1.toInt(), 0xFF8EA4E3.toInt()), floatArrayOf(0f, 0.65f, 1f), Shader.TileMode.CLAMP)
        c.drawPath(egg, paint)
        paint.shader = null
        c.save(); c.clipPath(egg)
        // holographic bubbles
        val rnd = java.util.Random(11)
        val holo = intArrayOf(0x7066E0E8, 0x60B08CF0, 0x58F0A8E0, 0x6090F0D0, 0x50FFFFFF)
        repeat(26) {
            val x = b.left + rnd.nextFloat() * b.width(); val y = b.top + rnd.nextFloat() * b.height()
            val r = b.width() * (0.03f + rnd.nextFloat() * 0.07f)
            paint.color = Color.WHITE
            paint.shader = RadialGradient(x, y, r, intArrayOf(holo[it % holo.size], 0x00FFFFFF), null, Shader.TileMode.CLAMP)
            c.drawCircle(x, y, r, paint)
        }
        paint.shader = null
        // white window-pane grid
        stroke.color = 0xCCF6F8FF.toInt(); stroke.strokeWidth = b.width() * 0.018f
        for (fx in floatArrayOf(0.14f, 0.36f, 0.64f, 0.86f)) c.drawLine(b.left + b.width() * fx, b.top, b.left + b.width() * fx, b.bottom, stroke)
        for (fy in floatArrayOf(0.13f, 0.33f, 0.55f, 0.77f)) c.drawLine(b.left, b.top + b.height() * fy, b.right, b.top + b.height() * fy, stroke)
        c.restore()
        // rim
        stroke.color = 0xFF7F95D8.toInt(); stroke.strokeWidth = b.width() * 0.012f
        c.drawPath(egg, stroke)
        // top loop
        stroke.color = 0xFFA9B9EE.toInt(); stroke.strokeWidth = b.width() * 0.03f
        c.drawArc(b.centerX() - b.width() * 0.06f, b.top - b.width() * 0.035f, b.centerX() + b.width() * 0.06f, b.top + b.width() * 0.06f, 180f, 180f, false, stroke)
        // star-shaped bezel around the screen
        drawStarBezel(c, 12, 0.76f, 0.62f, 0xFFA8BFF4.toInt(), 0xFF6E8FE0.toInt())
        // wings either side of the bezel
        drawWing(c, wingL, false); drawWing(c, wingR, true)
        // glossy highlight
        paint.color = Color.WHITE
        paint.shader = LinearGradient(b.left, b.top, b.left + b.width() * 0.5f, b.top + b.height() * 0.5f, 0x55FFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP)
        c.save(); c.clipPath(egg)
        c.drawOval(b.left + b.width() * 0.05f, b.top + b.height() * 0.04f, b.left + b.width() * 0.5f, b.top + b.height() * 0.45f, paint)
        c.restore(); paint.shader = null
    }

    private fun drawStarBezel(c: Canvas, points: Int, outer: Float, inner: Float, c0: Int, c1: Int, rot: Double = 0.0) {
        val b = eggBounds
        val star = Path()
        val cx = lcd.centerX(); val cy = lcd.centerY()
        val ro = lcd.width() * outer; val ri = lcd.width() * inner
        for (k in 0 until points * 2) {
            val a = -PI / 2 + rot + k * PI / points
            val rr = if (k % 2 == 0) ro else ri
            val x = cx + (cos(a) * rr).toFloat(); val y = cy + (sin(a) * rr * 0.98f).toFloat()
            if (k == 0) star.moveTo(x, y) else star.lineTo(x, y)
        }
        star.close()
        paint.color = Color.WHITE
        paint.shader = LinearGradient(cx - ro, cy - ro, cx + ro, cy + ro, c0, c1, Shader.TileMode.CLAMP)
        c.drawPath(star, paint); paint.shader = null
        stroke.color = 0x88FFFFFF.toInt(); stroke.strokeWidth = b.width() * 0.005f
        c.drawPath(star, stroke)
    }

    /** 2024-style deep-sea shell: teal to green, reef plants, bubbles, pink star bezel. */
    private fun drawReefShell(c: Canvas) {
        val b = eggBounds
        paint.color = Color.WHITE
        paint.shader = LinearGradient(0f, b.top, 0f, b.bottom,
            intArrayOf(0xFF2A93B8.toInt(), 0xFF1D6F96.toInt(), 0xFF1F7A6A.toInt(), 0xFF2B8A5A.toInt()), floatArrayOf(0f, 0.45f, 0.7f, 1f), Shader.TileMode.CLAMP)
        c.drawPath(egg, paint); paint.shader = null
        c.save(); c.clipPath(egg)
        // bubbles
        stroke.color = 0x99BFEFFF.toInt(); stroke.strokeWidth = b.width() * 0.006f
        val rnd = java.util.Random(5)
        repeat(14) {
            val x = b.left + b.width() * (0.12f + rnd.nextFloat() * 0.76f)
            val y = b.top + b.height() * (0.06f + rnd.nextFloat() * 0.22f)
            c.drawCircle(x, y, b.width() * (0.008f + rnd.nextFloat() * 0.02f), stroke)
        }
        // reef: anemones, sea plants and corals around the lower half
        val cols = intArrayOf(0xFFF08A5D.toInt(), 0xFFF4C542.toInt(), 0xFFB57AD8.toInt(), 0xFFF2A3B8.toInt(), 0xFF6FD3C6.toInt())
        for (k in 0 until 16) {
            val left = k % 2 == 0
            val fx = if (left) 0.04f + rnd.nextFloat() * 0.2f else 0.76f + rnd.nextFloat() * 0.2f
            val fy = 0.42f + rnd.nextFloat() * 0.5f
            val x = b.left + b.width() * fx; val y = b.top + b.height() * fy
            val r = b.width() * (0.025f + rnd.nextFloat() * 0.03f)
            paint.color = cols[k % cols.size]
            when (k % 3) {
                0 -> for (j in 0 until 7) { val a = j * 2 * PI / 7; c.drawCircle(x + (cos(a) * r).toFloat(), y + (sin(a) * r).toFloat(), r * 0.45f, paint) }
                1 -> { stroke.color = paint.color; stroke.strokeWidth = r * 0.35f
                       for (j in -2..2) c.drawLine(x, y + r * 2, x + j * r * 0.5f, y - r * 1.2f, stroke) }
                else -> c.drawOval(x - r * 0.6f, y - r * 1.5f, x + r * 0.6f, y + r * 1.5f, paint)
            }
        }
        // sea floor
        paint.color = 0x553A9A6A
        c.drawRect(b.left, b.top + b.height() * 0.88f, b.right, b.bottom, paint)
        c.restore()
        stroke.color = 0xFF125A73.toInt(); stroke.strokeWidth = b.width() * 0.012f
        c.drawPath(egg, stroke)
        // top loop
        stroke.color = 0xFF2B9FC6.toInt(); stroke.strokeWidth = b.width() * 0.03f
        c.drawArc(b.centerX() - b.width() * 0.06f, b.top - b.width() * 0.035f, b.centerX() + b.width() * 0.06f, b.top + b.width() * 0.06f, 180f, 180f, false, stroke)
        drawStarBezel(c, 8, 0.70f, 0.60f, 0xFFF7B9BC.toInt(), 0xFFE58F98.toInt(), PI / 8)
        paint.color = Color.WHITE
        paint.shader = LinearGradient(b.left, b.top, b.left + b.width() * 0.5f, b.top + b.height() * 0.5f, 0x44FFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP)
        c.save(); c.clipPath(egg)
        c.drawOval(b.left + b.width() * 0.05f, b.top + b.height() * 0.04f, b.left + b.width() * 0.5f, b.top + b.height() * 0.45f, paint)
        c.restore(); paint.shader = null
    }

    /** Clear grey 1998-style ocean egg with blue wave decals and a blue star bezel. */
    private fun drawUminoShell(c: Canvas) {
        val b = eggBounds
        paint.color = Color.WHITE
        paint.shader = RadialGradient(b.centerX() - b.width() * 0.1f, b.top + b.height() * 0.4f, b.height() * 0.75f,
            intArrayOf(0xFFD8DADC.toInt(), 0xFFB9BCBF.toInt(), 0xFF8E9296.toInt()), floatArrayOf(0f, 0.7f, 1f), Shader.TileMode.CLAMP)
        c.drawPath(egg, paint); paint.shader = null
        c.save(); c.clipPath(egg)
        // the circuit board faintly visible through the clear plastic
        paint.color = 0x2A2E6B3A
        c.drawRect(b.left + b.width() * 0.08f, b.top + b.height() * 0.22f, b.left + b.width() * 0.2f, b.top + b.height() * 0.72f, paint)
        c.drawRect(b.right - b.width() * 0.22f, b.top + b.height() * 0.3f, b.right - b.width() * 0.1f, b.top + b.height() * 0.62f, paint)
        stroke.color = 0x3A2B4E36; stroke.strokeWidth = b.width() * 0.004f
        for (k in 0 until 6) { val y = b.top + b.height() * (0.25f + k * 0.08f); c.drawLine(b.left + b.width() * 0.1f, y, b.left + b.width() * 0.19f, y, stroke) }
        // blue wave decals with white edges
        fun wave(x: Float, y: Float, w: Float, h: Float, flip: Boolean) {
            val p = Path()
            val dir = if (flip) -1 else 1
            p.moveTo(x, y)
            p.cubicTo(x + dir * w * 0.3f, y - h, x + dir * w * 0.6f, y + h, x + dir * w, y - h * 0.2f)
            stroke.color = 0xFFFFFFFF.toInt(); stroke.strokeWidth = b.width() * 0.05f; c.drawPath(p, stroke)
            stroke.color = 0xFF2F63CF.toInt(); stroke.strokeWidth = b.width() * 0.032f; c.drawPath(p, stroke)
        }
        wave(b.left + b.width() * 0.1f, b.top + b.height() * 0.2f, b.width() * 0.3f, b.height() * 0.05f, false)
        wave(b.right - b.width() * 0.08f, b.top + b.height() * 0.24f, b.width() * 0.3f, b.height() * 0.05f, true)
        wave(b.left + b.width() * 0.04f, b.top + b.height() * 0.78f, b.width() * 0.22f, b.height() * 0.05f, false)
        wave(b.right - b.width() * 0.04f, b.top + b.height() * 0.72f, b.width() * 0.2f, b.height() * 0.05f, true)
        c.restore()
        stroke.color = 0xFF7C8085.toInt(); stroke.strokeWidth = b.width() * 0.012f
        c.drawPath(egg, stroke)
        stroke.color = 0xFFA9ADB1.toInt(); stroke.strokeWidth = b.width() * 0.03f
        c.drawArc(b.centerX() - b.width() * 0.06f, b.top - b.width() * 0.035f, b.centerX() + b.width() * 0.06f, b.top + b.width() * 0.06f, 180f, 180f, false, stroke)
        drawStarBezel(c, 8, 0.74f, 0.60f, 0xFF4E84E6.toInt(), 0xFF2550B8.toInt(), PI / 8)
        paint.color = Color.WHITE
        paint.shader = LinearGradient(b.left, b.top, b.left + b.width() * 0.5f, b.top + b.height() * 0.5f, 0x66FFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP)
        c.save(); c.clipPath(egg)
        c.drawOval(b.left + b.width() * 0.05f, b.top + b.height() * 0.04f, b.left + b.width() * 0.5f, b.top + b.height() * 0.45f, paint)
        c.restore(); paint.shader = null
    }

    private fun drawWing(c: Canvas, r: RectF, right: Boolean) {
        c.save()
        if (right) c.scale(-1f, 1f, r.centerX(), r.centerY())
        val w = r.width(); val h = r.height()
        val glow = (externalMask or touchMask) and EggPetBits.SHAKE != 0
        // three layered feathers sweeping up and out (drawn as the left wing)
        for (k in 0 until 3) {
            val f = Path()
            val y0 = r.top + h * (0.18f + k * 0.2f)
            f.moveTo(r.right, r.top + h * 0.55f + k * h * 0.08f)
            f.cubicTo(r.left + w * 0.55f, y0 + h * 0.2f, r.left + w * (0.05f + k * 0.08f), y0 + h * 0.05f, r.left + w * (0.02f + k * 0.1f), y0 - h * 0.08f)
            f.cubicTo(r.left + w * (0.3f + k * 0.05f), y0 - h * 0.12f, r.left + w * 0.8f, y0 + h * 0.02f, r.right, r.top + h * 0.38f + k * h * 0.07f)
            f.close()
            paint.color = Color.WHITE
            paint.shader = LinearGradient(r.left, r.top, r.right, r.bottom,
                if (glow) intArrayOf(0xFFFFFFFF.toInt(), 0xFFFFF2B0.toInt()) else intArrayOf(0xFFF2F8FF.toInt(), 0xFF9DC2F0.toInt()),
                null, Shader.TileMode.CLAMP)
            c.drawPath(f, paint)
            paint.shader = null
            stroke.color = 0x995C86D8.toInt(); stroke.strokeWidth = w * 0.02f
            c.drawPath(f, stroke)
        }
        c.restore()
    }

    private fun drawLcd(c: Canvas) {
        val s = lcd.width()
        // bezel
        paint.color = when { angel -> 0xFF7E98DE.toInt(); umino -> 0xFF2D5CC4.toInt(); reef -> 0xFFE58F98.toInt(); else -> 0xFF9AA39C.toInt() }
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
            if (angel) drawAngelIcon(c, i, cx, cy, isz, if (icons[i]) 0xF0141414.toInt() else 0x40303A40)
            else if (umino) drawOceanIcon(c, i, cx, cy, isz, if (icons[i]) 0xF0141414.toInt() else 0x22000000)
            else drawIcon(c, i, cx, cy, isz, if (icons[i]) 0xF0141414.toInt() else 0x10000000)
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

    /** The angel model's printed icons (heart face, spoon, sparkle, wings / bell, bow, torch, winged face). */
    private fun drawAngelIcon(c: Canvas, i: Int, x: Float, y: Float, sz: Float, col: Int) {
        val h = sz / 2
        paint.color = col; stroke.color = col; stroke.strokeWidth = sz * 0.08f; stroke.shader = null
        when (i) {
            0 -> { // heart with a face
                val p = Path()
                p.moveTo(x, y + h * 0.85f)
                p.cubicTo(x - h * 1.3f, y - h * 0.05f, x - h * 0.7f, y - h * 1.05f, x, y - h * 0.4f)
                p.cubicTo(x + h * 0.7f, y - h * 1.05f, x + h * 1.3f, y - h * 0.05f, x, y + h * 0.85f)
                c.drawPath(p, stroke)
                c.drawCircle(x - h * 0.25f, y - h * 0.1f, sz * 0.05f, paint); c.drawCircle(x + h * 0.25f, y - h * 0.1f, sz * 0.05f, paint)
                c.drawArc(x - h * 0.2f, y + h * 0.05f, x + h * 0.2f, y + h * 0.35f, 0f, 180f, false, stroke)
            }
            1 -> { // spoon
                c.drawOval(x - h * 0.75f, y - h * 0.25f, x - h * 0.05f, y + h * 0.35f, stroke)
                c.drawLine(x - h * 0.1f, y, x + h * 0.85f, y - h * 0.65f, stroke)
            }
            2 -> { // sparkle star
                val p = Path()
                for (k in 0 until 10) {
                    val a = -PI / 2 + k * PI / 5; val rr = if (k % 2 == 0) h * 0.7f else h * 0.3f
                    val px = x + (cos(a) * rr).toFloat(); val py = y + (sin(a) * rr).toFloat()
                    if (k == 0) p.moveTo(px, py) else p.lineTo(px, py)
                }
                p.close(); c.drawPath(p, stroke)
                for (k in 0 until 8) { val a = k * PI / 4; c.drawLine(x + (cos(a) * h * 0.85f).toFloat(), y + (sin(a) * h * 0.85f).toFloat(), x + (cos(a) * h).toFloat(), y + (sin(a) * h).toFloat(), stroke) }
            }
            3 -> { // pair of wings
                for (sgn in intArrayOf(-1, 1)) {
                    c.drawArc(x + sgn * h * 0.05f - (if (sgn < 0) h * 0.9f else 0f), y - h * 0.5f, x + sgn * h * 0.05f + (if (sgn > 0) h * 0.9f else 0f), y + h * 0.5f,
                        if (sgn < 0) 90f else -90f, 180f, false, stroke)
                }
            }
            4 -> { // bell
                val p = Path()
                p.moveTo(x - h * 0.6f, y + h * 0.5f)
                p.cubicTo(x - h * 0.45f, y + h * 0.2f, x - h * 0.55f, y - h * 0.75f, x, y - h * 0.75f)
                p.cubicTo(x + h * 0.55f, y - h * 0.75f, x + h * 0.45f, y + h * 0.2f, x + h * 0.6f, y + h * 0.5f)
                p.close(); c.drawPath(p, stroke)
                c.drawCircle(x, y + h * 0.72f, sz * 0.07f, paint)
            }
            5 -> { // bow and arrow
                c.drawArc(x - h * 0.9f, y - h * 0.9f, x + h * 0.5f, y + h * 0.9f, -60f, 120f, false, stroke)
                c.drawLine(x - h * 0.75f, y + h * 0.7f, x + h * 0.85f, y - h * 0.75f, stroke)
                c.drawLine(x + h * 0.85f, y - h * 0.75f, x + h * 0.5f, y - h * 0.7f, stroke)
                c.drawLine(x + h * 0.85f, y - h * 0.75f, x + h * 0.8f, y - h * 0.4f, stroke)
            }
            6 -> { // torch
                c.drawLine(x, y - h * 0.05f, x, y + h * 0.9f, stroke)
                val p = Path(); p.moveTo(x, y - h * 0.85f)
                p.cubicTo(x + h * 0.45f, y - h * 0.4f, x + h * 0.3f, y, x, y); p.cubicTo(x - h * 0.3f, y, x - h * 0.45f, y - h * 0.4f, x, y - h * 0.85f)
                c.drawPath(p, paint)
                for (k in -1..1 step 2) c.drawLine(x + k * h * 0.55f, y - h * 0.75f, x + k * h * 0.8f, y - h * 0.95f, stroke)
            }
            7 -> { // smiling face with little wings
                c.drawCircle(x, y + h * 0.1f, h * 0.45f, stroke)
                c.drawArc(x - h * 0.2f, y, x + h * 0.2f, y + h * 0.3f, 0f, 180f, false, stroke)
                c.drawArc(x - h, y - h * 0.4f, x - h * 0.4f, y + h * 0.3f, 90f, 180f, false, stroke)
                c.drawArc(x + h * 0.4f, y - h * 0.4f, x + h, y + h * 0.3f, -90f, 180f, false, stroke)
            }
        }
    }

    /** The ocean pet's icons: meal, sun/moon, treasure chest, medicine / flush, meter, discipline, attention. */
    private fun drawOceanIcon(c: Canvas, i: Int, x: Float, y: Float, sz: Float, col: Int) {
        val h = sz / 2
        paint.color = col; stroke.color = col; stroke.strokeWidth = sz * 0.09f; stroke.shader = null
        when (i) {
            1 -> { // sun and moon
                c.drawCircle(x - h * 0.35f, y, h * 0.35f, paint)
                for (k in 0 until 8) { val a = k * PI / 4; c.drawLine(x - h * 0.35f + (cos(a) * h * 0.5f).toFloat(), y + (sin(a) * h * 0.5f).toFloat(), x - h * 0.35f + (cos(a) * h * 0.7f).toFloat(), y + (sin(a) * h * 0.7f).toFloat(), stroke) }
                c.drawArc(x + h * 0.1f, y - h * 0.6f, x + h * 0.95f, y + h * 0.6f, -90f, 180f, false, stroke)
            }
            2 -> { // treasure chest
                c.drawRect(x - h * 0.8f, y - h * 0.1f, x + h * 0.8f, y + h * 0.7f, stroke)
                c.drawArc(x - h * 0.8f, y - h * 0.7f, x + h * 0.8f, y + h * 0.5f, 180f, 180f, false, stroke)
                c.drawRect(x - h * 0.15f, y - h * 0.1f, x + h * 0.15f, y + h * 0.3f, paint)
            }
            4 -> { // flush: a wave
                val p = Path(); p.moveTo(x - h, y + h * 0.3f)
                p.cubicTo(x - h * 0.5f, y - h * 0.9f, x + h * 0.1f, y - h * 0.4f, x - h * 0.05f, y + h * 0.05f)
                p.cubicTo(x + h * 0.4f, y - h * 0.2f, x + h * 0.6f, y + h * 0.2f, x + h, y + h * 0.3f)
                c.drawPath(p, stroke)
                c.drawLine(x - h, y + h * 0.7f, x + h, y + h * 0.7f, stroke)
            }
            6 -> { // discipline: a pointing hand / exclamation
                c.drawLine(x - h * 0.3f, y - h * 0.8f, x - h * 0.3f, y + h * 0.3f, stroke)
                c.drawCircle(x - h * 0.3f, y + h * 0.7f, sz * 0.06f, paint)
                c.drawLine(x + h * 0.3f, y - h * 0.8f, x + h * 0.3f, y + h * 0.3f, stroke)
                c.drawCircle(x + h * 0.3f, y + h * 0.7f, sz * 0.06f, paint)
            }
            else -> drawIcon(c, if (i == 5) 5 else i, x, y, sz, col)
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
        if (reef || umino) {
            for (i in 0 until 3) {
                val p = btn[i]; val down = held and (1 shl i) != 0
                paint.color = if (umino) 0x669AA0A6 else 0x55000000
                c.drawCircle(p.x, p.y, btnR * 1.25f, paint)
                paint.color = Color.WHITE
                paint.shader = RadialGradient(p.x - btnR * 0.3f, p.y - btnR * 0.35f, btnR * 1.3f,
                    if (umino) (if (down) intArrayOf(0xFFD8D8D2.toInt(), 0xFFB4B4AE.toInt()) else intArrayOf(0xFFFFFFFA.toInt(), 0xFFE2E2DA.toInt()))
                    else (if (down) intArrayOf(0xFFE68C96.toInt(), 0xFFC8707C.toInt()) else intArrayOf(0xFFFFC3C8.toInt(), 0xFFF0939E.toInt())),
                    null, Shader.TileMode.CLAMP)
                c.drawCircle(p.x, p.y + (if (down) btnR * 0.06f else 0f), btnR * (if (down) 0.94f else 1f), paint)
                paint.shader = null
            }
            return
        }
        if (angel) {
            for (i in 0 until 3) {
                val p = btn[i]; val down = held and (1 shl i) != 0
                paint.color = 0xFF7D93D6.toInt()
                c.drawCircle(p.x, p.y, btnR * 1.22f, paint)
                paint.color = Color.WHITE
                paint.shader = RadialGradient(p.x - btnR * 0.3f, p.y - btnR * 0.35f, btnR * 1.3f,
                    if (down) intArrayOf(0xFFB9C9F0.toInt(), 0xFF8EA6E0.toInt()) else intArrayOf(0xFFF8FBFF.toInt(), 0xFFC3D2F6.toInt()),
                    null, Shader.TileMode.CLAMP)
                c.drawCircle(p.x, p.y + (if (down) btnR * 0.06f else 0f), btnR * (if (down) 0.94f else 1f), paint)
                paint.shader = null
            }
            return
        }
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
                val chain = (angel && x > eggBounds.right - eggBounds.width() * 0.06f) ||
                    (umino && x < eggBounds.left + eggBounds.width() * 0.06f)
                if ((!centred && !reef && hypot(x - ring.x, y - ring.y) < ringR * 1.3f) || y < eggBounds.top || chain) {
                    if (e.actionMasked == MotionEvent.ACTION_DOWN) { onMenu?.invoke(); return true }
                }
                if (angel && (wingL.contains(x, y) || wingR.contains(x, y))) {
                    pointerBtn[e.getPointerId(idx)] = 3
                    performHapticFeedback(android.view.HapticFeedbackConstants.LONG_PRESS)
                }
                val b = hitButton(x, y)
                if (umino && b < 0 && eggBounds.contains(x, y)) {
                    // touching the shell: the ocean pet's touch sensor
                    pointerBtn[e.getPointerId(idx)] = 3
                    performHapticFeedback(android.view.HapticFeedbackConstants.LONG_PRESS)
                }
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
