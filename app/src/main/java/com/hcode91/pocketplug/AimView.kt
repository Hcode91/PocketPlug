package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.min

/**
 * The 1977 single-board computer with its printer, drawn as the boxed
 * unit: brown top panel holding the 20-column thermal printer (paper on
 * the left) and the red 20-character 16-segment display, and the cream
 * full keyboard below, with RESET and MENU on the panel.
 *
 * Frame (see aim65.h): 160 x 201. Rows 0-199 are the printer paper (1 = dot,
 * newest line at the bottom); row 200 holds the 20 digits' segment masks in
 * MAME's led16seg order. Keys go back as row * 8 + bit codes; RESET is input
 * bit 0. Shift and CTRL latch until the next key. No maker's name or logo.
 */
class AimView(ctx: Context) : View(ctx) {

    var onKey: ((Int, Boolean) -> Unit)? = null
    var onReset: ((Boolean) -> Unit)? = null
    var onMenu: (() -> Unit)? = null

    private val lock = Any()
    private val incoming = IntArray(160 * 200)
    private val segIn = IntArray(20)
    private var fresh = false
    private val paperPx = IntArray(160 * 200)
    private val paper = Bitmap.createBitmap(160, 200, Bitmap.Config.ARGB_8888)
    private val seg = IntArray(20)

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val bmp = Paint(Paint.FILTER_BITMAP_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.SANS_SERIF }

    // laid out in a 2000 x 1100 space, scaled to fit
    private var sc = 1f
    private var ox = 0f
    private var oy = 0f

    private class Key(val x: Float, val y: Float, val w: Float, val top: String, val main: String, val code: Int) {
        fun isMod() = code == SHIFT_L || code == SHIFT_R || code == CTRL
    }

    private val keys = ArrayList<Key>()
    private val down = HashSet<Int>()          // codes held by a finger
    private val latched = HashSet<Int>()       // shift / ctrl waiting for the next key
    private val pointers = HashMap<Int, Int>() // pointer -> key code (or -2 reset)
    private var resetDown = false

    private val resetR = RectF(1720f, 300f, 1860f, 372f)
    private val menuR = RectF(1530f, 300f, 1670f, 372f)

    init {
        val p = 118f; val k = 108f
        val x0 = 92f
        fun row(y: Float, start: Float, items: List<Triple<String, String, Int>>, widths: Map<Int, Float> = emptyMap()) {
            var x = start
            for ((i, it) in items.withIndex()) {
                val w = widths[i] ?: 1f
                keys.add(Key(x, y, k + (w - 1f) * p, it.first, it.second, it.third))
                x += w * p
            }
        }
        val y1 = 470f
        row(y1, x0, listOf(
            Triple("", "ESC", 23), Triple("!", "1", 39), Triple("\"", "2", 46), Triple("#", "3", 38),
            Triple("$", "4", 45), Triple("%", "5", 37), Triple("&", "6", 44), Triple("'", "7", 36),
            Triple("(", "8", 43), Triple(")", "9", 35), Triple("", "0", 42), Triple("*", ":", 34),
            Triple("=", "-", 26), Triple("", "DEL", 49)))
        row(y1 + p, x0, listOf(
            Triple("", "CTRL", CTRL), Triple("", "Q", 31), Triple("", "W", 22), Triple("", "E", 30),
            Triple("", "R", 21), Triple("", "T", 29), Triple("", "Y", 20), Triple("", "U", 28),
            Triple("", "I", 19), Triple("", "O", 27), Triple("", "P", 18), Triple("@", "LF", 9),
            Triple("", "PRINT", 17)), mapOf(12 to 2f))
        row(y1 + 2 * p, x0 + p * 0.5f, listOf(
            Triple("", "A", 14), Triple("", "S", 54), Triple("", "D", 13), Triple("", "F", 53),
            Triple("", "G", 12), Triple("", "H", 52), Triple("", "J", 11), Triple("", "K", 51),
            Triple("", "L", 10), Triple("+", ";", 50), Triple("", "RETURN", 24)), mapOf(10 to 2.5f))
        row(y1 + 3 * p, x0, listOf(
            Triple("", "SHIFT", SHIFT_L), Triple("", "Z", 6), Triple("", "X", 62), Triple("", "C", 5),
            Triple("", "V", 61), Triple("", "B", 4), Triple("", "N", 60), Triple("", "M", 3),
            Triple("<", ",", 59), Triple(">", ".", 2), Triple("?", "/", 58), Triple("", "SHIFT", SHIFT_R)),
            mapOf(0 to 1.5f, 11 to 2.5f))
        keys.add(Key(x0 + p * 3f, y1 + 4 * p, k + 7f * p, "", "", 0))
        // function keys, a column on the right
        val fx = x0 + 14f * p + 70f
        keys.add(Key(fx, y1, k, "", "F1", 63))
        keys.add(Key(fx, y1 + p, k, "", "F2", 55))
        keys.add(Key(fx, y1 + 2 * p, k, "", "F3", 47))
    }

