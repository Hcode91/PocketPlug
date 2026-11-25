package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.hypot

/**
 * The white PSP-style 16-bit handheld (Beijue 220-in-1), after a photo: a long white body with
 * fine curved lines at both ends, the 4:3 colour screen in a black frame in the middle, a black
 * D-pad of four separate keys on the left, four round black keys (X Y A B) in a diamond on the
 * right, a small round R key over the pad and the slim N (left) and S (right) keys under the
 * pads. Drawn for the phone held sideways; no maker's name - the strip under the screen is the
 * menu. Input bits as gpl162xx: 0-3 up / down / left / right, 4 button 1, 5 button 2, 6 menu
 * (A, X and S press button 1; B, Y and R button 2; N is the handheld's menu key).
 *
 * kid = true: the yellow 218-in-1 (BornKid BC-19 kind), after a photo: a rounded yellow body,
 * the screen in a dark grey frame, the D-pad top left with a slim R key under it, the X Y A B
 * diamond on the right (Y top, X left, B right, A bottom) and the slim Select and S/P keys
 * under it. A / X = button 1, B / Y = button 2, S/P = start (0x40), Select = sound (0x80);
 * R resets the handheld (back to its menu).
 */
@SuppressLint("ViewConstructor")
class BeijueView(ctx: Context, private val kid: Boolean = false) : View(ctx) {

    var onReset: (() -> Unit)? = null

    var onInput: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    companion object {
        const val UP = 0x01; const val DOWN = 0x02; const val LEFT = 0x04; const val RIGHT = 0x08
        const val B1 = 0x10; const val B2 = 0x20; const val MENU = 0x40; const val SOUND = 0x80
        fun keyBit(code: Int): Int = when (code) {
            android.view.KeyEvent.KEYCODE_DPAD_UP, android.view.KeyEvent.KEYCODE_W -> UP
            android.view.KeyEvent.KEYCODE_DPAD_DOWN, android.view.KeyEvent.KEYCODE_S -> DOWN
            android.view.KeyEvent.KEYCODE_DPAD_LEFT, android.view.KeyEvent.KEYCODE_A -> LEFT
            android.view.KeyEvent.KEYCODE_DPAD_RIGHT, android.view.KeyEvent.KEYCODE_D -> RIGHT
            android.view.KeyEvent.KEYCODE_BUTTON_A, android.view.KeyEvent.KEYCODE_BUTTON_X, android.view.KeyEvent.KEYCODE_BUTTON_START,
            android.view.KeyEvent.KEYCODE_ENTER, android.view.KeyEvent.KEYCODE_SPACE, android.view.KeyEvent.KEYCODE_Z -> B1
            android.view.KeyEvent.KEYCODE_BUTTON_B, android.view.KeyEvent.KEYCODE_BUTTON_Y, android.view.KeyEvent.KEYCODE_BUTTON_R1, android.view.KeyEvent.KEYCODE_X -> B2
            android.view.KeyEvent.KEYCODE_BUTTON_SELECT, android.view.KeyEvent.KEYCODE_TAB -> MENU
            else -> 0
        }
    }

    private val frame = Bitmap.createBitmap(320, 240, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(320 * 240)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }
    private val smooth = Paint(Paint.FILTER_BITMAP_FLAG)

    private val body = RectF()
    private val bezel = RectF()
    private val screen = RectF()
    private val menuStrip = RectF()
    private val dpad = PointF(); private var dpadR = 0f
    private val btnA = PointF(); private val btnB = PointF(); private val btnX = PointF(); private val btnY = PointF(); private var abR = 0f
    private val btnR = PointF(); private var smallR = 0f
    private val slimN = RectF(); private val slimS = RectF(); private val slimP = RectF()

    private val pointers = HashMap<Int, Int>()
    private var touchMask = 0

