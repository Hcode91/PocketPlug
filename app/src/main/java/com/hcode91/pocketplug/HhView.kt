package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.min

/**
 * Dedicated handheld / tabletop games (MAME's hh_* drivers): the case, the
 * screen (the core renders the game's own SVG artwork into the frame) and the
 * game's controls, all described by an [HhSkin] per machine. No maker's name
 * or logo is drawn.
 *
 * Input bits (hh_base.h): 0 up, 1 down, 2 left, 3 right, 4.. buttons;
 * switches are core options (setOption) and go out through [onOption].
 */
class HhView(ctx: Context, private val skin: HhSkin, private val srcW: Int, private val srcH: Int) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    var onOption: ((Int, Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    private val frame = Bitmap.createBitmap(srcW, srcH, Bitmap.Config.ARGB_8888)
    private val lock = Any()
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.DEFAULT_BOLD }
    private val pic = Paint(Paint.FILTER_BITMAP_FLAG or Paint.ANTI_ALIAS_FLAG)

    // layout in a W x H design space (skin.w x skin.h), fitted to the view
    private var sc = 1f; private var ox = 0f; private var oy = 0f
    private val pointers = HashMap<Int, Int>()     // pointer -> control index
    private val stickDir = HashMap<Int, Int>()     // pointer -> stick bits
    private var touchMask = 0
    private val optValue = IntArray(8).also { a -> skin.controls.forEach { if (it.kind == HhSkin.Kind.SWITCH) a[it.option] = it.initial } }

    fun initialOptions(): List<Pair<Int, Int>> =
        skin.controls.filter { it.kind == HhSkin.Kind.SWITCH }.flatMap { switchSets(it, optValue[it.option]) }

    private fun switchSets(k: HhSkin.Control, pos: Int): List<Pair<Int, Int>> =
        if (k.sets.isNotEmpty()) k.sets[pos] else listOf(k.option to pos)

    private val lit = BooleanArray(srcW * srcH)

    fun updateFrame(px: IntArray) {
        if (px.size < srcW * srcH) return
        if (skin.leds.isNotEmpty()) {
            var changed = false
            for (i in 0 until srcW * srcH) { val on = (px[i] and 0xFFFFFF) != 0; if (lit[i] != on) { lit[i] = on; changed = true } }
            if (changed) postInvalidate()
            return
        }
        synchronized(lock) { frame.setPixels(px, 0, srcW, 0, 0, srcW, srcH) }
        postInvalidate()
    }

    private fun on(y: Int, x: Int) = y in 0 until srcH && x in 0 until srcW && lit[y * srcW + x]

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        sc = min(w / skin.w, h / skin.h)
        ox = (w - skin.w * sc) / 2f
        oy = (h - skin.h * sc) / 2f
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(skin.backdrop)
        c.save(); c.translate(ox, oy); c.scale(sc, sc)
        skin.drawBody(c, paint, stroke)
        if (skin.leds.isNotEmpty()) {
            for (l in skin.leds) drawLed(c, l)
        } else drawScreen(c)
        val mask = touchMask or externalMask
        for (ctl in skin.controls) drawControl(c, ctl, mask)
        // menu dot
        paint.color = 0x55000000; c.drawCircle(skin.menu.x, skin.menu.y, 26f, paint)
        text.color = 0xCCFFFFFF.toInt(); text.textSize = 24f; c.drawText("≡", skin.menu.x, skin.menu.y + 9f, text)
        c.restore()
    }

    private fun drawLed(c: Canvas, l: HhSkin.Led) {
        when (l.kind) {
            HhSkin.LedKind.ROUND -> {
                val o = on(l.row, l.col)
                paint.color = if (o) l.color else blendDark(l.color)
                c.drawCircle(l.x, l.y, l.r, paint)
                if (o) { paint.color = (l.color and 0x00FFFFFF) or 0x40000000; c.drawCircle(l.x, l.y, l.r * 1.9f, paint)
                    if (l.text.isEmpty()) { paint.color = 0x88FFFFFF.toInt(); c.drawCircle(l.x - l.r * 0.3f, l.y - l.r * 0.3f, l.r * 0.3f, paint) } }
                if (l.text.isNotEmpty()) {
                    text.color = l.textColor; text.textSize = l.r * 0.95f
                    c.drawText(l.text, l.x, l.y + l.r * 0.34f, text)
                }
            }
            HhSkin.LedKind.BAR -> {
                // a thin upright lamp; l.r = half its height
                val o = on(l.row, l.col)
                paint.color = if (o) l.color else blendDark(l.color)
                c.drawRoundRect(l.x - l.r * 0.3f, l.y - l.r, l.x + l.r * 0.3f, l.y + l.r, l.r * 0.1f, l.r * 0.1f, paint)
                if (o) { paint.color = (l.color and 0x00FFFFFF) or 0x30000000; c.drawRoundRect(l.x - l.r * 0.7f, l.y - l.r * 1.3f, l.x + l.r * 0.7f, l.y + l.r * 1.3f, l.r * 0.5f, l.r * 0.5f, paint) }
            }
            HhSkin.LedKind.DIGIT -> {
                // 7 segments a-g on columns 0-6 of the row; l.r = digit height
                val h = l.r; val w = h * 0.55f; val t = h * 0.12f
                val x0 = l.x - w / 2; val y0 = l.y - h / 2
                val segs = arrayOf(
                    floatArrayOf(x0 + t, y0, x0 + w - t, y0 + t), floatArrayOf(x0 + w - t, y0 + t, x0 + w, y0 + h / 2 - t / 2),
                    floatArrayOf(x0 + w - t, y0 + h / 2 + t / 2, x0 + w, y0 + h - t), floatArrayOf(x0 + t, y0 + h - t, x0 + w - t, y0 + h),
                    floatArrayOf(x0, y0 + h / 2 + t / 2, x0 + t, y0 + h - t), floatArrayOf(x0, y0 + t, x0 + t, y0 + h / 2 - t / 2),
                    floatArrayOf(x0 + t, y0 + h / 2 - t / 2, x0 + w - t, y0 + h / 2 + t / 2))
                for (i in 0 until 7) {
                    paint.color = if (on(l.row, i)) l.color else blendDark(l.color)
                    val sg = segs[i]; c.drawRoundRect(sg[0], sg[1], sg[2], sg[3], t / 2, t / 2, paint)
                }
            }
        }
    }

    private fun blendDark(c: Int) = Color.rgb(Color.red(c) / 6 + 18, Color.green(c) / 6 + 14, Color.blue(c) / 6 + 14)

    /** white LCD glass -> clear, dark segments -> dark ink */
    private val ink = Paint(Paint.FILTER_BITMAP_FLAG or Paint.ANTI_ALIAS_FLAG).apply {
        colorFilter = ColorMatrixColorFilter(ColorMatrix(floatArrayOf(
            0f, 0f, 0f, 0f, 18f,
            0f, 0f, 0f, 0f, 20f,
            0f, 0f, 0f, 0f, 16f,
            -0.33f, -0.33f, -0.34f, 0f, 255f)))
    }

    private fun drawScreen(c: Canvas) {
        if (skin.parts.isNotEmpty()) {
            synchronized(lock) {
                for (p in skin.parts) {
                    val src = Rect(0, (p.srcTop * srcH).toInt(), srcW, (p.srcBottom * srcH).toInt())
                    c.drawBitmap(frame, src, p.dst, ink)
                }
            }
            return
        }
        // screen bezel and picture
        val s = skin.screen
        paint.color = skin.bezel
        c.drawRoundRect(RectF(s.left - skin.bezelPad, s.top - skin.bezelPad, s.right + skin.bezelPad, s.bottom + skin.bezelPad), skin.bezelPad, skin.bezelPad, paint)
        paint.color = Color.BLACK; c.drawRect(s, paint)
        synchronized(lock) {
            // keep the artwork's aspect inside the screen window
            val fa = srcW.toFloat() / srcH; val sa = s.width() / s.height()
            val dst = if (fa > sa) { val hh = s.width() / fa; RectF(s.left, s.centerY() - hh / 2, s.right, s.centerY() + hh / 2) }
                      else { val ww = s.height() * fa; RectF(s.centerX() - ww / 2, s.top, s.centerX() + ww / 2, s.bottom) }
            c.drawBitmap(frame, null, dst, pic)
        }
        // a little glass sheen
        paint.shader = LinearGradient(s.left, s.top, s.left, s.bottom, 0x22FFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP)
        c.drawRect(s.left, s.top, s.right, s.top + s.height() * 0.4f, paint); paint.shader = null
    }

    private fun drawControl(c: Canvas, k: HhSkin.Control, mask: Int) {
        text.textSize = k.labelSize
        when (k.kind) {
            HhSkin.Kind.STICK -> {
                val dir = mask and 0xF
                val dx = (if (dir and 8 != 0) 1 else 0) - (if (dir and 4 != 0) 1 else 0)
                val dy = (if (dir and 2 != 0) 1 else 0) - (if (dir and 1 != 0) 1 else 0)
                paint.color = k.base; c.drawCircle(k.x, k.y, k.r, paint)
                paint.color = 0x33000000; c.drawCircle(k.x, k.y, k.r * 0.75f, paint)
                if (k.arrows) {
                    paint.color = 0xFFF0F0F0.toInt()
                    val a = k.r * 0.17f; val d = k.r * 1.0f
                    for ((ux, uy) in listOf(0f to -1f, 0f to 1f, -1f to 0f, 1f to 0f)) {
                        val cx = k.x + ux * d; val cy = k.y + uy * d
                        c.drawPath(Path().apply {
                            moveTo(cx + ux * a, cy + uy * a)
                            lineTo(cx - ux * a + uy * a, cy - uy * a + ux * a)
                            lineTo(cx - ux * a - uy * a, cy - uy * a - ux * a); close()
                        }, paint)
                    }
                }
                val kx = k.x + dx * k.r * 0.35f; val ky = k.y + dy * k.r * 0.35f
                paint.color = 0xFF202020.toInt(); c.drawCircle(k.x, k.y, k.r * 0.2f, paint)
                stroke.color = 0xFF202020.toInt(); stroke.strokeWidth = k.r * 0.22f; c.drawLine(k.x, k.y, kx, ky, stroke)
                paint.color = k.color; c.drawCircle(kx, ky, k.r * 0.42f, paint)
                paint.color = 0x44FFFFFF; c.drawCircle(kx - k.r * 0.12f, ky - k.r * 0.12f, k.r * 0.16f, paint)
            }
            HhSkin.Kind.BUTTON -> if (k.bw > 0f) {
                val down = mask and k.bit != 0
                val off = if (down) 5f else 0f
                paint.color = darker(darker(k.color)); c.drawRoundRect(k.x - k.bw / 2, k.y - k.bh / 2 + 8f, k.x + k.bw / 2, k.y + k.bh / 2 + 8f, k.bh * 0.3f, k.bh * 0.3f, paint)
                paint.color = if (down) darker(k.color) else k.color
                c.drawRoundRect(k.x - k.bw / 2, k.y - k.bh / 2 + off, k.x + k.bw / 2, k.y + k.bh / 2 + off, k.bh * 0.3f, k.bh * 0.3f, paint)
                if (!down && k.shine) { paint.color = 0x30FFFFFF; c.drawRoundRect(k.x - k.bw / 2 + 8f, k.y - k.bh / 2 + 6f, k.x + k.bw / 2 - 8f, k.y - k.bh / 2 + k.bh * 0.3f, 10f, 10f, paint) }
                if (k.face.isNotEmpty()) {
                    text.color = k.faceColor; text.textSize = k.faceSize
                    val lines = k.face.split("\n")
                    val y0 = k.y + off - (lines.size - 1) * k.faceSize * 0.55f + k.faceSize * 0.36f
                    lines.forEachIndexed { li, ln -> c.drawText(ln, k.x, y0 + li * k.faceSize * 1.1f, text) }
                    text.textSize = k.labelSize
                }
            } else {
                val down = mask and k.bit != 0
                paint.color = k.base; c.drawCircle(k.x, k.y, k.r * 1.18f, paint)
                paint.color = if (down) darker(k.color) else k.color
                c.drawCircle(k.x, k.y + (if (down) k.r * 0.06f else 0f), k.r, paint)
                if (!down) { paint.color = 0x33FFFFFF; c.drawCircle(k.x - k.r * 0.3f, k.y - k.r * 0.3f, k.r * 0.3f, paint) }
            }
            HhSkin.Kind.SWITCH -> {
                // a slide switch with its positions labelled underneath
                val n = k.positions.size
                if (k.vertical) {
                    val hgt = k.r * 2f * n; val top = k.y - hgt / 2
                    paint.color = 0xFF151515.toInt(); c.drawRoundRect(k.x - k.r * 0.45f, top, k.x + k.r * 0.45f, top + hgt, k.r * 0.4f, k.r * 0.4f, paint)
                    val v = optValue[k.option]
                    val ky = top + k.r * (2 * v + 1)
                    paint.color = k.color; c.drawRoundRect(k.x - k.r * 0.9f, ky - k.r * 0.6f, k.x + k.r * 0.9f, ky + k.r * 0.6f, k.r * 0.25f, k.r * 0.25f, paint)
                    text.color = k.labelColor
                    for (i in 0 until n) c.drawText(k.positions[i], k.x + (if (k.labelLeft) -1 else 1) * (k.r + k.labelSize), top + k.r * (2 * i + 1) + k.labelSize * 0.35f, text)
                    if (k.label.isNotEmpty()) c.drawText(k.label, k.x, top - k.labelSize * 1.6f, text)
                    return
                }
                val w = k.r * 2f * n; val l = k.x - w / 2
                if (k.plate != 0) { paint.color = k.plate; c.drawRect(k.x - k.plateW / 2, k.y - k.plateH / 2, k.x + k.plateW / 2, k.y + k.plateH / 2, paint)
                    paint.color = 0x22000000; c.drawRect(k.x - k.plateW / 2, k.y + k.plateH / 2 - 4f, k.x + k.plateW / 2, k.y + k.plateH / 2, paint) }
                paint.color = 0xFF151515.toInt(); c.drawRoundRect(l, k.y - k.r * 0.45f, l + w, k.y + k.r * 0.45f, k.r * 0.4f, k.r * 0.4f, paint)
                val v = optValue[k.option]
                val kx = l + k.r * (2 * v + 1)
                paint.color = k.color; c.drawRoundRect(kx - k.r * 0.8f, k.y - k.r * 0.55f, kx + k.r * 0.8f, k.y + k.r * 0.55f, k.r * 0.25f, k.r * 0.25f, paint)
                text.color = k.labelColor
                if (k.sideLabels && n == 3) {
                    text.textAlign = Paint.Align.RIGHT; c.drawText(k.positions[0], l - 12f, k.y + k.labelSize * 0.35f, text)
                    text.textAlign = Paint.Align.CENTER; c.drawText(k.positions[1], k.x, k.y - k.r * 0.7f, text)
                    text.textAlign = Paint.Align.LEFT; c.drawText(k.positions[2], l + w + 12f, k.y + k.labelSize * 0.35f, text)
                    text.textAlign = Paint.Align.CENTER
                } else for (i in 0 until n) c.drawText(k.positions[i], l + k.r * (2 * i + 1), k.y + k.r + k.labelSize, text)
            }
        }
        if (k.label.isNotEmpty()) {
            text.color = k.labelColor
            val ly = if (k.labelAbove) k.y - k.r * 1.3f - k.labelSize * 0.3f else k.y + k.r * 1.3f + k.labelSize
            if (k.kind != HhSkin.Kind.SWITCH || k.labelAbove) c.drawText(k.label, k.x, ly, text)
        }
    }

    private fun darker(c: Int) = Color.rgb(Color.red(c) * 6 / 10, Color.green(c) * 6 / 10, Color.blue(c) * 6 / 10)

    // ---- touch ----
    private fun hit(sx: Float, sy: Float): Int {
        val x = (sx - ox) / sc; val y = (sy - oy) / sc
        skin.controls.forEachIndexed { i, k ->
            val reach = if (k.kind == HhSkin.Kind.SWITCH) k.r * k.positions.size + 20f else k.r * 1.6f
            if (k.kind == HhSkin.Kind.SWITCH && k.vertical) {
                if (abs(x - k.x) < k.r * 2.5f && abs(y - k.y) < reach) return i
            } else if (k.kind == HhSkin.Kind.SWITCH) {
                if (abs(x - k.x) < reach && abs(y - k.y) < k.r * 2f) return i
            } else if (k.bw > 0f) {
                if (abs(x - k.x) < k.bw / 2 + 20f && abs(y - k.y) < k.bh / 2 + 20f) return i
            } else if (hypot(x - k.x, y - k.y) < reach) return i
        }
        if (hypot(x - skin.menu.x, y - skin.menu.y) < 60f) return -2
        return -1
    }

    private fun stickBits(k: HhSkin.Control, sx: Float, sy: Float): Int {
        val x = (sx - ox) / sc - k.x; val y = (sy - oy) / sc - k.y
        if (hypot(x, y) < k.r * 0.25f) return 0
        val a = Math.toDegrees(atan2(y.toDouble(), x.toDouble()))
        var m = 0
        if (k.eightWay) {
            if (a > -157.5 && a < -22.5) m = m or 1
            if (a > 22.5 && a < 157.5) m = m or 2
            if (a > 112.5 || a < -112.5) m = m or 4
            if (a > -67.5 && a < 67.5) m = m or 8
        } else {
            m = when {
                a >= -135 && a < -45 -> 1
                a >= 45 && a < 135 -> 2
                a >= 135 || a < -135 -> 4
                else -> 8
            }
        }
        return m
    }

    private fun update() {
        var m = 0
        for ((p, i) in pointers) {
            val k = skin.controls[i]
            m = m or if (k.kind == HhSkin.Kind.STICK) (stickDir[p] ?: 0) else k.bit
        }
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
                val i = e.actionIndex; val id = e.getPointerId(i)
                val h = hit(e.getX(i), e.getY(i))
                when {
                    h == -2 -> onMenu?.invoke()
                    h >= 0 -> {
                        val k = skin.controls[h]
                        if (k.kind == HhSkin.Kind.SWITCH) {
                            val n = k.positions.size
                            // tap the left or right half to move the slider that way
                            val v = optValue[k.option]
                            optValue[k.option] = if (k.vertical) {
                                // tap a position to move the slider there
                                val y = (e.getY(i) - oy) / sc
                                ((y - (k.y - k.r * n)) / (2 * k.r)).toInt().coerceIn(0, n - 1)
                            } else {
                                val x = (e.getX(i) - ox) / sc
                                if (x < k.x - k.r * 0.5f) maxOf(0, v - 1) else if (x > k.x + k.r * 0.5f) minOf(n - 1, v + 1) else (v + 1) % n
                            }
                            for ((o, value) in switchSets(k, optValue[k.option])) onOption?.invoke(o, value)
                            performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
                            invalidate()
                        } else {
                            pointers[id] = h
                            if (k.kind == HhSkin.Kind.STICK) stickDir[id] = stickBits(k, e.getX(i), e.getY(i))
                            update()
                        }
                    }
                }
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until e.pointerCount) {
                    val id = e.getPointerId(i)
                    val idx = pointers[id] ?: continue
                    val k = skin.controls[idx]
                    if (k.kind == HhSkin.Kind.STICK) stickDir[id] = stickBits(k, e.getX(i), e.getY(i))
                }
                update()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val id = e.getPointerId(e.actionIndex); pointers.remove(id); stickDir.remove(id); update()
            }
            MotionEvent.ACTION_CANCEL -> { pointers.clear(); stickDir.clear(); update() }
        }
        return true
    }

    companion object {
        fun keyBit(code: Int): Int = when (code) {
            KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> 0x01
            KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> 0x02
            KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> 0x04
            KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> 0x08
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_Z, KeyEvent.KEYCODE_SPACE -> 0x10
            KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_X -> 0x20
            KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_C -> 0x40
            KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_V -> 0x80
            KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_ENTER -> 0x100
            KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_TAB -> 0x200
            else -> 0
        }
    }
}