    fun updateFrame(px: IntArray) {
        if (px.size < 160 * 201) return
        synchronized(lock) {
            System.arraycopy(px, 0, incoming, 0, 160 * 200)
            System.arraycopy(px, 160 * 200, segIn, 0, 20)
            fresh = true
        }
        postInvalidate()
    }

    fun releaseAll() {
        for (c in down) onKey?.invoke(c, false)
        for (c in latched) onKey?.invoke(c, false)
        down.clear(); latched.clear(); pointers.clear()
        if (resetDown) { resetDown = false; onReset?.invoke(false) }
        invalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        sc = min(w / 2000f, h / 1100f)
        ox = (w - 2000f * sc) / 2f
        oy = (h - 1100f * sc) / 2f
    }

    private fun X(v: Float) = ox + v * sc
    private fun Y(v: Float) = oy + v * sc
    private fun rect(l: Float, t: Float, r: Float, b: Float) = RectF(X(l), Y(t), X(r), Y(b))
    private fun rect(r: RectF) = rect(r.left, r.top, r.right, r.bottom)

    override fun onDraw(c: Canvas) {
        synchronized(lock) {
            if (fresh) {
                for (i in incoming.indices) paperPx[i] = if (incoming[i] != 0) 0xFF23263A.toInt() else 0xFFF4F1E8.toInt()
                System.arraycopy(segIn, 0, seg, 0, 20)
                fresh = false
                paper.setPixels(paperPx, 0, 160, 0, 0, 160, 200)
            }
        }
        c.drawColor(0xFF121212.toInt())
        // case: cream body, brown top panel
        paint.color = 0xFFD9CFB6.toInt(); c.drawRoundRect(rect(10f, 10f, 1990f, 1090f), 30f * sc, 30f * sc, paint)
        paint.color = 0xFFE6DDC6.toInt(); c.drawRoundRect(rect(24f, 24f, 1976f, 1076f), 24f * sc, 24f * sc, paint)
        paint.color = 0xFF54382A.toInt(); c.drawRoundRect(rect(24f, 24f, 1976f, 420f), 24f * sc, 24f * sc, paint)
        c.drawRect(rect(24f, 380f, 1976f, 420f), paint)
        paint.color = 0xFF3E281D.toInt(); c.drawRect(rect(24f, 414f, 1976f, 426f), paint)
        drawPrinter(c)
        drawDisplay(c)
        drawButtons(c)
        // keyboard well
        paint.color = 0xFFCFC5AB.toInt(); c.drawRoundRect(rect(64f, 448f, 1936f, 1060f), 14f * sc, 14f * sc, paint)
        for (k in keys) drawKey(c, k)
    }

    private fun drawPrinter(c: Canvas) {
        // housing and paper window
        paint.color = 0xFF2A2420.toInt(); c.drawRoundRect(rect(70f, 52f, 570f, 396f), 14f * sc, 14f * sc, paint)
        paint.color = 0xFF181513.toInt(); c.drawRoundRect(rect(100f, 76f, 540f, 352f), 8f * sc, 8f * sc, paint)
        // paper: the last 74 rows, 125 dots wide
        val win = rect(126f, 84f, 514f, 340f)
        val rows = 74
        c.drawBitmap(paper, Rect(0, 200 - rows, 125, 200), win, bmp)
        // shade where the paper curls in at the bottom
        paint.shader = LinearGradient(0f, Y(300f), 0f, Y(340f), 0x00000000, 0x55000000, Shader.TileMode.CLAMP)
        c.drawRect(rect(126f, 300f, 514f, 340f), paint)
        paint.shader = null
        // paper tongue out of the top slot, and the tear bar
        paint.color = 0xFFF4F1E8.toInt(); c.drawRect(rect(126f, 36f, 514f, 84f), paint)
        paint.color = 0xFF9EA1A4.toInt(); c.drawRoundRect(rect(112f, 70f, 528f, 86f), 6f * sc, 6f * sc, paint)
        paint.color = 0xFFC8CBCE.toInt(); c.drawRect(rect(112f, 70f, 528f, 75f), paint)
        // paper feed slot / roll cover lines
        stroke.color = 0xFF3A322C.toInt(); stroke.strokeWidth = 4f * sc
        c.drawLine(X(100f), Y(372f), X(540f), Y(372f), stroke)
    }