    fun updateFrame(px: IntArray) {
        if (px.size < framePx.size) return
        System.arraycopy(px, 0, framePx, 0, framePx.size)
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        if (kid) { layoutKid(w, h); return }
        // fit a 2.05:1 body in the view
        var bw = w * 0.98f; var bh = bw / 2.05f
        if (bh > h * 0.96f) { bh = h * 0.96f; bw = bh * 2.05f }
        val l = (w - bw) / 2; val t = (h - bh) / 2
        body.set(l, t, l + bw, t + bh)
        val sh = bh * 0.66f; val sw = sh * 320f / 240f
        screen.set(body.centerX() - sw / 2, t + bh * 0.1f, body.centerX() + sw / 2, t + bh * 0.1f + sh)
        bezel.set(screen.left - bh * 0.04f, screen.top - bh * 0.04f, screen.right + bh * 0.04f, screen.bottom + bh * 0.1f)
        menuStrip.set(bezel.left, screen.bottom + bh * 0.015f, bezel.right, bezel.bottom - bh * 0.015f)
        dpadR = bh * 0.17f; dpad.set(l + bw * 0.12f, t + bh * 0.42f)
        abR = bh * 0.062f
        val cx = l + bw * 0.88f; val cy = t + bh * 0.42f; val d = bh * 0.13f
        btnX.set(cx, cy - d); btnB.set(cx, cy + d); btnY.set(cx - d, cy); btnA.set(cx + d, cy)
        smallR = bh * 0.045f
        btnR.set(l + bw * 0.205f, t + bh * 0.1f)
        val nw = bh * 0.15f; val nh = bh * 0.06f
        slimN.set(l + bw * 0.16f - nw / 2, t + bh * 0.78f - nh / 2, l + bw * 0.16f + nw / 2, t + bh * 0.78f + nh / 2)
        slimS.set(l + bw * 0.84f - nw / 2, t + bh * 0.78f - nh / 2, l + bw * 0.84f + nw / 2, t + bh * 0.78f + nh / 2)
    }

    private fun layoutKid(w: Int, h: Int) {
        var bw = w * 0.98f; var bh = bw / 1.9f
        if (bh > h * 0.96f) { bh = h * 0.96f; bw = bh * 1.9f }
        val l = (w - bw) / 2; val t = (h - bh) / 2
        body.set(l, t, l + bw, t + bh)
        val sh = bh * 0.68f; val sw = sh * 320f / 240f
        screen.set(body.centerX() - sw / 2 - bw * 0.01f, t + bh * 0.1f, body.centerX() + sw / 2 - bw * 0.01f, t + bh * 0.1f + sh)
        bezel.set(screen.left - bh * 0.045f, screen.top - bh * 0.045f, screen.right + bh * 0.045f, screen.bottom + bh * 0.1f)
        menuStrip.set(bezel.left, screen.bottom + bh * 0.015f, bezel.right, bezel.bottom - bh * 0.015f)
        dpadR = bh * 0.17f; dpad.set(l + bw * 0.125f, t + bh * 0.33f)
        abR = bh * 0.07f
        val cx = l + bw * 0.875f; val cy = t + bh * 0.47f; val d = bh * 0.135f
        btnY.set(cx, cy - d); btnA.set(cx, cy + d); btnX.set(cx - d, cy); btnB.set(cx + d, cy)
        smallR = bh * 0.045f
        val nw = bh * 0.16f; val nh = bh * 0.065f
        fun slim(r: RectF, x: Float, y: Float) = r.set(x - nw / 2, y - nh / 2, x + nw / 2, y + nh / 2)
        slim(slimN, l + bw * 0.12f, t + bh * 0.66f)     // R
        slim(slimS, l + bw * 0.80f, t + bh * 0.82f)     // Select
        slim(slimP, l + bw * 0.905f, t + bh * 0.82f)    // S/P
    }

