package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.min
import kotlin.math.sin

/**
 * A 1997 pachislot keychain game (Chibi Pachi: Alien Fever): a clear blue
 * shell with a round face on top, a printed slot-machine panel around the
 * 39x16 dot LCD, three small white buttons (MODE, SELECT, SOUND) and the big
 * HANDLE button that spins the reels. A ball chain hangs from the top loop;
 * tapping it opens the emulator menu. The printed artwork is drawn as plain
 * shapes, with no title, characters or maker's name.
 *
 * Frames: 39x16, 0xFF000000 = dot on. Input bits: 0 MODE, 1 SELECT, 2 SOUND, 3 HANDLE.
 */
class KeychainGameView(ctx: Context) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    companion object {
        const val W = 39
        const val H = 16
        const val MODE = 1; const val SELECT = 2; const val SOUND = 4; const val HANDLE = 8

        fun keyBit(code: Int): Int = when (code) {
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_SPACE, KeyEvent.KEYCODE_Z, KeyEvent.KEYCODE_ENTER,
            KeyEvent.KEYCODE_DPAD_CENTER, KeyEvent.KEYCODE_BUTTON_R1 -> HANDLE
            KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_1, KeyEvent.KEYCODE_BUTTON_Y -> MODE
            KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_2, KeyEvent.KEYCODE_X -> SELECT
            KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_3, KeyEvent.KEYCODE_S -> SOUND
            else -> 0
        }
    }

    private val dots = BooleanArray(W * H)
    private var touchMask = 0
    private val pointers = HashMap<Int, Int>()

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.DEFAULT_BOLD }

    // drawn in a 1000 x 1400 space
    private var sc = 1f; private var ox = 0f; private var oy = 0f
    private val faceC = PointF(500f, 560f); private val faceR = 400f
    private val lcd = RectF(255f, 470f, 745f, 690f)
    private val small = arrayOf(PointF(250f, 1105f), PointF(345f, 1105f), PointF(440f, 1105f)); private val smallR = 34f
    private val handle = PointF(720f, 1110f); private val handleR = 78f
    private val ring = PointF(500f, 130f)

    fun updateFrame(px: IntArray) {
        if (px.size < W * H) return
        var changed = false
        for (i in 0 until W * H) { val on = px[i] != 0; if (dots[i] != on) { dots[i] = on; changed = true } }
        if (changed) postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        sc = min(w / 1000f, h / 1400f)
        ox = (w - 1000f * sc) / 2f
        oy = (h - 1400f * sc) / 2f
    }

    private fun bodyPath(): Path {
        // round face on top, flaring into the lower body with its rounded corners
        val p = Path()
        p.addCircle(faceC.x, faceC.y, faceR, Path.Direction.CW)
        val body = Path().apply { addRoundRect(RectF(145f, 760f, 855f, 1260f), 120f, 120f, Path.Direction.CW) }
        p.op(body, Path.Op.UNION)
        // the two ears beside the top loop
        val ears = Path().apply { addRoundRect(RectF(390f, 130f, 610f, 220f), 40f, 40f, Path.Direction.CW) }
        p.op(ears, Path.Op.UNION)
        return p
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF17121F.toInt())
        c.save(); c.translate(ox, oy); c.scale(sc, sc)
        val mask = touchMask or externalMask

        // ball chain from the top
        paint.color = 0xFFB8B8BC.toInt()
        var y = -20f; var x = 470f
        while (y < 115f) { c.drawCircle(x, y, 9f, paint); y += 22f; x += 1.4f }
        stroke.color = 0xFFB8B8BC.toInt(); stroke.strokeWidth = 7f
        c.drawOval(RectF(470f, 100f, 530f, 170f), stroke)

        // shell: translucent blue with a darker rim
        val body = bodyPath()
        paint.shader = LinearGradient(0f, 140f, 0f, 1260f, intArrayOf(0xFF3E9BE0.toInt(), 0xFF2A78C8.toInt(), 0xFF5AAEEA.toInt()), null, Shader.TileMode.CLAMP)
        c.drawPath(body, paint); paint.shader = null
        stroke.color = 0xFF1C5A9E.toInt(); stroke.strokeWidth = 8f; c.drawPath(body, stroke)
        // hole in the loop
        paint.color = 0xFF17121F.toInt(); c.drawRoundRect(RectF(465f, 150f, 535f, 190f), 18f, 18f, paint)

        // printed face panel
        val face = RectF(faceC.x - faceR + 40f, faceC.y - faceR + 40f, faceC.x + faceR - 40f, faceC.y + faceR - 40f)
        c.save()
        val clip = Path().apply { addCircle(faceC.x, faceC.y, faceR - 40f, Path.Direction.CW) }
        c.clipPath(clip)
        paint.shader = RadialGradient(faceC.x, faceC.y + 80f, faceR, intArrayOf(0xFFFFA020.toInt(), 0xFFF06018.toInt(), 0xFFC8301C.toInt()), null, Shader.TileMode.CLAMP)
        c.drawRect(face, paint); paint.shader = null
        // a dark blue band across the top with lamps
        paint.color = 0xFF1A2A70.toInt(); c.drawRect(face.left, face.top, face.right, 400f, paint)
        for (i in 0 until 7) {
            val lx = 230f + i * 90f
            paint.color = if (i % 2 == 0) 0xFFFFE040.toInt() else 0xFFE83030.toInt()
            c.drawCircle(lx, 365f, 22f, paint)
            paint.color = 0x66FFFFFF; c.drawCircle(lx - 6f, 359f, 8f, paint)
        }
        // stars and coins round the sides
        for ((sx, sy, col) in listOf(Triple(200f, 520f, 0xFF3060E0.toInt()), Triple(800f, 520f, 0xFF3060E0.toInt()),
                Triple(205f, 690f, 0xFFFFE040.toInt()), Triple(795f, 690f, 0xFFFFE040.toInt()))) {
            paint.color = col; star(c, sx, sy, 34f)
        }
        // a red-and-blue diamond under the LCD
        paint.color = 0xFF2440B0.toInt()
        c.drawPath(Path().apply { moveTo(500f, 725f); lineTo(560f, 775f); lineTo(500f, 825f); lineTo(440f, 775f); close() }, paint)
        paint.color = 0xFFE83080.toInt()
        c.drawPath(Path().apply { moveTo(500f, 745f); lineTo(536f, 775f); lineTo(500f, 805f); lineTo(464f, 775f); close() }, paint)
        // payout tray along the bottom of the face
        paint.color = 0xFF8A1A10.toInt(); c.drawRect(330f, 845f, 670f, 960f, paint)
        paint.color = 0xFFFFD040.toInt(); for (i in 0 until 4) c.drawRect(350f + i * 85f, 865f, 405f + i * 85f, 940f, paint)
        c.restore()
        // clear dome rim over the face
        stroke.color = 0x885CC0FF.toInt(); stroke.strokeWidth = 16f
        c.drawCircle(faceC.x, faceC.y, faceR - 32f, stroke)

        // LCD
        paint.color = 0xFF2A2A2A.toInt(); c.drawRoundRect(RectF(lcd.left - 22f, lcd.top - 22f, lcd.right + 22f, lcd.bottom + 22f), 18f, 18f, paint)
        paint.color = 0xFFA9B4A0.toInt(); c.drawRect(lcd, paint)
        val dw = lcd.width() / W; val dh = lcd.height() / H
        for (yy in 0 until H) for (xx in 0 until W) {
            paint.color = if (dots[yy * W + xx]) 0xFF20241C.toInt() else 0x10000000
            val l = lcd.left + xx * dw; val t = lcd.top + yy * dh
            c.drawRect(l + dw * 0.06f, t + dh * 0.06f, l + dw * 0.94f, t + dh * 0.94f, paint)
        }

        // buttons
        val labels = arrayOf("MODE", "SELECT", "SOUND")
        text.textSize = 30f; text.color = 0xFFFFFFFF.toInt()
        for (i in 0 until 3) {
            val p = small[i]; val down = mask and (1 shl i) != 0
            paint.color = 0xFF1E5FA8.toInt(); c.drawCircle(p.x, p.y, smallR + 8f, paint)
            paint.color = if (down) 0xFFC8C8C8.toInt() else 0xFFF4F4F0.toInt()
            c.drawCircle(p.x, p.y + (if (down) 3f else 0f), smallR, paint)
            c.drawText(labels[i], p.x, p.y + 85f, text)
        }
        val hd = mask and HANDLE != 0
        paint.color = 0xFF1E5FA8.toInt(); c.drawCircle(handle.x, handle.y, handleR + 12f, paint)
        paint.color = if (hd) 0xFFCFCFCB.toInt() else 0xFFF6F6F2.toInt()
        c.drawCircle(handle.x, handle.y + (if (hd) 4f else 0f), handleR, paint)
        paint.color = 0x22000000; c.drawCircle(handle.x, handle.y + (if (hd) 4f else 0f), handleR * 0.7f, paint)
        c.drawText("HANDLE", handle.x, handle.y + handleR + 60f, text)
        c.restore()
    }

    private fun star(c: Canvas, x: Float, y: Float, r: Float) {
        val p = Path()
        for (i in 0 until 10) {
            val a = Math.PI / 2 + i * Math.PI / 5
            val rr = if (i % 2 == 0) r else r * 0.45f
            val px = x + (rr * cos(a)).toFloat(); val py = y - (rr * sin(a)).toFloat()
            if (i == 0) p.moveTo(px, py) else p.lineTo(px, py)
        }
        p.close(); c.drawPath(p, paint)
    }

    // ---- touch ----
    private fun hit(sx: Float, sy: Float): Int {
        val x = (sx - ox) / sc; val y = (sy - oy) / sc
        if (hypot(x - handle.x, y - handle.y) < handleR * 1.5f) return HANDLE
        for (i in 0 until 3) if (hypot(x - small[i].x, y - small[i].y) < smallR * 1.9f) return 1 shl i
        if (y < 230f) return -1   // ring / chain: menu
        return 0
    }

    private fun update() {
        var m = 0
        for (v in pointers.values) m = m or v
        if (m != touchMask) {
            if (m and touchMask.inv() != 0) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
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
                val b = hit(e.getX(i), e.getY(i))
                if (b == -1) onMenu?.invoke()
                else if (b != 0) { pointers[e.getPointerId(i)] = b; update() }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> { pointers.remove(e.getPointerId(e.actionIndex)); update() }
            MotionEvent.ACTION_CANCEL -> { pointers.clear(); update() }
        }
        return true
    }
}