    private fun drawDisplay(c: Canvas) {
        paint.color = 0xFF1B1210.toInt(); c.drawRoundRect(rect(620f, 92f, 1940f, 262f), 12f * sc, 12f * sc, paint)
        paint.color = 0xFF2C0707.toInt(); c.drawRoundRect(rect(642f, 112f, 1918f, 242f), 6f * sc, 6f * sc, paint)
        // five 4-character modules
        for (m in 0 until 5) {
            paint.color = 0xFF380B0A.toInt()
            c.drawRoundRect(rect(656f + m * 252f, 124f, 896f + m * 252f, 230f), 6f * sc, 6f * sc, paint)
        }
        stroke.strokeCap = Paint.Cap.ROUND
        for (d in 0 until 20) {
            val m = d / 4; val i = d % 4
            val l = 676f + m * 252f + i * 54f
            val t = 140f
            val w = 38f; val h = 72f
            val s = seg[d]
            for (b in 0 until 16) {
                val p = SEG[b]
                val on = (s shr b) and 1 != 0
                stroke.strokeWidth = 6.5f * sc
                stroke.color = if (on) 0xFFFF2A1C.toInt() else 0x22FF4030
                // a slight forward slant, like the real parts
                val sk = 0.12f
                fun sx(fx: Float, fy: Float) = X(l + fx * w + (1f - fy) * sk * w)
                c.drawLine(sx(p[0], p[1]), Y(t + p[1] * h), sx(p[2], p[3]), Y(t + p[3] * h), stroke)
            }
        }
        stroke.strokeCap = Paint.Cap.BUTT
        // glare on the filter
        paint.shader = LinearGradient(0f, Y(112f), 0f, Y(180f), 0x18FFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP)
        c.drawRect(rect(642f, 112f, 1918f, 180f), paint)
        paint.shader = null
    }

    private fun drawButtons(c: Canvas) {
        fun btn(r: RectF, label: String, pressed: Boolean, col: Int) {
            paint.color = 0xFF2A1A12.toInt(); c.drawRoundRect(rect(r.left, r.top + 8f, r.right, r.bottom + 8f), 10f * sc, 10f * sc, paint)
            paint.color = if (pressed) darker(col) else col
            val rr = rect(r).apply { if (pressed) offset(0f, 5f * sc) }
            c.drawRoundRect(rr, 10f * sc, 10f * sc, paint)
            text.color = 0xFFF1E9D8.toInt(); text.textSize = 30f * sc; text.isFakeBoldText = true
            c.drawText(label, rr.centerX(), rr.centerY() + 11f * sc, text)
            text.isFakeBoldText = false
        }
        btn(menuR, "MENU", false, 0xFF6E5444.toInt())
        btn(resetR, "RESET", resetDown, 0xFF9A2E22.toInt())
    }

    private fun darker(c: Int) = Color.rgb(Color.red(c) * 4 / 5, Color.green(c) * 4 / 5, Color.blue(c) * 4 / 5)

    private fun drawKey(c: Canvas, k: Key) {
        val pressed = k.code in down || k.code in latched
        val r = RectF(k.x, k.y, k.x + k.w, k.y + 108f)
        paint.color = 0xFF9C9178.toInt(); c.drawRoundRect(rect(r.left, r.top + 8f, r.right, r.bottom + 8f), 12f * sc, 12f * sc, paint)
        val dark = k.isMod() || k.main.length > 1 && k.main != "PRINT" && k.code != 0
        var col = if (dark) 0xFF7A5A44.toInt() else 0xFFF1EBDA.toInt()
        if (k.code == 17) col = 0xFF9A2E22.toInt()
        if (pressed) col = darker(col)
        paint.color = col
        val rr = rect(r).apply { if (pressed) offset(0f, 5f * sc) }
        c.drawRoundRect(rr, 12f * sc, 12f * sc, paint)
        // dished top
        paint.color = Color.argb(28, 255, 255, 255)
        c.drawRoundRect(RectF(rr.left + 10f * sc, rr.top + 8f * sc, rr.right - 10f * sc, rr.bottom - 18f * sc), 10f * sc, 10f * sc, paint)
        text.color = if (dark || k.code == 17) 0xFFF4ECDA.toInt() else 0xFF2A221C.toInt()
        if (k.top.isEmpty()) {
            text.textSize = (if (k.main.length > 2) 26f else 44f) * sc
            c.drawText(k.main, rr.centerX(), rr.centerY() + text.textSize * 0.36f, text)
        } else {
            text.textSize = 34f * sc
            c.drawText(k.top, rr.centerX(), rr.top + 42f * sc, text)
            text.textSize = (if (k.main.length > 1) 26f else 38f) * sc
            c.drawText(k.main, rr.centerX(), rr.bottom - 22f * sc, text)
        }
    }

