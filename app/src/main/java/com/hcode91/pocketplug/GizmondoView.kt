package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import kotlin.math.hypot

/** Input bits of the Gizmondo core (gizmondo.h). */
object GizBits {
    const val UP = 0x001; const val DOWN = 0x002; const val LEFT = 0x004; const val RIGHT = 0x008
    const val PLAY = 0x010; const val FORWARD = 0x020; const val REWIND = 0x040; const val STOP = 0x080
    const val L = 0x100; const val R = 0x200
    const val F1 = 0x400; const val F2 = 0x800; const val F3 = 0x1000; const val F4 = 0x2000; const val F5 = 0x4000

    fun keyBit(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> UP
        KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> DOWN
        KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> RIGHT
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_X, KeyEvent.KEYCODE_ENTER -> PLAY
        KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_C -> FORWARD
        KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_Z -> REWIND
        KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_V, KeyEvent.KEYCODE_ESCAPE -> STOP
        KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_Q -> L
        KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_E -> R
        KeyEvent.KEYCODE_F1, KeyEvent.KEYCODE_1, KeyEvent.KEYCODE_BUTTON_SELECT -> F1
        KeyEvent.KEYCODE_F2, KeyEvent.KEYCODE_2 -> F2
        KeyEvent.KEYCODE_F3, KeyEvent.KEYCODE_3, KeyEvent.KEYCODE_BUTTON_START -> F3
        KeyEvent.KEYCODE_F4, KeyEvent.KEYCODE_4 -> F4
        KeyEvent.KEYCODE_F5, KeyEvent.KEYCODE_5 -> F5
        else -> 0
    }
}

/**
 * The Gizmondo: a charcoal, rounded handheld held sideways. The 320x240 screen
 * sits in a silver-grey bezel in the middle with five slim silver keys standing
 * up from its top edge (F1-F5). Silver knob-like L and R shoulder buttons sit on
 * the top corners, a big brushed-silver disc (the D-pad) in a dark ring on the
 * left, and four round silver buttons in a diamond on the right: stop at the top,
 * rewind and fast-forward at the sides, play at the bottom, with a speaker grille
 * below them. No maker's name or logo is drawn. The emulator's menu opens from
 * the small round key above the D-pad.
 */
