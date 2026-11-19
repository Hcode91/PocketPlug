package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.os.SystemClock
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.*
import kotlin.random.Random

/**
 * Mini upright arcade cabinet (the 240-in-1 handheld type): a lit marquee on top,
 * the screen in a black bezel, a slanted control panel with a red ball-top
 * joystick, small RESET / START / VOLUME buttons and one big A button, and a
 * lower door with a coin slot. The space artwork is original.
 *
 * Tap the marquee for the menu. Input bits follow the VT369 controller:
 * A 0x01, B 0x02, Select 0x04, Start 0x08, Up 0x10, Down 0x20, Left 0x40, Right 0x80.
 */
class ArcadeCabinetView(ctx: Context, private val a6: Boolean = false) : View(ctx) {
    // a6 = true: the A6Plus type (white and blue cabinet, red ball-top stick,
    // eight red action buttons and small SELECT / START / RESET buttons)

    var onInput: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var onReset: (() -> Unit)? = null
    /** Called with the new level index (0 = loudest .. VOLUME_LEVELS-1 = mute). */
    var onVolume: ((Int) -> Unit)? = null
    var volumeLevel = 0
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    companion object { const val VOLUME_LEVELS = 4 }

    private val frame = Bitmap.createBitmap(256, 240, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(256 * 240)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val label = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD_ITALIC)
    }
    private val nearest = Paint().apply { isFilterBitmap = false }

    // geometry
    private val cab = RectF()          // front face
    private val marquee = RectF()
    private val screen = RectF()
    private val bezel = RectF()
    private val panel = RectF()        // control panel (drawn as a trapezoid)
    private val lower = RectF()
    private val coinDoor = RectF()
    private val stick = PointF(); private var stickR = 0f
    private val btnA = PointF(); private var aR = 0f
    private val btnReset = PointF(); private val btnStart = PointF(); private val btnVol = PointF(); private var smallR = 0f
    private val btnSelect = PointF()
    // A6Plus: 2 rows x 4 buttons; bits, with turbo marked 0x10000 (A) / 0x20000 (B)
    private val a6Buttons = Array(8) { PointF() }
    private val a6Bits = intArrayOf(0x20000, 0x10000, 0x20000, 0x10000, 0x02, 0x01, 0x02, 0x01)
    private val a6Labels = arrayOf("TB", "TA", "TB", "TA", "B", "A", "B", "A")
    private var turboOn = true
    private val turboTick = object : Runnable {
        override fun run() {
            if (pointers.values.none { it and 0x30000 != 0 }) return
            turboOn = !turboOn
            pushMask()
            postDelayed(this, 50)
        }
    }
    private var art: Bitmap? = null

    // touch
    private val pointers = HashMap<Int, Int>()
    private var touchMask = 0
    private var volumeShownAt = Long.MIN_VALUE / 2

    fun updateFrame(px: IntArray) {
        if (px.size < 256 * 240) return
        System.arraycopy(px, 0, framePx, 0, 256 * 240)
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        // cabinet front roughly 0.5 : 1 plus a visible right side panel
        var cw = w * 0.86f
        var ch = cw / 0.5f
        if (ch > h * 0.97f) { ch = h * 0.97f; cw = ch * 0.5f }
        val l = (w - cw * 1.08f) / 2; val t = (h - ch) / 2
        cab.set(l, t, l + cw, t + ch)
        marquee.set(l - cw * 0.02f, t, l + cw * 1.02f, t + cw * 0.26f)
        val sw = cw * 0.74f
        val sh = sw * 240f / 256f
        val sTop = t + cw * 0.36f
        screen.set(l + (cw - sw) / 2, sTop, l + (cw + sw) / 2, sTop + sh)
        bezel.set(screen.left - cw * 0.06f, screen.top - cw * 0.05f, screen.right + cw * 0.06f, screen.bottom + cw * 0.05f)
        panel.set(l - cw * 0.03f, bezel.bottom + ch * 0.012f, l + cw * 1.03f, bezel.bottom + ch * 0.012f + cw * 0.36f)
        lower.set(l, panel.bottom, l + cw, t + ch)
        val dw = cw * 0.2f
        coinDoor.set(cab.centerX() - dw / 2, lower.top + cw * 0.06f, cab.centerX() + dw / 2, lower.top + cw * 0.06f + dw * 1.15f)

        val pcY = panel.top + panel.height() * 0.55f
        stickR = cw * 0.075f
        stick.set(l + cw * 0.2f, pcY)
        aR = cw * 0.085f
        btnA.set(l + cw * 0.72f, pcY + panel.height() * 0.12f)
        smallR = cw * 0.028f
        btnReset.set(l + cw * 0.40f, panel.top + panel.height() * 0.25f)
        btnStart.set(l + cw * 0.56f, panel.top + panel.height() * 0.40f)
        btnVol.set(l + cw * 0.84f, panel.top + panel.height() * 0.25f)
        if (a6) {
            stick.set(l + cw * 0.2f, panel.top + panel.height() * 0.62f)
            aR = cw * 0.052f
            for (i in 0 until 8) {
                val col = i % 4; val row = i / 4
                a6Buttons[i].set(l + cw * (0.47f + col * 0.13f) + (if (row == 1) -cw * 0.02f else 0f),
                    panel.top + panel.height() * (if (row == 0) 0.34f else 0.68f))
            }
            smallR = cw * 0.026f
            val sy = lower.top + cw * 0.07f
            btnSelect.set(cab.centerX() - cw * 0.2f, sy)
            btnStart.set(cab.centerX(), sy)
            btnReset.set(cab.centerX() + cw * 0.2f, sy)
            coinDoor.offset(0f, cw * 0.1f)
        }
        art = makeArt(w, h)
    }

    // ---------------- artwork (all original) ----------------

    private fun makeArt(w: Int, h: Int): Bitmap {
        if (a6) return makeA6Art(w, h)
        val bm = Bitmap.createBitmap(max(1, w), max(1, h), Bitmap.Config.ARGB_8888)
        val c = Canvas(bm)
        val cw = cab.width()
        val rnd = Random(240)
        val p = Paint(Paint.ANTI_ALIAS_FLAG)

        // right side panel (the cabinet is seen a little from the right)
        val side = Path().apply {
            moveTo(cab.right, cab.top + cw * 0.02f); lineTo(cab.right + cw * 0.08f, cab.top + cw * 0.06f)
            lineTo(cab.right + cw * 0.08f, cab.bottom - cw * 0.02f); lineTo(cab.right, cab.bottom); close()
        }
        p.color = Color.WHITE; p.shader = LinearGradient(cab.right, 0f, cab.right + cw * 0.08f, 0f, 0xFF231A38.toInt(), 0xFF0D0915.toInt(), Shader.TileMode.CLAMP)
        c.drawPath(side, p); p.shader = null

        // front: deep purple space with a nebula glow
        p.color = Color.WHITE; p.shader = LinearGradient(0f, cab.top, 0f, cab.bottom,
            intArrayOf(0xFF1B1230.toInt(), 0xFF2A1A45.toInt(), 0xFF140E24.toInt()), floatArrayOf(0f, 0.55f, 1f), Shader.TileMode.CLAMP)
        c.drawRect(cab, p); p.shader = null
        p.color = Color.WHITE; p.shader = RadialGradient(cab.left + cw * 0.25f, lower.top + lower.height() * 0.6f, cw * 0.5f,
            0x55A040C0, 0x00A040C0, Shader.TileMode.CLAMP)
        c.drawRect(cab, p); p.shader = null
        p.color = Color.WHITE; p.shader = RadialGradient(cab.right - cw * 0.1f, bezel.top + bezel.height() * 0.3f, cw * 0.4f,
            0x403070E0, 0x003070E0, Shader.TileMode.CLAMP)
        c.drawRect(cab, p); p.shader = null
        // stars
        repeat(170) {
            val x = cab.left + rnd.nextFloat() * cw
            val y = cab.top + rnd.nextFloat() * cab.height()
            val big = rnd.nextFloat() < 0.12f
            p.color = if (rnd.nextBoolean()) 0xFFFFFFFF.toInt() else 0xFFBFD4FF.toInt()
            p.alpha = 90 + rnd.nextInt(165)
            c.drawCircle(x, y, if (big) cw * 0.006f else cw * 0.0028f, p)
            if (big) { p.alpha = 70; c.drawRect(x - cw * 0.014f, y - cw * 0.001f, x + cw * 0.014f, y + cw * 0.001f, p); c.drawRect(x - cw * 0.001f, y - cw * 0.014f, x + cw * 0.001f, y + cw * 0.014f, p) }
        }
        p.alpha = 255
        // a ringed planet beside the screen
        val px = cab.left + cw * 0.08f; val py = bezel.top + bezel.height() * 0.2f; val pr = cw * 0.05f
        p.color = Color.WHITE; p.shader = RadialGradient(px - pr * 0.4f, py - pr * 0.4f, pr * 1.4f, 0xFFF6B15C.toInt(), 0xFF8A3A1C.toInt(), Shader.TileMode.CLAMP)
        c.drawCircle(px, py, pr, p); p.shader = null
        val sp = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeWidth = cw * 0.008f; color = 0xCCFFD9A0.toInt() }
        c.save(); c.rotate(-18f, px, py); c.drawOval(px - pr * 1.8f, py - pr * 0.45f, px + pr * 1.8f, py + pr * 0.45f, sp); c.restore()

        // pixel invaders in formation on the lower door (own designs)
        val bugs = listOf(
            arrayOf("..X....X..", "...X..X...", "..XXXXXX..", ".XX.XX.XX.", "XXXXXXXXXX", "X.XXXXXX.X", "X.X....X.X", "...XX.XX.."),
            arrayOf("....XX....", "...XXXX...", "..XXXXXX..", ".XX.XX.XX.", ".XXXXXXXX.", "...X..X...", "..X.XX.X..", ".X.X..X.X."),
            arrayOf("X........X", ".X..XX..X.", "..XXXXXX..", ".XX.XX.XX.", "XXXXXXXXXX", "..X.XX.X..", ".X......X.", "X........X"))
        val colors = intArrayOf(0xFFFF5A3C.toInt(), 0xFFFFC23C.toInt(), 0xFF56D6FF.toInt())
        val cell = cw * 0.0085f
        for (row in 0 until 4) for (col in 0 until 7) {
            if ((row * 3 + col) % 7 == 5) continue
            val k = row % 3
            val bx = cab.left + cw * 0.06f + col * cw * 0.13f + (if (row % 2 == 1) cw * 0.04f else 0f)
            val by = lower.top + cw * 0.05f + row * cw * 0.075f
            // keep clear of the coin door and the score
            if (bx + cell * 10 > coinDoor.left - cw * 0.02f && bx < coinDoor.right + cw * 0.02f && by < coinDoor.bottom + cw * 0.02f) continue
            if (bx > cab.right - cw * 0.4f && by < lower.top + cw * 0.11f) continue
            if (by + cell * 8 > cab.bottom - cw * 0.2f) continue
            p.color = colors[k]
            bugs[k].forEachIndexed { yy, line -> line.forEachIndexed { xx, ch -> if (ch == 'X') c.drawRect(bx + xx * cell, by + yy * cell, bx + (xx + 1) * cell, by + (yy + 1) * cell, p) } }
        }
        // the player's ship and its shots
        val shipX = cab.centerX() - cw * 0.04f; val shipY = cab.bottom - cw * 0.12f
        val ship = arrayOf("....X....", "....X....", "...XXX...", "X..XXX..X", "X.XXXXX.X", "XXXXXXXXX", "XX.XXX.XX", "X...X...X")
        p.color = 0xFFEDEDF5.toInt()
        ship.forEachIndexed { yy, line -> line.forEachIndexed { xx, ch -> if (ch == 'X') c.drawRect(shipX + xx * cell, shipY + yy * cell, shipX + (xx + 1) * cell, shipY + (yy + 1) * cell, p) } }
        p.color = 0xFFFFF27A.toInt()
        for (i in 0 until 3) c.drawRect(shipX + 4 * cell, shipY - cw * (0.05f + i * 0.045f), shipX + 5 * cell, shipY - cw * (0.03f + i * 0.045f), p)
        // score readout
        val sc = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF6FB8FF.toInt(); textSize = cw * 0.05f; typeface = Typeface.MONOSPACE; isFakeBoldText = true; textAlign = Paint.Align.RIGHT }
        c.drawText("HI 24000", cab.right - cw * 0.05f, lower.top + cw * 0.08f, sc)
        return bm
    }

    /** White cabinet with blue side panels and a blue wave across the front (original artwork). */
    private fun makeA6Art(w: Int, h: Int): Bitmap {
        val bm = Bitmap.createBitmap(max(1, w), max(1, h), Bitmap.Config.ARGB_8888)
        val c = Canvas(bm)
        val cw = cab.width()
        val p = Paint(Paint.ANTI_ALIAS_FLAG)
        // blue side panel
        val side = Path().apply {
            moveTo(cab.right, cab.top + cw * 0.02f); lineTo(cab.right + cw * 0.08f, cab.top + cw * 0.06f)
            lineTo(cab.right + cw * 0.08f, cab.bottom - cw * 0.02f); lineTo(cab.right, cab.bottom); close()
        }
        p.color = Color.WHITE; p.shader = LinearGradient(cab.right, 0f, cab.right + cw * 0.08f, 0f, 0xFF1E5FD0.toInt(), 0xFF0F3A8C.toInt(), Shader.TileMode.CLAMP)
        c.drawPath(side, p); p.shader = null
        // white front with a soft shade
        p.color = Color.WHITE; p.shader = LinearGradient(cab.left, 0f, cab.right, 0f,
            intArrayOf(0xFFF4F6FA.toInt(), 0xFFFFFFFF.toInt(), 0xFFE2E6EE.toInt()), floatArrayOf(0f, 0.4f, 1f), Shader.TileMode.CLAMP)
        c.drawRect(cab, p); p.shader = null
        // blue trim down both edges
        p.color = 0xFF1F63D8.toInt()
        c.drawRect(cab.left, cab.top, cab.left + cw * 0.035f, cab.bottom, p)
        c.drawRect(cab.right - cw * 0.035f, cab.top, cab.right, cab.bottom, p)
        // a wave of blue across the lower door
        val wave = Path().apply {
            moveTo(cab.left, lower.top + cw * 0.3f)
            cubicTo(cab.left + cw * 0.3f, lower.top + cw * 0.18f, cab.left + cw * 0.6f, lower.top + cw * 0.42f, cab.right, lower.top + cw * 0.26f)
            lineTo(cab.right, cab.bottom); lineTo(cab.left, cab.bottom); close()
        }
        p.color = Color.WHITE; p.shader = LinearGradient(0f, lower.top, 0f, cab.bottom, 0xFF3C82F0.toInt(), 0xFF123E9A.toInt(), Shader.TileMode.CLAMP)
        c.drawPath(wave, p); p.shader = null
        val wave2 = Path().apply {
            moveTo(cab.left, lower.top + cw * 0.36f)
            cubicTo(cab.left + cw * 0.35f, lower.top + cw * 0.26f, cab.left + cw * 0.65f, lower.top + cw * 0.5f, cab.right, lower.top + cw * 0.33f)
        }
        val sp = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeWidth = cw * 0.012f; color = 0xCCFFFFFF.toInt() }
        c.drawPath(wave2, sp)
        // a few pixel stars on the blue
        val rnd = Random(6)
        p.color = 0xFFFFFFFF.toInt()
        repeat(30) {
            val x = cab.left + cw * 0.06f + rnd.nextFloat() * cw * 0.88f
            val y = lower.top + cw * 0.5f + rnd.nextFloat() * (cab.bottom - lower.top - cw * 0.55f)
            if (!coinDoor.contains(x, y)) { p.alpha = 120 + rnd.nextInt(135); c.drawRect(x, y, x + cw * 0.008f, y + cw * 0.008f, p) }
        }
        p.alpha = 255
        return bm
    }

    // ---------------- drawing ----------------

    override fun onDraw(c: Canvas) {
        c.drawColor(if (a6) 0xFF101826.toInt() else 0xFF0B0814.toInt())
        val cw = cab.width()
        art?.let { c.drawBitmap(it, 0f, 0f, null) }
        drawMarquee(c, cw)

        // screen bezel and picture
        paint.color = 0xFF050407.toInt()
        c.drawRoundRect(bezel, cw * 0.03f, cw * 0.03f, paint)
        stroke.color = 0xFF3B2F55.toInt(); stroke.strokeWidth = cw * 0.008f
        c.drawRoundRect(bezel, cw * 0.03f, cw * 0.03f, stroke)
        frame.setPixels(framePx, 0, 256, 0, 0, 256, 240)
        c.drawBitmap(frame, null, screen, nearest)
        paint.color = Color.WHITE; paint.shader = LinearGradient(screen.left, screen.top, screen.right, screen.bottom, 0x14FFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP)
        c.drawRect(screen, paint); paint.color = Color.WHITE; paint.shader = null

        if (a6) drawA6Panel(c, cw) else drawPanel(c, cw)
        drawCoinDoor(c, cw)
        drawVolumeOsd(c, cw)
    }

    private fun drawMarquee(c: Canvas, cw: Float) {
        if (a6) { drawA6Marquee(c, cw); return }
        val m = marquee
        paint.color = 0xFF0E0A16.toInt()
        c.drawRoundRect(m.left, m.top, m.right + cw * 0.06f, m.bottom + cw * 0.02f, cw * 0.02f, cw * 0.02f, paint)
        paint.color = Color.WHITE; paint.shader = LinearGradient(0f, m.top, 0f, m.bottom, 0xFF2B1D46.toInt(), 0xFF171026.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(m, cw * 0.02f, cw * 0.02f, paint); paint.color = Color.WHITE; paint.shader = null
        // backlit glow
        paint.color = Color.WHITE; paint.shader = RadialGradient(m.centerX(), m.centerY(), m.width() * 0.5f, 0x40FF4FD8, 0x00FF4FD8, Shader.TileMode.CLAMP)
        c.drawRect(m, paint); paint.color = Color.WHITE; paint.shader = null
        // chrome-gradient title
        val tp = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD_ITALIC)
            textSize = m.height() * 0.42f; textSkewX = -0.15f
        }
        val y = m.top + m.height() * 0.5f
        tp.shader = LinearGradient(0f, y - tp.textSize, 0f, y, intArrayOf(0xFFFFE07A.toInt(), 0xFFFF7A3C.toInt(), 0xFFE0328E.toInt()), null, Shader.TileMode.CLAMP)
        tp.setShadowLayer(m.height() * 0.06f, 0f, m.height() * 0.03f, 0xFF000000.toInt())
        c.drawText("MINI ARCADE", m.centerX(), y, tp)
        val sp = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
            textSize = m.height() * 0.2f; color = 0xFF8FE3FF.toInt(); letterSpacing = 0.25f
        }
        c.drawText("240 GAMES IN 1", m.centerX(), m.top + m.height() * 0.82f, sp)
    }

    private fun drawPanel(c: Canvas, cw: Float) {
        // slanted panel: wider at the front edge
        val pnl = Path().apply {
            moveTo(panel.left + cw * 0.04f, panel.top); lineTo(panel.right - cw * 0.04f, panel.top)
            lineTo(panel.right, panel.bottom - cw * 0.03f); lineTo(panel.left, panel.bottom - cw * 0.03f); close()
        }
        paint.color = Color.WHITE; paint.shader = LinearGradient(0f, panel.top, 0f, panel.bottom, 0xFF3A2A5C.toInt(), 0xFF22183A.toInt(), Shader.TileMode.CLAMP)
        c.drawPath(pnl, paint); paint.color = Color.WHITE; paint.shader = null
        // front lip
        paint.color = 0xFF120C1E.toInt()
        c.drawRect(panel.left, panel.bottom - cw * 0.03f, panel.right, panel.bottom, paint)
        stroke.color = 0xFF5A4688.toInt(); stroke.strokeWidth = cw * 0.004f
        c.drawPath(pnl, stroke)
        // swoosh decal behind the buttons
        stroke.color = 0x66FF5AC8; stroke.strokeWidth = cw * 0.01f
        c.drawArc(btnA.x - aR * 1.6f, btnA.y - aR * 1.25f, btnA.x + aR * 1.6f, btnA.y + aR * 1.25f, 150f, 180f, false, stroke)

        val held = touchMask or externalMask
        drawJoystick(c, cw, held)
        // A button with a pink ring
        paint.color = 0xFFE04AAE.toInt()
        c.drawCircle(btnA.x, btnA.y, aR * 1.22f, paint)
        paint.color = 0xFF1A1226.toInt()
        c.drawCircle(btnA.x, btnA.y, aR * 1.1f, paint)
        drawRedButton(c, btnA, aR, held and 0x01 != 0)
        label.textSize = aR * 1.0f
        label.color = 0xFFF2F2F7.toInt()
        c.drawText("A", btnA.x + aR * 1.75f, btnA.y + aR * 0.9f, label)

        // small buttons with labels
        label.textSize = cw * 0.04f
        label.color = 0xFFE9E3F7.toInt()
        drawRedButton(c, btnReset, smallR, false)
        drawRedButton(c, btnStart, smallR, held and 0x08 != 0)
        drawRedButton(c, btnVol, smallR, false)
        c.drawText("Reset", btnReset.x, btnReset.y - smallR * 1.6f, label)
        c.drawText("Start", btnStart.x, btnStart.y + smallR * 3.0f, label)
        c.drawText("Volume", btnVol.x, btnVol.y - smallR * 1.6f, label)
    }

    private fun drawA6Marquee(c: Canvas, cw: Float) {
        val m = marquee
        paint.color = 0xFF0C2E6E.toInt()
        c.drawRoundRect(m.left, m.top, m.right + cw * 0.06f, m.bottom + cw * 0.02f, cw * 0.02f, cw * 0.02f, paint)
        paint.color = Color.WHITE; paint.shader = LinearGradient(0f, m.top, 0f, m.bottom, 0xFF2F7BF0.toInt(), 0xFF1446B0.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(m, cw * 0.02f, cw * 0.02f, paint); paint.color = Color.WHITE; paint.shader = null
        paint.color = Color.WHITE; paint.shader = RadialGradient(m.centerX(), m.centerY(), m.width() * 0.5f, 0x50FFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP)
        c.drawRect(m, paint); paint.color = Color.WHITE; paint.shader = null
        val tp = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD_ITALIC)
            textSize = m.height() * 0.4f; textSkewX = -0.12f; color = Color.WHITE
            setShadowLayer(m.height() * 0.05f, 0f, m.height() * 0.03f, 0xFF0A2050.toInt())
        }
        c.drawText("RETRO ARCADE", m.centerX(), m.top + m.height() * 0.52f, tp)
        val sp = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
            textSize = m.height() * 0.19f; color = 0xFFFFD84A.toInt(); letterSpacing = 0.25f
        }
        c.drawText("8-BIT GAMES", m.centerX(), m.top + m.height() * 0.82f, sp)
    }

    private fun drawA6Panel(c: Canvas, cw: Float) {
        val pnl = Path().apply {
            moveTo(panel.left + cw * 0.04f, panel.top); lineTo(panel.right - cw * 0.04f, panel.top)
            lineTo(panel.right, panel.bottom - cw * 0.03f); lineTo(panel.left, panel.bottom - cw * 0.03f); close()
        }
        paint.color = Color.WHITE; paint.shader = LinearGradient(0f, panel.top, 0f, panel.bottom, 0xFF2A6EE0.toInt(), 0xFF184BB0.toInt(), Shader.TileMode.CLAMP)
        c.drawPath(pnl, paint); paint.color = Color.WHITE; paint.shader = null
        paint.color = 0xFF0E2F70.toInt()
        c.drawRect(panel.left, panel.bottom - cw * 0.03f, panel.right, panel.bottom, paint)
        stroke.color = 0xFFBFD6FF.toInt(); stroke.strokeWidth = cw * 0.004f
        c.drawPath(pnl, stroke)

        val held = touchMask or externalMask
        drawJoystick(c, cw, held)
        val pressed = HashSet<Int>()
        for (v in pointers.values) if (v and 0x100 != 0) pressed += (v shr 20) and 7
        label.textSize = aR * 0.62f; label.color = 0xFFE8F0FF.toInt()
        for (i in 0 until 8) {
            val b = a6Buttons[i]
            drawRedButton(c, b, aR, i in pressed || (held and a6Bits[i] and 0xff != 0 && pointers.isEmpty()))
            c.drawText(a6Labels[i], b.x, b.y + aR * 1.85f, label)
        }
        // small buttons on the door below the panel
        label.textSize = cw * 0.036f; label.color = 0xFF1A3C80.toInt()
        drawRedButton(c, btnSelect, smallR, held and 0x04 != 0)
        drawRedButton(c, btnStart, smallR, held and 0x08 != 0)
        drawRedButton(c, btnReset, smallR, false)
        c.drawText("SELECT", btnSelect.x, btnSelect.y + smallR * 2.9f, label)
        c.drawText("START", btnStart.x, btnStart.y + smallR * 2.9f, label)
        c.drawText("RESET", btnReset.x, btnReset.y + smallR * 2.9f, label)
    }

    private fun drawRedButton(c: Canvas, p: PointF, r: Float, down: Boolean) {
        paint.color = 0xFF0A0710.toInt()
        c.drawCircle(p.x, p.y + r * 0.18f, r * 1.12f, paint)
        val lift = if (down) 0f else r * 0.16f
        paint.color = 0xFF8E1218.toInt()
        c.drawCircle(p.x, p.y, r, paint)
        paint.color = Color.WHITE; paint.shader = RadialGradient(p.x - r * 0.3f, p.y - lift - r * 0.35f, r * 1.3f,
            if (down) intArrayOf(0xFFB8222A.toInt(), 0xFF8E1218.toInt()) else intArrayOf(0xFFFF6A5E.toInt(), 0xFFD0202A.toInt(), 0xFF9C141C.toInt()),
            null, Shader.TileMode.CLAMP)
        c.drawCircle(p.x, p.y - lift, r * 0.96f, paint); paint.color = Color.WHITE; paint.shader = null
    }

    private fun drawJoystick(c: Canvas, cw: Float, held: Int) {
        // base plate and dust washer
        paint.color = 0xFF0C0912.toInt()
        c.drawOval(stick.x - stickR * 1.5f, stick.y - stickR * 0.55f, stick.x + stickR * 1.5f, stick.y + stickR * 0.75f, paint)
        paint.color = 0xFF2A2433.toInt()
        c.drawOval(stick.x - stickR * 1.05f, stick.y - stickR * 0.35f, stick.x + stickR * 1.05f, stick.y + stickR * 0.45f, paint)
        // direction arrows printed on the panel
        paint.color = 0x88FFFFFF.toInt()
        fun arrow(dx: Float, dy: Float) {
            val x = stick.x + dx * stickR * 1.75f; val y = stick.y + dy * stickR * 1.0f + stickR * 0.08f; val a = stickR * 0.16f
            val pth = Path(); pth.moveTo(x + dx * a, y + dy * a); pth.lineTo(x - dy * a + (-dx) * a * 0.3f, y + dx * a - dy * a * 0.3f)
            pth.lineTo(x + dy * a - dx * a * 0.3f, y - dx * a - dy * a * 0.3f); pth.close(); c.drawPath(pth, paint)
        }
        arrow(-1f, 0f); arrow(1f, 0f)
        // tilt the shaft with the input
        var dx = 0f; var dy = 0f
        if (held and 0x40 != 0) dx -= 1f
        if (held and 0x80 != 0) dx += 1f
        if (held and 0x10 != 0) dy -= 1f
        if (held and 0x20 != 0) dy += 1f
        val tipX = stick.x + dx * stickR * 0.55f
        val tipY = stick.y - stickR * 1.5f + dy * stickR * 0.35f
        stroke.color = 0xFF16121C.toInt(); stroke.strokeWidth = stickR * 0.38f; stroke.strokeCap = Paint.Cap.ROUND
        c.drawLine(stick.x, stick.y, tipX, tipY, stroke)
        stroke.color = 0xFF4A4452.toInt(); stroke.strokeWidth = stickR * 0.12f
        c.drawLine(stick.x - stickR * 0.06f, stick.y, tipX - stickR * 0.06f, tipY, stroke)
        stroke.strokeCap = Paint.Cap.BUTT
        // ball top
        paint.color = 0x66000000
        c.drawOval(tipX - stickR * 0.9f, tipY + stickR * 0.55f, tipX + stickR * 0.9f, tipY + stickR * 0.95f, paint)
        paint.color = Color.WHITE; paint.shader = RadialGradient(tipX - stickR * 0.35f, tipY - stickR * 0.4f, stickR * 1.25f,
            intArrayOf(0xFFFF8A80.toInt(), 0xFFE0242E.toInt(), 0xFF7C0E14.toInt()), floatArrayOf(0f, 0.45f, 1f), Shader.TileMode.CLAMP)
        c.drawCircle(tipX, tipY, stickR, paint); paint.color = Color.WHITE; paint.shader = null
        paint.color = 0xCCFFFFFF.toInt()
        c.drawOval(tipX - stickR * 0.5f, tipY - stickR * 0.7f, tipX - stickR * 0.1f, tipY - stickR * 0.45f, paint)
    }

    private fun drawCoinDoor(c: Canvas, cw: Float) {
        val d = coinDoor
        val r = cw * 0.015f
        paint.color = 0xFF08060C.toInt()
        c.drawRoundRect(d.left - cw * 0.01f, d.top - cw * 0.01f, d.right + cw * 0.01f, d.bottom + cw * 0.01f, r, r, paint)
        paint.color = Color.WHITE; paint.shader = LinearGradient(d.left, 0f, d.right, 0f, intArrayOf(0xFF2B2B30.toInt(), 0xFF46464D.toInt(), 0xFF26262B.toInt()), null, Shader.TileMode.CLAMP)
        c.drawRoundRect(d, r, r, paint); paint.color = Color.WHITE; paint.shader = null
        // coin slot with a lit reject button
        val sx = d.left + d.width() * 0.3f
        paint.color = 0xFF111114.toInt()
        c.drawRoundRect(sx - d.width() * 0.06f, d.top + d.height() * 0.15f, sx + d.width() * 0.06f, d.top + d.height() * 0.5f, r, r, paint)
        paint.color = 0xFFE23A2E.toInt()
        c.drawRect(sx - d.width() * 0.1f, d.top + d.height() * 0.6f, sx + d.width() * 0.1f, d.top + d.height() * 0.72f, paint)
        paint.color = 0xFF16161A.toInt()
        c.drawCircle(d.left + d.width() * 0.72f, d.top + d.height() * 0.32f, d.width() * 0.08f, paint)
        label.textSize = d.height() * 0.1f; label.color = 0xFFB0B0B8.toInt()
        c.drawText("25¢", d.left + d.width() * 0.72f, d.top + d.height() * 0.62f, label)
        // keyhole
        paint.color = 0xFF0C0C0E.toInt()
        c.drawCircle(d.left + d.width() * 0.72f, d.top + d.height() * 0.82f, d.width() * 0.035f, paint)
    }

    private fun drawVolumeOsd(c: Canvas, cw: Float) {
        val age = SystemClock.uptimeMillis() - volumeShownAt
        if (age > 1200) return
        val bars = VOLUME_LEVELS - 1 - volumeLevel
        val w = cw * 0.4f; val h = cw * 0.1f
        val x = screen.centerX() - w / 2; val y = screen.bottom - h * 1.4f
        paint.color = 0xCC000000.toInt()
        c.drawRoundRect(x, y, x + w, y + h, h * 0.3f, h * 0.3f, paint)
        label.textSize = h * 0.5f; label.color = 0xFF7CFF7C.toInt()
        val txt = if (bars == 0) "MUTE" else "VOL " + "▮".repeat(bars) + "▯".repeat(VOLUME_LEVELS - 1 - bars)
        c.drawText(txt, x + w / 2, y + h * 0.68f, label)
        postInvalidateDelayed(100)
    }

    // ---------------- touch ----------------

    private fun stickMaskAt(x: Float, y: Float): Int {
        val dx = x - stick.x; val dy = y - (stick.y - stickR * 0.8f)
        if (hypot(dx, dy) < stickR * 0.35f) return 0
        val ang = Math.toDegrees(atan2(dy.toDouble(), dx.toDouble()))
        var m = 0
        if (ang > -157.5 && ang < -22.5) m = m or 0x10
        if (ang > 22.5 && ang < 157.5) m = m or 0x20
        if (ang > 112.5 || ang < -112.5) m = m or 0x40
        if (ang > -67.5 && ang < 67.5) m = m or 0x80
        return m
    }

    private fun hit(x: Float, y: Float): Int {
        if (a6) {
            if (hypot(x - stick.x, y - (stick.y - stickR * 0.8f)) < stickR * 2.6f && x < a6Buttons[4].x - aR * 1.2f) return 0x1000 or stickMaskAt(x, y)
            for (i in 0 until 8) if (hypot(x - a6Buttons[i].x, y - a6Buttons[i].y) < aR * 1.5f) return a6Bits[i] or 0x100 or (i shl 20)
            if (hypot(x - btnSelect.x, y - btnSelect.y) < smallR * 2.6f) return 0x04
            if (hypot(x - btnStart.x, y - btnStart.y) < smallR * 2.6f) return 0x08
            if (hypot(x - btnReset.x, y - btnReset.y) < smallR * 2.6f) return 0x2000
            return 0
        }
        if (hypot(x - stick.x, y - (stick.y - stickR * 0.8f)) < stickR * 2.6f && x < (stick.x + btnReset.x) / 2 + stickR) return 0x1000 or stickMaskAt(x, y)
        if (hypot(x - btnA.x, y - btnA.y) < aR * 1.7f) return 0x01
        if (hypot(x - btnStart.x, y - btnStart.y) < smallR * 2.6f) return 0x08
        if (hypot(x - btnReset.x, y - btnReset.y) < smallR * 2.6f) return 0x2000
        if (hypot(x - btnVol.x, y - btnVol.y) < smallR * 2.6f) return 0x4000
        return 0
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                val x = e.getX(i); val y = e.getY(i)
                if (marquee.contains(x, y) && e.actionMasked == MotionEvent.ACTION_DOWN) { onMenu?.invoke(); return true }
                val b = hit(x, y)
                if (b != 0) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
                when (b) {
                    0x2000 -> onReset?.invoke()
                    0x4000 -> {
                        volumeLevel = (volumeLevel + 1) % VOLUME_LEVELS
                        volumeShownAt = SystemClock.uptimeMillis()
                        onVolume?.invoke(volumeLevel)
                    }
                    else -> {
                        pointers[e.getPointerId(i)] = b
                        if (b and 0x30000 != 0) { turboOn = true; removeCallbacks(turboTick); postDelayed(turboTick, 50) }
                    }
                }
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until e.pointerCount) {
                    val id = e.getPointerId(i)
                    val old = pointers[id] ?: continue
                    // a finger on the stick keeps steering it wherever it slides
                    if (old and 0x1000 != 0) pointers[id] = 0x1000 or stickMaskAt(e.getX(i), e.getY(i))
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> pointers.remove(e.getPointerId(e.actionIndex))
            MotionEvent.ACTION_CANCEL -> pointers.clear()
        }
        pushMask()
        invalidate()
        return true
    }

    private fun pushMask() {
        var m = 0
        for (v in pointers.values) {
            m = m or (v and 0xff)
            if (turboOn && v and 0x10000 != 0) m = m or 0x01
            if (turboOn && v and 0x20000 != 0) m = m or 0x02
        }
        if (m != touchMask) { touchMask = m; onInput?.invoke(m); postInvalidate() }
    }
}
