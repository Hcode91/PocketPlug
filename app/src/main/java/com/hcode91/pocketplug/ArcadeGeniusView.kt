package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import kotlin.math.hypot

/** Input bits of the Arcade Genius core (gpl32.cpp). */
object GeniusBits {
    const val UP = 0x001; const val DOWN = 0x002; const val LEFT = 0x004; const val RIGHT = 0x008
    const val A = 0x010; const val B = 0x020; const val L = 0x040; const val R = 0x080
    const val MENU = 0x100; const val VOL_UP = 0x200; const val VOL_DOWN = 0x400

    fun keyBit(code: Int): Int = when (code) {
        KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> UP
        KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> DOWN
        KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> RIGHT
        KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_X, KeyEvent.KEYCODE_ENTER -> A
        KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_Z, KeyEvent.KEYCODE_ESCAPE -> B
        KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_Q -> L
        KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_E -> R
        KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_M -> MENU
        KeyEvent.KEYCODE_EQUALS, KeyEvent.KEYCODE_PLUS -> VOL_UP
        KeyEvent.KEYCODE_MINUS -> VOL_DOWN
        else -> 0
    }
}

/**
 * The Arcade Genius: a white tablet-style handheld held in both hands, the
 * 320x240 screen in a black glass panel in the middle, a camera in a raised
 * block on the top edge, a grey cross-shaped D-pad on the left grip with the
 * round power key above it, round grey A and B keys on the right grip under
 * the speaker, L and R on the top corners, and a row along the bottom edge:
 * VOL- and VOL+ on the left, the rounded MENU key in the middle and a red
 * light on the right. Drawn for the phone held sideways; no maker's name is
 * shown. The power key opens the emulator's menu.
 */