    private fun drawKid(c: Canvas) {
        c.drawColor(0xFF15161A.toInt())
        val bh = body.height()
        val r = bh * 0.4f
        paint.color = 0x55000000
        c.drawRoundRect(body.left + bh * 0.02f, body.top + bh * 0.03f, body.right + bh * 0.02f, body.bottom + bh * 0.03f, r, r, paint)
        paint.shader = LinearGradient(body.left, body.top, body.left, body.bottom, 0xFFFBF04A.toInt(), 0xFFE9CF0E.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, r, r, paint); paint.shader = null
        // a soft highlight down the left end, as the photo's glossy plastic
        paint.shader = LinearGradient(body.left, 0f, body.left + body.width() * 0.12f, 0f, 0x55FFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP)
        c.drawRoundRect(body, r, r, paint); paint.shader = null
        stroke.color = 0xFFCDB20A.toInt(); stroke.strokeWidth = bh * 0.008f
        c.drawRoundRect(body, r, r, stroke)
        // dark grey screen frame
        paint.color = 0xFF3A3B3E.toInt()
        c.drawRoundRect(bezel, bh * 0.04f, bh * 0.04f, paint)
        paint.color = 0xFF0C0C0E.toInt()
        c.drawRect(screen.left - bh * 0.006f, screen.top - bh * 0.006f, screen.right + bh * 0.006f, screen.bottom + bh * 0.006f, paint)
        frame.setPixels(framePx, 0, 320, 0, 0, 320, 240)
        c.drawBitmap(frame, null, screen, smooth)
        text.color = 0xFF6A6B70.toInt(); text.textSize = menuStrip.height() * 0.45f
        c.drawText("MENU", menuStrip.centerX(), menuStrip.centerY() + text.textSize * 0.36f, text)

        val held = touchMask or externalMask
        drawDpadKid(c, held)
        val pressed = touchKeys
        for ((p, n) in listOf(btnX to "X", btnY to "Y", btnA to "A", btnB to "B")) {
            val down = n in pressed || (pressed.isEmpty() && ((n == "A" && held and B1 != 0) || (n == "B" && held and B2 != 0)))
            paint.color = 0x40000000; c.drawCircle(p.x + abR * 0.06f, p.y + abR * 0.12f, abR * 1.04f, paint)
            paint.shader = RadialGradient(p.x - abR * 0.3f, p.y - abR * 0.35f, abR * 1.4f, if (down) 0xFF2A2A2C.toInt() else 0xFF5A5B5E.toInt(), 0xFF1E1E20.toInt(), Shader.TileMode.CLAMP)
            c.drawCircle(p.x, p.y + (if (down) abR * 0.05f else 0f), abR, paint); paint.shader = null
            text.color = 0xFF111112.toInt(); text.textSize = abR * 0.8f
            c.drawText(n, p.x, p.y + text.textSize * 0.36f, text)
        }
        for ((rr, name, label) in listOf(Triple(slimN, "R", "R"), Triple(slimS, "SEL", "Select"), Triple(slimP, "SP", "S/P"))) {
            val down = name in pressed || (name == "SP" && held and MENU != 0) || (name == "SEL" && held and SOUND != 0)
            paint.color = 0x40000000
            c.drawRoundRect(rr.left, rr.top + rr.height() * 0.15f, rr.right, rr.bottom + rr.height() * 0.15f, rr.height() / 2, rr.height() / 2, paint)
            paint.color = if (down) 0xFF2A2A2C.toInt() else 0xFF4E4F52.toInt()
            c.drawRoundRect(rr, rr.height() / 2, rr.height() / 2, paint)
            text.color = 0xFF6E6A40.toInt(); text.textSize = smallR * 0.95f
            c.drawText(label, rr.centerX(), rr.bottom + text.textSize * 1.25f, text)
        }
    }

    private fun drawDpadKid(c: Canvas, held: Int) {
        // four separate pentagon keys round a faint ring, dark grey, with a thin arrow line on each
        stroke.color = 0x22000000; stroke.strokeWidth = dpadR * 0.03f
        c.drawCircle(dpad.x, dpad.y, dpadR * 0.75f, stroke)
        val k = dpadR * 0.36f
        for ((dx, dy, bit) in listOf(Triple(0f, -1f, UP), Triple(0f, 1f, DOWN), Triple(-1f, 0f, LEFT), Triple(1f, 0f, RIGHT))) {
            val cx = dpad.x + dx * dpadR * 0.55f; val cy = dpad.y + dy * dpadR * 0.55f
            // pentagon: square-ish with the inner side pointed toward the centre
            val path = Path()
            val px = -dy; val py = dx   // perpendicular
            fun pt(a: Float, b: Float) = PointF(cx + dx * a + px * b, cy + dy * a + py * b)
            val pts = listOf(pt(k, -k), pt(k, k), pt(-k * 0.45f, k), pt(-k * 1.05f, 0f), pt(-k * 0.45f, -k))
            path.moveTo(pts[0].x, pts[0].y); for (q in pts.drop(1)) path.lineTo(q.x, q.y); path.close()
            paint.color = 0x40000000
            c.save(); c.translate(dpadR * 0.03f, dpadR * 0.06f); c.drawPath(path, paint); c.restore()
            paint.color = if (held and bit != 0) 0xFF2A2A2C.toInt() else 0xFF55565A.toInt()
            c.drawPath(path, paint)
            stroke.color = 0xFF3A3A3E.toInt(); stroke.strokeWidth = k * 0.08f
            val a0 = pt(k * 0.45f, -k * 0.3f); val a1 = pt(k * 0.45f, k * 0.3f)
            c.drawLine(a0.x, a0.y, a1.x, a1.y, stroke)
        }
    }

