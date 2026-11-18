package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View

/**
 * A home-built nixie clock: six glass tubes and two pairs of neon dots on top
 * of the board that drives them (the 4004 CPU in its white ceramic package,
 * its ROMs and RAMs), standing on a walnut base with the SELECT and BROWSE
 * push buttons and the TICK-TOCK and 50/60 Hz switches. A printed card
 * underneath explains the buttons. Tap the power switch for the menu.
 *
 * The core's "frame" carries the tube digits (pixels 0-5, 10 = dark) and the
 * neon dots (pixel 6, bits 0-3), not a picture.
 */
class NixieClockView(ctx: Context) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var onTickTock: ((Boolean) -> Unit)? = null
    var onMains: ((Int) -> Unit)? = null

    var tickTock = true
        set(v) { field = v; postInvalidate() }
    var mainsHz = 50
        set(v) { field = v; postInvalidate() }
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    companion object {
        const val SELECT = 1
        const val BROWSE = 2
    }

    private val digits = IntArray(6) { 10 }
    private var neons = 0

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER }
    private val glow = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER }

    private val case = RectF()
    private val board = RectF()
    private val base = RectF()
    private val card = RectF()
    private val tubes = Array(6) { RectF() }
    private val dots = Array(4) { PointF() }
    private var dotR = 0f
    private val btnSelect = PointF()
    private val btnBrowse = PointF()
    private var btnR = 0f
    private val swTick = RectF()
    private val swMains = RectF()
    private val power = RectF()

    private val pointers = HashMap<Int, Int>()
    private var touchMask = 0

    fun updateFrame(px: IntArray) {
        if (px.size < 7) return
        var changed = false
        for (i in 0..5) { val d = px[i].coerceIn(0, 10); if (d != digits[i]) { digits[i] = d; changed = true } }
        if (px[6] != neons) { neons = px[6]; changed = true }
        if (changed) postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        val cw = w * 0.96f
        val left = (w - cw) / 2
        // tubes: six, with a narrow column for the dots after the 2nd and 4th
        val unitW = cw * 0.92f / (6f + 2 * 0.45f)
        val tubeH = unitW * 2.25f
        val top = h * 0.06f
        case.set(left, top, left + cw, top + tubeH + unitW * 0.9f)
        var x = case.centerX() - unitW * (6f + 0.9f) / 2
        val ty = case.top + unitW * 0.35f
        var d = 0
        for (i in 0..5) {
            tubes[i].set(x + unitW * 0.04f, ty, x + unitW * 0.96f, ty + tubeH)
            x += unitW
            if (i == 1 || i == 3) {
                val cx = x + unitW * 0.225f
                // upper dot = neon1 / neon2, lower = neon0 / neon3 (as MAME's layout)
                if (i == 1) { dots[1].set(cx, ty + tubeH * 0.38f); dots[0].set(cx, ty + tubeH * 0.62f) }
                else { dots[2].set(cx, ty + tubeH * 0.38f); dots[3].set(cx, ty + tubeH * 0.62f) }
                x += unitW * 0.45f
                d++
            }
        }
        dotR = unitW * 0.09f
        board.set(case.left + cw * 0.02f, case.bottom - unitW * 0.5f, case.right - cw * 0.02f, case.bottom + unitW * 1.4f)
        base.set(left - w * 0.01f, board.bottom, left + cw + w * 0.01f, board.bottom + unitW * 2.1f)
        btnR = unitW * 0.42f
        btnSelect.set(base.left + base.width() * 0.2f, base.centerY() - unitW * 0.05f)
        btnBrowse.set(base.left + base.width() * 0.4f, base.centerY() - unitW * 0.05f)
        swTick.set(base.left + base.width() * 0.56f, base.centerY() - unitW * 0.45f, base.left + base.width() * 0.66f, base.centerY() + unitW * 0.35f)
        swMains.set(base.left + base.width() * 0.72f, base.centerY() - unitW * 0.45f, base.left + base.width() * 0.82f, base.centerY() + unitW * 0.35f)
        power.set(base.right - base.width() * 0.12f, base.centerY() - unitW * 0.32f, base.right - base.width() * 0.05f, base.centerY() + unitW * 0.22f)
        card.set(w * 0.08f, base.bottom + h * 0.05f, w * 0.92f, minOf(h * 0.97f, base.bottom + h * 0.05f + w * 0.95f))
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF1B1714.toInt())
        val u = tubes[0].width()

        // the board behind and below the tubes
        paint.color = 0xFF1F4A2C.toInt()
        c.drawRoundRect(board, u * 0.08f, u * 0.08f, paint)
        stroke.color = 0xFF3E7A4E.toInt(); stroke.strokeWidth = u * 0.015f
        for (i in 0..13) {
            val y = board.top + board.height() * (0.15f + i * 0.06f)
            c.drawLine(board.left + u * 0.2f, y, board.right - u * 0.2f, y, stroke)
        }
        // the 4004: white ceramic, gold lid, 16 pins
        val chip = RectF(board.centerX() - u * 0.55f, board.bottom - u * 0.95f, board.centerX() + u * 0.55f, board.bottom - u * 0.3f)
        paint.color = 0xFFC9C2B2.toInt()
        for (p in 0..7) {
            val px = chip.left + chip.width() * (0.08f + p * 0.12f)
            c.drawRect(px, chip.top - u * 0.06f, px + chip.width() * 0.05f, chip.top, paint)
            c.drawRect(px, chip.bottom, px + chip.width() * 0.05f, chip.bottom + u * 0.06f, paint)
        }
        paint.color = 0xFFECE7DC.toInt()
        c.drawRoundRect(chip, u * 0.04f, u * 0.04f, paint)
        paint.color = 0xFFC9A44A.toInt()
        c.drawRoundRect(chip.centerX() - chip.width() * 0.22f, chip.top + chip.height() * 0.18f, chip.centerX() + chip.width() * 0.22f, chip.bottom - chip.height() * 0.18f, u * 0.03f, u * 0.03f, paint)
        text.color = 0xFF5A4A28.toInt(); text.textSize = chip.height() * 0.28f
        text.typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
        c.drawText("4004", chip.centerX(), chip.centerY() + text.textSize * 0.35f, text)
        // ROMs and RAMs: small black DIPs either side
        paint.color = 0xFF151515.toInt()
        for (side in 0..1) for (i in 0..3) {
            val cx = if (side == 0) board.left + u * (0.5f + i * 0.75f) else board.right - u * (0.5f + i * 0.75f)
            c.drawRoundRect(cx - u * 0.28f, chip.top + u * 0.05f, cx + u * 0.28f, chip.bottom - u * 0.05f, u * 0.03f, u * 0.03f, paint)
        }

        // smoked acrylic case behind the tubes
        paint.color = 0x55303030
        c.drawRoundRect(case, u * 0.12f, u * 0.12f, paint)
        stroke.color = 0x40FFFFFF; stroke.strokeWidth = u * 0.02f
        c.drawRoundRect(case, u * 0.12f, u * 0.12f, stroke)

        for (i in 0..5) drawTube(c, tubes[i], digits[i])
        for (i in 0..3) drawNeon(c, dots[i], neons and (1 shl i) != 0)

        // walnut base
        paint.color = Color.WHITE
        paint.shader = LinearGradient(0f, base.top, 0f, base.bottom, 0xFF6B4329.toInt(), 0xFF3E2516.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(base, u * 0.15f, u * 0.15f, paint)
        paint.shader = null
        stroke.color = 0x22000000; stroke.strokeWidth = u * 0.02f
        for (i in 0..5) {
            val y = base.top + base.height() * (0.15f + i * 0.14f)
            c.drawLine(base.left + u * 0.3f, y, base.right - u * 0.3f, y + u * 0.05f * (if (i % 2 == 0) 1 else -1), stroke)
        }
        stroke.color = 0x55FFFFFF; stroke.strokeWidth = u * 0.02f
        c.drawLine(base.left + u * 0.2f, base.top + u * 0.04f, base.right - u * 0.2f, base.top + u * 0.04f, stroke)

        val held = touchMask or externalMask
        drawButton(c, btnSelect, "SELECT", held and SELECT != 0)
        drawButton(c, btnBrowse, "BROWSE", held and BROWSE != 0)
        drawToggle(c, swTick, tickTock, "TICK-TOCK", "ON", "OFF")
        drawToggle(c, swMains, mainsHz == 60, "MAINS", "60", "50")
        // power switch (menu)
        paint.color = 0xFF1A1A1A.toInt()
        c.drawRoundRect(power, u * 0.06f, u * 0.06f, paint)
        paint.color = 0xFFB0281E.toInt()
        c.drawRoundRect(power.left + power.width() * 0.15f, power.top + power.height() * 0.1f, power.right - power.width() * 0.15f, power.centerY(), u * 0.04f, u * 0.04f, paint)
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
        text.color = 0xFFD9C6A8.toInt(); text.textSize = u * 0.16f
        c.drawText("POWER", power.centerX(), power.bottom + u * 0.24f, text)

        drawCard(c)
    }

    private fun drawTube(c: Canvas, r: RectF, digit: Int) {
        val w = r.width()
        // glass envelope with a domed top
        paint.color = 0x332A2622
        c.drawRoundRect(r, w * 0.48f, w * 0.48f, paint)
        paint.shader = LinearGradient(r.left, 0f, r.right, 0f, intArrayOf(0x30FFFFFF, 0x08FFFFFF, 0x00FFFFFF, 0x18FFFFFF),
            floatArrayOf(0f, 0.3f, 0.7f, 1f), Shader.TileMode.CLAMP)
        c.drawRoundRect(r, w * 0.48f, w * 0.48f, paint)
        paint.shader = null
        // anode mesh
        stroke.color = 0x22B0A090; stroke.strokeWidth = w * 0.012f
        val mesh = RectF(r.left + w * 0.12f, r.top + w * 0.35f, r.right - w * 0.12f, r.bottom - w * 0.3f)
        var gx = mesh.left
        while (gx < mesh.right) { c.drawLine(gx, mesh.top, gx, mesh.bottom, stroke); gx += w * 0.07f }
        var gy = mesh.top
        while (gy < mesh.bottom) { c.drawLine(mesh.left, gy, mesh.right, gy, stroke); gy += w * 0.07f }
        // the unlit cathodes, stacked
        val size = mesh.height() * 0.78f
        val cx = r.centerX()
        val cy = mesh.centerY() + size * 0.36f
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL)
        text.textSize = size
        text.color = 0x22D8B898
        for (n in intArrayOf(1, 0, 2, 9, 3, 8, 4, 7, 5, 6)) if (n != digit) c.drawText(n.toString(), cx, cy, text)
        if (digit in 0..9) {
            glow.typeface = text.typeface
            glow.textSize = size
            glow.color = 0xFFFF8A2A.toInt()
            glow.maskFilter = BlurMaskFilter(size * 0.12f, BlurMaskFilter.Blur.NORMAL)
            c.drawText(digit.toString(), cx, cy, glow)
            glow.maskFilter = null
            glow.color = 0xFFFFC27A.toInt()
            c.drawText(digit.toString(), cx, cy, glow)
            // orange haze in the glass
            paint.shader = RadialGradient(cx, mesh.centerY(), w * 0.7f, 0x33FF7A1A, 0x00FF7A1A, Shader.TileMode.CLAMP)
            c.drawRoundRect(r, w * 0.48f, w * 0.48f, paint)
            paint.shader = null
        }
        // base pins and socket
        paint.color = 0xFF2A2420.toInt()
        c.drawRoundRect(r.left + w * 0.05f, r.bottom - w * 0.22f, r.right - w * 0.05f, r.bottom + w * 0.04f, w * 0.06f, w * 0.06f, paint)
        // highlight
        stroke.color = 0x30FFFFFF; stroke.strokeWidth = w * 0.03f
        c.drawLine(r.left + w * 0.18f, r.top + w * 0.5f, r.left + w * 0.18f, r.bottom - w * 0.5f, stroke)
    }

    private fun drawNeon(c: Canvas, p: PointF, on: Boolean) {
        paint.color = 0x40D8C8B8
        c.drawOval(p.x - dotR * 1.3f, p.y - dotR * 2f, p.x + dotR * 1.3f, p.y + dotR * 2f, paint)
        if (on) {
            paint.color = 0xFFFF7A2A.toInt()
            paint.maskFilter = BlurMaskFilter(dotR * 1.2f, BlurMaskFilter.Blur.NORMAL)
            c.drawCircle(p.x, p.y, dotR * 1.2f, paint)
            paint.maskFilter = null
            paint.color = 0xFFFFB070.toInt()
            c.drawCircle(p.x, p.y, dotR * 0.8f, paint)
        } else {
            paint.color = 0x55806050
            c.drawCircle(p.x, p.y, dotR * 0.7f, paint)
        }
    }

    private fun drawButton(c: Canvas, p: PointF, label: String, down: Boolean) {
        paint.color = 0xFF1A1A1A.toInt()
        c.drawCircle(p.x, p.y, btnR * 1.15f, paint)
        paint.color = Color.WHITE
        paint.shader = RadialGradient(p.x - btnR * 0.3f, p.y - btnR * 0.3f, btnR * 1.4f,
            if (down) 0xFF8A8A8A.toInt() else 0xFFE4E4E4.toInt(), 0xFF5A5A5A.toInt(), Shader.TileMode.CLAMP)
        c.drawCircle(p.x, p.y + (if (down) btnR * 0.06f else 0f), btnR * (if (down) 0.92f else 1f), paint)
        paint.shader = null
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
        text.color = 0xFFD9C6A8.toInt(); text.textSize = btnR * 0.4f
        c.drawText(label, p.x, p.y + btnR * 1.75f, text)
    }

    private fun drawToggle(c: Canvas, r: RectF, up: Boolean, label: String, upText: String, downText: String) {
        paint.color = 0xFFB8B8B8.toInt()
        c.drawRoundRect(r.centerX() - r.width() * 0.32f, r.centerY() - r.height() * 0.18f, r.centerX() + r.width() * 0.32f, r.centerY() + r.height() * 0.18f, r.width() * 0.08f, r.width() * 0.08f, paint)
        paint.color = 0xFF2A2A2A.toInt()
        c.drawCircle(r.centerX(), r.centerY(), r.width() * 0.16f, paint)
        // lever
        stroke.color = 0xFFDADADA.toInt(); stroke.strokeWidth = r.width() * 0.13f; stroke.strokeCap = Paint.Cap.ROUND
        val ey = if (up) r.top + r.height() * 0.08f else r.bottom - r.height() * 0.08f
        c.drawLine(r.centerX(), r.centerY(), r.centerX(), ey, stroke)
        stroke.strokeCap = Paint.Cap.BUTT
        text.typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
        text.color = 0xFFD9C6A8.toInt(); text.textSize = r.width() * 0.2f
        c.drawText(upText, r.right + r.width() * 0.05f, r.top + r.height() * 0.15f, text)
        c.drawText(downText, r.right + r.width() * 0.05f, r.bottom, text)
        c.drawText(label, r.centerX(), r.bottom + r.height() * 0.38f, text)
    }

    private fun drawCard(c: Canvas) {
        paint.color = 0xFFEFE6D2.toInt()
        c.drawRoundRect(card, card.width() * 0.015f, card.width() * 0.015f, paint)
        val lines = listOf(
            "#How to use it",
            "It starts by itself and is set from your",
            "phone's clock (24 hours, month-day-year).",
            "",
            "#Choosing a display",
            "SELECT, then BROWSE shows the display",
            "in use. SELECT steps the number, BROWSE",
            "takes it:",
            "  1  date        2  days to retirement",
            "  3  time        4  time to retirement",
            "  0  cycles through 1 to 3",
            "",
            "#Retirement date",
            "Press SELECT five times to set it.",
            "",
            "#Switches",
            "TICK-TOCK: a click every second.",
            "MAINS: the frequency the clock counts.",
        )
        val lh = card.height() / (lines.size + 1.5f)
        text.textAlign = Paint.Align.LEFT
        var y = card.top + lh * 1.2f
        for (l in lines) {
            if (l.startsWith("#")) {
                text.typeface = Typeface.create(Typeface.SERIF, Typeface.BOLD)
                text.color = 0xFF3A2A1A.toInt(); text.textSize = lh * 0.68f
                c.drawText(l.substring(1), card.left + card.width() * 0.06f, y, text)
            } else {
                text.typeface = Typeface.create(Typeface.MONOSPACE, Typeface.NORMAL)
                text.color = 0xFF4A3A2A.toInt(); text.textSize = minOf(lh * 0.6f, card.width() * 0.88f / 40f / 0.6f)
                c.drawText(l, card.left + card.width() * 0.06f, y, text)
            }
            y += lh
        }
        text.textAlign = Paint.Align.CENTER
    }

    // ---- touch ----
    private fun hit(x: Float, y: Float): Int {
        val r2 = btnR * btnR * 2.2f
        fun near(p: PointF) = (x - p.x) * (x - p.x) + (y - p.y) * (y - p.y) < r2
        return when {
            near(btnSelect) -> SELECT
            near(btnBrowse) -> BROWSE
            else -> 0
        }
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
                if (e.actionMasked == MotionEvent.ACTION_DOWN) {
                    if (RectF(power).apply { inset(-power.width() * 0.5f, -power.height() * 0.5f) }.contains(x, y)) { onMenu?.invoke(); return true }
                    if (RectF(swTick).apply { inset(-swTick.width() * 0.3f, -swTick.height() * 0.2f) }.contains(x, y)) {
                        tickTock = !tickTock; onTickTock?.invoke(tickTock)
                        performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP); return true
                    }
                    if (RectF(swMains).apply { inset(-swMains.width() * 0.3f, -swMains.height() * 0.2f) }.contains(x, y)) {
                        mainsHz = if (mainsHz == 50) 60 else 50; onMains?.invoke(mainsHz)
                        performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP); return true
                    }
                }
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
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                pointers.remove(e.getPointerId(e.actionIndex))
                update()
            }
            MotionEvent.ACTION_CANCEL -> { pointers.clear(); update() }
        }
        return true
    }
}
