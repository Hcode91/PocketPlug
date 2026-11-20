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
                    paint.color = 0x88FFFFFF.toInt(); c.drawCircle(l.x - l.r * 0.3f, l.y - l.r * 0.3f, l.r * 0.3f, paint) }
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

    private fun drawScreen(c: Canvas) {
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
                if (!down) { paint.color = 0x30FFFFFF; c.drawRoundRect(k.x - k.bw / 2 + 8f, k.y - k.bh / 2 + 6f, k.x + k.bw / 2 - 8f, k.y - k.bh / 2 + k.bh * 0.3f, 10f, 10f, paint) }
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
) {
    enum class Kind { STICK, BUTTON, SWITCH }
    enum class LedKind { ROUND, DIGIT }
    class Led(val kind: LedKind, val row: Int, val col: Int, val x: Float, val y: Float, val r: Float, val color: Int = 0xFFFF2A20.toInt())
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
    )

    companion object {
        fun forMachine(id: String): HhSkin? = when (id) {
            "alnattck" -> alienAttack()
            "alnchase" -> alienChase()
            else -> null
        }

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
