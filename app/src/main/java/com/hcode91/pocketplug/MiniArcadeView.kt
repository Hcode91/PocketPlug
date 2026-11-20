package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import kotlin.math.*

/**
 * The Mini Arcade Console 10-in-1 (Fizz Creations), drawn from a photo of its
 * family: a black rounded-rectangle handheld, the colour screen in a raised
 * black bezel at the top, and a starfield control panel below with big red
 * round buttons (here the four directions and A / B), the SOUND and START
 * pills either side of the speaker slots, and a small round RESET. No logos or
 * game art. Tap the bezel above the screen for the menu.
 *
 * Input bits (arcade10 core): Up 0x01, Down 0x02, Left 0x04, Right 0x08, A 0x10, B 0x20,
 * START 0x40, SOUND 0x80.
 */
class MiniArcadeView(ctx: Context) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var onReset: (() -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    companion object {
        const val UP = 0x01; const val DOWN = 0x02; const val LEFT = 0x04; const val RIGHT = 0x08
        const val A = 0x10; const val B = 0x20; const val START = 0x40; const val SOUND = 0x80
        fun keyBit(code: Int): Int = when (code) {
            KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> UP
            KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> DOWN
            KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> LEFT
            KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> RIGHT
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_Z -> A
            KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_X -> B
            KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_ENTER -> START
            KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_M -> SOUND
            else -> 0
        }
    }

    private val frame = Bitmap.createBitmap(160, 128, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(160 * 128)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }
    private val pixelPaint = Paint().apply { isFilterBitmap = false }

    private val body = RectF()
    private val bezel = RectF()
    private val screen = RectF()
    private val panel = RectF()
    private class Btn(val bit: Int, val label: String) { val c = PointF(); var r = 0f }
    private val buttons = listOf(Btn(UP, "▲"), Btn(DOWN, "▼"), Btn(LEFT, "◀"), Btn(RIGHT, "▶"), Btn(B, "B"), Btn(A, "A"))
    private val sound = RectF()
    private val start = RectF()
    private val reset = PointF(); private var resetR = 0f
    private var resetDown = false
    private var stars: List<Triple<Float, Float, Float>> = emptyList()

    private val pointers = HashMap<Int, Int>()
    private var touchMask = 0

    fun updateFrame(px: IntArray) {
        if (px.size < 160 * 128) return
        System.arraycopy(px, 0, framePx, 0, 160 * 128)
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        var bw = w * 0.94f
        var bh = bw / 0.64f
        if (bh > h * 0.97f) { bh = h * 0.97f; bw = bh * 0.64f }
        val l = (w - bw) / 2; val t = (h - bh) / 2
        body.set(l, t, l + bw, t + bh)
        bezel.set(l + bw * 0.06f, t + bh * 0.04f, l + bw * 0.94f, t + bh * 0.48f)
        // 160 x 128 screen (5:4) centred in the bezel
        val sw = bezel.width() * 0.82f; val sh = sw * 128f / 160f
        val scx = bezel.centerX(); val scy = bezel.top + bezel.height() * 0.5f
        screen.set(scx - sw / 2, scy - sh / 2, scx + sw / 2, scy + sh / 2)
        panel.set(l + bw * 0.04f, t + bh * 0.51f, l + bw * 0.96f, t + bh * 0.975f)
        val r = bw * 0.085f
        // directions as a cross on the left, B and A on the right
        val dx = panel.left + panel.width() * 0.26f; val dy = panel.top + panel.height() * 0.33f
        val off = r * 2.05f
        buttons[0].apply { c.set(dx, dy - off); this.r = r }
        buttons[1].apply { c.set(dx, dy + off); this.r = r }
        buttons[2].apply { c.set(dx - off, dy); this.r = r }
        buttons[3].apply { c.set(dx + off, dy); this.r = r }
        val ax = panel.left + panel.width() * 0.72f
        buttons[4].apply { c.set(ax - r * 1.25f, dy + r * 0.7f); this.r = r * 1.05f }
        buttons[5].apply { c.set(ax + r * 1.45f, dy - r * 0.7f); this.r = r * 1.05f }
        val py = panel.top + panel.height() * 0.76f
        val pw = bw * 0.12f; val ph = bw * 0.045f
        sound.set(panel.left + panel.width() * 0.2f - pw / 2, py - ph / 2, panel.left + panel.width() * 0.2f + pw / 2, py + ph / 2)
        start.set(panel.right - panel.width() * 0.2f - pw / 2, py - ph / 2, panel.right - panel.width() * 0.2f + pw / 2, py + ph / 2)
        reset.set(panel.centerX(), panel.top + panel.height() * 0.9f); resetR = bw * 0.03f
        val rnd = java.util.Random(7)
        stars = List(220) { Triple(panel.left + rnd.nextFloat() * panel.width(), panel.top + rnd.nextFloat() * panel.height(), 0.6f + rnd.nextFloat() * 1.8f) }
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0814.toInt())
        val held = touchMask or externalMask
        // shell
        paint.shader = LinearGradient(body.left, 0f, body.right, 0f, intArrayOf(0xFF1A1A1C.toInt(), 0xFF2A2B2E.toInt(), 0xFF151517.toInt()),
            floatArrayOf(0f, 0.5f, 1f), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, body.width() * 0.08f, body.width() * 0.08f, paint)
        paint.shader = null
        // raised bezel and screen
        paint.color = 0xFF202022.toInt()
        c.drawRoundRect(bezel, bezel.width() * 0.04f, bezel.width() * 0.04f, paint)
        paint.color = 0xFF0C0C0D.toInt()
        c.drawRect(screen.left - 6, screen.top - 6, screen.right + 6, screen.bottom + 6, paint)
        frame.setPixels(framePx, 0, 160, 0, 0, 160, 128)
        c.drawBitmap(frame, null, screen, pixelPaint)
        // starfield panel
        paint.color = 0xFF050508.toInt()
        c.drawRoundRect(panel, panel.width() * 0.05f, panel.width() * 0.05f, paint)
        c.save(); c.clipRect(panel)
        for ((x, y, r) in stars) {
            paint.color = if (r > 2f) 0xFF8FB6FF.toInt() else 0xFFE8E8F0.toInt()
            c.drawCircle(x, y, r, paint)
        }
        c.restore()
        // buttons
        for (b in buttons) {
            val down = (held and b.bit) != 0
            paint.color = 0xFF2A0703.toInt()
            c.drawCircle(b.c.x, b.c.y + b.r * 0.12f, b.r * 1.08f, paint)
            paint.shader = RadialGradient(b.c.x - b.r * 0.3f, b.c.y - b.r * 0.35f, b.r * 1.4f,
                if (down) 0xFFC2341B.toInt() else 0xFFFF6A45.toInt(), if (down) 0xFF7E1608.toInt() else 0xFFD8381C.toInt(), Shader.TileMode.CLAMP)
            c.drawCircle(b.c.x, b.c.y + if (down) b.r * 0.06f else 0f, b.r, paint)
            paint.shader = null
            text.color = 0xFFF2F2F2.toInt(); text.textSize = b.r * 0.55f
            val ly = if (b.bit == A || b.bit == B) b.c.y - b.r * 1.25f else b.c.y + text.textSize * 0.36f
            if (b.bit == A || b.bit == B) c.drawText(b.label, b.c.x, ly, text)
            else { text.color = 0x99FFFFFF.toInt(); c.drawText(b.label, b.c.x, ly, text) }
        }
        // pills and speaker slots
        for ((rect, bitv, label) in listOf(Triple(sound, SOUND, "SOUND"), Triple(start, START, "START"))) {
            val down = (held and bitv) != 0
            paint.color = if (down) 0xFFA8250F.toInt() else 0xFFE44A28.toInt()
            c.drawRoundRect(rect, rect.height() / 2, rect.height() / 2, paint)
            text.color = 0xFFF2F2F2.toInt(); text.textSize = rect.height() * 0.85f
            c.drawText(label, rect.centerX(), rect.top - rect.height() * 0.45f, text)
        }
        paint.color = 0xFF111113.toInt()
        val gw = panel.width() * 0.14f; val gy = panel.top + panel.height() * 0.7f
        c.drawRoundRect(panel.centerX() - gw / 2, gy, panel.centerX() + gw / 2, gy + panel.height() * 0.13f, 6f, 6f, paint)
        paint.color = 0xFF2E2F33.toInt()
        for (i in 0 until 5) { val y = gy + panel.height() * (0.02f + i * 0.023f); c.drawRect(panel.centerX() - gw * 0.42f, y, panel.centerX() + gw * 0.42f, y + panel.height() * 0.01f, paint) }
        paint.color = if (resetDown) 0xFFA8250F.toInt() else 0xFFE44A28.toInt()
        c.drawCircle(reset.x, reset.y, resetR, paint)
        text.color = 0xFFF2F2F2.toInt(); text.textSize = resetR * 1.1f
        c.drawText("RESET", reset.x, reset.y - resetR * 1.5f, text)
    }

    private fun hit(x: Float, y: Float): Int {
        for (b in buttons) if (hypot(x - b.c.x, y - b.c.y) < b.r * 1.15f) return b.bit
        if (RectF(sound).apply { inset(-sound.width() * 0.3f, -sound.height() * 1.2f) }.contains(x, y)) return SOUND
        if (RectF(start).apply { inset(-start.width() * 0.3f, -start.height() * 1.2f) }.contains(x, y)) return START
        return 0
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                val x = e.getX(i); val y = e.getY(i)
                if (e.actionMasked == MotionEvent.ACTION_DOWN && y < screen.top && bezel.contains(x, y)) { onMenu?.invoke(); return true }
                if (hypot(x - reset.x, y - reset.y) < resetR * 2.5f) {
                    resetDown = true; performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); onReset?.invoke(); invalidate(); return true
                }
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
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> { pointers.remove(e.getPointerId(e.actionIndex)); resetDown = false }
            MotionEvent.ACTION_CANCEL -> { pointers.clear(); resetDown = false }
        }
        var m = 0
        for (v in pointers.values) m = m or v
        if (m != touchMask) { touchMask = m; onInput?.invoke(m) }
        invalidate()
        return true
    }
}
