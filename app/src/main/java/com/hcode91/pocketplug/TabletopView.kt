package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.min

/**
 * 1982 tabletop LED console (the "advision" style), seen from the front: cream
 * side panels, a black top band with its red and maroon stripes (no name on
 * it), the screen window where the red LED picture appears, and the sloped
 * control panel - a chrome joystick in the middle, a cartridge in its slot
 * behind it, four cream buttons on each side (numbered 2 1 / 3 4 on the left,
 * 4 1 / 3 2 on the right, as on the real panel) and the red sound switch.
 *
 * Tap the top band for the menu. Input bits: up 0x01, down 0x02, left 0x04,
 * right 0x08, buttons 1-4 = 0x10, 0x20, 0x40, 0x80.
 */
class TabletopView(ctx: Context, private val srcW: Int, private val srcH: Int) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    /** The sound switch: true = muted. */
    var onMute: ((Boolean) -> Unit)? = null
    var muted = false
        set(v) { field = v; postInvalidate() }

    private val frame = Bitmap.createBitmap(srcW, srcH, Bitmap.Config.ARGB_8888)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }
    private val smooth = Paint(Paint.FILTER_BITMAP_FLAG)

    private val cab = RectF()
    private val band = RectF()
    private val bezel = RectF()
    private val window = RectF()
    private val picture = RectF()
    private val panel = RectF()
    private val slot = RectF()
    private val muteR = RectF()
    private val stick = PointF(); private var stickR = 0f
    private val buttons = Array(8) { RectF() }
    // left panel 2 1 / 3 4, right panel 4 1 / 3 2
    private val btnNum = intArrayOf(2, 1, 3, 4, 4, 1, 3, 2)

    private var stickMask = 0
    private var stickPointer = -1
    private val stickPos = PointF()
    private val btnPointers = HashMap<Int, Int>()
    private var mask = 0

    fun updateFrame(px: IntArray) {
        frame.setPixels(px, 0, srcW, 0, 0, srcW, srcH)
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        // the cabinet front is about 0.8 wide per 1.0 tall
        val cw = min(w * 0.98f, h * 0.86f)
        val ch = min(h * 0.98f, cw * 1.25f)
        cab.set((w - cw) / 2f, (h - ch) / 2f, (w + cw) / 2f, (h + ch) / 2f)
        val side = cw * 0.075f
        band.set(cab.left + side, cab.top + ch * 0.03f, cab.right - side, cab.top + ch * 0.11f)
        bezel.set(cab.left + side, band.bottom, cab.right - side, cab.top + ch * 0.56f)
        window.set(bezel.left + cw * 0.08f, bezel.top + ch * 0.06f, bezel.right - cw * 0.08f, bezel.bottom - ch * 0.05f)
        val pw = window.width() * 0.94f
        val ph = pw * srcH / srcW * 1.15f
        picture.set(window.centerX() - pw / 2f, window.centerY() - ph / 2f, window.centerX() + pw / 2f, window.centerY() + ph / 2f)
        panel.set(cab.left + side, bezel.bottom + ch * 0.06f, cab.right - side, cab.bottom - ch * 0.02f)
        slot.set(panel.centerX() - cw * 0.12f, bezel.bottom + ch * 0.005f, panel.centerX() + cw * 0.12f, bezel.bottom + ch * 0.075f)
        stickR = cw * 0.11f
        stick.set(panel.centerX() - cw * 0.04f, panel.top + panel.height() * 0.55f)
        val b = cw * 0.085f
        val gap = b * 0.18f
        fun cluster(cx: Float, cy: Float, first: Int) {
            for (i in 0 until 4) {
                val x = cx + (if (i % 2 == 0) -1 else 0) * (b + gap) + gap / 2f
                val y = cy + (if (i < 2) -1 else 0) * (b + gap) + gap / 2f
                buttons[first + i].set(x, y, x + b, y + b)
            }
        }
        cluster(panel.left + panel.width() * 0.19f, panel.top + panel.height() * 0.42f, 0)
        cluster(panel.left + panel.width() * 0.76f, panel.top + panel.height() * 0.62f, 4)
        muteR.set(panel.right - cw * 0.2f, panel.top + panel.height() * 0.2f, panel.right - cw * 0.12f, panel.top + panel.height() * 0.3f)
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF1B1712.toInt())
        // cream body
        paint.color = 0xFFE9E1C9.toInt(); c.drawRoundRect(cab, cab.width() * 0.04f, cab.width() * 0.04f, paint)
        paint.shader = LinearGradient(cab.left, 0f, cab.left + cab.width() * 0.08f, 0f, 0xFFCFC6AB.toInt(), 0xFFEDE6CF.toInt(), Shader.TileMode.CLAMP)
        c.drawRect(cab.left, cab.top + cab.height() * 0.02f, cab.left + cab.width() * 0.075f, cab.bottom - cab.height() * 0.02f, paint)
        paint.shader = null
        // top band with stripes
        paint.color = 0xFF151515.toInt(); c.drawRect(band, paint)
        val sh = band.height()
        paint.color = 0xFFE8E2D4.toInt(); c.drawRect(band.left, band.top + sh * 0.12f, band.right, band.top + sh * 0.2f, paint)
        paint.color = 0xFFB0302A.toInt(); c.drawRect(band.left, band.top + sh * 0.26f, band.right, band.top + sh * 0.74f, paint)
        paint.color = 0xFF5E1E1C.toInt()
        for (i in 0 until 3) c.drawRect(band.left, band.top + sh * (0.36f + i * 0.13f), band.right, band.top + sh * (0.42f + i * 0.13f), paint)
        paint.color = 0xFFE8E2D4.toInt(); c.drawRect(band.left, band.top + sh * 0.8f, band.right, band.top + sh * 0.88f, paint)
        // bezel and screen window
        paint.color = 0xFF1E1E1F.toInt(); c.drawRect(bezel, paint)
        paint.color = 0xFF2A2A2C.toInt()
        c.drawRoundRect(RectF(window).apply { inset(-bezel.width() * 0.05f, -bezel.width() * 0.04f) }, 18f, 18f, paint)
        paint.color = 0xFF060303.toInt(); c.drawRoundRect(window, 10f, 10f, paint)
        c.drawBitmap(frame, null, picture, smooth)
        // a faint reflection on the glass
        paint.shader = LinearGradient(window.left, window.top, window.right, window.bottom,
            intArrayOf(0x22FFFFFF, 0x00FFFFFF, 0x00FFFFFF, 0x10FFFFFF), floatArrayOf(0f, 0.35f, 0.75f, 1f), Shader.TileMode.CLAMP)
        c.drawRoundRect(window, 10f, 10f, paint)
        paint.shader = null
        // the cartridge slot area and cartridge
        paint.color = 0xFF202022.toInt(); c.drawRect(bezel.left, bezel.bottom, bezel.right, panel.top, paint)
        paint.color = 0xFF0E0E0F.toInt(); c.drawRect(slot.left - slot.width() * 0.1f, slot.bottom - slot.height() * 0.25f, slot.right + slot.width() * 0.1f, slot.bottom + slot.height() * 0.15f, paint)
        paint.color = 0xFF1A1A1B.toInt(); c.drawRoundRect(slot, 6f, 6f, paint)
        paint.color = 0xFFDCDCD8.toInt()
        c.drawRect(slot.left + slot.width() * 0.1f, slot.top + slot.height() * 0.35f, slot.right - slot.width() * 0.1f, slot.top + slot.height() * 0.7f, paint)
        paint.color = 0xFFB0302A.toInt()
        c.drawRect(slot.left + slot.width() * 0.25f, slot.top + slot.height() * 0.15f, slot.right - slot.width() * 0.25f, slot.top + slot.height() * 0.33f, paint)
        // control panel: black, with silver strips under the buttons and a red pinstripe
        paint.color = 0xFF19191A.toInt(); c.drawRect(panel, paint)
        paint.color = 0xFFB0302A.toInt(); c.drawRect(panel.left, panel.top + panel.height() * 0.1f, panel.right, panel.top + panel.height() * 0.115f, paint)
        paint.color = 0xFFD9DAD6.toInt()
        c.drawRect(panel.left, panel.top + panel.height() * 0.16f, panel.right, panel.top + panel.height() * 0.9f, paint)
        paint.color = 0xFF2C2C2E.toInt()
        for (first in intArrayOf(0, 4)) {
            val l = buttons[first].left; val t = buttons[first].top; val r = buttons[first + 3].right; val bt = buttons[first + 3].bottom
            val pad = buttons[0].width() * 0.35f
            c.drawRoundRect(l - pad, t - pad, r + pad, bt + pad, pad, pad, paint)
        }
        // joystick base and shaft
        val base = stickR * 1.25f
        paint.color = 0xFF111112.toInt(); c.drawRect(stick.x - base, stick.y - base, stick.x + base, stick.y + base, paint)
        stroke.color = 0xFF2E2E30.toInt(); stroke.strokeWidth = base * 0.06f
        for (i in 1..3) { val r = base * (1f - i * 0.18f); c.drawRect(stick.x - r, stick.y - r, stick.x + r, stick.y + r, stroke) }
        paint.color = 0xFF050505.toInt(); c.drawCircle(stick.x, stick.y, stickR * 0.42f, paint)
        var dx = 0f; var dy = 0f
        if (stickMask and 0x04 != 0) dx = -1f; if (stickMask and 0x08 != 0) dx = 1f
        if (stickMask and 0x01 != 0) dy = -1f; if (stickMask and 0x02 != 0) dy = 1f
        val tipX = stick.x + dx * stickR * 0.45f; val tipY = stick.y - stickR * 0.9f + dy * stickR * 0.35f
        stroke.color = 0xFF8C8F93.toInt(); stroke.strokeWidth = stickR * 0.34f; stroke.strokeCap = Paint.Cap.ROUND
        c.drawLine(stick.x, stick.y, tipX, tipY, stroke)
        stroke.color = 0xFFE4E6E8.toInt(); stroke.strokeWidth = stickR * 0.1f
        c.drawLine(stick.x - stickR * 0.06f, stick.y, tipX - stickR * 0.06f, tipY, stroke)
        stroke.strokeCap = Paint.Cap.BUTT
        paint.color = 0xFFC9CCD0.toInt(); c.drawOval(tipX - stickR * 0.17f, tipY - stickR * 0.08f, tipX + stickR * 0.17f, tipY + stickR * 0.08f, paint)
        // buttons
        text.textSize = buttons[0].height() * 0.5f
        for (i in 0 until 8) {
            val r = buttons[i]
            val down = mask and (0x10 shl (btnNum[i] - 1)) != 0
            paint.color = 0xFF8E8A7C.toInt(); c.drawRoundRect(r.left, r.top + r.height() * 0.08f, r.right, r.bottom + r.height() * 0.08f, r.width() * 0.16f, r.width() * 0.16f, paint)
            val o = if (down) r.height() * 0.06f else 0f
            paint.color = if (down) 0xFFD6D0BC.toInt() else 0xFFEDE7D3.toInt()
            c.drawRoundRect(r.left, r.top + o, r.right, r.bottom + o, r.width() * 0.16f, r.width() * 0.16f, paint)
            text.color = 0xFF3A3A3A.toInt()
            c.drawText(btnNum[i].toString(), r.centerX(), r.centerY() + o + text.textSize * 0.36f, text)
        }
        // sound switch
        text.textSize = muteR.height() * 0.55f; text.color = 0xFF202020.toInt()
        c.drawText("SOUND / OFF", muteR.centerX(), muteR.top - muteR.height() * 0.25f, text)
        paint.color = 0xFF0A0A0A.toInt(); c.drawRect(muteR, paint)
        paint.color = 0xFFD8352B.toInt()
        val kx = if (muted) muteR.right - muteR.width() * 0.32f else muteR.left + muteR.width() * 0.02f
        c.drawRect(kx, muteR.top - muteR.height() * 0.1f, kx + muteR.width() * 0.3f, muteR.bottom + muteR.height() * 0.1f, paint)
    }

    // ---- touch ----
    private fun stickFrom(x: Float, y: Float): Int {
        val dx = x - stick.x; val dy = y - (stick.y - stickR * 0.5f)
        if (hypot(dx, dy) < stickR * 0.3f) return 0
        return if (abs(dx) > abs(dy)) (if (dx < 0) 0x04 else 0x08) else (if (dy < 0) 0x01 else 0x02)
    }

    private fun buttonAt(x: Float, y: Float): Int {
        for (i in 0 until 8) {
            val r = RectF(buttons[i]).apply { inset(-width() * 0.12f, -height() * 0.12f) }
            if (r.contains(x, y)) return 0x10 shl (btnNum[i] - 1)
        }
        return 0
    }

    private fun update() {
        var m = stickMask
        for (v in btnPointers.values) m = m or v
        if (m != mask) {
            if (m and mask.inv() and 0xf0 != 0) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            mask = m
            onInput?.invoke(m)
            invalidate()
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex; val x = e.getX(i); val y = e.getY(i); val id = e.getPointerId(i)
                when {
                    band.contains(x, y) || (y < band.bottom) -> onMenu?.invoke()
                    RectF(muteR).apply { inset(-width() * 0.4f, -height() * 1.2f) }.contains(x, y) -> {
                        muted = !muted; onMute?.invoke(muted)
                        performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
                    }
                    buttonAt(x, y) != 0 -> { btnPointers[id] = buttonAt(x, y); update() }
                    hypot(x - stick.x, y - stick.y) < stickR * 2.6f -> {
                        stickPointer = id; stickPos.set(x, y); stickMask = stickFrom(x, y); update()
                    }
                }
            }
            MotionEvent.ACTION_MOVE -> {
                for (k in 0 until e.pointerCount) {
                    if (e.getPointerId(k) == stickPointer) { stickMask = stickFrom(e.getX(k), e.getY(k)); update() }
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val id = e.getPointerId(e.actionIndex)
                if (id == stickPointer) { stickPointer = -1; stickMask = 0 }
                btnPointers.remove(id)
                update()
            }
            MotionEvent.ACTION_CANCEL -> { stickPointer = -1; stickMask = 0; btnPointers.clear(); update() }
        }
        return true
    }
}