    override fun onDraw(c: Canvas) {
        if (kid) { drawKid(c); return }
        c.drawColor(0xFF15161A.toInt())
        val bh = body.height()
        val r = bh * 0.42f
        paint.color = 0x55000000
        c.drawRoundRect(body.left + bh * 0.02f, body.top + bh * 0.03f, body.right + bh * 0.02f, body.bottom + bh * 0.03f, r, r, paint)
        paint.color = Color.WHITE
        paint.shader = LinearGradient(0f, body.top, 0f, body.bottom, 0xFFFAFAFA.toInt(), 0xFFDCDDE0.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, r, r, paint)
        paint.shader = null
        stroke.color = 0xFFC5C7CC.toInt(); stroke.strokeWidth = bh * 0.008f
        c.drawRoundRect(body, r, r, stroke)
        // the fine curved lines at both ends
        stroke.color = 0xFFD6D8DC.toInt(); stroke.strokeWidth = bh * 0.004f
        for (k in 0 until 7) {
            val off = bh * (0.05f + k * 0.022f)
            c.drawArc(body.left + off, body.top + bh * 0.25f, body.left + off + bh * 0.5f, body.bottom - bh * 0.05f, 120f, 120f, false, stroke)
            c.drawArc(body.right - off - bh * 0.5f, body.top + bh * 0.25f, body.right - off, body.bottom - bh * 0.05f, -60f, 120f, false, stroke)
        }

        // screen frame and picture
        paint.color = 0xFFE9EAEC.toInt()
        c.drawRoundRect(bezel.left - bh * 0.02f, bezel.top - bh * 0.02f, bezel.right + bh * 0.02f, bezel.bottom + bh * 0.02f, bh * 0.06f, bh * 0.06f, paint)
        stroke.color = 0xFFCFD1D5.toInt(); stroke.strokeWidth = bh * 0.006f
        c.drawRoundRect(bezel.left - bh * 0.02f, bezel.top - bh * 0.02f, bezel.right + bh * 0.02f, bezel.bottom + bh * 0.02f, bh * 0.06f, bh * 0.06f, stroke)
        paint.color = 0xFF0C0C0E.toInt()
        c.drawRect(screen.left - bh * 0.008f, screen.top - bh * 0.008f, screen.right + bh * 0.008f, screen.bottom + bh * 0.008f, paint)
        frame.setPixels(framePx, 0, 320, 0, 0, 320, 240)
        c.drawBitmap(frame, null, screen, smooth)
        text.color = 0xFFB0B3B8.toInt(); text.textSize = menuStrip.height() * 0.5f
        c.drawText("MENU", menuStrip.centerX(), menuStrip.centerY() + text.textSize * 0.36f, text)

        val held = touchMask or externalMask
        drawDpad(c, held)
        val pressed = touchKeys
        drawRound(c, btnX, abR, "X" in pressed, "X")
        drawRound(c, btnY, abR, "Y" in pressed, "Y")
        drawRound(c, btnA, abR, "A" in pressed || (held and B1 != 0 && pressed.isEmpty()), "A")
        drawRound(c, btnB, abR, "B" in pressed || (held and B2 != 0 && pressed.isEmpty()), "B")
        drawRound(c, btnR, smallR, "R" in pressed, "")
        text.color = 0xFF9A9DA3.toInt(); text.textSize = smallR * 0.9f
        c.drawText("R", btnR.x + smallR * 2.0f, btnR.y + text.textSize * 0.36f, text)
        for ((rr, name) in listOf(slimN to "N", slimS to "S")) {
            paint.color = if (name in pressed || (name == "N" && held and MENU != 0)) 0xFF0E0E10.toInt() else 0xFF2C2C30.toInt()
            c.save(); c.rotate(if (name == "N") -18f else 18f, rr.centerX(), rr.centerY())
            c.drawRoundRect(rr, rr.height() / 2, rr.height() / 2, paint); c.restore()
            text.color = 0xFF9A9DA3.toInt(); text.textSize = smallR * 0.9f
            c.drawText(name, rr.centerX() + (if (name == "N") -rr.width() * 0.7f else rr.width() * 0.7f), rr.bottom + text.textSize * 1.1f, text)
        }
    }

    private fun drawRound(c: Canvas, p: PointF, rad: Float, down: Boolean, label: String) {
        paint.color = 0xFFC9CBD0.toInt(); c.drawCircle(p.x, p.y, rad * 1.18f, paint)
        paint.color = Color.WHITE
        paint.shader = RadialGradient(p.x - rad * 0.3f, p.y - rad * 0.3f, rad * 1.3f, if (down) 0xFF1A1A1A.toInt() else 0xFF4A4A4E.toInt(), 0xFF0A0A0B.toInt(), Shader.TileMode.CLAMP)
        c.drawCircle(p.x, p.y + (if (down) rad * 0.05f else 0f), rad, paint)
        paint.shader = null
        if (label.isNotEmpty()) {
            text.color = 0xFF6E7076.toInt(); text.textSize = rad * 0.75f
            c.drawText(label, p.x, p.y + text.textSize * 0.36f, text)
        }
    }