class GizmondoView(ctx: Context) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    private val frame = Bitmap.createBitmap(320, 240, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(320 * 240)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val smooth = Paint(Paint.FILTER_BITMAP_FLAG)

    private val body = RectF()
    private val bezel = RectF()
    private val screen = RectF()
    private val fKeys = Array(5) { RectF() }
    private val shoulderL = PointF(); private val shoulderR = PointF(); private var shoulderRad = 0f
    private val dpad = PointF(); private var dpadR = 0f
    private val btn = Array(4) { PointF() }     // stop, rewind, forward, play
    private var btnR = 0f
    private val speaker = PointF()
    private val menuKey = PointF(); private var menuR = 0f

    private val pointers = HashMap<Int, Int>()
    private var touchMask = 0

    private val btnBits = intArrayOf(GizBits.STOP, GizBits.REWIND, GizBits.FORWARD, GizBits.PLAY)
    private val fBits = intArrayOf(GizBits.F1, GizBits.F2, GizBits.F3, GizBits.F4, GizBits.F5)

    fun updateFrame(px: IntArray) {
        if (px.size < framePx.size) return
        System.arraycopy(px, 0, framePx, 0, framePx.size)
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        // the body is about 2.1 times as wide as it is tall
        var bw = w * 0.98f; var bh = bw / 2.1f
        if (bh > h * 0.86f) { bh = h * 0.86f; bw = bh * 2.1f }
        val l = (w - bw) / 2; val t = (h - bh) / 2 + bh * 0.06f
        body.set(l, t, l + bw, t + bh)
        val sh = bh * 0.58f; val sw = sh * 4f / 3f
        val cx = body.centerX() - bw * 0.01f
        screen.set(cx - sw / 2, t + bh * 0.14f, cx + sw / 2, t + bh * 0.14f + sh)
        bezel.set(screen.left - bh * 0.06f, screen.top - bh * 0.07f, screen.right + bh * 0.06f, screen.bottom + bh * 0.2f)
        val kw = bezel.width() * 0.07f; val gap = (bezel.width() - 5 * kw) / 6
        for (i in 0 until 5) {
            val kl = bezel.left + gap + i * (kw + gap)
            fKeys[i].set(kl, bezel.top - bh * 0.13f, kl + kw, bezel.top + bh * 0.03f)
        }
        shoulderRad = bh * 0.075f
        shoulderL.set(l + bw * 0.12f, t + bh * 0.02f); shoulderR.set(l + bw * 0.88f, t + bh * 0.02f)
        dpadR = bh * 0.17f; dpad.set(l + bw * 0.13f, t + bh * 0.5f)
        menuR = bh * 0.035f; menuKey.set(l + bw * 0.13f, t + bh * 0.2f)
        btnR = bh * 0.072f
        val bx = l + bw * 0.865f; val byc = t + bh * 0.47f; val d = bh * 0.155f
        btn[0].set(bx, byc - d); btn[1].set(bx - d, byc); btn[2].set(bx + d, byc); btn[3].set(bx, byc + d)
        speaker.set(l + bw * 0.905f, t + bh * 0.83f)
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF15161A.toInt())
        val bh = body.height()
        val held = touchMask or externalMask

        // shoulder knobs behind the top corners
        drawKnob(c, shoulderL, held and GizBits.L != 0)
        drawKnob(c, shoulderR, held and GizBits.R != 0)
        // F1-F5: slim silver keys standing up behind the bezel's top edge
        for (i in 0 until 5) drawFKey(c, fKeys[i], i, held and fBits[i] != 0)

        // body: charcoal, a rounded rectangle with fuller grips
        paint.color = 0x66000000
        c.drawRoundRect(body.left + bh * 0.02f, body.top + bh * 0.03f, body.right + bh * 0.02f, body.bottom + bh * 0.03f, bh * 0.42f, bh * 0.42f, paint)
        paint.shader = LinearGradient(0f, body.top, 0f, body.bottom, 0xFF45474B.toInt(), 0xFF26272A.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, bh * 0.42f, bh * 0.42f, paint)
        paint.shader = null
        stroke.color = 0xFF55575C.toInt(); stroke.strokeWidth = bh * 0.006f
        c.drawRoundRect(body, bh * 0.42f, bh * 0.42f, stroke)

        // silver-grey bezel and picture
        paint.shader = LinearGradient(0f, bezel.top, 0f, bezel.bottom, 0xFFA7A9AC.toInt(), 0xFF7C7E82.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(bezel, bh * 0.05f, bh * 0.05f, paint)
        paint.shader = null
        stroke.color = 0xFFC4C6C9.toInt(); stroke.strokeWidth = bh * 0.005f
        c.drawRoundRect(bezel, bh * 0.05f, bh * 0.05f, stroke)
        paint.color = 0xFF0B0C0E.toInt()
        c.drawRect(screen.left - bh * 0.012f, screen.top - bh * 0.012f, screen.right + bh * 0.012f, screen.bottom + bh * 0.012f, paint)
        frame.setPixels(framePx, 0, 320, 0, 0, 320, 240)
        c.drawBitmap(frame, null, screen, smooth)

        // menu key and D-pad on the left grip
        drawSilverButton(c, menuKey, menuR, false)
        stroke.color = 0xFF55575B.toInt(); stroke.strokeWidth = menuR * 0.14f
        c.drawArc(menuKey.x - menuR * 0.5f, menuKey.y - menuR * 0.5f, menuKey.x + menuR * 0.5f, menuKey.y + menuR * 0.5f, -60f, 300f, false, stroke)
        c.drawLine(menuKey.x, menuKey.y - menuR * 0.65f, menuKey.x, menuKey.y - menuR * 0.1f, stroke)
        drawDpad(c, held)

        // the four round buttons and the speaker on the right grip
        for (i in 0 until 4) {
            drawSilverButton(c, btn[i], btnR, held and btnBits[i] != 0)
            drawSymbol(c, btn[i], btnR, i)
        }
        paint.color = 0xFF16171A.toInt()
        val sp = bh * 0.022f
        for (r in -2..2) for (q in -2..2) if (r * r + q * q <= 6) c.drawCircle(speaker.x + q * sp, speaker.y + r * sp, sp * 0.3f, paint)
    }

    private fun drawKnob(c: Canvas, p: PointF, down: Boolean) {
        val r = shoulderRad
        val y = p.y + (if (down) r * 0.15f else 0f)
        paint.shader = LinearGradient(p.x - r, 0f, p.x + r, 0f,
            intArrayOf(0xFF8E9094.toInt(), 0xFFECEDEF.toInt(), 0xFF9C9EA2.toInt(), 0xFF6E7074.toInt()), null, Shader.TileMode.CLAMP)
        c.drawRoundRect(p.x - r, y - r * 0.9f, p.x + r, y + r * 0.6f, r * 0.45f, r * 0.45f, paint)
        paint.shader = null
        paint.color = if (down) 0xFFB8BABD.toInt() else 0xFFD9DADC.toInt()
        c.drawOval(p.x - r * 0.95f, y - r * 1.15f, p.x + r * 0.95f, y - r * 0.55f, paint)
    }

    private fun drawFKey(c: Canvas, r: RectF, i: Int, down: Boolean) {
        val dy = if (down) r.height() * 0.12f else 0f
        paint.shader = LinearGradient(r.left, 0f, r.right, 0f,
            intArrayOf(0xFF8A8C90.toInt(), 0xFFF1F2F3.toInt(), 0xFF9A9CA0.toInt()), null, Shader.TileMode.CLAMP)
        c.drawRoundRect(r.left, r.top + dy, r.right, r.bottom, r.width() * 0.45f, r.width() * 0.45f, paint)
        paint.shader = null
        // the key's icon near its top: home, back, star, triangle, power
        stroke.color = 0xFF5D5F63.toInt(); stroke.strokeWidth = r.width() * 0.09f
        val cx = r.centerX(); val cy = r.top + dy + r.height() * 0.22f; val s = r.width() * 0.26f
        when (i) {
            0 -> { val p = Path(); p.moveTo(cx - s, cy + s * 0.1f); p.lineTo(cx, cy - s); p.lineTo(cx + s, cy + s * 0.1f); p.moveTo(cx - s * 0.7f, cy); p.lineTo(cx - s * 0.7f, cy + s); p.lineTo(cx + s * 0.7f, cy + s); p.lineTo(cx + s * 0.7f, cy); c.drawPath(p, stroke) }
            1 -> { val p = Path(); p.moveTo(cx + s * 0.6f, cy - s); p.lineTo(cx - s * 0.6f, cy); p.lineTo(cx + s * 0.6f, cy + s); p.close(); c.drawPath(p, stroke) }
            2 -> { for (k in 0 until 3) { val a = Math.PI * k / 3; c.drawLine(cx - (s * Math.cos(a)).toFloat(), cy - (s * Math.sin(a)).toFloat(), cx + (s * Math.cos(a)).toFloat(), cy + (s * Math.sin(a)).toFloat(), stroke) } }
            3 -> { val p = Path(); p.moveTo(cx, cy - s); p.lineTo(cx - s, cy + s * 0.7f); p.lineTo(cx + s, cy + s * 0.7f); p.close(); c.drawPath(p, stroke) }
            else -> { c.drawArc(cx - s, cy - s, cx + s, cy + s, -60f, 300f, false, stroke); c.drawLine(cx, cy - s * 1.2f, cx, cy - s * 0.2f, stroke) }
        }
    }

    private fun drawSilverButton(c: Canvas, p: PointF, rad: Float, down: Boolean) {
        paint.color = 0xFF1A1B1E.toInt(); c.drawCircle(p.x, p.y, rad * 1.14f, paint)
        paint.shader = RadialGradient(p.x - rad * 0.35f, p.y - rad * 0.4f, rad * 1.6f,
            if (down) 0xFFC3C5C8.toInt() else 0xFFFBFBFC.toInt(), if (down) 0xFF86888C.toInt() else 0xFFA9ABAF.toInt(), Shader.TileMode.CLAMP)
        c.drawCircle(p.x, p.y + (if (down) rad * 0.04f else 0f), rad, paint)
        paint.shader = null
    }

    private fun drawSymbol(c: Canvas, p: PointF, rad: Float, i: Int) {
        paint.color = 0xFF2B2C30.toInt()
        val s = rad * 0.32f
        when (i) {
            0 -> c.drawRect(p.x - s, p.y - s, p.x + s, p.y + s, paint)
            3 -> { val q = Path(); q.moveTo(p.x - s * 0.8f, p.y - s * 1.1f); q.lineTo(p.x + s * 1.1f, p.y); q.lineTo(p.x - s * 0.8f, p.y + s * 1.1f); q.close(); c.drawPath(q, paint) }
            else -> {
                val dir = if (i == 2) 1f else -1f
                for (k in 0..1) {
                    val ox = p.x + dir * (k - 0.5f) * s * 1.1f
                    val q = Path(); q.moveTo(ox - dir * s * 0.6f, p.y - s); q.lineTo(ox + dir * s * 0.6f, p.y); q.lineTo(ox - dir * s * 0.6f, p.y + s); q.close()
                    c.drawPath(q, paint)
                }
            }
        }
    }

    private fun drawDpad(c: Canvas, held: Int) {
        // dark ring, then a brushed-silver disc that tilts towards the pressed side
        paint.color = 0xFF1C1D20.toInt(); c.drawCircle(dpad.x, dpad.y, dpadR * 1.2f, paint)
        stroke.color = 0xFF505257.toInt(); stroke.strokeWidth = dpadR * 0.04f
        c.drawCircle(dpad.x, dpad.y, dpadR * 1.2f, stroke)
        var ox = 0f; var oy = 0f
        if (held and GizBits.LEFT != 0) ox -= 1f
        if (held and GizBits.RIGHT != 0) ox += 1f
        if (held and GizBits.UP != 0) oy -= 1f
        if (held and GizBits.DOWN != 0) oy += 1f
        val cx = dpad.x + ox * dpadR * 0.06f; val cy = dpad.y + oy * dpadR * 0.06f
        paint.shader = RadialGradient(cx - dpadR * 0.3f, cy - dpadR * 0.35f, dpadR * 1.5f, 0xFFF4F5F6.toInt(), 0xFF9C9EA2.toInt(), Shader.TileMode.CLAMP)
        c.drawCircle(cx, cy, dpadR, paint)
        paint.shader = null
        stroke.color = 0x22000000; stroke.strokeWidth = dpadR * 0.012f
        var rr = dpadR * 0.15f
        while (rr < dpadR * 0.95f) { c.drawCircle(cx, cy, rr, stroke); rr += dpadR * 0.07f }
        paint.color = 0x55FFFFFF
        c.drawCircle(cx - dpadR * 0.35f, cy - dpadR * 0.4f, dpadR * 0.18f, paint)
    }

    private fun hit(x: Float, y: Float): Int {
        val dx = x - dpad.x; val dy = y - dpad.y
        if (hypot(dx, dy) < dpadR * 1.5f) {
            var b = 0
            if (hypot(dx, dy) < dpadR * 0.18f) return 0
            if (dx > dpadR * 0.35f) b = b or GizBits.RIGHT
            if (dx < -dpadR * 0.35f) b = b or GizBits.LEFT
            if (dy > dpadR * 0.35f) b = b or GizBits.DOWN
            if (dy < -dpadR * 0.35f) b = b or GizBits.UP
            return b
        }
        for (i in 0 until 4) if (hypot(x - btn[i].x, y - btn[i].y) < btnR * 1.35f) return btnBits[i]
        val bh = body.height()
        for (i in 0 until 5) {
            val r = fKeys[i]
            if (x > r.left - bh * 0.02f && x < r.right + bh * 0.02f && y > r.top - bh * 0.06f && y < r.bottom + bh * 0.02f) return fBits[i]
        }
        if (hypot(x - shoulderL.x, y - shoulderL.y) < shoulderRad * 1.8f) return GizBits.L
        if (hypot(x - shoulderR.x, y - shoulderR.y) < shoulderRad * 1.8f) return GizBits.R
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
                if (e.actionMasked == MotionEvent.ACTION_DOWN && hypot(x - menuKey.x, y - menuKey.y) < menuR * 2.2f) { onMenu?.invoke(); return true }
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
