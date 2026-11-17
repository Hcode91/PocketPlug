package com.hcode91.pocketplug

import android.content.Context
import android.graphics.BlurMaskFilter
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View

/**
 * On-screen dance mat drawn like the real vinyl mat: a 3x3 grid with the four
 * arrows, the two upper diagonals, BACK and START in the lower corners and the
 * logo panel in the middle. Multi-touch, so jumps (two arrows) work.
 */
class DanceMatView(ctx: Context) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    /** Buttons pressed from outside (gamepad / keyboard), shown lit on the mat. */
    var externalMask = 0
        set(v) { field = v; invalidate() }

    private enum class Kind { ARROW, DIAG, BACK, START, LOGO }
    private class Pad(val bit: Int, val kind: Kind, val angle: Float, val base: Int, val lit: Int) { val r = RectF() }

    private val pads = arrayOf(
        Pad(DanceMatBits.UP_LEFT, Kind.DIAG, -45f, 0xFFB98A00.toInt(), 0xFFFFE14D.toInt()),
        Pad(DanceMatBits.UP, Kind.ARROW, 0f, 0xFF1F5FD6.toInt(), 0xFF6FD3FF.toInt()),
        Pad(DanceMatBits.UP_RIGHT, Kind.DIAG, 45f, 0xFFB98A00.toInt(), 0xFFFFE14D.toInt()),
        Pad(DanceMatBits.LEFT, Kind.ARROW, -90f, 0xFFC0267E.toInt(), 0xFFFF7FD0.toInt()),
        Pad(0, Kind.LOGO, 0f, 0xFF231A4E.toInt(), 0xFF231A4E.toInt()),
        Pad(DanceMatBits.RIGHT, Kind.ARROW, 90f, 0xFFC0267E.toInt(), 0xFFFF7FD0.toInt()),
        Pad(DanceMatBits.BACK, Kind.BACK, 0f, 0xFF8E1D2A.toInt(), 0xFFFF5D6E.toInt()),
        Pad(DanceMatBits.DOWN, Kind.ARROW, 180f, 0xFF1F5FD6.toInt(), 0xFF6FD3FF.toInt()),
        Pad(DanceMatBits.START, Kind.START, 0f, 0xFF167A45.toInt(), 0xFF5DFF9C.toInt()),
    )

    private var touchMask = 0
    private val mat = RectF()
    private val pFill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val pStroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val pGlow = Paint(Paint.ANTI_ALIAS_FLAG)
    private val pText = Paint(Paint.ANTI_ALIAS_FLAG).apply { typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD); textAlign = Paint.Align.CENTER }
    private val arrow = Path()
    private val star = Path()
    private var cell = 0f

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val pad = w * 0.04f
        val size = minOf(w - pad * 2, h - pad * 2)
        mat.set((w - size) / 2, (h - size) / 2, (w + size) / 2, (h + size) / 2)
        val border = size * 0.045f
        val gap = size * 0.022f
        cell = (size - border * 2 - gap * 2) / 3
        for (i in pads.indices) {
            val col = i % 3; val row = i / 3
            val l = mat.left + border + col * (cell + gap)
            val t = mat.top + border + row * (cell + gap)
            pads[i].r.set(l, t, l + cell, t + cell)
        }
        // chunky arrow pointing up, centred on (0,0), unit = cell
        arrow.reset()
        arrow.moveTo(0f, -0.34f); arrow.lineTo(0.30f, -0.02f); arrow.lineTo(0.12f, -0.02f)
        arrow.lineTo(0.12f, 0.32f); arrow.lineTo(-0.12f, 0.32f); arrow.lineTo(-0.12f, -0.02f)
        arrow.lineTo(-0.30f, -0.02f); arrow.close()
        star.reset()
        for (k in 0 until 10) {
            val a = Math.toRadians((k * 36 - 90).toDouble())
            val rr = if (k % 2 == 0) 0.3f else 0.13f
            val x = (Math.cos(a) * rr).toFloat(); val y = (Math.sin(a) * rr).toFloat()
            if (k == 0) star.moveTo(x, y) else star.lineTo(x, y)
        }
        star.close()
        pGlow.maskFilter = BlurMaskFilter(cell * 0.12f, BlurMaskFilter.Blur.NORMAL)
        setLayerType(LAYER_TYPE_SOFTWARE, null) // BlurMaskFilter needs software rendering
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0814.toInt())
        // the vinyl mat
        val r = mat.width() * 0.04f
        pFill.shader = LinearGradient(0f, mat.top, 0f, mat.bottom, 0xFF1C1640.toInt(), 0xFF0F0B26.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(mat, r, r, pFill)
        pFill.shader = null
        pStroke.color = 0xFF3D3380.toInt(); pStroke.strokeWidth = mat.width() * 0.008f
        c.drawRoundRect(mat, r, r, pStroke)

        val pressed = touchMask or externalMask
        for (p in pads) drawPad(c, p, p.bit != 0 && (pressed and p.bit) != 0)
    }

    private fun drawPad(c: Canvas, p: Pad, on: Boolean) {
        val rr = cell * 0.1f
        if (on) {
            pGlow.color = p.lit
            c.drawRoundRect(p.r, rr, rr, pGlow)
        }
        pFill.shader = LinearGradient(p.r.left, p.r.top, p.r.right, p.r.bottom,
            if (on) p.lit else lighten(p.base), if (on) p.base else darken(p.base), Shader.TileMode.CLAMP)
        c.drawRoundRect(p.r, rr, rr, pFill)
        pFill.shader = null
        pStroke.color = if (on) 0xFFFFFFFF.toInt() else 0x55FFFFFF
        pStroke.strokeWidth = cell * 0.025f
        c.drawRoundRect(p.r, rr, rr, pStroke)

        c.save()
        c.translate(p.r.centerX(), p.r.centerY())
        c.scale(cell, cell)
        when (p.kind) {
            Kind.ARROW, Kind.DIAG -> {
                c.rotate(p.angle)
                pFill.color = if (on) 0xFFFFFFFF.toInt() else 0xF0FFFFFF.toInt()
                c.drawPath(arrow, pFill)
                pStroke.color = 0x99000000.toInt(); pStroke.strokeWidth = 0.02f
                c.drawPath(arrow, pStroke)
            }
            Kind.LOGO -> {
                pFill.color = 0xFFFF4FD8.toInt()
                c.drawPath(star, pFill)
            }
            Kind.BACK, Kind.START -> {
                pStroke.color = 0xFFFFFFFF.toInt(); pStroke.strokeWidth = 0.05f
                if (p.kind == Kind.BACK) {
                    c.drawLine(-0.14f, -0.24f, 0.14f, 0.04f, pStroke); c.drawLine(0.14f, -0.24f, -0.14f, 0.04f, pStroke)
                } else {
                    c.drawCircle(0f, -0.1f, 0.15f, pStroke)
                }
            }
        }
        c.restore()

        if (p.kind == Kind.BACK || p.kind == Kind.START || p.kind == Kind.LOGO) {
            pText.color = 0xFFFFFFFF.toInt()
            pText.textSize = cell * (if (p.kind == Kind.LOGO) 0.11f else 0.15f)
            val label = when (p.kind) { Kind.BACK -> "BACK"; Kind.START -> "START"; else -> "DANCE" }
            c.drawText(label, p.r.centerX(), p.r.centerY() + cell * (if (p.kind == Kind.LOGO) 0.36f else 0.32f), pText)
        }
    }

    private fun lighten(c: Int) = blend(c, 0xFFFFFFFF.toInt(), 0.12f)
    private fun darken(c: Int) = blend(c, 0xFF000000.toInt(), 0.35f)
    private fun blend(a: Int, b: Int, t: Float): Int {
        fun ch(s: Int) = (((a shr s) and 0xff) * (1 - t) + ((b shr s) and 0xff) * t).toInt() shl s
        return (0xFF shl 24) or ch(16) or ch(8) or ch(0)
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        var mask = 0
        val upIndex = when (e.actionMasked) {
            MotionEvent.ACTION_POINTER_UP -> e.actionIndex
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> -2
            else -> -1
        }
        if (upIndex != -2) {
            for (i in 0 until e.pointerCount) {
                if (i == upIndex) continue
                mask = mask or hit(e.getX(i), e.getY(i))
            }
        }
        if (mask != touchMask) {
            if ((mask and touchMask.inv()) != 0) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
            touchMask = mask
            onInput?.invoke(mask)
            invalidate()
        }
        return true
    }

    /** Each pad's hit area extends half the gap around it, so the whole mat is live. */
    private fun hit(x: Float, y: Float): Int {
        if (cell <= 0f) return 0
        var best: Pad? = null
        var bestD = Float.MAX_VALUE
        for (p in pads) {
            val dx = maxOf(p.r.left - x, 0f, x - p.r.right)
            val dy = maxOf(p.r.top - y, 0f, y - p.r.bottom)
            val d = dx * dx + dy * dy
            if (d < bestD) { bestD = d; best = p }
        }
        return if (bestD <= (cell * 0.15f) * (cell * 0.15f)) best?.bit ?: 0 else 0
    }
}