    private fun drawDpad(c: Canvas, held: Int) {
        // four separate wedge keys round a centre, as on the photo's pad
        val k = dpadR * 0.42f
        val dirs = listOf(Triple(0f, -1f, UP), Triple(0f, 1f, DOWN), Triple(-1f, 0f, LEFT), Triple(1f, 0f, RIGHT))
        paint.color = 0xFFC9CBD0.toInt(); c.drawCircle(dpad.x, dpad.y, dpadR * 1.05f, paint)
        for ((dx, dy, bit) in dirs) {
            val cx = dpad.x + dx * dpadR * 0.55f; val cy = dpad.y + dy * dpadR * 0.55f
            val rr = RectF(cx - k, cy - k, cx + k, cy + k)
            paint.color = if (held and bit != 0) 0xFF0E0E10.toInt() else 0xFF2C2C30.toInt()
            c.drawRoundRect(rr, k * 0.35f, k * 0.35f, paint)
            // arrow notch
            paint.color = 0xFF55575C.toInt()
            val path = Path()
            path.moveTo(cx + dx * k * 0.45f, cy + dy * k * 0.45f)
            path.lineTo(cx - dy * k * 0.3f - dx * k * 0.1f, cy - dx * k * 0.3f - dy * k * 0.1f)
            path.lineTo(cx + dy * k * 0.3f - dx * k * 0.1f, cy + dx * k * 0.3f - dy * k * 0.1f)
            path.close()
            c.drawPath(path, paint)
        }
    }

    private val touchKeys = HashSet<String>()

    private fun key(x: Float, y: Float): String {
        val dx = x - dpad.x; val dy = y - dpad.y
        if (hypot(dx, dy) < dpadR * 1.4f && hypot(dx, dy) > dpadR * 0.15f) return "D"
        for ((p, n) in listOf(btnA to "A", btnB to "B", btnX to "X", btnY to "Y")) if (hypot(x - p.x, y - p.y) < abR * 1.4f) return n
        val pad = body.height() * 0.05f
        if (kid) {
            if (RectF(slimN).apply { inset(-pad, -pad) }.contains(x, y)) return "R"
            if (RectF(slimS).apply { inset(-pad * 0.6f, -pad) }.contains(x, y)) return "SEL"
            if (RectF(slimP).apply { inset(-pad * 0.6f, -pad) }.contains(x, y)) return "SP"
            return ""
        }
        if (hypot(x - btnR.x, y - btnR.y) < smallR * 2.4f) return "R"
        if (RectF(slimN).apply { inset(-pad, -pad) }.contains(x, y)) return "N"
        if (RectF(slimS).apply { inset(-pad, -pad) }.contains(x, y)) return "S"
        return ""
    }

    private fun hit(x: Float, y: Float): Int {
        var b = 0
        val dx = x - dpad.x; val dy = y - dpad.y
        return when (key(x, y)) {
            "D" -> {
                if (dx > dpadR * 0.25f) b = b or RIGHT
                if (dx < -dpadR * 0.25f) b = b or LEFT
                if (dy > dpadR * 0.25f) b = b or DOWN
                if (dy < -dpadR * 0.25f) b = b or UP
                b
            }
            "A", "X", "S" -> B1
            "B", "Y" -> B2
            "R" -> if (kid) 0 else B2
            "N", "SP" -> MENU
            "SEL" -> SOUND
            else -> 0
        }
    }

    private val pointerKeys = HashMap<Int, String>()

    private fun update() {
        var m = 0
        for (v in pointers.values) m = m or v
        val keys = pointerKeys.values.toHashSet()
        if (keys != touchKeys) { touchKeys.clear(); touchKeys.addAll(keys); invalidate() }
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
                if (e.actionMasked == MotionEvent.ACTION_DOWN && menuStrip.contains(x, y)) { onMenu?.invoke(); return true }
                pointers[e.getPointerId(i)] = hit(x, y)
                pointerKeys[e.getPointerId(i)] = key(x, y)
                if (kid && key(x, y) == "R") { performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP); onReset?.invoke() }
                update()
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until e.pointerCount) {
                    val id = e.getPointerId(i)
                    if (pointers.containsKey(id)) { pointers[id] = hit(e.getX(i), e.getY(i)); pointerKeys[id] = key(e.getX(i), e.getY(i)) }
                }
                update()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> { pointers.remove(e.getPointerId(e.actionIndex)); pointerKeys.remove(e.getPointerId(e.actionIndex)); update() }
            MotionEvent.ACTION_CANCEL -> { pointers.clear(); pointerKeys.clear(); update() }
        }
        return true
    }
}