/** A handheld's look: case, screen window and controls, in a w x h design space. */
class HhSkin(
    val w: Float, val h: Float,
    val backdrop: Int,
    val screen: RectF,
    val bezel: Int,
    val bezelPad: Float,
    val menu: PointF,
    val controls: List<Control>,
    val drawBody: (Canvas, Paint, Paint) -> Unit,
    /** LED / lamp games: the lights (the frame is the PWM matrix) instead of a screen picture */
    val leds: List<Led> = emptyList(),
    /** wide cases (Game & Watch style) are shown in landscape */
    val landscape: Boolean = false,
    /** split screens: a band of the frame (srcTop..srcBottom, 0-1 of its height) shown in dst.
     *  These are LCDs with printed artwork under the glass: the body draws the print and
     *  only the dark segments are laid over it */
    val parts: List<ScreenPart> = emptyList(),
) {
    class ScreenPart(val srcTop: Float, val srcBottom: Float, val dst: RectF)
    enum class Kind { STICK, BUTTON, SWITCH }
    enum class LedKind { ROUND, DIGIT, BAR }
    class Led(val kind: LedKind, val row: Int, val col: Int, val x: Float, val y: Float, val r: Float, val color: Int = 0xFFFF2A20.toInt(),
              /** printed over a round lamp (e.g. its number) */
              val text: String = "", val textColor: Int = Color.WHITE)
    class Control(
        val kind: Kind, val x: Float, val y: Float, val r: Float,
        val color: Int = 0xFFD02020.toInt(), val base: Int = 0xFF1A1A1A.toInt(),
        val bit: Int = 0, val label: String = "", val labelColor: Int = Color.WHITE, val labelSize: Float = 30f,
        val labelAbove: Boolean = false,
        val option: Int = 0, val positions: List<String> = emptyList(), val initial: Int = 0,
        val eightWay: Boolean = true,
        // rectangular buttons (0 = round)
        val bw: Float = 0f, val bh: Float = 0f,
        val arrows: Boolean = false,
        // switches: the options each position sets (position -> list of option id to value)
        val sets: List<List<Pair<Int, Int>>> = emptyList(),
        val plate: Int = 0, val plateW: Float = 0f, val plateH: Float = 0f, val sideLabels: Boolean = false,
        val vertical: Boolean = false, val labelLeft: Boolean = false,
        // text printed on the button itself
        val face: String = "", val faceColor: Int = Color.WHITE, val faceSize: Float = 30f, val shine: Boolean = true,
    )

    companion object {
        fun forMachine(id: String): HhSkin? = when (id) {
            "alnattck" -> alienAttack()
            "bambball" -> bambinoBasketball()
            "alnchase" -> alienChase()
            "alphie" -> alphie()
            "amaztron" -> amazeATron()
            "arcmania" -> arcadeMania()
            "arrball" -> arrangeBall()
            "bankshot" -> bankShot()
            "astrocmd" -> astroCommand()
            "atakaast" -> atakaAsteroidov()
            "auslalom" -> autoslalom()
            "autorace" -> autoRace()
            "bassmate" -> bassmate()
            "bbear" -> talkingBear()
            else -> null
        }

        /** Tandy (A-One LSI) Arrange Ball (1980), from a photo: the black case with a blank badge,
         *  the red panel with the 2-digit LED, the 4 x 4 numbered lamps joined by lines, the arc of
         *  lamps up to START and the L1-L4 tabs; underneath the speaker slots, the ON / OFF and
         *  SLOW / FAST slides and the STOP and SHOT push buttons. */
        private fun bankShot(): HhSkin {
            // the table: MAME layout units (17 x 11) -> design space
            fun tx(x: Float) = 75f + x * 50f
            fun ty(y: Float) = 430f + y * 50f
            val spots = listOf(
                9 to 7 to (8f to 0f),
                10 to 7 to (1f to 1f),
                10 to 6 to (3f to 1f),
                10 to 5 to (5f to 1f),
                10 to 4 to (7f to 1f),
                10 to 3 to (9f to 1f),
                10 to 2 to (11f to 1f),
                10 to 1 to (13f to 1f),
                10 to 0 to (15f to 1f),
                9 to 6 to (2f to 2f),
                9 to 5 to (4f to 2f),
                9 to 4 to (6f to 2f),
                9 to 3 to (8f to 2f),
                9 to 2 to (10f to 2f),
                9 to 1 to (12f to 2f),
                9 to 0 to (14f to 2f),
                8 to 7 to (1f to 3f),
                8 to 6 to (3f to 3f),
                8 to 5 to (5f to 3f),
                8 to 4 to (7f to 3f),
                8 to 3 to (9f to 3f),
                8 to 2 to (11f to 3f),
                8 to 1 to (13f to 3f),
                8 to 0 to (15f to 3f),
                7 to 6 to (2f to 4f),
                7 to 5 to (4f to 4f),
                7 to 4 to (6f to 4f),
                7 to 3 to (8f to 4f),
                7 to 2 to (10f to 4f),
                7 to 1 to (12f to 4f),
                7 to 0 to (14f to 4f),
                6 to 7 to (1f to 5f),
                6 to 6 to (3f to 5f),
                6 to 5 to (5f to 5f),
                6 to 4 to (7f to 5f),
                6 to 3 to (9f to 5f),
                6 to 2 to (11f to 5f),
                6 to 1 to (13f to 5f),
                6 to 0 to (15f to 5f),
                5 to 6 to (2f to 6f),
                5 to 5 to (4f to 6f),
                5 to 4 to (6f to 6f),
                5 to 3 to (8f to 6f),
                5 to 2 to (10f to 6f),
                5 to 1 to (12f to 6f),
                5 to 0 to (14f to 6f),
                4 to 7 to (1f to 7f),
                4 to 6 to (3f to 7f),
                4 to 5 to (5f to 7f),
                4 to 4 to (7f to 7f),
                4 to 3 to (9f to 7f),
                4 to 2 to (11f to 7f),
                4 to 1 to (13f to 7f),
                4 to 0 to (15f to 7f),
                3 to 6 to (2f to 8f),
                3 to 5 to (4f to 8f),
                3 to 4 to (6f to 8f),
                3 to 3 to (8f to 8f),
                3 to 2 to (10f to 8f),
                3 to 1 to (12f to 8f),
                3 to 0 to (14f to 8f),
                2 to 7 to (1f to 9f),
                2 to 6 to (3f to 9f),
                2 to 5 to (5f to 9f),
                2 to 4 to (7f to 9f),
                2 to 3 to (9f to 9f),
                2 to 2 to (11f to 9f),
                2 to 1 to (13f to 9f),
                2 to 0 to (15f to 9f),
                3 to 7 to (8f to 10f)
            )
            val leds = spots.map { (ro, xy) -> Led(LedKind.ROUND, ro.first, ro.second, tx(xy.first + 0.5f), ty(xy.second + 0.5f), 17f, 0xFFFF3A44.toInt()) }
            fun key(x: Float, y: Float, bit: Int, label: String) = Control(Kind.BUTTON, x, y, 40f, color = 0xFF202024.toInt(), bit = bit, bw = 170f, bh = 90f,
                face = "", shine = false, label = label, labelAbove = y < 600f, labelColor = 0xFFE8E8E8.toInt(), labelSize = 26f)
            return HhSkin(
                1000f, 1460f, 0xFF2E2E34.toInt(),
                screen = RectF(), bezel = 0, bezelPad = 0f,
                menu = PointF(500f, 1420f),
                controls = listOf(
                    key(230f, 300f, 0x08, "SELECT / SCORE"), key(500f, 300f, 0x10, "BALL UP"), key(770f, 300f, 0x20, "BALL OVER"),
                    key(230f, 1130f, 0x01, "ANGLE"), key(500f, 1130f, 0x02, "AIM"), key(770f, 1130f, 0x04, "CUE UP / SHOOT"),
                ),
                leds = leds,
                drawBody = { c, p, s ->
                    // the black case
                    p.color = 0xFF141418.toInt(); c.drawRoundRect(30f, 40f, 970f, 1400f, 60f, 60f, p)
                    p.color = 0xFF1E1E24.toInt(); c.drawRoundRect(55f, 160f, 945f, 1260f, 40f, 40f, p)
                    // white rails and the dark felt, as the unit's printed table
                    p.color = 0xFFE6E6E6.toInt(); c.drawRect(tx(0f), ty(0f), tx(17f), ty(11f), p)
                    p.color = 0xFF210709.toInt(); c.drawRect(tx(1f), ty(1f), tx(16f), ty(10f), p)
                    // the six pockets
                    for ((px, py) in listOf(0.7f to 0.7f, 8.5f to 0.5f, 16.3f to 0.7f, 0.7f to 10.3f, 8.5f to 10.5f, 16.3f to 10.3f)) {
                        p.color = 0xFF0A0A0C.toInt(); c.drawCircle(tx(px), ty(py), 34f, p)
                    }
                    // diamond marks on the rails
                    p.color = 0xFF202020.toInt()
                    for (x in listOf(3.5f, 5.5f, 11.5f, 13.5f)) { c.drawRect(tx(x) - 5f, ty(0.2f), tx(x) + 5f, ty(0.8f), p); c.drawRect(tx(x) - 5f, ty(10.2f), tx(x) + 5f, ty(10.8f), p) }
                    for (y in listOf(3.5f, 5.5f, 7.5f)) { c.drawRect(tx(0.2f), ty(y) - 5f, tx(0.8f), ty(y) + 5f, p); c.drawRect(tx(16.2f), ty(y) - 5f, tx(16.8f), ty(y) + 5f, p) }
                    // unlit lamps
                    p.color = 0xFF3A0C10.toInt()
                    for (l in leds) c.drawCircle(l.x, l.y, 16f, p)
                    // speaker grille
                    p.color = 0xFF0A0A0C.toInt()
                    for (i in 0 until 8) c.drawRoundRect(400f + i * 26f, 1300f, 412f + i * 26f, 1370f, 6f, 6f, p)
                    s.color = 0xFF5A5A62.toInt(); s.strokeWidth = 3f
                    c.drawRoundRect(30f, 40f, 970f, 1400f, 60f, 60f, s)
                },
            )
        }

        private fun arrangeBall(): HhSkin {
            // MAME layout units -> design space
            fun lx(x: Float) = 200f + x * 24f
            fun ly(y: Float) = 330f + (y - 3f) * 24f
            val grid = (0 until 16).map { n ->
                Led(LedKind.ROUND, n / 4, n % 4, lx(1.5f + 5f * (n % 4)), ly(11.5f + 5f * (n / 4)), 24f, 0xFFFF2030.toInt(),
                    text = "${n + 1}", textColor = 0xFFE8E2E0.toInt())
            }
            val arcPos = listOf(22.5f to 26.5f, 21.9f to 20.5f, 21.2f to 15f, 20.4f to 9.5f, 17.1f to 7.5f, 13.9f to 8.5f)
            val arc = arcPos.mapIndexed { i, (x, y) -> Led(LedKind.ROUND, 6, i, lx(x), ly(y), if (i == 0) 16f else 14f, 0xFFFF3A20.toInt()) }
            val panelRed = 0xFF6E1420.toInt(); val orange = 0xFFE8855A.toInt()
            return HhSkin(
                1000f, 1720f, 0xFF3A3A3E.toInt(),
                screen = RectF(), bezel = 0, bezelPad = 0f,
                menu = PointF(500f, 1680f),
                controls = listOf(
                    Control(Kind.SWITCH, 420f, 1440f, 34f, color = 0xFF9A9AA0.toInt(), option = 0, vertical = true,
                        positions = listOf("", ""), initial = 0, sets = listOf(listOf(0 to 0), listOf(0 to 1))),
                    Control(Kind.BUTTON, 590f, 1440f, 40f, color = 0xFF26262C.toInt(), bit = 0x2, bw = 120f, bh = 108f,
                        face = "PUSH", faceColor = 0xFF8A8A90.toInt(), faceSize = 28f, shine = false,
                        label = "STOP", labelAbove = true, labelColor = 0xFFE0E0E0.toInt(), labelSize = 28f),
                    Control(Kind.BUTTON, 760f, 1440f, 40f, color = 0xFF26262C.toInt(), bit = 0x1, bw = 120f, bh = 108f,
                        face = "PUSH", faceColor = 0xFF8A8A90.toInt(), faceSize = 28f, shine = false,
                        label = "SHOT", labelAbove = true, labelColor = 0xFFE0E0E0.toInt(), labelSize = 28f),
                ),
                leds = grid + arc + listOf(
                    Led(LedKind.DIGIT, 5, 0, lx(2.75f), ly(5.5f), 88f, 0xFFFF2040.toInt()),
                    Led(LedKind.DIGIT, 4, 0, lx(5.25f), ly(5.5f), 88f, 0xFFFF2040.toInt()),
                ),
                drawBody = { c, p, s ->
                    // the black case, wider at the top
                    val body = Path().apply {
                        moveTo(190f, 60f); quadTo(500f, 20f, 810f, 60f); quadTo(880f, 70f, 885f, 150f)
                        lineTo(900f, 1580f); quadTo(900f, 1650f, 830f, 1655f); lineTo(170f, 1655f); quadTo(100f, 1650f, 100f, 1580f)
                        lineTo(115f, 150f); quadTo(120f, 70f, 190f, 60f); close()
                    }
                    p.color = 0xFF16161E.toInt(); c.drawPath(body, p)
                    p.color = 0xFF1E1E28.toInt(); c.drawRoundRect(140f, 230f, 860f, 1150f, 60f, 60f, p)
                    // the blank badge (a dark arch with a gold rim)
                    s.color = 0xFFB8A040.toInt(); s.strokeWidth = 5f
                    val badge = Path().apply { moveTo(300f, 205f); quadTo(500f, 70f, 700f, 205f); close() }
                    p.color = 0xFF2A2A2A.toInt(); c.drawPath(badge, p); c.drawPath(badge, s)
                    // the red panel
                    p.color = 0xFF0C0C10.toInt(); c.drawRoundRect(lx(-2.2f), ly(1.6f), lx(26.2f), ly(33.4f), 30f, 30f, p)
                    p.color = panelRed; c.drawRoundRect(lx(-1.4f), ly(2.3f), lx(25.4f), ly(32.7f), 14f, 14f, p)
                    p.color = 0xFF5A0E18.toInt(); c.drawRect(lx(-1.4f), ly(9.5f), lx(19.5f), ly(28.2f), p)
                    // display window
                    s.color = orange; s.strokeWidth = 6f
                    p.color = 0xFF220406.toInt(); c.drawRoundRect(lx(0.5f), ly(3f), lx(7.5f), ly(8f), 10f, 10f, p)
                    c.drawRoundRect(lx(0.5f), ly(3f), lx(7.5f), ly(8f), 10f, 10f, s)
                    // the lines joining the lamps
                    s.color = orange; s.strokeWidth = 5f
                    fun g(cx: Int, cy: Int) = PointF(lx(1.5f + 5f * cx), ly(11.5f + 5f * cy))
                    for (i in 0 until 4) {
                        c.drawLine(g(0, i).x, g(0, i).y, g(3, i).x, g(3, i).y, s)
                        c.drawLine(g(i, 0).x, g(i, 0).y, g(i, 3).x, g(i, 3).y, s)
                    }
                    for ((a, b) in listOf((1 to 0) to (0 to 1), (3 to 0) to (2 to 1), (1 to 1) to (2 to 2), (2 to 1) to (1 to 2), (1 to 2) to (0 to 3), (2 to 2) to (3 to 3))) {
                        val pa = g(a.first, a.second); val pb = g(b.first, b.second); c.drawLine(pa.x, pa.y, pb.x, pb.y, s)
                    }
                    // white rings round the numbered lamps
                    for (n in 0 until 16) {
                        val q = g(n % 4, n / 4)
                        p.color = 0xFFF2EEEA.toInt(); c.drawCircle(q.x, q.y, 36f, p)
                        p.color = 0xFF2A0608.toInt(); c.drawCircle(q.x, q.y, 28f, p)
                    }
                    // rings round the arc lamps, the small dots between them, START
                    for ((x, y) in arcPos) { p.color = orange; c.drawCircle(lx(x), ly(y), 24f, p); p.color = 0xFF2A0608.toInt(); c.drawCircle(lx(x), ly(y), 18f, p) }
                    s.color = orange; s.strokeWidth = 5f; c.drawCircle(lx(22.5f), ly(26.5f), 34f, s)
                    p.color = orange
                    for ((x, y) in listOf(15.5f to 8f, 18.75f to 8.5f, 20.67f to 11.43f, 20.93f to 13.07f, 21.43f to 16.93f, 21.67f to 18.57f, 22.1f to 22.4f, 22.27f to 24f))
                        c.drawCircle(lx(x), ly(y), 6f, p)
                    val t = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.DEFAULT_BOLD; color = 0xFFF0F0F0.toInt(); textSize = 26f }
                    c.drawText("START", lx(22.5f), ly(29.3f), t)
                    // the L1-L4 tabs
                    t.textSize = 30f
                    for (i in 0 until 4) {
                        val cx = lx(1.5f + 5f * i)
                        p.color = if (i % 2 == 0) orange else 0xFFF2EEEA.toInt()
                        c.drawRoundRect(cx - 40f, ly(28.9f), cx + 40f, ly(29.6f) + 30f, 10f, 10f, p)
                        c.drawCircle(cx, ly(29.6f) + 12f, 40f, p)
                        t.color = if (i % 2 == 0) 0xFFF8F0E8.toInt() else 0xFF3A2A2A.toInt()
                        c.drawText("L${i + 1}", cx, ly(29.9f) + 6f, t)
                    }
                    // speaker slots
                    p.color = 0xFF0A0A0E.toInt()
                    for (i in 0 until 10) c.drawRoundRect(368f + i * 28f, 1210f, 382f + i * 28f, 1290f, 7f, 7f, p)
                    // the control well, the ON / OFF slide (always on) and the labels
                    p.color = 0xFF101016.toInt(); c.drawRoundRect(170f, 1340f, 830f, 1560f, 16f, 16f, p)
                    p.color = 0xFF151515.toInt(); c.drawRoundRect(234f, 1378f, 266f, 1502f, 14f, 14f, p)
                    p.color = 0xFF9A9AA0.toInt(); c.drawRoundRect(220f, 1384f, 280f, 1428f, 8f, 8f, p)
                    t.color = 0xFFE0E0E0.toInt(); t.textSize = 28f
                    c.drawText("ON", 250f, 1366f, t); c.drawText("OFF", 250f, 1540f, t)
                    c.drawText("SLOW", 420f, 1366f, t); c.drawText("FAST", 420f, 1540f, t)
                },
            )
        }

        /** Epoch Astro Command (1982), from a photo: the black tabletop with the wide smoked window
         *  over the VFD, the joystick with its arrows, the OFF / ON slide, SELECT and START, the
         *  round BOMB and square MISSILE buttons, and the silver JOYSTICK / MODE / FIRE strip. */
        private fun astroCommand() = HhSkin(
            1000f, 1200f, 0xFF8E8E8A.toInt(),
            screen = RectF(150f, 190f, 850f, 381f), bezel = 0xFF050505.toInt(), bezelPad = 6f,
            menu = PointF(500f, 1165f),
            controls = listOf(
                Control(Kind.STICK, 250f, 860f, 105f, color = 0xFF151515.toInt(), base = 0xFF242424.toInt(), arrows = true, eightWay = true),
                Control(Kind.BUTTON, 500f, 820f, 40f, color = 0xFF1C1C1C.toInt(), bit = 0x10, bw = 96f, bh = 60f,
                    label = "SELECT", labelColor = 0xFFE8E8E8.toInt(), labelSize = 24f),
                Control(Kind.BUTTON, 500f, 940f, 40f, color = 0xFF1C1C1C.toInt(), bit = 0x20, bw = 96f, bh = 60f,
                    label = "START", labelColor = 0xFFE8E8E8.toInt(), labelSize = 24f),
                Control(Kind.BUTTON, 760f, 700f, 62f, color = 0xFF1A1A1A.toInt(), base = 0xFF0C0C0C.toInt(), bit = 0x80,
                    label = "BOMB", labelColor = 0xFFE8E8E8.toInt(), labelSize = 24f),
                Control(Kind.BUTTON, 690f, 850f, 40f, color = 0xFF1A1A1A.toInt(), bit = 0x40, bw = 130f, bh = 118f,
                    label = "MISSILE", labelColor = 0xFFE8E8E8.toInt(), labelSize = 24f),
            ),
            drawBody = { c, p, _ ->
                // the case: a slanted top over the control deck
                p.color = 0xFFB4B4B0.toInt(); c.drawRoundRect(40f, 40f, 960f, 1130f, 30f, 30f, p)
                p.color = 0xFF1E1E1E.toInt(); c.drawRoundRect(50f, 52f, 950f, 1110f, 26f, 26f, p)
                p.color = 0xFF2A2A2A.toInt(); c.drawRoundRect(80f, 80f, 920f, 560f, 16f, 16f, p)
                // the smoked window
                p.shader = LinearGradient(0f, 120f, 0f, 520f, intArrayOf(0xFF0E0C0C.toInt(), 0xFF1E1A1A.toInt(), 0xFF0A0A0A.toInt()), null, Shader.TileMode.CLAMP)
                c.drawRoundRect(120f, 120f, 880f, 500f, 10f, 10f, p); p.shader = null
                // the deck
                val deck = Path().apply { moveTo(60f, 600f); lineTo(940f, 600f); lineTo(950f, 1060f); lineTo(50f, 1060f); close() }
                p.color = 0xFF262626.toInt(); c.drawPath(deck, p)
                p.color = 0xFF303030.toInt(); c.drawRoundRect(380f, 640f, 620f, 1010f, 12f, 12f, p)
                p.color = 0xFF1A1A1A.toInt(); c.drawRoundRect(395f, 655f, 605f, 995f, 10f, 10f, p)
                // the OFF / ON slide (always on)
                p.color = 0xFF080808.toInt(); c.drawRoundRect(440f, 690f, 560f, 724f, 12f, 12f, p)
                p.color = 0xFF3A3A3A.toInt(); c.drawRoundRect(500f, 682f, 556f, 732f, 6f, 6f, p)
                val t = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.DEFAULT_BOLD; color = 0xFFE8E8E8.toInt(); textSize = 22f }
                c.drawText("OFF", 440f, 760f, t); c.drawText("ON", 556f, 760f, t)
                // the silver strip
                p.color = 0xFFD8D8D4.toInt(); c.drawRect(70f, 1050f, 930f, 1090f, p)
                t.color = 0xFF2A2A2A.toInt(); t.textSize = 26f
                c.drawText("JOYSTICK", 250f, 1080f, t); c.drawText("MODE", 500f, 1080f, t); c.drawText("FIRE", 730f, 1080f, t)
            },
        )

        /** Elektronika "Ataka asteroidov" in the Game & Watch style case of the photo: red frame,
         *  gold face, the screen in its raised gold surround with a red rim, the cross pad on the left
         *  (up / down: the left cannons), B and A on the right (the right cannons: up, down) and the
         *  three small grey keys (GAME A, GAME B, TIME). */
        private fun atakaAsteroidov() = HhSkin(
            1600f, 960f, 0xFF5A1018.toInt(),
            screen = RectF(430f, 190f, 1150f, 670f), bezel = 0xFFC82838.toInt(), bezelPad = 16f,
            menu = PointF(800f, 900f),
            landscape = true,
            controls = listOf(
                Control(Kind.STICK, 200f, 690f, 130f, color = 0x55000000, base = 0x00000000, eightWay = false),
                Control(Kind.BUTTON, 1310f, 700f, 64f, color = 0xFFD8303E.toInt(), base = 0xFF8A1C26.toInt(), bit = 0x10,
                    label = "B", labelColor = 0xFF3A2A20.toInt(), labelSize = 40f),
                Control(Kind.BUTTON, 1480f, 700f, 64f, color = 0xFFD8303E.toInt(), base = 0xFF8A1C26.toInt(), bit = 0x20,
                    label = "A", labelColor = 0xFF3A2A20.toInt(), labelSize = 40f),
                Control(Kind.BUTTON, 1370f, 110f, 40f, color = 0xFFCFCFCF.toInt(), bit = 0x40, bw = 110f, bh = 52f,
                    label = "GAME A", labelColor = 0xFF3A2A20.toInt(), labelSize = 24f),
                Control(Kind.BUTTON, 1370f, 230f, 40f, color = 0xFFCFCFCF.toInt(), bit = 0x80, bw = 110f, bh = 52f,
                    label = "GAME B", labelColor = 0xFF3A2A20.toInt(), labelSize = 24f),
                Control(Kind.BUTTON, 1370f, 350f, 40f, color = 0xFFCFCFCF.toInt(), bit = 0x100, bw = 110f, bh = 52f,
                    label = "TIME", labelColor = 0xFF3A2A20.toInt(), labelSize = 24f),
            ),
            drawBody = { c, p, s ->
                p.color = 0xFFC2283A.toInt(); c.drawRoundRect(10f, 10f, 1590f, 950f, 40f, 40f, p)
                p.color = 0xFFE4D2A2.toInt(); c.drawRoundRect(40f, 46f, 1560f, 914f, 14f, 14f, p)
                // raised surround of the screen
                p.color = 0xFFD6C290.toInt(); c.drawRoundRect(375f, 95f, 1205f, 770f, 24f, 24f, p)
                p.color = 0xFFEEDFB6.toInt(); c.drawRoundRect(385f, 102f, 1195f, 760f, 20f, 20f, p)
                // red rings round the small keys
                for (y in listOf(110f, 230f, 350f)) { p.color = 0xFFC2283A.toInt(); c.drawRoundRect(1305f, y - 36f, 1435f, y + 36f, 34f, 34f, p) }
                // the cross pad
                p.color = 0xFFB01E2E.toInt(); c.drawRoundRect(60f, 550f, 340f, 830f, 26f, 26f, p)
                p.color = 0xFF2C2C2C.toInt()
                c.drawRoundRect(155f, 570f, 245f, 810f, 12f, 12f, p); c.drawRoundRect(80f, 645f, 320f, 735f, 12f, 12f, p)
                p.color = 0xFF3C3C3C.toInt(); c.drawCircle(200f, 690f, 34f, p)
                s.color = 0xFF4A4A4A.toInt(); s.strokeWidth = 4f
                for ((dx, dy) in listOf(0f to -1f, 0f to 1f, -1f to 0f, 1f to 0f)) c.drawCircle(200f + dx * 85f, 690f + dy * 85f, 18f, s)
            },
        )

        /** Elektronika IM-23 Autoslalom (1991), from a photo: the silver Elektronika case, the screen
         *  in the middle, the big orange keys at the sides (left: SPEED and ←, right: START and →)
         *  and the small black GAME A / GAME B / TIME keys at the top right. */
        private fun autoslalom() = HhSkin(
            1600f, 900f, 0xFF404448.toInt(),
            screen = RectF(450f, 200f, 1150f, 637f), bezel = 0xFFE8862C.toInt(), bezelPad = 12f,
            menu = PointF(800f, 850f),
            landscape = true,
            controls = listOf(
                Control(Kind.BUTTON, 170f, 330f, 66f, color = 0xFFF08A2C.toInt(), base = 0xFF9A9A9A.toInt(), bit = 0x10,
                    label = "SPEED", labelColor = 0xFF2A2A2A.toInt(), labelSize = 28f),
                Control(Kind.BUTTON, 170f, 630f, 66f, color = 0xFFF08A2C.toInt(), base = 0xFF9A9A9A.toInt(), bit = 0x04,
                    label = "◄", labelColor = 0xFF2A2A2A.toInt(), labelSize = 34f),
                Control(Kind.BUTTON, 1430f, 480f, 66f, color = 0xFFF08A2C.toInt(), base = 0xFF9A9A9A.toInt(), bit = 0x20,
                    label = "START", labelColor = 0xFF2A2A2A.toInt(), labelSize = 28f),
                Control(Kind.BUTTON, 1430f, 700f, 66f, color = 0xFFF08A2C.toInt(), base = 0xFF9A9A9A.toInt(), bit = 0x08,
                    label = "►", labelColor = 0xFF2A2A2A.toInt(), labelSize = 34f),
                Control(Kind.BUTTON, 1470f, 120f, 26f, color = 0xFF202020.toInt(), base = 0xFF8A8A8A.toInt(), bit = 0x40,
                    label = "GAME A", labelAbove = false, labelColor = 0xFF2A2A2A.toInt(), labelSize = 22f),
                Control(Kind.BUTTON, 1470f, 235f, 26f, color = 0xFF202020.toInt(), base = 0xFF8A8A8A.toInt(), bit = 0x80,
                    label = "GAME B", labelColor = 0xFF2A2A2A.toInt(), labelSize = 22f),
                Control(Kind.BUTTON, 1300f, 120f, 26f, color = 0xFF202020.toInt(), base = 0xFF8A8A8A.toInt(), bit = 0x100,
                    label = "TIME", labelColor = 0xFF2A2A2A.toInt(), labelSize = 22f),
            ),
            drawBody = { c, p, s ->
                p.color = 0xFFF4F4F0.toInt(); c.drawRoundRect(10f, 10f, 1590f, 890f, 50f, 50f, p)
                p.shader = LinearGradient(0f, 40f, 0f, 860f, intArrayOf(0xFFDADCDC.toInt(), 0xFFC4C6C8.toInt(), 0xFFD6D8D8.toInt()), null, Shader.TileMode.CLAMP)
                c.drawRoundRect(40f, 40f, 1560f, 860f, 30f, 30f, p); p.shader = null
                s.color = 0xFF9A9C9E.toInt(); s.strokeWidth = 3f; c.drawRoundRect(40f, 40f, 1560f, 860f, 30f, 30f, s)
                // the orange line round the screen
                s.color = 0xFFE8862C.toInt(); s.strokeWidth = 6f; c.drawRoundRect(410f, 140f, 1190f, 700f, 20f, 20f, s)
                p.color = 0xFFE8862C.toInt(); c.drawRect(500f, 760f, 1100f, 770f, p)
            },
        )

        /** Bassmate Computer (1984): a clamshell. The lid holds the lure screen, its row tags and
         *  pictures of the lures; the base the conditions screen with its eight keys, COMPUTE,
         *  and the ALARM / ACL pinholes. The LCDs are printed in red and teal bands with the
         *  choices; a lit segment darkens its box. Frame: 960 x 1263, the lid's screen in the
         *  top 627 rows. */
        private fun bassmate(): HhSkin {
            val fw = 960f; val split = 627f; val fh = 1263f
            val top = RectF(300f, 225f, 840f, 225f + 540f * split / fw)
            val bot = RectF(300f, 1215f, 840f, 1215f + 540f * (fh - split) / fw)
            val sx = 540f / fw
            // source rows and columns of the boxes (see the SVGs)
            val topRows = floatArrayOf(40f, 108f, 177f, 246f, 315f, 384f, 453f, 522f)
            val botRows = floatArrayOf(665f, 734f, 803f, 872f, 941f, 1010f, 1079f, 1148f)
            val topCols = floatArrayOf(47f, 223f, 398f, 573f, 748f)
            val botCols = floatArrayOf(60f, 234f, 406f, 579f, 751f)
            fun ty(r: Int) = top.top + topRows[r] * sx
            fun by(r: Int) = bot.top + (botRows[r] - split) * sx
            val rowH = 65f * sx
            val red = 0xFFB9584A.toInt(); val teal = 0xFF3F8580.toInt(); val olive = 0xFF8A8446.toInt()
            val topText = listOf(
                listOf("LIZD", "WORM", "JIG", "SPOON", "GRUB"),
                listOf("CHUGR", "MINNW", "BUZZ", "CrSHL", "CrMED"),
                listOf("CrDEP", "SPIN"),
                listOf("1/8", "1/4", "3/8", "1/2", "1"),
                listOf("SILVR", "GOLD", "FLUOR"), listOf("LITE", "MED", "DARK"),
                listOf("LARGE", "MED", "SMALL"), listOf("SLOW", "MOD", "JERKY"))
            val topBand = intArrayOf(red, red, red, teal, red, teal, red, teal)
            val botText = listOf(
                listOf("SPRG", "SUM", "FALL", "WNTR"), listOf("35-49", "50-64", "65-79", "80+"),
                listOf("1-6", "7-15", "16-30", "30+"), listOf("EARLY", "DKmid", "CLmid", "LATE"),
                listOf("CHANL", "COVE", "LAKE", "INLET", "BKWTR"), listOf("TIMBR", "ROCKS", "GRASS", "NONE"),
                listOf("MUDDY", "MURKY", "CLEAR"), listOf("CALM", "MOD", "STRG"))
            val keyY = FloatArray(8) { by(it) + rowH / 2 }
            fun key(r: Int, bit: Int) = Control(Kind.BUTTON, 888f + (if (r % 2 == 0) 0f else 22f), keyY[r], 0f,
                color = 0xFFC9C9C4.toInt(), base = 0xFF6E6E6A.toInt(), bit = bit, bw = 60f, bh = 30f)
            val tp = Paint(Paint.ANTI_ALIAS_FLAG).apply { typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }
            fun tag(c: Canvas, p: Paint, x0: Float, y0: Float, x1: Float, y1: Float, fill: Int, ink: Int, label: String, size: Float, pointRight: Boolean = true) {
                val path = Path().apply {
                    moveTo(x0, y0); lineTo(x1 - (if (pointRight) (y1 - y0) / 2 else 0f), y0); lineTo(x1, (y0 + y1) / 2)
                    lineTo(x1 - (if (pointRight) (y1 - y0) / 2 else 0f), y1); lineTo(x0, y1); close()
                }
                p.color = fill; c.drawPath(path, p)
                tp.color = ink; tp.textSize = size; tp.textAlign = Paint.Align.LEFT
                c.drawText(label, x0 + 12f, (y0 + y1) / 2 + size * 0.36f, tp)
            }
            fun lcd(c: Canvas, p: Paint, area: RectF, rows: FloatArray, cols: FloatArray, srcTop: Float, texts: List<List<String>>, bands: IntArray, extra: (Canvas) -> Unit) {
                p.color = 0xFF151512.toInt(); c.drawRoundRect(area.left - 18f, area.top - 18f, area.right + 18f, area.bottom + 18f, 14f, 14f, p)
                p.color = 0xFF4E5040.toInt(); c.drawRect(area, p)
                for (r in 0 until 8) {
                    val y0 = area.top + (rows[r] - srcTop) * sx
                    val n = texts[r].size
                    val x1 = area.left + (cols[n - 1] + 172f) * sx
                    p.color = bands[r]; c.drawRect(area.left + cols[0] * sx - 2f, y0, x1, y0 + rowH, p)
                    tp.textAlign = Paint.Align.CENTER; tp.textSize = rowH * 0.5f
                    tp.color = 0xFFF2DCCB.toInt().let { if (bands[r] == teal) 0xFFD2ECE6.toInt() else it }
                    for ((k, t) in texts[r].withIndex())
                        c.drawText(t, area.left + (cols[k] + 86f) * sx, y0 + rowH * 0.68f, tp)
                }
                extra(c)
                // glass sheen
                p.shader = LinearGradient(0f, area.top, 0f, area.bottom, 0x18FFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP)
                c.drawRect(area, p); p.shader = null
            }
            fun lure(c: Canvas, p: Paint, s: Paint, x: Float, y: Float, w: Float, kind: Int) {
                // simple line drawings: 0 plug with a lip, 1 spoon, 2 worm, 3 skirted jig/spinner
                s.color = 0xFF2A2A2A.toInt(); s.strokeWidth = 3f
                p.color = 0xFFEDEDE8.toInt()
                when (kind) {
                    0 -> { c.drawOval(x, y - w * 0.13f, x + w * 0.8f, y + w * 0.13f, p); c.drawOval(x, y - w * 0.13f, x + w * 0.8f, y + w * 0.13f, s)
                           c.drawLine(x, y, x - w * 0.15f, y + w * 0.12f, s); c.drawCircle(x + w * 0.12f, y - w * 0.03f, w * 0.03f, s)
                           c.drawLine(x + w * 0.35f, y + w * 0.13f, x + w * 0.35f, y + w * 0.24f, s); c.drawLine(x + w * 0.65f, y + w * 0.12f, x + w * 0.65f, y + w * 0.24f, s)
                           c.drawLine(x + w * 0.8f, y, x + w, y, s) }
                    1 -> { c.drawOval(x + w * 0.3f, y - w * 0.12f, x + w * 0.75f, y + w * 0.12f, p); c.drawOval(x + w * 0.3f, y - w * 0.12f, x + w * 0.75f, y + w * 0.12f, s)
                           c.drawLine(x, y, x + w * 0.3f, y, s); c.drawLine(x + w * 0.75f, y, x + w, y + w * 0.1f, s) }
                    2 -> { val path = Path().apply { moveTo(x, y); cubicTo(x + w * 0.3f, y - w * 0.2f, x + w * 0.6f, y + w * 0.2f, x + w, y - w * 0.05f) }
                           s.strokeWidth = w * 0.06f; s.color = 0xFFDCDCD6.toInt(); c.drawPath(path, s)
                           s.strokeWidth = 2f; s.color = 0xFF2A2A2A.toInt(); c.drawPath(path, s) }
                    else -> { c.drawCircle(x + w * 0.2f, y, w * 0.08f, p); c.drawCircle(x + w * 0.2f, y, w * 0.08f, s)
                              for (k in 0 until 6) c.drawLine(x + w * 0.28f, y, x + w * (0.75f + k * 0.04f), y - w * 0.12f + k * w * 0.05f, s)
                              c.drawLine(x, y - w * 0.1f, x + w * 0.2f, y, s) }
                }
            }
            return HhSkin(
                1000f, 2000f, 0xFF0E0E0E.toInt(),
                screen = RectF(top), bezel = 0, bezelPad = 0f,
                menu = PointF(500f, 1965f),
                parts = listOf(ScreenPart(0f, split / fh, top), ScreenPart(split / fh, 1f, bot)),
                controls = listOf(
                    key(0, 0x10), key(1, 0x20), key(2, 0x40), key(3, 0x80),
                    key(4, 0x100), key(5, 0x200), key(6, 0x400), key(7, 0x800),
                    Control(Kind.BUTTON, 200f, 1120f, 40f, color = 0xFF1A1A1A.toInt(), base = 0xFF8C8C86.toInt(), bit = 0x1000),
                    Control(Kind.BUTTON, 815f, 1880f, 12f, color = 0xFF2A2A2A.toInt(), base = 0xFF8A7A50.toInt(), bit = 0x4000,
                        label = "ACL", labelColor = 0xFF222222.toInt(), labelSize = 22f),
                    Control(Kind.BUTTON, 880f, 1880f, 12f, color = 0xFF2A2A2A.toInt(), base = 0xFF8A7A50.toInt(), bit = 0x2000,
                        label = "ALARM", labelColor = 0xFF222222.toInt(), labelSize = 22f),
                ),
                drawBody = { c, p, s ->
                    // the black clamshell, lid above the base
                    p.color = 0xFF1C1C1E.toInt()
                    c.drawRoundRect(20f, 20f, 980f, 975f, 40f, 40f, p)
                    c.drawRoundRect(20f, 1000f, 980f, 1980f, 40f, 40f, p)
                    // hinge
                    p.color = 0xFF2A2A2C.toInt(); c.drawRect(120f, 960f, 880f, 1015f, p)
                    p.color = 0xFFD8D8D8.toInt(); for (k in 0 until 40) c.drawRect(300f + k * 10f, 972f, 305f + k * 10f, 1003f, p)
                    // lid plate: brushed silver panel inside a cream frame
                    p.color = 0xFFE9E4D2.toInt(); c.drawRoundRect(45f, 45f, 955f, 945f, 22f, 22f, p)
                    p.shader = LinearGradient(0f, 60f, 940f, 900f, intArrayOf(0xFFCFCFCB.toInt(), 0xFFEDEDEA.toInt(), 0xFFC4C4C0.toInt()), null, Shader.TileMode.MIRROR)
                    c.drawRoundRect(70f, 70f, 930f, 920f, 10f, 10f, p); p.shader = null
                    // lure pictures with their names, around the screen
                    tp.textAlign = Paint.Align.CENTER; tp.textSize = 22f; tp.color = 0xFF2C6FD0.toInt()
                    val pics = listOf(Triple(320f, 625f, 2) to "WORM", Triple(520f, 625f, 3) to "LIZARD", Triple(720f, 625f, 0) to "MINNOW",
                        Triple(110f, 95f, 3) to "JIG", Triple(330f, 95f, 3) to "GRUB", Triple(550f, 95f, 1) to "SPOON", Triple(760f, 95f, 0) to "CHUGGER")
                    for ((t, name) in pics) { lure(c, p, s, t.first, t.second + 45f, 140f, t.third); c.drawText(name, t.first + 70f, t.second + 10f, tp) }
                    // the lid's row tags, lined up with the screen's rows
                    tag(c, p, 75f, ty(0) + 2f, 282f, ty(2) + rowH, 0xFF2A78D6.toInt(), Color.WHITE, "BODY TYPE", 24f)
                    val lidTags = listOf("WT. SINKER", "METAL COLOR", "BASE COLOR", "SIZE BAIT", "RETRIEVE")
                    for ((k, t) in lidTags.withIndex()) {
                        val dark = k % 2 == 0
                        tag(c, p, 75f, ty(3 + k) + 3f, 282f, ty(3 + k) + rowH - 3f, if (dark) 0xFF151515.toInt() else Color.WHITE, if (dark) Color.WHITE else 0xFF151515.toInt(), t, 21f)
                    }
                    // the red "use this lure" arrow under the tags
                    p.color = 0xFFD9312A.toInt()
                    c.drawPath(Path().apply { moveTo(85f, ty(7) + rowH + 36f); lineTo(195f, ty(7) + rowH + 14f); lineTo(305f, ty(7) + rowH + 36f); close() }, p)
                    c.drawRoundRect(85f, ty(7) + rowH + 40f, 305f, ty(7) + rowH + 110f, 8f, 8f, p)
                    tp.color = Color.WHITE; tp.textSize = 24f
                    c.drawText("USE THIS", 195f, ty(7) + rowH + 70f, tp); c.drawText("LURE", 195f, ty(7) + rowH + 98f, tp)
                    // crankbait column on the right of the lid
                    tp.textSize = 20f; tp.color = 0xFF2C6FD0.toInt()
                    c.drawText("CRANKBAIT", 500f, 760f, tp)
                    for ((k, n) in listOf("SHALLOW", "MIDWATER", "DEEP").withIndex()) {
                        c.drawText(n, 220f + k * 280f, 795f, tp); lure(c, p, s, 160f + k * 280f, 850f, 130f, 0)
                    }

                    lcd(c, p, top, topRows, topCols, 0f, topText, topBand) { cc ->
                        // CHANGE LOCATION across the last three boxes of row 2, and the clock's olive window
                        val y0 = ty(2)
                        p.color = olive; cc.drawRect(top.left + 398f * sx, y0, top.left + 921f * sx, y0 + rowH, p)
                        tp.color = 0xFFF4E8B0.toInt(); tp.textSize = rowH * 0.5f; tp.textAlign = Paint.Align.CENTER
                        cc.drawText("CHANGE LOCATION", top.left + 660f * sx, y0 + rowH * 0.68f, tp)
                        p.color = 0xFF6F6C3C.toInt(); cc.drawRect(top.left + 585f * sx, ty(4), top.left + 921f * sx, ty(7) + rowH, p)
                    }
                    // base plate
                    p.color = 0xFFE9E4D2.toInt(); c.drawRoundRect(45f, 1035f, 955f, 1945f, 22f, 22f, p)
                    // the keys' black and white stripes fanning out to the right edge
                    for (r in 0 until 8) {
                        p.color = if (r % 2 == 0) 0xFF151515.toInt() else Color.WHITE
                        val y0 = by(r); val y1 = y0 + rowH
                        c.drawPath(Path().apply {
                            moveTo(bot.right + 18f, y0 + 4f); lineTo(930f, y0 - 2f); lineTo(950f, y0 - 2f)
                            lineTo(950f, y1 + 2f); lineTo(930f, y1 + 2f); lineTo(bot.right + 18f, y1 - 4f); close()
                        }, p)
                    }
                    // condition tags
                    val conds = listOf("SEASON", "WTR. TEMP F", "WTR. DEPTH (FT)", "TIME OF DAY", "STRUCTURE", "COVER", "WTR. CLARITY", "WIND")
                    for ((k, t) in conds.withIndex()) {
                        val dark = k % 2 == 0
                        tag(c, p, 75f, by(k) + 3f, 282f, by(k) + rowH - 3f, if (dark) 0xFF151515.toInt() else Color.WHITE, if (dark) Color.WHITE else 0xFF151515.toInt(), t, 20f)
                    }
                    // COMPUTE: the round key's dish and its red arrow tag
                    p.color = 0xFFD4CFBC.toInt(); c.drawCircle(200f, 1120f, 58f, p)
                    p.color = 0xFFD9312A.toInt()
                    c.drawPath(Path().apply { moveTo(270f, 1120f); lineTo(300f, 1095f); lineTo(520f, 1095f); lineTo(520f, 1145f); lineTo(300f, 1145f); close() }, p)
                    tp.color = Color.WHITE; tp.textSize = 26f; tp.textAlign = Paint.Align.CENTER
                    c.drawText("COMPUTE", 410f, 1129f, tp)
                    // ENTER CONDITIONS arrow under the tags
                    val ey = by(7) + rowH + 20f
                    p.color = 0xFFD9312A.toInt()
                    c.drawPath(Path().apply { moveTo(85f, ey + 24f); lineTo(195f, ey); lineTo(305f, ey + 24f); close() }, p)
                    c.drawRoundRect(85f, ey + 28f, 305f, ey + 100f, 8f, 8f, p)
                    tp.color = Color.WHITE; tp.textSize = 24f
                    c.drawText("ENTER", 195f, ey + 58f, tp); c.drawText("CONDITIONS", 195f, ey + 88f, tp)
                    lcd(c, p, bot, botRows, botCols, split, botText, intArrayOf(red, teal, red, teal, red, teal, red, teal)) {}
                },
            )
        }

        /** Mattel Auto Race (1976), from a photo: the grey case with a blank black badge, the black
         *  track with white lines down the right side holding the LED window (score on top, the cars as
         *  red bars in three lanes), the GEAR 1-4 slide and the START / RESET slide beside the coloured
         *  labels, the speaker grille, and the car-shaped steering slide at the foot of the track. */
        private fun autoRace(): HhSkin {
            fun lx(x: Float) = 560f + (x - 16f) * 35f
            fun ly(y: Float) = 250f + (y - 5.8f) * 30f
            val cars = (0 until 7).flatMap { k ->
                val y = ly(12.5f + 3.2f * k)
                listOf(2 to 17.5f, 5 to 20f, 6 to 22.5f).map { (col, x) -> Led(LedKind.BAR, 6 - k, col, lx(x), y, 22f, 0xFFFF1A26.toInt()) }
            }
            return HhSkin(
                900f, 1500f, 0xFF2A2626.toInt(),
                screen = RectF(), bezel = 0, bezelPad = 0f,
                menu = PointF(450f, 1465f),
                controls = listOf(
                    Control(Kind.SWITCH, 330f, 560f, 40f, color = 0xFF2A2A2A.toInt(), option = 0, vertical = true,
                        positions = listOf("", "", "", ""), initial = 3,
                        sets = listOf(listOf(0 to 3), listOf(0 to 2), listOf(0 to 1), listOf(0 to 0))),
                    Control(Kind.SWITCH, 330f, 900f, 40f, color = 0xFF2A2A2A.toInt(), option = 2, vertical = true,
                        positions = listOf("", ""), initial = 0, sets = listOf(listOf(2 to 0), listOf(2 to 1))),
                    Control(Kind.SWITCH, lx(20f), 1310f, 60f, color = 0xFFC8C8C4.toInt(), option = 1,
                        positions = listOf("", "", ""), initial = 1, labelColor = 0xFFE0E0E0.toInt()),
                ),
                leds = cars + listOf(
                    Led(LedKind.DIGIT, 7, 0, lx(18.9f), ly(7.75f), 66f, 0xFFFF1A26.toInt()),
                    Led(LedKind.DIGIT, 8, 0, lx(21.1f), ly(7.75f), 66f, 0xFFFF1A26.toInt()),
                ),
                drawBody = { c, p, s ->
                    p.color = 0xFFA8A8A4.toInt(); c.drawRoundRect(40f, 30f, 870f, 1440f, 40f, 40f, p)
                    p.color = 0xFFCDCDC8.toInt(); c.drawRoundRect(50f, 38f, 860f, 1428f, 36f, 36f, p)
                    // blank badge
                    p.color = 0xFF141414.toInt(); c.drawRoundRect(90f, 80f, 470f, 190f, 14f, 14f, p)
                    // the track: black with white lines
                    val track = Path().apply { moveTo(500f, 70f); lineTo(830f, 70f); lineTo(830f, 1410f); lineTo(560f, 1410f); lineTo(500f, 1300f); close() }
                    p.color = 0xFF111111.toInt(); c.drawPath(track, p)
                    s.color = 0xFFE8E8E8.toInt(); s.strokeWidth = 4f
                    for (i in 1 until 6) { val x = 500f + i * 55f; c.drawLine(x, 80f, x, 200f, s); c.drawLine(x, 1200f, x, 1400f, s) }
                    // the display well and window
                    p.color = 0xFFB8B8B4.toInt(); c.drawRoundRect(lx(15.4f), ly(5.2f), lx(24.6f), ly(35.2f), 30f, 30f, p)
                    p.color = 0xFF180606.toInt(); c.drawRoundRect(lx(16f), ly(5.8f), lx(24f), ly(34.5f), 12f, 12f, p)
                    s.color = 0xFF8A8A8A.toInt(); s.strokeWidth = 4f
                    c.drawLine(lx(18.69f), ly(9.7f), lx(18.69f), ly(33.7f), s); c.drawLine(lx(21.31f), ly(9.7f), lx(21.31f), ly(33.7f), s)
                    p.color = 0xFFE8E8E8.toInt(); c.drawRect(lx(16f), ly(9.7f), lx(24f), ly(9.9f), p)
                    // the steering slot
                    p.color = 0xFF050505.toInt(); c.drawRoundRect(lx(20f) - 190f, 1290f, lx(20f) + 190f, 1330f, 16f, 16f, p)
                    // the label strip beside the slides
                    p.color = 0xFF141414.toInt(); c.drawRoundRect(170f, 380f, 280f, 990f, 10f, 10f, p)
                    val t = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.DEFAULT_BOLD; textSize = 26f; color = 0xFFEDEDED.toInt() }
                    c.drawText("GEAR", 225f, 425f, t)
                    val gearCol = intArrayOf(0xFFF2D21E.toInt(), 0xFFF2A21E.toInt(), 0xFFF2761E.toInt(), 0xFFE83C1E.toInt())
                    for (i in 0 until 4) {
                        val y = 560f - 120f + i * 80f
                        p.color = gearCol[i]; c.drawRoundRect(185f, y - 30f, 265f, y + 30f, 6f, 6f, p)
                        t.color = 0xFF141414.toInt(); t.textSize = 38f; c.drawText("${4 - i}", 220f, y + 14f, t)
                        p.color = 0xFF141414.toInt(); c.drawCircle(252f, y, 5f, p)
                    }
                    // chequered flags
                    p.color = 0xFFEDEDED.toInt()
                    for (i in 0 until 4) for (j in 0 until 3) if ((i + j) % 2 == 0) c.drawRect(195f + i * 14f, 760f + j * 14f, 209f + i * 14f, 774f + j * 14f, p)
                    for ((y, label) in listOf(880f to "START", 920f to "RESET")) {
                        p.color = 0xFFE8501E.toInt(); c.drawRoundRect(180f, y - 18f, 270f, y + 18f, 6f, 6f, p)
                        t.color = 0xFF141414.toInt(); t.textSize = 24f; c.drawText(label, 225f, y + 8f, t)
                    }
                    // the slide wells
                    p.color = 0xFFB4B4B0.toInt(); c.drawRoundRect(295f, 380f, 365f, 990f, 14f, 14f, p)
                    // speaker grille
                    p.color = 0xFFB0B0AC.toInt()
                    for (i in 0 until 6) c.drawRoundRect(120f, 1080f + i * 50f, 470f - i * 6f, 1106f + i * 50f, 6f, 6f, p)
                },
            )
        }

        /** Talking plush puppet (placeholder until photos): a plain brown bear with its three
         *  touch sensors: the mouth (open / close it to make him talk), the neck (scratch it when he
         *  asks) and the body (hug). No screen. */
        private fun talkingBear() = HhSkin(
            1000f, 1320f, 0xFF1C2A3A.toInt(),
            screen = RectF(), bezel = 0, bezelPad = 0f,
            menu = PointF(500f, 1290f),
            parts = listOf(HhSkin.ScreenPart(0f, 1f, RectF())),
            controls = listOf(
                Control(Kind.BUTTON, 500f, 470f, 60f, color = 0xFFE9C9A0.toInt(), bit = 1 shl 4, bw = 200f, bh = 110f,
                    face = "MOUTH", faceColor = 0xFF5A3418.toInt(), faceSize = 34f, shine = false),
                Control(Kind.BUTTON, 500f, 640f, 50f, color = 0xFF9A6236.toInt(), bit = 1 shl 6, bw = 260f, bh = 70f,
                    face = "NECK", faceColor = 0xFFF6E6D0.toInt(), faceSize = 30f, shine = false),
                Control(Kind.BUTTON, 500f, 930f, 80f, color = 0xFFE9C9A0.toInt(), bit = 1 shl 5, bw = 300f, bh = 240f,
                    face = "HUG", faceColor = 0xFF5A3418.toInt(), faceSize = 44f, shine = false),
            ),
            drawBody = { c, p, _ ->
                val fur = 0xFF8A5530.toInt(); val dark = 0xFF6E4122.toInt(); val light = 0xFFD9B48A.toInt()
                // body, arms and feet
                p.color = dark
                c.drawOval(RectF(150f, 700f, 330f, 1000f), p); c.drawOval(RectF(670f, 700f, 850f, 1000f), p)
                p.color = fur; c.drawOval(RectF(250f, 640f, 750f, 1210f), p)
                p.color = dark; c.drawOval(RectF(250f, 1120f, 450f, 1260f), p); c.drawOval(RectF(550f, 1120f, 750f, 1260f), p)
                p.color = light; c.drawOval(RectF(290f, 1150f, 410f, 1240f), p); c.drawOval(RectF(590f, 1150f, 710f, 1240f), p)
                // head, ears, eyes and nose
                p.color = fur; c.drawCircle(285f, 190f, 95f, p); c.drawCircle(715f, 190f, 95f, p)
                p.color = light; c.drawCircle(285f, 190f, 55f, p); c.drawCircle(715f, 190f, 55f, p)
                p.color = fur; c.drawOval(RectF(250f, 130f, 750f, 640f), p)
                p.color = 0xFF1A1210.toInt(); c.drawCircle(405f, 330f, 30f, p); c.drawCircle(595f, 330f, 30f, p)
                p.color = Color.WHITE; c.drawCircle(395f, 320f, 9f, p); c.drawCircle(585f, 320f, 9f, p)
                p.color = light; c.drawOval(RectF(370f, 380f, 630f, 560f), p)
                p.color = 0xFF2A1A12.toInt(); c.drawOval(RectF(460f, 385f, 540f, 430f), p)
            },
        )

        /** Playskool Alphie (1978), from the box: the blue robot with the button at its neck, the
         *  five LEDs down its chest over the coloured stripes, the question arm (left) and answer
         *  arm (right) that slide along the stripes, and the activity selector at its waist. */
        private fun alphie() = HhSkin(
            1000f, 1320f, 0xFF0E1428.toInt(),
            screen = RectF(), bezel = 0, bezelPad = 0f,
            menu = PointF(500f, 1290f),
            controls = listOf(
                Control(Kind.BUTTON, 500f, 338f, 44f, color = 0xFFF4F4F4.toInt(), base = 0xFF1E6FB0.toInt(), bit = 0x01),
                Control(Kind.SWITCH, 255f, 675f, 46f, color = 0xFFF6D02A.toInt(), option = 0, vertical = true,
                    positions = listOf("", "", "", "", ""), label = "QUESTION", labelColor = 0xFFE8F2FF.toInt(), labelSize = 22f),
                Control(Kind.SWITCH, 745f, 675f, 46f, color = 0xFFF6D02A.toInt(), option = 1, vertical = true,
                    positions = listOf("", "", "", "", ""), label = "ANSWER", labelColor = 0xFFE8F2FF.toInt(), labelSize = 22f),
                Control(Kind.SWITCH, 500f, 990f, 40f, color = 0xFFF6D02A.toInt(), option = 2,
                    positions = listOf("?", "☾", "★", "♪"), labelColor = 0xFFE8F2FF.toInt(), labelSize = 26f),
            ),
            leds = (0 until 5).map { Led(LedKind.ROUND, 0, it, 500f, 465f + it * 92f, 20f, 0xFFFF3020.toInt()) },
            drawBody = { c, p, s ->
                // antenna and head
                p.color = 0xFFD8302A.toInt(); c.drawRoundRect(465f, 18f, 535f, 80f, 14f, 14f, p)
                p.color = 0xFF2C8BD2.toInt(); c.drawRoundRect(320f, 60f, 680f, 300f, 46f, 46f, p)
                p.color = 0xFF1E6FB0.toInt(); c.drawCircle(305f, 180f, 42f, p); c.drawCircle(695f, 180f, 42f, p)
                p.color = 0xFF7CC4F0.toInt(); c.drawRoundRect(360f, 100f, 640f, 265f, 30f, 30f, p)
                for (ex in listOf(430f, 570f)) {
                    p.color = 0xFFF2A21C.toInt(); c.drawCircle(ex, 170f, 52f, p)
                    p.color = 0xFFFFE07A.toInt(); c.drawCircle(ex, 170f, 40f, p)
                    p.color = 0xFF3A3A9A.toInt(); c.drawCircle(ex, 176f, 26f, p)
                    p.color = Color.WHITE; c.drawCircle(ex - 8f, 166f, 7f, p)
                }
                p.color = 0xFFF2A21C.toInt(); c.drawRoundRect(450f, 228f, 550f, 250f, 10f, 10f, p)
                // arms
                p.color = 0xFFD8302A.toInt()
                c.drawRoundRect(50f, 560f, 160f, 640f, 18f, 18f, p); c.drawRoundRect(840f, 560f, 950f, 640f, 18f, 18f, p)
                // body frame and the striped panel
                p.color = 0xFF2C8BD2.toInt(); c.drawRoundRect(150f, 380f, 850f, 1060f, 60f, 60f, p)
                p.color = 0xFFF2F2F2.toInt(); c.drawRoundRect(205f, 418f, 795f, 935f, 24f, 24f, p)
                val stripes = intArrayOf(0xFF3EA84A.toInt(), 0xFFD8302A.toInt(), 0xFFF28A1C.toInt(), 0xFF7A3A9A.toInt(), 0xFF2A7AD2.toInt())
                for (i in 0 until 5) { p.color = stripes[i]; c.drawRect(222f, 430f + i * 92f + 8f, 778f, 430f + (i + 1) * 92f - 8f, p) }
                // the chest column with the LED housings and the speaker rings
                p.color = 0xFF2C8BD2.toInt(); c.drawRoundRect(440f, 400f, 560f, 940f, 20f, 20f, p)
                p.color = 0xFF16161A.toInt(); for (i in 0 until 5) c.drawRoundRect(462f, 430f + i * 92f + 4f, 538f, 430f + (i + 1) * 92f - 4f, 18f, 18f, p)
                s.color = 0xEEFFFFFF.toInt(); s.strokeWidth = 5f
                for (rr in listOf(70f, 90f, 110f)) {
                    c.drawArc(RectF(500f - rr, 649f - rr, 500f + rr, 649f + rr), 140f, 80f, false, s)
                    c.drawArc(RectF(500f - rr, 649f - rr, 500f + rr, 649f + rr), -40f, 80f, false, s)
                }
                // waist slot for the activity selector
                p.color = 0xFF16161A.toInt(); c.drawRoundRect(320f, 955f, 680f, 1000f, 20f, 20f, p)
                // legs with red and white bands
                for (lx in listOf(230f, 560f)) {
                    p.color = 0xFF2C8BD2.toInt(); c.drawRoundRect(lx, 1060f, lx + 210f, 1250f, 26f, 26f, p)
                    for (b in 0 until 3) { p.color = if (b % 2 == 0) 0xFFD8302A.toInt() else 0xFFF2F2F2.toInt(); c.drawRect(lx + 10f, 1150f + b * 26f, lx + 200f, 1172f + b * 26f, p) }
                }
            },
        )

        /** Coleco Amaze-A-Tron (1978), from a photo: white case, blue top with the 5 x 5 numbered
         *  squares, the red 2-digit LED display, the red and green player LEDs, the OFF / ON switch,
         *  and the blue GAME START and green GAME SELECT buttons. */
        private fun amazeATron(): HhSkin {
            val squares = (0 until 25).map { n ->
                val col = n % 5; val row = n / 5
                Control(Kind.BUTTON, 200f + col * 150f, 175f + row * 125f, 40f, color = 0xFFF4F4F2.toInt(), bit = 1 shl n,
                    bw = 118f, bh = 98f, face = "${n + 1}", faceColor = 0xFF1E3A8A.toInt(), faceSize = 50f, shine = false)
            }
            return HhSkin(
                1000f, 1320f, 0xFF2A4A9A.toInt(),
                screen = RectF(), bezel = 0, bezelPad = 0f,
                menu = PointF(500f, 1290f),
                controls = squares + listOf(
                    Control(Kind.SWITCH, 700f, 925f, 26f, color = 0xFF2A2A2A.toInt(), option = 6, positions = listOf("OFF", "ON"), initial = 1,
                        labelColor = 0xFFE8E8E8.toInt(), labelSize = 22f, sets = listOf(listOf(7 to 1), listOf(7 to 0))),
                    Control(Kind.BUTTON, 300f, 1085f, 40f, color = 0xFF1E5AC8.toInt(), bit = 1 shl 26, bw = 110f, bh = 100f,
                        label = "GAME\nSTART".replace("\n", " "), labelColor = 0xFFE8E8E8.toInt(), labelSize = 24f),
                    Control(Kind.BUTTON, 440f, 1085f, 40f, color = 0xFF22A050.toInt(), bit = 1 shl 25, bw = 110f, bh = 100f,
                        label = "GAME SELECT", labelColor = 0xFFE8E8E8.toInt(), labelSize = 24f),
                ),
                leds = listOf(
                    Led(LedKind.ROUND, 0, 0, 240f, 925f, 16f, 0xFFFF3020.toInt()),
                    Led(LedKind.ROUND, 1, 0, 340f, 925f, 16f, 0xFF30E050.toInt()),
                    Led(LedKind.DIGIT, 3, 0, 700f, 830f, 60f, 0xFFFF2A20.toInt()),
                    Led(LedKind.DIGIT, 2, 0, 750f, 830f, 60f, 0xFFFF2A20.toInt()),
                ),
                drawBody = { c, p, _ ->
                    // white case
                    p.color = 0xFFCFCFCF.toInt(); c.drawRoundRect(70f, 40f, 930f, 1250f, 50f, 50f, p)
                    p.color = 0xFFF6F6F4.toInt(); c.drawRoundRect(78f, 46f, 922f, 1238f, 46f, 46f, p)
                    // blue top
                    p.color = 0xFF2449A8.toInt(); c.drawRoundRect(105f, 90f, 895f, 780f, 26f, 26f, p)
                    p.color = 0xFF1C3C90.toInt(); for (n in 0 until 25) { val x = 200f + (n % 5) * 150f; val y = 175f + (n / 5) * 125f; c.drawRect(x - 66f, y - 56f, x + 66f, y + 56f, p) }
                    // display strip
                    p.color = 0xFF151515.toInt(); c.drawRect(105f, 790f, 895f, 870f, p)
                    p.color = 0xFF2A0606.toInt(); c.drawRoundRect(650f, 795f, 800f, 865f, 6f, 6f, p)
                    // black control panel
                    p.color = 0xFF151515.toInt(); c.drawRoundRect(140f, 885f, 880f, 1200f, 18f, 18f, p)
                    p.color = 0xFF202020.toInt(); c.drawRect(140f, 965f, 880f, 975f, p)
                },
            )
        }

        /** Milton Bradley Electronic Arcade Mania (1983), from the box: "The Machine", a small black
         *  arcade cabinet with the game slide over 9 LEDs, and three rows of buttons: orange 1 2 3;
         *  red Run Amuk, yellow, blue Sneak Attack; green Alien Raiders, yellow, purple Rattler. */
        private fun arcadeMania() = HhSkin(
            800f, 1320f, 0xFF120E1E.toInt(),
            screen = RectF(), bezel = 0, bezelPad = 0f,
            menu = PointF(400f, 1290f),
            controls = listOf(
                Control(Kind.BUTTON, 270f, 790f, 40f, color = 0xFFF28A1C.toInt(), bit = 0x001, bw = 112f, bh = 92f, face = "1", faceColor = Color.WHITE, faceSize = 54f),
                Control(Kind.BUTTON, 400f, 790f, 40f, color = 0xFFF28A1C.toInt(), bit = 0x002, bw = 112f, bh = 92f, face = "2", faceColor = Color.WHITE, faceSize = 54f),
                Control(Kind.BUTTON, 530f, 790f, 40f, color = 0xFFF28A1C.toInt(), bit = 0x004, bw = 112f, bh = 92f, face = "3", faceColor = Color.WHITE, faceSize = 54f),
                Control(Kind.BUTTON, 270f, 905f, 40f, color = 0xFFC03232.toInt(), bit = 0x008, bw = 112f, bh = 92f, face = "RUN\nAMUK", faceSize = 22f),
                Control(Kind.BUTTON, 400f, 905f, 40f, color = 0xFFF2D22A.toInt(), bit = 0x010, bw = 112f, bh = 92f),
                Control(Kind.BUTTON, 530f, 905f, 40f, color = 0xFF2640C0.toInt(), bit = 0x020, bw = 112f, bh = 92f, face = "SNEAK\nATTACK", faceSize = 20f),
                Control(Kind.BUTTON, 270f, 1020f, 40f, color = 0xFF269A34.toInt(), bit = 0x040, bw = 112f, bh = 92f, face = "Alien\nRaiders", faceSize = 21f),
                Control(Kind.BUTTON, 400f, 1020f, 40f, color = 0xFFF2D22A.toInt(), bit = 0x080, bw = 112f, bh = 92f),
                Control(Kind.BUTTON, 530f, 1020f, 40f, color = 0xFFB24080.toInt(), bit = 0x100, bw = 112f, bh = 92f, face = "Rattler", faceSize = 22f),
            ),
            leds = (0 until 9).map { Led(LedKind.ROUND, 0, it, 280f + (it % 3) * 120f, 345f + (it / 3) * 105f, 22f, 0xFFFF6A10.toInt()) },
            drawBody = { c, p, s ->
                // the cabinet
                p.color = 0xFF050505.toInt(); c.drawRoundRect(150f, 50f, 650f, 1240f, 30f, 30f, p)
                p.color = 0xFF141414.toInt(); c.drawRoundRect(160f, 60f, 640f, 1230f, 26f, 26f, p)
                // marquee slot (the slide's title shows here; drawn blank)
                p.color = 0xFF1E3A1E.toInt(); c.drawRoundRect(205f, 95f, 595f, 225f, 12f, 12f, p)
                p.color = 0x2290FF90; c.drawRoundRect(215f, 105f, 585f, 215f, 10f, 10f, p)
                // the slide window over the LEDs
                p.color = 0xFF2A0E06.toInt(); c.drawRoundRect(205f, 265f, 595f, 610f, 14f, 14f, p)
                s.color = 0xFF3A1A10.toInt(); s.strokeWidth = 3f
                for (i in 0 until 3) for (j in 0 until 3) c.drawCircle(280f + j * 120f, 345f + i * 105f, 40f, s)
                // vent ridges
                p.color = 0xFF0A0A0A.toInt(); for (i in 0 until 4) c.drawRoundRect(215f, 640f + i * 18f, 585f, 650f + i * 18f, 4f, 4f, p)
                // button deck
                p.color = 0xFF0C0C0C.toInt(); c.drawRoundRect(195f, 725f, 605f, 1085f, 16f, 16f, p)
            },
        )

        /** Tomy Alien Chase (1984), from a photo: a cream base with a dark grey control strip
         *  (POWER, SOUND, LEVEL and PLAYER slide switches, the joystick), and the dark grey
         *  box on top holding the two-sided VFD beside a printed card (drawn without its text). */
        private fun alienChase() = HhSkin(
            1000f, 1150f, 0xFF8E9296.toInt(),
            screen = RectF(540f, 70f, 790f, 700f), bezel = 0xFF101010.toInt(), bezelPad = 10f,
            menu = PointF(500f, 1118f),
            controls = listOf(
                Control(Kind.SWITCH, 210f, 915f, 22f, color = 0xFFF26B1D.toInt(), option = 4, vertical = true,
                    positions = listOf("", ""), initial = 0, label = "POWER", labelColor = 0xFFE8E4D8.toInt(), labelSize = 20f,
                    sets = listOf(listOf(7 to 0), listOf(7 to 1))),
                Control(Kind.SWITCH, 300f, 915f, 22f, color = 0xFF6E6E6E.toInt(), option = 3, vertical = true,
                    positions = listOf("", ""), initial = 0, label = "SOUND", labelColor = 0xFFE8E4D8.toInt(), labelSize = 20f,
                    sets = listOf(listOf(5 to 0), listOf(5 to 1))),
                Control(Kind.SWITCH, 390f, 915f, 22f, color = 0xFFB8B8B8.toInt(), option = 2, vertical = true,
                    positions = listOf("", ""), initial = 1, label = "LEVEL", labelColor = 0xFFE8E4D8.toInt(), labelSize = 20f,
                    sets = listOf(listOf(1 to 1), listOf(1 to 0))),
                Control(Kind.SWITCH, 480f, 915f, 22f, color = 0xFFB8B8B8.toInt(), option = 6, vertical = true,
                    positions = listOf("", ""), initial = 1, label = "PLAYER", labelColor = 0xFFE8E4D8.toInt(), labelSize = 20f,
                    sets = listOf(listOf(0 to 1), listOf(0 to 0))),
                Control(Kind.STICK, 760f, 900f, 90f, color = 0xFF151515.toInt(), base = 0xFF2C2E31.toInt(), eightWay = false),
            ),
            drawBody = { c, p, s ->
                // cream base
                p.color = 0xFF2A2A2A.toInt(); c.drawRoundRect(70f, 1050f, 930f, 1090f, 12f, 12f, p)
                p.shader = LinearGradient(0f, 720f, 0f, 1060f, intArrayOf(0xFFF3EBCF.toInt(), 0xFFE6DCBC.toInt()), null, Shader.TileMode.CLAMP)
                c.drawRoundRect(60f, 720f, 940f, 1060f, 36f, 36f, p); p.shader = null
                // control strip
                p.color = 0xFF3A3C40.toInt(); c.drawRoundRect(120f, 800f, 880f, 1018f, 10f, 10f, p)
                p.color = 0xFF2E3034.toInt(); c.drawRect(600f, 800f, 604f, 1018f, p)
                val t = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.DEFAULT_BOLD; color = 0xFFE8E4D8.toInt(); textSize = 17f }
                for ((x, top, bot) in listOf(Triple(210f, "ON", "OFF"), Triple(300f, "ON", "OFF"), Triple(390f, "PRO", "AMA"), Triple(480f, "TWO", "ONE"))) {
                    c.drawText(top, x, 866f, t); c.drawText(bot, x, 978f, t)
                }
                t.textSize = 30f; c.drawText("1 PLAYER SIDE", 345f, 1006f, t)
                t.textSize = 20f; c.drawText("START", 760f, 820f, t); c.drawText("GAME 0,1,2,3", 760f, 990f, t)
                // the display box
                p.color = 0xFF2F3134.toInt(); c.drawRoundRect(110f, 30f, 890f, 760f, 14f, 14f, p)
                p.color = 0xFF46494D.toInt(); c.drawRect(110f, 30f, 890f, 46f, p)
                // printed card beside the screen (no text): white grid, blue and pink bands
                p.color = 0xFFF2F2F4.toInt(); c.drawRect(150f, 70f, 520f, 700f, p)
                s.color = 0xFFC9C3E0.toInt(); s.strokeWidth = 2f
                var x = 150f; while (x <= 520f) { c.drawLine(x, 70f, x, 700f, s); x += 28f }
                var y = 70f; while (y <= 700f) { c.drawLine(150f, y, 520f, y, s); y += 28f }
                p.color = 0xFF5160C8.toInt(); c.drawRect(150f, 82f, 520f, 128f, p); c.drawRect(150f, 640f, 520f, 688f, p)
                p.color = 0xFFC8407A.toInt(); c.drawRect(150f, 340f, 520f, 384f, p)
                p.color = 0xFF5160C8.toInt(); c.drawRect(800f, 82f, 860f, 128f, p); c.drawRect(800f, 640f, 860f, 688f, p)
                p.color = 0xFFC8407A.toInt(); c.drawRect(800f, 340f, 860f, 384f, p)
                // the dark window over both
                p.color = 0x22000000; c.drawRect(150f, 70f, 860f, 700f, p)
            },
        )

        /** Coleco Alien Attack (1981), from a photo: orange case with wings and black grilles,
         *  a black front panel with the smoked VFD window, a silver strip with the
         *  SKILL 1 / OFF / SKILL 2 slide, orange FIRE and teal MOVE paddles, and the
         *  joystick disc with its four arrows. */
        private fun bambinoBasketball() = HhSkin(
            1000f, 1500f, 0xFF140A0A.toInt(),
            screen = RectF(150f, 210f, 850f, 385f), bezel = 0xFF0C0606.toInt(), bezelPad = 14f,
            menu = PointF(500f, 1400f),
            controls = listOf(
                Control(Kind.BUTTON, 300f, 500f, 34f, color = 0xFFEDE6D6.toInt(), bit = 0x200,
                    label = "DISPLAY", labelAbove = true, labelColor = Color.WHITE, labelSize = 24f),
                Control(Kind.BUTTON, 700f, 500f, 34f, color = 0xFFEDE6D6.toInt(), bit = 0x100,
                    label = "START", labelAbove = true, labelColor = Color.WHITE, labelSize = 24f),
                // option 0: skill level 1-3
                Control(Kind.SWITCH, 300f, 640f, 20f, color = 0xFFDADADA.toInt(), option = 0,
                    positions = listOf("1", "2", "3"), initial = 0, labelColor = Color.WHITE, labelSize = 22f,
                    plate = 0xFF1A1010.toInt(), plateW = 200f, plateH = 50f),
                // option 1: teams (0 = one player, 1 = two)
                Control(Kind.SWITCH, 700f, 640f, 20f, color = 0xFFDADADA.toInt(), option = 1,
                    positions = listOf("1", "2"), initial = 0, labelColor = Color.WHITE, labelSize = 22f,
                    plate = 0xFF1A1010.toInt(), plateW = 150f, plateH = 50f),
                Control(Kind.BUTTON, 500f, 860f, 58f, color = 0xFFF2C21E.toInt(), bit = 0x01,
                    label = "SHOOT", labelAbove = true, labelColor = Color.WHITE, labelSize = 28f),
                Control(Kind.BUTTON, 300f, 960f, 52f, color = 0xFFEDE6D6.toInt(), bit = 0x04,
                    face = "\u25C0", faceColor = 0xFF8A1A14.toInt(), faceSize = 36f),
                Control(Kind.BUTTON, 700f, 960f, 52f, color = 0xFFEDE6D6.toInt(), bit = 0x08,
                    face = "\u25B6", faceColor = 0xFF8A1A14.toInt(), faceSize = 36f),
                Control(Kind.BUTTON, 270f, 1200f, 52f, color = 0xFF2A2A2A.toInt(), bit = 0x10,
                    label = "LOW", labelAbove = true, labelColor = Color.WHITE, labelSize = 26f),
                Control(Kind.BUTTON, 500f, 1200f, 52f, color = 0xFF2A2A2A.toInt(), bit = 0x20,
                    label = "MEDIUM", labelAbove = true, labelColor = Color.WHITE, labelSize = 26f),
                Control(Kind.BUTTON, 730f, 1200f, 52f, color = 0xFF2A2A2A.toInt(), bit = 0x40,
                    label = "HIGH", labelAbove = true, labelColor = Color.WHITE, labelSize = 26f),
            ),
            drawBody = { c, p, s ->
                // the red hourglass case
                val body = Path().apply {
                    moveTo(110f, 90f); quadTo(110f, 60f, 140f, 60f); lineTo(860f, 60f); quadTo(890f, 60f, 890f, 90f)
                    lineTo(900f, 560f); cubicTo(900f, 700f, 780f, 720f, 780f, 800f)
                    cubicTo(780f, 880f, 920f, 900f, 920f, 1050f); lineTo(930f, 1430f); quadTo(930f, 1460f, 900f, 1460f)
                    lineTo(100f, 1460f); quadTo(70f, 1460f, 70f, 1430f); lineTo(80f, 1050f)
                    cubicTo(80f, 900f, 220f, 880f, 220f, 800f); cubicTo(220f, 720f, 100f, 700f, 100f, 560f); close()
                }
                p.shader = LinearGradient(80f, 0f, 920f, 0f, intArrayOf(0xFFA3140F.toInt(), 0xFFD9261C.toInt(), 0xFFC21E16.toInt(), 0xFF8E110C.toInt()),
                    floatArrayOf(0f, 0.35f, 0.7f, 1f), Shader.TileMode.CLAMP)
                c.drawPath(body, p); p.shader = null
                s.color = 0x55000000; s.strokeWidth = 6f; c.drawPath(body, s)
                // the black visor round the tube
                p.color = 0xFF1A0A0A.toInt(); c.drawRoundRect(120f, 160f, 880f, 440f, 30f, 30f, p)
                p.color = 0x22FFFFFF; c.drawRoundRect(130f, 168f, 870f, 200f, 16f, 16f, p)
                // switch labels
                val t = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE; textSize = 24f; textAlign = Paint.Align.CENTER
                    typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }
                c.drawText("SKILL LEVEL", 300f, 590f, t)
                c.drawText("TEAMS", 700f, 590f, t)
                // the lower court panel
                p.color = 0x33000000; c.drawRoundRect(170f, 1090f, 830f, 1300f, 40f, 40f, p)
                c.drawText("DRIBBLE", 500f, 1330f, t)
                c.drawText("BALL CARRIER", 500f, 1050f, t)
            },
        )

        private fun alienAttack() = HhSkin(
            1200f, 1000f, 0xFF120E0A.toInt(),
            screen = RectF(408f, 160f, 798f, 302f), bezel = 0xFF1C0C0A.toInt(), bezelPad = 12f,
            menu = PointF(600f, 960f),
            controls = listOf(
                Control(Kind.SWITCH, 595f, 414f, 22f, color = 0xFFDADADA.toInt(), option = 6,
                    positions = listOf("SKILL 1", "OFF", "SKILL 2"), initial = 0, sideLabels = true,
                    labelColor = 0xFF1A1A1A.toInt(), labelSize = 17f,
                    plate = 0xFFB4B2AC.toInt(), plateW = 476f, plateH = 60f,
                    // option 0: skill (0 = 1, 1 = 2); option 7: power (1 = off)
                    sets = listOf(listOf(0 to 0, 7 to 0), listOf(7 to 1), listOf(0 to 1, 7 to 0))),
                Control(Kind.BUTTON, 466f, 552f, 40f, color = 0xFFEA6A2C.toInt(), bit = 0x20, bw = 158f, bh = 74f,
                    label = "FIRE", labelAbove = true, labelColor = Color.WHITE, labelSize = 26f),
                Control(Kind.BUTTON, 460f, 652f, 40f, color = 0xFF2E9AA8.toInt(), bit = 0x10, bw = 160f, bh = 76f,
                    label = "MOVE", labelColor = Color.WHITE, labelSize = 26f),
                Control(Kind.STICK, 730f, 604f, 96f, color = 0xFFEDEBE2.toInt(), base = 0xFF1E1E1E.toInt(), arrows = true, eightWay = true),
            ),
            drawBody = { c, p, s ->
                val body = Path().apply {
                    moveTo(140f, 72f); lineTo(1080f, 72f); quadTo(1098f, 74f, 1100f, 96f)
                    lineTo(1106f, 296f); lineTo(936f, 880f); lineTo(936f, 912f); lineTo(266f, 912f); lineTo(266f, 880f)
                    lineTo(96f, 296f); lineTo(118f, 96f); quadTo(120f, 74f, 140f, 72f); close()
                }
                p.shader = LinearGradient(0f, 72f, 0f, 912f, intArrayOf(0xFFF26A28.toInt(), 0xFFE85E1E.toInt(), 0xFFD9541A.toInt()), null, Shader.TileMode.CLAMP)
                c.drawPath(body, p); p.shader = null
                // the facets beside the panel
                p.color = 0x22FFFFFF
                c.drawPath(Path().apply { moveTo(300f, 72f); lineTo(350f, 72f); lineTo(350f, 880f); lineTo(312f, 880f); close() }, p)
                c.drawPath(Path().apply { moveTo(850f, 72f); lineTo(900f, 72f); lineTo(888f, 880f); lineTo(850f, 880f); close() }, p)
                p.color = 0x18000000
                c.drawPath(Path().apply { moveTo(96f, 296f); lineTo(300f, 330f); lineTo(330f, 880f); lineTo(266f, 880f); close() }, p)
                c.drawPath(Path().apply { moveTo(1106f, 296f); lineTo(900f, 330f); lineTo(870f, 880f); lineTo(936f, 880f); close() }, p)
                // black grilles on the wings
                for (left in listOf(true, false)) {
                    val g = if (left) Path().apply { moveTo(136f, 108f); lineTo(230f, 112f); lineTo(236f, 330f); lineTo(126f, 262f); close() }
                            else Path().apply { moveTo(970f, 112f); lineTo(1064f, 108f); lineTo(1074f, 262f); lineTo(964f, 330f); close() }
                    p.color = 0xFF1A1A1A.toInt(); c.drawPath(g, p)
                    s.color = 0xFF050505.toInt(); s.strokeWidth = 4f
                    for (i in 0 until 6) { val y = 170f + i * 18f; if (left) c.drawLine(140f, y, 228f, y, s) else c.drawLine(972f, y, 1060f, y, s) }
                }
                // the black front panel
                p.color = 0xFF1B1B1B.toInt(); c.drawRect(350f, 72f, 850f, 880f, p)
                p.color = 0xFF101010.toInt(); c.drawRect(350f, 72f, 850f, 80f, p)
                // smoked window round the VFD
                p.color = 0xFF3A1A14.toInt(); c.drawRect(396f, 148f, 810f, 314f, p)
                // FIRE / MOVE recess
                p.color = 0xFF111111.toInt(); c.drawRoundRect(368f, 482f, 560f, 728f, 18f, 18f, p)
                // blank silver name plate
                p.color = 0xFFB4B2AC.toInt(); c.drawRect(340f, 768f, 858f, 832f, p)
                p.color = 0x22000000; c.drawRect(340f, 828f, 858f, 832f, p)
                // the front lip
                p.color = 0xFFC94E16.toInt(); c.drawRect(266f, 880f, 936f, 912f, p)
            },
        )
    }
}
