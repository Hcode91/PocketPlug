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
 * The palm-sized upright mini arcade cabinets (Basic Fun's arcade classics), after photos, in
 * three colour schemes and with no game art, characters or logos: a marquee sign, the screen
 * between two decorated side strips, a control panel with a red ball-top stick and white round
 * buttons, and a lower front with two speaker grilles and the red ON/OFF and sound keys.
 *
 * Styles: MAZE (blue cabinet, pink and yellow stripes; stick then START), SWARM (green front,
 * black sides, purple streaks on starfield strips; START, stick, FIRE), DIG (black cabinet,
 * white side panel, yellow / purple strips; stick, START, PUMP). CHASE, SPY and BLOCKS use the DIG
 * layout in their own colours (placeholders until photos of those cabinets): CHASE (yellow;
 * stick, START, BUTTON), SPY (charcoal and blue; stick, START, GUNS, MISSILES) and BLOCKS (red and
 * navy; stick, START, A, B).
 * Tap the marquee for the menu; ON/OFF resets, the sound key steps the volume.
 * Input bits as gpl951: A 0x01, B 0x02, Start 0x08, Up 0x10, Down 0x20, Left 0x40, Right 0x80.
 */
class BasicFunCabinetView(ctx: Context, private val style: Int) : View(ctx) {
    companion object {
        const val MAZE = 0; const val SWARM = 1; const val DIG = 2; const val CHASE = 3; const val SPY = 4; const val BLOCKS = 5
        fun styleFor(id: String) = when (id) {
            "bfgalaga" -> SWARM; "bfdigdug" -> DIG; "bfpacman" -> CHASE; "bfspyhnt" -> SPY; "bftetris" -> BLOCKS; else -> MAZE
        }
        const val A = 0x01; const val B = 0x02; const val START = 0x08; const val UP = 0x10; const val DOWN = 0x20; const val LEFT = 0x40; const val RIGHT = 0x80
        const val VOLUME_LEVELS = 4
    }

    var onInput: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var onReset: (() -> Unit)? = null
    var onVolume: ((Int) -> Unit)? = null
    var volumeLevel = 0
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    private val frame = Bitmap.createBitmap(320, 240, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(320 * 240)
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }
    private val smooth = Paint(Paint.FILTER_BITMAP_FLAG)

    // 600 x 900 design space
    private val marquee = RectF(40f, 20f, 560f, 150f)
    private val screen = RectF(150f, 200f, 450f, 425f)
    private val leftStrip = RectF(60f, 200f, 135f, 430f)
    private val rightStrip = RectF(465f, 200f, 540f, 430f)
    private val panel = RectF(30f, 470f, 570f, 600f)
    private val power = RectF(205f, 735f, 285f, 805f)
    private val sound = RectF(315f, 735f, 395f, 805f)
    private val stick = PointF(); private val stickR = 42f
    private class Btn(val bit: Int, val x: Float, val y: Float, val label: String)
    private val buttons: List<Btn>
    private var sc = 1f; private var ox = 0f; private var oy = 0f

    private val pointers = HashMap<Int, Int>()
    private var mask = 0
    private var volumeShownAt = Long.MIN_VALUE / 2

    init {
        when (style) {
            MAZE -> { stick.set(250f, 540f); buttons = listOf(Btn(START, 470f, 540f, "START GAME")) }
            SWARM -> { stick.set(300f, 540f); buttons = listOf(Btn(START, 120f, 540f, "START"), Btn(A, 480f, 540f, "FIRE")) }
            CHASE -> { stick.set(150f, 540f); buttons = listOf(Btn(START, 330f, 545f, "START"), Btn(A, 470f, 545f, "BUTTON")) }
            SPY -> { stick.set(110f, 540f); buttons = listOf(Btn(START, 250f, 545f, "START"), Btn(A, 370f, 545f, "GUNS"), Btn(B, 490f, 545f, "MISSILES")) }
            BLOCKS -> { stick.set(110f, 540f); buttons = listOf(Btn(START, 250f, 545f, "START"), Btn(A, 370f, 545f, "A"), Btn(B, 490f, 545f, "B")) }
            else -> { stick.set(110f, 540f); buttons = listOf(Btn(START, 300f, 545f, "START"), Btn(A, 470f, 545f, "PUMP")) }
        }
    }

    /** the DIG-layout colours: cabinet, its shade, then the strip / shelf colours (outer band, inner band, fill, dots) */
    private class Pal(val body: Int, val dark: Int, val c1: Int, val c2: Int, val c3: Int, val c4: Int)
    private val pal = when (style) {
        CHASE -> Pal(0xFFF2C81E.toInt(), 0xFFC89A10.toInt(), 0xFF1A1A1A.toInt(), 0xFF2050C8.toInt(), 0xFFF6F2E4.toInt(), 0xFFE4202A.toInt())
        SPY -> Pal(0xFF2A2C30.toInt(), 0xFF16171A.toInt(), 0xFF1E4FA8.toInt(), 0xFF8A8E96.toInt(), 0xFFE8E8E8.toInt(), 0xFFD8352A.toInt())
        BLOCKS -> Pal(0xFFB41E24.toInt(), 0xFF7E1218.toInt(), 0xFF14204A.toInt(), 0xFF2A9AD8.toInt(), 0xFFF5DC2A.toInt(), 0xFF3FA04A.toInt())
        else -> Pal(0xFF161616.toInt(), 0xFF0A0A0A.toInt(), 0xFF6A4CA8.toInt(), 0xFF3FA04A.toInt(), 0xFFF5DC2A.toInt(), 0xFFD8352A.toInt())
    }

    fun updateFrame(px: IntArray) {
        if (px.size < framePx.size) return
        System.arraycopy(px, 0, framePx, 0, framePx.size)
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        sc = min(w / 600f, h / 900f)
        ox = (w - 600f * sc) / 2; oy = (h - 900f * sc) / 2
    }

    private val body get() = when (style) { MAZE -> 0xFF1F5FC8.toInt(); SWARM -> 0xFF4CC84A.toInt(); else -> pal.body }
    private val bodyDark get() = when (style) { MAZE -> 0xFF17489A.toInt(); SWARM -> 0xFF33A033.toInt(); else -> pal.dark }
    private val darkText get() = style == SWARM || style == CHASE

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF15161A.toInt())
        c.save(); c.translate(ox, oy); c.scale(sc, sc)
        // cabinet: top box, slanted control shelf, lower box
        p.color = bodyDark
        c.drawRoundRect(20f, 10f, 580f, 890f, 24f, 24f, p)
        p.color = body
        c.drawRoundRect(28f, 10f, 572f, 460f, 22f, 22f, p)
        c.drawRoundRect(28f, 610f, 572f, 885f, 22f, 22f, p)
        if (style == SWARM) {   // black side edges as on the photo
            p.color = 0xFF111111.toInt(); c.drawRect(20f, 30f, 34f, 880f, p); c.drawRect(566f, 30f, 580f, 880f, p)
        }
        if (style == DIG) {     // white side panel edge on the right
            p.color = 0xFFF4F2EC.toInt(); c.drawRect(560f, 20f, 580f, 880f, p)
        }
        drawMarquee(c)
        drawStrips(c)
        // screen bezel and picture
        p.color = if (style == MAZE) 0xFF1A50AE.toInt() else 0xFF0C0C0C.toInt()
        c.drawRect(screen.left - 18f, screen.top - 18f, screen.right + 18f, screen.bottom + 18f, p)
        p.color = 0xFF000000.toInt(); c.drawRect(screen.left - 4f, screen.top - 4f, screen.right + 4f, screen.bottom + 4f, p)
        frame.setPixels(framePx, 0, 320, 0, 0, 320, 240)
        c.drawBitmap(frame, null, screen, smooth)
        drawPanel(c)
        // lower front: speaker grilles and the two red keys
        p.color = bodyDark
        for (gx in listOf(110f, 490f)) for (i in 0 until 7) c.drawRoundRect(gx - 70f, 680f + i * 18f, gx + 70f, 689f + i * 18f, 5f, 5f, p)
        p.color = if (style == DIG || style == SPY) 0xFF222222.toInt() else bodyDark
        c.drawRoundRect(185f, 715f, 415f, 860f, 14f, 14f, p)
        for (r in listOf(power, sound)) {
            p.color = 0xFF8E1A14.toInt(); c.drawRoundRect(r.left, r.top + 6f, r.right, r.bottom + 6f, 10f, 10f, p)
            p.color = 0xFFD8352A.toInt(); c.drawRoundRect(r, 10f, 10f, p)
        }
        t.color = if (darkText) 0xFF102010.toInt() else 0xFFF4F4F4.toInt(); t.textSize = 22f
        c.drawText("ON/OFF", power.centerX(), power.bottom + 40f, t)
        c.drawText("◀))", sound.centerX(), sound.bottom + 40f, t)
        if (System.currentTimeMillis() - volumeShownAt < 1500) {
            val bars = VOLUME_LEVELS - 1 - volumeLevel
            val txt = if (bars == 0) "MUTE" else "VOL " + "▮".repeat(bars) + "▯".repeat(VOLUME_LEVELS - 1 - bars)
            p.color = 0xC0000000.toInt(); c.drawRoundRect(screen.centerX() - 90f, screen.bottom - 50f, screen.centerX() + 90f, screen.bottom - 10f, 8f, 8f, p)
            t.color = Color.WHITE; t.textSize = 24f; c.drawText(txt, screen.centerX(), screen.bottom - 22f, t)
            postInvalidateDelayed(300)
        }
        c.restore()
    }

    private fun drawMarquee(c: Canvas) {
        when (style) {
            MAZE -> {
                p.color = 0xFF2A6BD8.toInt(); c.drawRoundRect(marquee, 10f, 10f, p)
                // the empty sign: yellow inside a pink frame
                p.color = 0xFFE85AA0.toInt(); c.drawRoundRect(160f, 40f, 440f, 130f, 30f, 30f, p)
                p.color = 0xFFF2E04A.toInt(); c.drawRoundRect(172f, 52f, 428f, 118f, 22f, 22f, p)
            }
            SWARM -> {
                p.color = 0xFF0C0C0C.toInt(); c.drawRect(marquee, p)
                p.color = 0xFF6FBF8F.toInt(); c.drawRoundRect(170f, 50f, 430f, 125f, 18f, 18f, p)
                p.color = 0xFFEDEAD0.toInt(); c.drawRoundRect(182f, 60f, 418f, 115f, 12f, 12f, p)
            }
            DIG -> {
                p.color = 0xFF0C0C0C.toInt(); c.drawRect(marquee, p)
                p.color = 0xFFE4202A.toInt(); c.drawRect(60f, 40f, 540f, 130f, p)
                p.color = 0xFFF5DC2A.toInt(); c.drawRect(66f, 46f, 534f, 124f, p)
                p.color = 0xFFF4F4F4.toInt(); c.drawRect(220f, 55f, 380f, 115f, p)
            }
            else -> {   // an empty sign in the style's colours
                p.color = pal.c1; c.drawRect(marquee, p)
                p.color = pal.c4; c.drawRect(60f, 40f, 540f, 130f, p)
                p.color = pal.c3; c.drawRect(66f, 46f, 534f, 124f, p)
                p.color = pal.c2; c.drawRect(200f, 58f, 400f, 112f, p)
            }
        }
        t.color = if (style == MAZE) 0xFFE8EEFA.toInt() else 0xFF8C8C8C.toInt(); t.textSize = 18f
        c.drawText("MENU", marquee.centerX(), marquee.bottom - 6f, t)
    }

    private fun drawStrips(c: Canvas) {
        for (r in listOf(leftStrip, rightStrip)) when (style) {
            MAZE -> {
                p.color = 0xFFF2E04A.toInt(); c.drawRect(r, p)
                p.color = 0xFFE85AA0.toInt(); c.drawRect(r.left + 10f, r.top, r.right - 10f, r.bottom, p)
                p.color = 0xFFF2E04A.toInt(); c.drawRect(r.left + 22f, r.top, r.right - 22f, r.bottom, p)
                p.color = 0xFFE85AA0.toInt(); c.drawRect(r.left + 28f, r.top, r.right - 28f, r.bottom, p)
            }
            SWARM -> {
                p.color = 0xFF0A0A12.toInt(); c.drawRect(r, p)
                p.color = 0xFF6A5BD8.toInt()
                for (i in 0 until 5) {
                    val y = r.top + 20f + i * 45f
                    val path = Path().apply { moveTo(r.left, y + 30f); lineTo(r.right, y); lineTo(r.right, y + 14f); lineTo(r.left, y + 44f); close() }
                    c.drawPath(path, p)
                }
                p.color = 0xFFF0F0F0.toInt()
                for (i in 0 until 9) c.drawCircle(r.left + ((i * 37) % 70) + 3f, r.top + 12f + i * 25f, 2f, p)
                p.color = 0xFFE6C23A.toInt(); c.drawCircle(r.centerX(), r.top + 25f, 14f, p)
            }
            else -> {
                p.color = pal.c1; c.drawRect(r, p)
                p.color = pal.c2; c.drawRect(r.left + 6f, r.top + 6f, r.right - 6f, r.bottom - 6f, p)
                p.color = pal.c3; c.drawRect(r.left + 12f, r.top + 12f, r.right - 12f, r.bottom - 12f, p)
                p.color = pal.c4
                for (i in 0 until 4) c.drawCircle(r.centerX(), r.top + 40f + i * 50f, 12f, p)
            }
        }
    }

    private fun drawPanel(c: Canvas) {
        // the slanted shelf
        val path = Path().apply { moveTo(40f, 465f); lineTo(560f, 465f); lineTo(575f, 605f); lineTo(25f, 605f); close() }
        p.color = when (style) { MAZE -> 0xFF2563C8.toInt(); SWARM -> 0xFF0E0E0E.toInt(); DIG -> 0xFF101010.toInt(); else -> pal.dark }
        c.drawPath(path, p)
        when (style) {
            MAZE -> {   // pink and yellow corner bands
                p.color = 0xFFF2E04A.toInt(); c.drawRect(60f, 490f, 200f, 585f, p)
                p.color = 0xFFE0405A.toInt(); c.drawRect(72f, 502f, 188f, 573f, p)
                p.color = 0xFF2563C8.toInt(); c.drawRect(110f, 502f, 200f, 548f, p)
                p.color = 0xFFF2E04A.toInt(); c.drawRoundRect(395f, 485f, 545f, 590f, 50f, 50f, p)
                p.color = 0xFFE0405A.toInt(); c.drawRoundRect(405f, 495f, 535f, 580f, 44f, 44f, p)
            }
            SWARM -> {
                p.color = 0xFF9ED8EC.toInt(); c.drawRoundRect(60f, 495f, 540f, 590f, 20f, 20f, p)
                p.color = 0xFF5FB25A.toInt(); c.drawRect(60f, 495f, 540f, 505f, p)
            }
            DIG -> {
                p.color = pal.c1; c.drawRect(45f, 495f, 555f, 590f, p)
                p.color = pal.c3; c.drawRect(45f, 505f, 555f, 512f, p); c.drawRect(45f, 573f, 555f, 580f, p)
                p.color = pal.c4; c.drawRect(220f, 512f, 400f, 573f, p)
            }
            else -> {
                p.color = pal.c1; c.drawRect(45f, 495f, 555f, 590f, p)
                p.color = pal.c3; c.drawRect(45f, 505f, 555f, 512f, p); c.drawRect(45f, 573f, 555f, 580f, p)
            }
        }
        val m = mask or externalMask
        // stick base and ball (leans with the direction)
        var dx = 0f; var dy = 0f
        if (m and LEFT != 0) dx -= 1f; if (m and RIGHT != 0) dx += 1f; if (m and UP != 0) dy -= 1f; if (m and DOWN != 0) dy += 1f
        p.color = 0xFF0A0A0A.toInt(); c.drawOval(stick.x - 34f, stick.y - 8f, stick.x + 34f, stick.y + 16f, p)
        val bx = stick.x + dx * 16f; val by = stick.y - 50f + dy * 10f
        p.color = 0xFFB01A10.toInt(); p.strokeWidth = 12f; c.drawLine(stick.x, stick.y + 2f, bx, by, p)
        p.shader = RadialGradient(bx - 10f, by - 10f, 40f, 0xFFF26050.toInt(), 0xFFA01810.toInt(), Shader.TileMode.CLAMP)
        c.drawCircle(bx, by, 30f, p); p.shader = null
        t.color = if (style == SWARM) 0xFF1A2A3A.toInt() else 0xFFF2E04A.toInt(); t.textSize = 18f
        c.drawText("◀  ▶", stick.x, stick.y + 40f, t)
        for (b in buttons) {
            val down = m and b.bit != 0
            p.color = 0xFF9A9A9A.toInt(); c.drawCircle(b.x, b.y + 8f, 36f, p)
            p.color = if (down) 0xFFC8C8C0.toInt() else 0xFFF6F4EE.toInt(); c.drawCircle(b.x, b.y + (if (down) 6f else 0f), 34f, p)
            t.color = if (style == SWARM) 0xFF1A2A3A.toInt() else 0xFFF4F4F4.toInt(); t.textSize = 16f
            c.drawText(b.label, b.x, b.y + 58f, t)
        }
    }

    private fun hit(x: Float, y: Float): Int {
        val ddx = x - stick.x; val ddy = y - (stick.y - 30f)
        if (hypot(ddx, ddy) < 95f) {
            if (hypot(ddx, ddy) < 12f) return 0
            var b = 0
            if (abs(ddx) > abs(ddy) * 0.5f) b = b or (if (ddx < 0) LEFT else RIGHT)
            if (abs(ddy) > abs(ddx) * 0.5f) b = b or (if (ddy < 0) UP else DOWN)
            return b
        }
        for (bt in buttons) if (hypot(x - bt.x, y - bt.y) < 55f) return bt.bit
        return 0
    }

    private fun send() {
        val m = pointers.values.fold(0) { a, b -> a or b }
        if (m != mask) {
            if (m and mask.inv() != 0) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            mask = m; onInput?.invoke(m); invalidate()
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                val x = (e.getX(i) - ox) / sc; val y = (e.getY(i) - oy) / sc
                when {
                    marquee.contains(x, y) -> { onMenu?.invoke(); return true }
                    RectF(power).apply { inset(-15f, -15f) }.contains(x, y) -> {
                        performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP); onReset?.invoke(); return true
                    }
                    RectF(sound).apply { inset(-15f, -15f) }.contains(x, y) -> {
                        performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                        volumeLevel = (volumeLevel + 1) % VOLUME_LEVELS
                        volumeShownAt = System.currentTimeMillis()
                        onVolume?.invoke(volumeLevel); invalidate(); return true
                    }
                }
                pointers[e.getPointerId(i)] = hit(x, y); send()
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until e.pointerCount) {
                    val id = e.getPointerId(i)
                    if (pointers.containsKey(id)) pointers[id] = hit((e.getX(i) - ox) / sc, (e.getY(i) - oy) / sc)
                }
                send()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> { pointers.remove(e.getPointerId(e.actionIndex)); send() }
            MotionEvent.ACTION_CANCEL -> { pointers.clear(); send() }
        }
        return true
    }
}