    // ---- touch ----
    private fun keyAt(x: Float, y: Float): Int {
        val vx = (x - ox) / sc; val vy = (y - oy) / sc
        if (resetR.contains(vx, vy)) return -2
        if (menuR.contains(vx, vy)) return -3
        for (k in keys) if (vx >= k.x - 5f && vx < k.x + k.w + 5f && vy >= k.y - 5f && vy < k.y + 113f) return k.code
        return -1
    }

    private fun press(code: Int) {
        performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
        if (code == SHIFT_L || code == SHIFT_R || code == CTRL) {
            if (code in latched) { latched.remove(code); onKey?.invoke(code, false) }
            else { latched.add(code); onKey?.invoke(code, true) }
        } else {
            down.add(code); onKey?.invoke(code, true)
        }
        invalidate()
    }

    private fun release(code: Int) {
        if (code !in down) return
        down.remove(code); onKey?.invoke(code, false)
        if (down.isEmpty() && latched.isNotEmpty()) {
            for (m in latched) onKey?.invoke(m, false)
            latched.clear()
        }
        invalidate()
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                when (val k = keyAt(e.getX(i), e.getY(i))) {
                    -3 -> onMenu?.invoke()
                    -2 -> { pointers[e.getPointerId(i)] = -2; resetDown = true; onReset?.invoke(true); performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP); invalidate() }
                    -1 -> {}
                    else -> { pointers[e.getPointerId(i)] = k; press(k) }
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val k = pointers.remove(e.getPointerId(e.actionIndex))
                if (k == -2) { resetDown = false; onReset?.invoke(false); invalidate() }
                else if (k != null) release(k)
            }
            MotionEvent.ACTION_CANCEL -> {
                for (k in pointers.values) if (k >= 0) release(k)
                if (resetDown) { resetDown = false; onReset?.invoke(false); invalidate() }
                pointers.clear()
            }
        }
        return true
    }

    companion object {
        const val CTRL = 32
        const val SHIFT_L = 40
        const val SHIFT_R = 48

        // MAME's led16seg segments, in a unit cell: x1, y1, x2, y2
        private val SEG = arrayOf(
            floatArrayOf(0.08f, 0f, 0.46f, 0f), floatArrayOf(0.54f, 0f, 0.92f, 0f),        // top left, top right
            floatArrayOf(1f, 0.06f, 1f, 0.45f), floatArrayOf(1f, 0.55f, 1f, 0.94f),          // right top, right bottom
            floatArrayOf(0.92f, 1f, 0.54f, 1f), floatArrayOf(0.46f, 1f, 0.08f, 1f),        // bottom right, bottom left
            floatArrayOf(0f, 0.94f, 0f, 0.55f), floatArrayOf(0f, 0.45f, 0f, 0.06f),          // left bottom, left top
            floatArrayOf(0.08f, 0.5f, 0.46f, 0.5f), floatArrayOf(0.54f, 0.5f, 0.92f, 0.5f), // middle left, middle right
            floatArrayOf(0.5f, 0.07f, 0.5f, 0.43f), floatArrayOf(0.5f, 0.57f, 0.5f, 0.93f), // centre top, centre bottom
            floatArrayOf(0.12f, 0.9f, 0.42f, 0.58f), floatArrayOf(0.12f, 0.1f, 0.42f, 0.42f), // diag left bottom "/", left top "\"
            floatArrayOf(0.58f, 0.42f, 0.88f, 0.1f), floatArrayOf(0.58f, 0.58f, 0.88f, 0.9f), // diag right top "/", right bottom "\"
        )
    }
}
