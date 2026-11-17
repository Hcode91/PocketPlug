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
 * On-screen dance mat drawn like the Retro Dance Mat's vinyl pad: silver mat
 * with a black border, SELECT (red) and START (blue) buttons along the top,
 * blue up/down and red left/right arrows, green diagonal arrows in the four
 * corners and the round logo panel in the middle. Multi-touch, so jumps work.
 *
 * The 110dance hardware only reads the two upper diagonals; the lower corner
 * arrows are drawn as on the mat but aren't connected to anything.
 * SELECT is the game's "back" button and START its "select/start" button.
 */
class DanceMatView(ctx: Context) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    /** Buttons pressed from outside (gamepad / keyboard), shown lit on the mat. */
    var externalMask = 0
        set(v) { field = v; invalidate() }

    private enum class Kind { ARROW, DIAG, LOGO, BUTTON }
    private class Pad(val bit: Int, val kind: Kind, val angle: Float, val color: Int, val lit: Int, val label: String = "") { val r = RectF() }

    private val BLUE = 0xFF1E6FD9.toInt();  private val BLUE_LIT = 0xFF7CC4FF.toInt()
    private val RED = 0xFFD7262E.toInt();   private val RED_LIT = 0xFFFF8088.toInt()
    private val GREEN = 0xFF2DB24A.toInt(); private val GREEN_LIT = 0xFF9BFFAE.toInt()

    // 3x3 grid, row by row, then the two buttons along the top
    private val pads = arrayOf(
        Pad(DanceMatBits.UP_LEFT, Kind.DIAG, -45f, GREEN, GREEN_LIT),
        Pad(DanceMatBits.UP, Kind.ARROW, 0f, BLUE, BLUE_LIT),
        Pad(DanceMatBits.UP_RIGHT, Kind.DIAG, 45f, GREEN, GREEN_LIT),
        Pad(DanceMatBits.LEFT, Kind.ARROW, -90f, RED, RED_LIT),
        Pad(0, Kind.LOGO, 0f, 0, 0),
        Pad(DanceMatBits.RIGHT, Kind.ARROW, 90f, RED, RED_LIT),
        Pad(0, Kind.DIAG, -135f, GREEN, GREEN_LIT),   // not wired on 110dance
        Pad(DanceMatBits.DOWN, Kind.ARROW, 180f, BLUE, BLUE_LIT),
        Pad(0, Kind.DIAG, 135f, GREEN, GREEN_LIT),    // not wired on 110dance
        Pad(DanceMatBits.BACK, Kind.BUTTON, 0f, RED, RED_LIT, "SELECT"),
        Pad(DanceMatBits.START, Kind.BUTTON, 0f, BLUE, BLUE_LIT, "START"),
    )

    private var touchMask = 0
    private val mat = RectF()
    private val inner = RectF()
    private val pFill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val pStroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeJoin = Paint.Join.ROUND }
    private val pGlow = Paint(Paint.ANTI_ALIAS_FLAG)
    private val pText = Paint(Paint.ANTI_ALIAS_FLAG).apply { typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD_ITALIC); textAlign = Paint.Align.CENTER }
    private val arrow = Path()
    private val dancer = Path()
    private var cell = 0f

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val pad = w * 0.03f
        // the real mat is a little taller than wide because of the button strip
        var mw = w - pad * 2
        var mh = mw * 1.1f
        if (mh > h - pad * 2) { mh = h - pad * 2; mw = mh / 1.1f }
        mat.set((w - mw) / 2, (h - mh) / 2, (w + mw) / 2, (h + mh) / 2)
        val border = mw * 0.035f
        inner.set(mat.left + border, mat.top + border, mat.right - border, mat.bottom - border)
        val strip = inner.height() - inner.width()          // button strip along the top
        val gap = inner.width() * 0.012f
        cell = (inner.width() - gap * 4) / 3
        for (i in 0 until 9) {
            val col = i % 3; val row = i / 3
            val l = inner.left + gap + col * (cell + gap)
            val t = inner.top + strip + gap + row * (cell + gap)
            pads[i].r.set(l, t, l + cell, t + cell)
        }
        val bw = inner.width() * 0.17f
        val bh = strip * 0.32f
        pads[9].r.set(inner.left + inner.width() * 0.07f, inner.top + strip * 0.16f, inner.left + inner.width() * 0.07f + bw, inner.top + strip * 0.16f + bh)
        pads[10].r.set(inner.right - inner.width() * 0.07f - bw, pads[9].r.top, inner.right - inner.width() * 0.07f, pads[9].r.bottom)

        // pentagon "house" arrow pointing up, centred on (0,0), unit = cell
        arrow.reset()
        arrow.moveTo(0f, -0.38f); arrow.lineTo(0.33f, -0.04f); arrow.lineTo(0.16f, -0.04f)
        arrow.lineTo(0.16f, 0.36f); arrow.lineTo(-0.16f, 0.36f); arrow.lineTo(-0.16f, -0.04f)
        arrow.lineTo(-0.33f, -0.04f); arrow.close()

        // simple dancer silhouette for the centre logo
        dancer.reset()
        dancer.addCircle(0f, -0.2f, 0.055f, Path.Direction.CW)
        dancer.moveTo(-0.03f, -0.14f); dancer.lineTo(0.04f, -0.14f); dancer.lineTo(0.05f, 0.04f); dancer.lineTo(-0.04f, 0.04f); dancer.close()
        dancer.moveTo(0.03f, -0.13f); dancer.lineTo(0.17f, -0.26f); dancer.lineTo(0.19f, -0.23f); dancer.lineTo(0.05f, -0.09f); dancer.close()
        dancer.moveTo(-0.02f, -0.12f); dancer.lineTo(-0.15f, -0.04f); dancer.lineTo(-0.13f, -0.01f); dancer.lineTo(-0.01f, -0.08f); dancer.close()
        dancer.moveTo(-0.04f, 0.02f); dancer.lineTo(-0.12f, 0.2f); dancer.lineTo(-0.08f, 0.21f); dancer.lineTo(0.0f, 0.04f); dancer.close()
        dancer.moveTo(0.02f, 0.03f); dancer.lineTo(0.13f, 0.12f); dancer.lineTo(0.1f, 0.22f); dancer.lineTo(0.06f, 0.21f); dancer.lineTo(0.08f, 0.13f); dancer.lineTo(0.0f, 0.06f); dancer.close()

        pGlow.maskFilter = BlurMaskFilter(cell * 0.1f, BlurMaskFilter.Blur.NORMAL)
        setLayerType(LAYER_TYPE_SOFTWARE, null) // BlurMaskFilter needs software rendering
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0814.toInt())
        // black border + silver vinyl
        val r = mat.width() * 0.05f
        pFill.color = 0xFF151517.toInt()
        c.drawRoundRect(mat, r, r, pFill)
        pFill.shader = LinearGradient(inner.left, inner.top, inner.right, inner.bottom,
            intArrayOf(0xFFB9BDC4.toInt(), 0xFFE6E8EC.toInt(), 0xFFA7ABB3.toInt(), 0xFFD9DCE1.toInt()),
            floatArrayOf(0f, 0.35f, 0.7f, 1f), Shader.TileMode.CLAMP)
        c.drawRoundRect(inner, r * 0.6f, r * 0.6f, pFill)
        pFill.shader = null
        // dark band behind the button strip, like the printed header
        pFill.color = 0xFF2A2C31.toInt()
        c.drawRect(inner.left + inner.width() * 0.3f, inner.top, inner.right - inner.width() * 0.3f, pads[0].r.top - cell * 0.02f, pFill)

        val pressed = touchMask or externalMask
        for (p in pads) drawPad(c, p, p.bit != 0 && (pressed and p.bit) != 0)
    }

    private fun drawPad(c: Canvas, p: Pad, on: Boolean) {
        when (p.kind) {
            Kind.BUTTON -> {
                val rr = p.r.height() / 2
                if (on) { pGlow.color = p.lit; c.drawRoundRect(p.r, rr, rr, pGlow) }
                pFill.shader = LinearGradient(0f, p.r.top, 0f, p.r.bottom, if (on) p.lit else p.color, darken(p.color), Shader.TileMode.CLAMP)
                c.drawRoundRect(p.r, rr, rr, pFill)
                pFill.shader = null
                pText.textSize = p.r.height() * 0.95f
                pText.color = 0xFF16171A.toInt()
                c.drawText(p.label, p.r.centerX(), p.r.bottom + pText.textSize * 1.05f, pText)
                return
            }
            else -> {}
        }

        // every square: silver pad with a dark outline and dark corner stripes
        val rr = cell * 0.04f
        pFill.shader = LinearGradient(p.r.left, p.r.top, p.r.right, p.r.bottom, 0xFFDCDEE2.toInt(), 0xFFAEB2B9.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(p.r, rr, rr, pFill)
        pFill.shader = null
        pStroke.color = 0xFF3A3C42.toInt(); pStroke.strokeWidth = cell * 0.02f
        c.drawRoundRect(p.r, rr, rr, pStroke)

        c.save()
        c.translate(p.r.centerX(), p.r.centerY())
        c.scale(cell, cell)
        when (p.kind) {
            Kind.DIAG -> {
                // green square with a dark green frame holding the arrow
                val sq = RectF(-0.36f, -0.36f, 0.36f, 0.36f)
                if (on) { pGlow.color = p.lit; c.drawRect(sq, pGlow) }
                pFill.color = if (on) p.lit else 0xFF1F2A22.toInt()
                c.drawRect(sq, pFill)
                pStroke.color = p.color; pStroke.strokeWidth = 0.04f
                c.drawRect(sq, pStroke)
                c.rotate(p.angle)
                c.scale(0.85f, 0.85f)
                pFill.color = if (on) 0xFFFFFFFF.toInt() else p.color
                c.drawPath(arrow, pFill)
                pStroke.color = 0xFFE9FFE9.toInt(); pStroke.strokeWidth = 0.035f
                c.drawPath(arrow, pStroke)
            }
            Kind.ARROW -> {
                c.rotate(p.angle)
                c.scale(1.12f, 1.12f)
                if (on) { pGlow.color = p.lit; c.drawPath(arrow, pGlow) }
                pFill.shader = LinearGradient(0f, -0.38f, 0f, 0.36f, if (on) p.lit else lighten(p.color), if (on) p.color else darken(p.color), Shader.TileMode.CLAMP)
                c.drawPath(arrow, pFill)
                pFill.shader = null
                pStroke.color = 0xFFFFFFFF.toInt(); pStroke.strokeWidth = 0.035f
                c.drawPath(arrow, pStroke)
            }
            Kind.LOGO -> {
                pFill.color = 0xFF5E626B.toInt()
                c.drawCircle(0f, 0f, 0.47f, pFill)
                pFill.color = 0xFFF4F5F7.toInt()
                c.drawCircle(0f, 0f, 0.41f, pFill)
                pFill.color = 0xFF1B2D5C.toInt()
                c.drawPath(dancer, pFill)
            }
            else -> {}
        }
        c.restore()
        if (p.kind == Kind.LOGO) {
            pText.textSize = cell * 0.13f
            pText.color = 0xFF1B2D5C.toInt()
            c.drawText("Dance!", p.r.centerX(), p.r.centerY() + cell * 0.36f, pText)
        }
    }

    private fun lighten(c: Int) = blend(c, 0xFFFFFFFF.toInt(), 0.25f)
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

    /** Nearest pad within a small margin, so the gaps between pads still count. */
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
        return if (bestD <= (cell * 0.12f) * (cell * 0.12f)) best?.bit ?: 0 else 0
    }
}
