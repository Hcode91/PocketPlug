package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.min

/**
 * Single-board microcomputer kits, drawn as the bare board:
 *
 *  - "acrnsys1": the 1979 keyboard/display card - green board with its
 *    chips on the left, the white key panel on the right with the
 *    nine-digit bubble LED display (the left digit isn't wired), the 25
 *    cream keys and the edge connector.
 *
 * The core's frame is a status block: [d * 8 + segment] = brightness of
 * segment a-g / dp of digit d (MAME's digit numbering). Keys go back as
 * input bits (see acrnsys1.h). No maker's name or logo is drawn.
 */
class KitBoardView(ctx: Context, private val style: String) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null

    private val seg = IntArray(64)
    private var keyMask = 0
    private val pointers = HashMap<Int, Int>()

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.SANS_SERIF }

    // the board is laid out in a 2048 x 1365 space, scaled to fit
    private var sc = 1f
    private var ox = 0f
    private var oy = 0f
    private val menuR = RectF()

    private data class Key(val cx: Float, val cy: Float, val label: String, val bit: Int)
    private val keys = ArrayList<Key>().apply {
        val xs = floatArrayOf(860f, 1010f, 1158f, 1305f, 1510f, 1655f)
        val ys = floatArrayOf(635f, 780f, 925f, 1072f)
        val labels = arrayOf(
            arrayOf("c", "d", "e", "f", "m", "l"),
            arrayOf("8", "9", "a", "b", "g", "r"),
            arrayOf("4", "5", "6", "7", "p", "^"),
            arrayOf("0", "1", "2", "3", "s", "v"))
        val cmdBits = mapOf("m" to 16, "g" to 17, "p" to 18, "s" to 19, "l" to 20, "r" to 21, "^" to 22, "v" to 23)
        for (r in 0 until 4) for (c in 0 until 6) {
            val l = labels[r][c]
            val bit = if (c < 4) l.toInt(16) else cmdBits.getValue(l)
            add(Key(xs[c], ys[r], l, bit))
        }
        add(Key(1662f, 482f, "rst", 24))
    }

    fun updateFrame(px: IntArray) {
        if (px.size < 64) return
        var changed = false
        for (i in 0 until 64) if (seg[i] != px[i]) { seg[i] = px[i]; changed = true }
        if (changed) postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        val menuW = min(w * 0.12f, 220f)
        sc = min((w - menuW * 1.1f) / 2048f, h / 1365f)
        ox = (w - menuW * 1.1f - 2048f * sc) / 2f
        oy = (h - 1365f * sc) / 2f
        menuR.set(w - menuW, h * 0.04f, w - menuW * 0.08f, h * 0.04f + menuW * 0.5f)
    }

    private fun X(v: Float) = ox + v * sc
    private fun Y(v: Float) = oy + v * sc
    private fun rect(l: Float, t: Float, r: Float, b: Float) = RectF(X(l), Y(t), X(r), Y(b))

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF15171A.toInt())
        drawBoard(c)
        drawPanel(c)
        drawDisplay(c)
        drawKeys(c)
        paint.color = 0xFF3A3D42.toInt(); c.drawRoundRect(menuR, 10f, 10f, paint)
        text.color = 0xFFE0E0E0.toInt(); text.textSize = menuR.height() * 0.34f; text.isFakeBoldText = true
        c.drawText("MENU", menuR.centerX(), menuR.centerY() + text.textSize * 0.36f, text)
        text.isFakeBoldText = false
    }

    private fun drawBoard(c: Canvas) {
        // board
        paint.color = 0xFF17703A.toInt()
        c.drawRoundRect(rect(55f, 95f, 1920f, 1250f), 14f * sc, 14f * sc, paint)
        // edge connector: cream strip and gold fingers
        paint.color = 0xFFE7E1C6.toInt(); c.drawRect(rect(1900f, 178f, 1975f, 1182f), paint)
        paint.color = 0xFFB9B9AE.toInt()
        for (i in 0 until 17) c.drawRoundRect(rect(1862f, 222f + i * 34f, 1905f, 244f + i * 34f), 6f * sc, 6f * sc, paint)
        // traces
        stroke.color = 0xFF27894C.toInt(); stroke.strokeWidth = 7f * sc
        val tr = Path()
        for (i in 0 until 12) {
            val y = 210f + i * 72f
            tr.moveTo(X(360f), Y(y)); tr.lineTo(X(700f), Y(y)); tr.lineTo(X(745f), Y(y + 30f))
        }
        for (i in 0 until 17) { tr.moveTo(X(1755f), Y(232f + i * 34f)); tr.lineTo(X(1862f), Y(232f + i * 34f)) }
        tr.moveTo(X(180f), Y(390f)); tr.lineTo(X(180f), Y(1150f))
        tr.moveTo(X(440f), Y(470f)); tr.lineTo(X(440f), Y(1150f))
        c.drawPath(tr, stroke)
        // chips (16 / 14 / 8 pin DIPs)
        fun dip(l: Float, t: Float, r: Float, b: Float, pins: Int, label: String) {
            paint.color = 0xFFC9CCCF.toInt()
            val n = pins / 2
            val step = (r - l - 20f) / n
            for (i in 0 until n) {
                c.drawRect(rect(l + 14f + i * step, t - 14f, l + 14f + i * step + step * 0.45f, t + 4f), paint)
                c.drawRect(rect(l + 14f + i * step, b - 4f, l + 14f + i * step + step * 0.45f, b + 14f), paint)
            }
            paint.color = 0xFF1C1C1E.toInt(); c.drawRoundRect(rect(l, t, r, b), 6f * sc, 6f * sc, paint)
            paint.color = 0xFF2C2C30.toInt(); c.drawCircle(X(l + 26f), Y((t + b) / 2f), 10f * sc, paint)
            text.color = 0xFFDDE8DD.toInt(); text.textSize = 30f * sc
            c.drawText(label, X(r + 40f), Y(t + 30f), text)
        }
        dip(110f, 240f, 345f, 350f, 14, "IC1")
        dip(435f, 240f, 670f, 350f, 16, "IC8")
        dip(110f, 525f, 345f, 635f, 14, "IC2")
        dip(460f, 525f, 680f, 635f, 14, "IC4")
        dip(130f, 790f, 360f, 900f, 14, "IC3")
        dip(470f, 790f, 690f, 900f, 14, "IC5")
        dip(232f, 1060f, 345f, 1155f, 8, "IC7")
        dip(470f, 1060f, 690f, 1165f, 14, "IC6")
        // resistors
        fun res(x1: Float, y: Float, x2: Float, bands: IntArray) {
            stroke.color = 0xFFB8BCC0.toInt(); stroke.strokeWidth = 5f * sc
            c.drawLine(X(x1), Y(y), X(x2), Y(y), stroke)
            paint.color = 0xFFD9BE96.toInt()
            val m = (x1 + x2) / 2f
            c.drawRoundRect(rect(m - 55f, y - 15f, m + 55f, y + 15f), 14f * sc, 14f * sc, paint)
            for ((i, col) in bands.withIndex()) { paint.color = col; c.drawRect(rect(m - 38f + i * 22f, y - 15f, m - 28f + i * 22f, y + 15f), paint) }
        }
        val brown = 0xFF6B3A1E.toInt(); val red = 0xFFB0302A.toInt(); val yel = 0xFFE0B21E.toInt(); val vio = 0xFF7A3E8E.toInt(); val blk = 0xFF202020.toInt()
        res(140f, 420f, 290f, intArrayOf(brown, blk, yel, 0xFFC8A040.toInt()))
        res(140f, 655f, 290f, intArrayOf(red, red, brown, 0xFFC8A040.toInt()))
        res(540f, 420f, 700f, intArrayOf(yel, vio, yel, 0xFFC8A040.toInt()))
        res(540f, 448f, 700f, intArrayOf(brown, blk, red, 0xFFC8A040.toInt()))
        res(540f, 718f, 700f, intArrayOf(red, red, yel, 0xFFC8A040.toInt()))
        res(540f, 746f, 700f, intArrayOf(brown, blk, yel, 0xFFC8A040.toInt()))
        res(180f, 935f, 330f, intArrayOf(brown, blk, red, 0xFFC8A040.toInt()))
        res(180f, 965f, 330f, intArrayOf(red, vio, brown, 0xFFC8A040.toInt()))
        res(470f, 980f, 640f, intArrayOf(yel, vio, red, 0xFFC8A040.toInt()))
        res(500f, 1190f, 660f, intArrayOf(brown, blk, yel, 0xFFC8A040.toInt()))
        // capacitors: orange discs, the blue electrolytic
        paint.color = 0xFFC7783C.toInt()
        c.drawOval(rect(160f, 120f, 290f, 160f), paint)
        c.drawOval(rect(225f, 445f, 330f, 485f), paint)
        c.drawOval(rect(365f, 800f, 440f, 900f), paint)
        c.drawOval(rect(335f, 975f, 445f, 1080f), paint)
        c.drawOval(rect(375f, 630f, 430f, 680f), paint)
        c.drawOval(rect(590f, 935f, 640f, 965f), paint)
        c.drawOval(rect(160f, 1110f, 240f, 1160f), paint)
        paint.color = 0xFF2F6FB8.toInt(); c.drawRoundRect(rect(145f, 685f, 320f, 755f), 20f * sc, 20f * sc, paint)
        paint.color = 0xFFDDE6F0.toInt(); c.drawRect(rect(150f, 685f, 170f, 755f), paint)
        // power leads
        stroke.color = 0xFFB0281E.toInt(); stroke.strokeWidth = 8f * sc
        c.drawLine(X(1840f), Y(1240f), X(2040f), Y(1245f), stroke)
        stroke.color = 0xFF151515.toInt()
        c.drawLine(X(1840f), Y(1255f), X(2040f), Y(1262f), stroke)
        // nylon mounting screws
        for ((x, y) in listOf(110f to 160f, 1880f to 155f, 1870f to 1190f, 125f to 1195f)) {
            paint.color = 0xFFD6DDD2.toInt(); c.drawCircle(X(x), Y(y), 26f * sc, paint)
            stroke.color = 0xFFAAB2A6.toInt(); stroke.strokeWidth = 4f * sc; c.drawCircle(X(x), Y(y), 26f * sc, stroke)
        }
    }

    private fun drawPanel(c: Canvas) {
        paint.color = 0xFFE9EAE6.toInt()
        c.drawRect(rect(755f, 160f, 1752f, 1218f), paint)
        // panel screws
        for ((x, y) in listOf(1700f to 212f, 815f to 1168f, 1690f to 1162f)) {
            paint.color = 0xFFDCE3DA.toInt(); c.drawCircle(X(x), Y(y), 24f * sc, paint)
            stroke.color = 0xFFA9B0A6.toInt(); stroke.strokeWidth = 3f * sc; c.drawCircle(X(x), Y(y), 24f * sc, stroke)
            c.drawLine(X(x - 14f), Y(y), X(x + 14f), Y(y), stroke)
        }
        // ribbon cable under the display
        paint.color = 0xFFA8AAAC.toInt()
        c.drawRoundRect(rect(838f, 432f, 1338f, 520f), 30f * sc, 30f * sc, paint)
        stroke.color = 0xFF8E9092.toInt(); stroke.strokeWidth = 3f * sc
        var x = 852f
        while (x < 1330f) { c.drawLine(X(x), Y(440f), X(x), Y(512f), stroke); x += 14f }
    }

    // segment shapes inside one lens (unit square, a-g)
    private val segLines = arrayOf(
        floatArrayOf(0.30f, 0.20f, 0.72f, 0.20f), floatArrayOf(0.74f, 0.23f, 0.70f, 0.48f),
        floatArrayOf(0.70f, 0.53f, 0.66f, 0.78f), floatArrayOf(0.26f, 0.80f, 0.64f, 0.80f),
        floatArrayOf(0.30f, 0.53f, 0.26f, 0.78f), floatArrayOf(0.34f, 0.23f, 0.30f, 0.48f),
        floatArrayOf(0.31f, 0.50f, 0.69f, 0.50f))

    private fun drawDisplay(c: Canvas) {
        // rusty mounting bar, dark carrier board, clear plastic window
        paint.color = 0xFF7A4A2E.toInt(); c.drawRect(rect(772f, 232f, 1345f, 268f), paint)
        paint.color = 0xFF2A4038.toInt(); c.drawRoundRect(rect(762f, 262f, 1352f, 430f), 8f * sc, 8f * sc, paint)
        paint.color = 0xFFCFD8D6.toInt(); c.drawRoundRect(rect(782f, 285f, 1340f, 395f), 10f * sc, 10f * sc, paint)
        paint.color = 0xFFB89A5A.toInt()
        for (i in 0 until 18) c.drawRect(rect(790f + i * 30f, 398f, 802f + i * 30f, 428f), paint)
        val order = intArrayOf(-1, 1, 2, 3, 4, 5, 6, 7, 0)     // left digit isn't wired
        for (i in 0 until 9) {
            val cx = 838f + i * 63.5f
            val cy = 340f
            paint.color = 0xFFF2F0EE.toInt(); c.drawOval(rect(cx - 28f, cy - 36f, cx + 28f, cy + 36f), paint)
            stroke.color = 0xFFB7BBC0.toInt(); stroke.strokeWidth = 2.5f * sc
            c.drawOval(rect(cx - 28f, cy - 36f, cx + 28f, cy + 36f), stroke)
            val d = order[i]
            val l = cx - 28f; val t = cy - 36f
            stroke.strokeCap = Paint.Cap.ROUND
            for (s in 0 until 7) {
                val b = if (d < 0) 0 else seg[d * 8 + s]
                stroke.strokeWidth = 7f * sc
                stroke.color = if (b > 0) Color.argb(55 + b * 200 / 255, 255, 30, 20) else 0x18FF6060
                val p = segLines[s]
                c.drawLine(X(l + p[0] * 56f), Y(t + p[1] * 72f), X(l + p[2] * 56f), Y(t + p[3] * 72f), stroke)
            }
            val dp = if (d < 0) 0 else seg[d * 8 + 7]
            paint.color = if (dp > 0) Color.argb(55 + dp * 200 / 255, 255, 30, 20) else 0x18FF6060
            c.drawCircle(X(l + 0.80f * 56f), Y(t + 0.83f * 72f), 4.5f * sc, paint)
            stroke.strokeCap = Paint.Cap.BUTT
        }
    }

    private fun drawKeys(c: Canvas) {
        text.color = 0xFF151515.toInt(); text.textSize = 62f * sc
        for (k in keys) {
            val down = keyMask and (1 shl k.bit) != 0
            val r = rect(k.cx - 55f, k.cy - 34f, k.cx + 55f, k.cy + 34f)
            paint.color = 0xFF9C977A.toInt(); c.drawRoundRect(RectF(r).apply { offset(0f, 6f * sc) }, 8f * sc, 8f * sc, paint)
            paint.color = if (down) 0xFFE2D69C.toInt() else 0xFFF6EBB2.toInt()
            if (down) r.offset(0f, 4f * sc)
            c.drawRoundRect(r, 8f * sc, 8f * sc, paint)
            val ly = if (k.bit == 24) k.cy - 62f else k.cy - 62f
            val lab = when (k.label) { "^" -> "Λ"; "v" -> "V"; else -> k.label }
            c.drawText(lab, X(k.cx), Y(ly), text)
        }
    }

    // ---- touch ----
    private fun keyAt(x: Float, y: Float): Int {
        for (k in keys) {
            if (RectF(X(k.cx - 72f), Y(k.cy - 90f), X(k.cx + 72f), Y(k.cy + 50f)).contains(x, y)) return 1 shl k.bit
        }
        return 0
    }

    private fun updateKeys() {
        var m = 0
        for (v in pointers.values) m = m or v
        if (m != keyMask) {
            if (m and keyMask.inv() != 0) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            keyMask = m
            onInput?.invoke(m)
            invalidate()
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex; val x = e.getX(i); val y = e.getY(i)
                if (menuR.contains(x, y)) { onMenu?.invoke(); return true }
                val k = keyAt(x, y)
                if (k != 0) { pointers[e.getPointerId(i)] = k; updateKeys() }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> { pointers.remove(e.getPointerId(e.actionIndex)); updateKeys() }
            MotionEvent.ACTION_CANCEL -> { pointers.clear(); updateKeys() }
        }
        return true
    }
}