class ArcadeGeniusView(ctx: Context) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    private val frame = Bitmap.createBitmap(320, 240, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(320 * 240)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }
    private val smooth = Paint(Paint.FILTER_BITMAP_FLAG)

    private val body = RectF()
    private val glass = RectF()
    private val screen = RectF()
    private val camera = RectF()
    private val shoulderL = RectF(); private val shoulderR = RectF()
    private val menuKey = RectF()
    private val dpad = PointF(); private var dpadR = 0f
    private val btnA = PointF(); private val btnB = PointF(); private var abR = 0f
    private val power = PointF(); private val speaker = PointF(); private var roundR = 0f
    private val volDown = PointF(); private val volUp = PointF(); private var smallR = 0f
    private val led = PointF()

    private val pointers = HashMap<Int, Int>()
    private var touchMask = 0

    fun updateFrame(px: IntArray) {
        if (px.size < framePx.size) return
        System.arraycopy(px, 0, framePx, 0, framePx.size)
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        // the body is about 2.15 times as wide as it is tall
        var bw = w * 0.98f; var bh = bw / 2.15f
        if (bh > h * 0.9f) { bh = h * 0.9f; bw = bh * 2.15f }
        val l = (w - bw) / 2; val t = (h - bh) / 2 + bh * 0.04f
        body.set(l, t, l + bw, t + bh)
        val sh = bh * 0.66f; val sw = sh * 4f / 3f
        screen.set(body.centerX() - sw / 2, t + bh * 0.13f, body.centerX() + sw / 2, t + bh * 0.13f + sh)
        glass.set(screen.left - bh * 0.045f, screen.top - bh * 0.04f, screen.right + bh * 0.045f, screen.bottom + bh * 0.04f)
        camera.set(body.centerX() + sw * 0.1f, t - bh * 0.075f, body.centerX() + sw * 0.42f, t + bh * 0.03f)
        shoulderL.set(l + bw * 0.06f, t - bh * 0.035f, l + bw * 0.2f, t + bh * 0.04f)
        shoulderR.set(l + bw * 0.8f, t - bh * 0.035f, l + bw * 0.94f, t + bh * 0.04f)
        dpadR = bh * 0.15f; dpad.set(l + bw * 0.115f, t + bh * 0.55f)
        roundR = bh * 0.06f
        power.set(l + bw * 0.115f, t + bh * 0.2f); speaker.set(l + bw * 0.885f, t + bh * 0.2f)
        abR = bh * 0.06f
        btnB.set(l + bw * 0.915f, t + bh * 0.44f); btnA.set(l + bw * 0.855f, t + bh * 0.6f)
        smallR = bh * 0.03f
        val by = t + bh * 0.9f
        volDown.set(l + bw * 0.33f, by); volUp.set(l + bw * 0.39f, by)
        menuKey.set(body.centerX() - bh * 0.045f, by - bh * 0.035f, body.centerX() + bh * 0.045f, by + bh * 0.035f)
        led.set(l + bw * 0.72f, by)
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF17181C.toInt())
        val bh = body.height()
        val held = touchMask or externalMask

        // shoulder keys behind the top edge
        drawShoulder(c, shoulderL, held and GeniusBits.L != 0, "L")
        drawShoulder(c, shoulderR, held and GeniusBits.R != 0, "R")
        // camera block
        paint.shader = LinearGradient(0f, camera.top, 0f, camera.bottom, 0xFFF4F4F2.toInt(), 0xFFD9DAD8.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(camera, bh * 0.025f, bh * 0.025f, paint)
        paint.shader = null
        stroke.color = 0xFFBFC1C3.toInt(); stroke.strokeWidth = bh * 0.005f
        c.drawRoundRect(camera, bh * 0.025f, bh * 0.025f, stroke)
        paint.color = 0xFF1C1D22.toInt(); c.drawCircle(camera.centerX(), camera.centerY() - bh * 0.01f, bh * 0.025f, paint)
        paint.color = 0xFF3A4D6B.toInt(); c.drawCircle(camera.centerX(), camera.centerY() - bh * 0.01f, bh * 0.012f, paint)

        // body: a wide rounded slab with fuller grips at the ends
        paint.color = 0x66000000
        c.drawRoundRect(body.left + bh * 0.02f, body.top + bh * 0.03f, body.right + bh * 0.02f, body.bottom + bh * 0.03f, bh * 0.3f, bh * 0.3f, paint)
        paint.shader = LinearGradient(0f, body.top, 0f, body.bottom, 0xFFFBFBFA.toInt(), 0xFFDDDEDC.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, bh * 0.3f, bh * 0.3f, paint)
        paint.shader = null
        stroke.color = 0xFFC3C5C7.toInt(); stroke.strokeWidth = bh * 0.007f
        c.drawRoundRect(body, bh * 0.3f, bh * 0.3f, stroke)
        // grip seams
        stroke.color = 0xFFD2D3D3.toInt(); stroke.strokeWidth = bh * 0.004f
        c.drawLine(glass.left - bh * 0.07f, body.top + bh * 0.08f, glass.left - bh * 0.07f, body.bottom - bh * 0.08f, stroke)
        c.drawLine(glass.right + bh * 0.07f, body.top + bh * 0.08f, glass.right + bh * 0.07f, body.bottom - bh * 0.08f, stroke)

        // black glass panel and picture
        paint.color = 0xFF0D0E11.toInt()
        c.drawRoundRect(glass, bh * 0.03f, bh * 0.03f, paint)
        paint.shader = LinearGradient(glass.left, glass.top, glass.right, glass.bottom, 0x22FFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP)
        c.drawRoundRect(glass, bh * 0.03f, bh * 0.03f, paint)
        paint.shader = null
        frame.setPixels(framePx, 0, 320, 0, 0, 320, 240)
        c.drawBitmap(frame, null, screen, smooth)

        // left grip: power key and D-pad
        drawRoundKey(c, power, roundR, false)
        stroke.color = 0xFF8E9196.toInt(); stroke.strokeWidth = roundR * 0.12f
        for (k in -1..1) c.drawLine(power.x - roundR * 0.4f, power.y + k * roundR * 0.3f, power.x + roundR * 0.4f, power.y + k * roundR * 0.3f, stroke)
        drawDpad(c, held)
        // right grip: speaker, B and A
        paint.color = 0xFFCDCECF.toInt(); c.drawCircle(speaker.x, speaker.y, roundR * 1.25f, paint)
        paint.color = 0xFFEDEDEC.toInt(); c.drawCircle(speaker.x, speaker.y, roundR * 1.05f, paint)
        paint.color = 0xFF9FA2A6.toInt()
        for (ring in 0..2) {
            val rr = roundR * (0.25f + ring * 0.3f)
            val n = if (ring == 0) 1 else ring * 6
            for (i in 0 until n) {
                val a = Math.PI * 2 * i / n
                c.drawCircle(speaker.x + (rr * Math.cos(a)).toFloat() * (if (ring == 0) 0f else 1f), speaker.y + (rr * Math.sin(a)).toFloat() * (if (ring == 0) 0f else 1f), roundR * 0.07f, paint)
            }
        }
        drawRoundKey(c, btnB, abR, held and GeniusBits.B != 0, "B")
        drawRoundKey(c, btnA, abR, held and GeniusBits.A != 0, "A")

        // bottom row
        drawRoundKey(c, volDown, smallR, held and GeniusBits.VOL_DOWN != 0)
        drawRoundKey(c, volUp, smallR, held and GeniusBits.VOL_UP != 0)
        stroke.color = 0xFF8E9196.toInt(); stroke.strokeWidth = smallR * 0.16f
        c.drawLine(volDown.x - smallR * 0.45f, volDown.y, volDown.x + smallR * 0.45f, volDown.y, stroke)
        c.drawLine(volUp.x - smallR * 0.45f, volUp.y, volUp.x + smallR * 0.45f, volUp.y, stroke)
        c.drawLine(volUp.x, volUp.y - smallR * 0.45f, volUp.x, volUp.y + smallR * 0.45f, stroke)
        val menuDown = held and GeniusBits.MENU != 0
        paint.color = 0xFFC4C6C8.toInt()
        c.drawRoundRect(menuKey.left - bh * 0.008f, menuKey.top - bh * 0.008f, menuKey.right + bh * 0.008f, menuKey.bottom + bh * 0.008f, bh * 0.025f, bh * 0.025f, paint)
        paint.color = if (menuDown) 0xFFD8D9DA.toInt() else 0xFFF3F3F2.toInt()
        c.drawRoundRect(menuKey, bh * 0.02f, bh * 0.02f, paint)
        stroke.color = 0xFF8E9196.toInt(); stroke.strokeWidth = bh * 0.004f
        val ins = menuKey.height() * 0.3f
        c.drawRoundRect(menuKey.left + ins, menuKey.top + ins, menuKey.right - ins, menuKey.bottom - ins, ins * 0.4f, ins * 0.4f, stroke)
        text.color = 0xFF9EA1A5.toInt(); text.textSize = bh * 0.03f
        c.drawText("MENU", menuKey.centerX(), menuKey.bottom + text.textSize * 1.1f, text)
        paint.color = 0xFFD0303A.toInt(); c.drawRoundRect(led.x - bh * 0.012f, led.y - bh * 0.004f, led.x + bh * 0.012f, led.y + bh * 0.004f, bh * 0.004f, bh * 0.004f, paint)
    }

    private fun drawShoulder(c: Canvas, r: RectF, down: Boolean, label: String) {
        paint.color = if (down) 0xFFB9BBBD.toInt() else 0xFFE2E3E2.toInt()
        c.drawRoundRect(r, r.height() * 0.5f, r.height() * 0.5f, paint)
        stroke.color = 0xFFB5B7BA.toInt(); stroke.strokeWidth = r.height() * 0.06f
        c.drawRoundRect(r, r.height() * 0.5f, r.height() * 0.5f, stroke)
        text.color = 0xFF8E9196.toInt(); text.textSize = r.height() * 0.42f
        c.drawText(label, r.centerX(), r.top + r.height() * 0.42f, text)
    }

    private fun drawRoundKey(c: Canvas, p: PointF, rad: Float, down: Boolean, label: String = "") {
        paint.color = 0xFFC6C8CA.toInt(); c.drawCircle(p.x, p.y, rad * 1.18f, paint)
        paint.shader = RadialGradient(p.x - rad * 0.3f, p.y - rad * 0.3f, rad * 1.4f,
            if (down) 0xFFC9CBCD.toInt() else 0xFFF6F6F5.toInt(), if (down) 0xFFA9ABAE.toInt() else 0xFFD5D6D7.toInt(), Shader.TileMode.CLAMP)
        c.drawCircle(p.x, p.y + (if (down) rad * 0.04f else 0f), rad, paint)
        paint.shader = null
        if (label.isNotEmpty()) {
            text.color = 0xFF8A8D92.toInt(); text.textSize = rad * 0.8f
            c.drawText(label, p.x, p.y + text.textSize * 0.36f, text)
        }
    }

    private fun drawDpad(c: Canvas, held: Int) {
        val arm = dpadR * 0.36f
        paint.color = 0xFFD3D4D5.toInt(); c.drawCircle(dpad.x, dpad.y, dpadR * 1.12f, paint)
        paint.color = 0xFFC2C4C6.toInt()
        c.drawRoundRect(dpad.x - arm, dpad.y - dpadR, dpad.x + arm, dpad.y + dpadR, arm * 0.35f, arm * 0.35f, paint)
        c.drawRoundRect(dpad.x - dpadR, dpad.y - arm, dpad.x + dpadR, dpad.y + arm, arm * 0.35f, arm * 0.35f, paint)
        val dirs = listOf(Triple(0f, -1f, GeniusBits.UP), Triple(0f, 1f, GeniusBits.DOWN), Triple(-1f, 0f, GeniusBits.LEFT), Triple(1f, 0f, GeniusBits.RIGHT))
        for ((dx, dy, bit) in dirs) {
            val cx = dpad.x + dx * dpadR * 0.62f; val cy = dpad.y + dy * dpadR * 0.62f
            if (held and bit != 0) {
                paint.color = 0xFFA4A7AA.toInt()
                c.drawRoundRect(cx - arm * 0.95f, cy - arm * 0.95f, cx + arm * 0.95f, cy + arm * 0.95f, arm * 0.3f, arm * 0.3f, paint)
            }
            paint.color = 0xFF8E9196.toInt()
            val k = arm * 0.55f
            val path = Path()
            path.moveTo(cx + dx * k * 0.6f, cy + dy * k * 0.6f)
            path.lineTo(cx - dy * k * 0.45f - dx * k * 0.2f, cy - dx * k * 0.45f - dy * k * 0.2f)
            path.lineTo(cx + dy * k * 0.45f - dx * k * 0.2f, cy + dx * k * 0.45f - dy * k * 0.2f)
            path.close()
            c.drawPath(path, paint)
        }
        stroke.color = 0xFFB0B3B6.toInt(); stroke.strokeWidth = arm * 0.08f
        c.drawCircle(dpad.x, dpad.y, arm * 0.45f, stroke)
    }

    private fun hit(x: Float, y: Float): Int {
        var b = 0
        val dx = x - dpad.x; val dy = y - dpad.y
        if (hypot(dx, dy) < dpadR * 1.5f && hypot(dx, dy) > dpadR * 0.12f) {
            if (dx > dpadR * 0.3f) b = b or GeniusBits.RIGHT
            if (dx < -dpadR * 0.3f) b = b or GeniusBits.LEFT
            if (dy > dpadR * 0.3f) b = b or GeniusBits.DOWN
            if (dy < -dpadR * 0.3f) b = b or GeniusBits.UP
            return b
        }
        if (hypot(x - btnA.x, y - btnA.y) < abR * 1.6f) return GeniusBits.A
        if (hypot(x - btnB.x, y - btnB.y) < abR * 1.6f) return GeniusBits.B
        val bh = body.height()
        if (RectF(shoulderL.left, shoulderL.top - bh * 0.05f, shoulderL.right, shoulderL.bottom + bh * 0.06f).contains(x, y)) return GeniusBits.L
        if (RectF(shoulderR.left, shoulderR.top - bh * 0.05f, shoulderR.right, shoulderR.bottom + bh * 0.06f).contains(x, y)) return GeniusBits.R
        if (hypot(x - volDown.x, y - volDown.y) < smallR * 2.2f) return GeniusBits.VOL_DOWN
        if (hypot(x - volUp.x, y - volUp.y) < smallR * 2.2f) return GeniusBits.VOL_UP
        if (RectF(menuKey.left - bh * 0.03f, menuKey.top - bh * 0.03f, menuKey.right + bh * 0.03f, menuKey.bottom + bh * 0.03f).contains(x, y)) return GeniusBits.MENU
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
                if (e.actionMasked == MotionEvent.ACTION_DOWN && hypot(x - power.x, y - power.y) < roundR * 1.8f) { onMenu?.invoke(); return true }
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
