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
 * white side panel, yellow / purple strips; stick, START, PUMP), CHASE (yellow cabinet, olive sign,
 * black side strips; MODE, stick, START GAME), SPY (black cabinet, silver sides with red / yellow
 * stripes; stick, a small START, GUNS, MISSILES) and BLOCKS (red cabinet, black sign, yellow side
 * strips; MOVE stick, START, rotate left, rotate right).
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
    private class Btn(val bit: Int, val x: Float, val y: Float, val label: String, val r: Float = 34f)
    private val buttons: List<Btn>
    private var sc = 1f; private var ox = 0f; private var oy = 0f

    private val pointers = HashMap<Int, Int>()
    private var mask = 0
    private var volumeShownAt = Long.MIN_VALUE / 2

    init {
        when (style) {
            MAZE -> { stick.set(250f, 540f); buttons = listOf(Btn(START, 470f, 540f, "START GAME")) }
            SWARM -> { stick.set(300f, 540f); buttons = listOf(Btn(START, 120f, 540f, "START"), Btn(A, 480f, 540f, "FIRE")) }
            CHASE -> { stick.set(300f, 540f); buttons = listOf(Btn(A, 110f, 545f, "MODE"), Btn(START, 490f, 545f, "START GAME")) }
            SPY -> { stick.set(130f, 540f); buttons = listOf(Btn(START, 260f, 552f, "START", 22f), Btn(A, 370f, 545f, "GUNS"), Btn(B, 490f, 545f, "MISSILES")) }
            BLOCKS -> { stick.set(100f, 540f); buttons = listOf(Btn(START, 230f, 545f, "START"), Btn(A, 360f, 545f, "↺"), Btn(B, 490f, 545f, "↻")) }
            else -> { stick.set(110f, 540f); buttons = listOf(Btn(START, 300f, 545f, "START"), Btn(A, 470f, 545f, "PUMP")) }
        }
    }

    /** the DIG-layout colours: cabinet, its shade, then the strip / shelf colours (outer band, inner band, fill, dots) */
    private class Pal(val body: Int, val dark: Int, val c1: Int, val c2: Int, val c3: Int, val c4: Int)
    private val pal = when (style) {
        CHASE -> Pal(0xFFF4CC1A.toInt(), 0xFFCFA210.toInt(), 0xFF0E0E0E.toInt(), 0xFF2E8A4A.toInt(), 0xFFF2D43A.toInt(), 0xFFE4202A.toInt())
        SPY -> Pal(0xFF161616.toInt(), 0xFF0A0A0A.toInt(), 0xFF0E0E0E.toInt(), 0xFFC8C8C6.toInt(), 0xFFF2B21E.toInt(), 0xFFE0302A.toInt())
        BLOCKS -> Pal(0xFFC22A2A.toInt(), 0xFF8E1C1E.toInt(), 0xFF0E0E0E.toInt(), 0xFFF2E21E.toInt(), 0xFFE88A1E.toInt(), 0xFF2A5AD8.toInt())
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
    private val darkText get() = style == SWARM
    private val blackPlate get() = style == DIG || style == SPY || style == CHASE || style == BLOCKS

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
        if (style == SPY) {     // silver side panels with the red / yellow stripes on the base
            for (x in listOf(20f, 548f)) {
                p.color = pal.c2; c.drawRect(x, 30f, x + 32f, 880f, p)
                p.color = 0xFF2A3A6A.toInt(); for (i in 0 until 4) c.drawRect(x, 640f + i * 12f, x + 32f, 645f + i * 12f, p)
                p.color = pal.c3; c.drawRect(x, 720f, x + 32f, 730f, p)
                p.color = pal.c4; c.drawRect(x, 732f, x + 32f, 750f, p)
                p.color = 0xFF2A3A6A.toInt(); for (i in 0 until 4) c.drawRect(x, 800f + i * 12f, x + 32f, 805f + i * 12f, p)
            }
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
        if (style == SPY || style == BLOCKS) {   // round grilles
            p.color = if (style == SPY) 0xFF050505.toInt() else bodyDark
            for (gx in listOf(105f, 495f)) {
                c.drawCircle(gx, 770f, 62f, p)
                p.color = if (style == SPY) 0xFF2A2A2A.toInt() else body
                for (i in -3..3) { val hw = kotlin.math.sqrt(3600f - (i * 15f) * (i * 15f)); c.drawRect(gx - hw, 768f + i * 15f, gx + hw, 772f + i * 15f, p) }
                p.color = if (style == SPY) 0xFF050505.toInt() else bodyDark
            }
        } else {
            p.color = bodyDark
            for (gx in listOf(110f, 490f)) for (i in 0 until 7) c.drawRoundRect(gx - 70f, 680f + i * 18f, gx + 70f, 689f + i * 18f, 5f, 5f, p)
        }
        p.color = if (blackPlate) 0xFF1E1E1E.toInt() else bodyDark
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
            CHASE -> {   // the empty olive-gold sign
                p.shader = LinearGradient(0f, marquee.top, 0f, marquee.bottom, intArrayOf(0xFF7A7426.toInt(), 0xFF4A4614.toInt(), 0xFF6A6420.toInt()), null, Shader.TileMode.CLAMP)
                c.drawRect(marquee, p); p.shader = null
                p.color = 0xFF2E2C0C.toInt(); c.drawRect(marquee.left, marquee.bottom - 10f, marquee.right, marquee.bottom, p)
            }
            SPY -> {     // black sign with the red / orange / yellow speed stripes
                p.color = 0xFF080808.toInt(); c.drawRect(marquee, p)
                p.color = 0xFF2A2A2A.toInt(); c.drawRect(marquee.left + 6f, marquee.top + 6f, marquee.right - 6f, marquee.bottom - 6f, p)
                p.color = 0xFF0C0C0C.toInt(); c.drawRect(marquee.left + 10f, marquee.top + 10f, marquee.right - 10f, marquee.bottom - 10f, p)
                p.color = pal.c4; c.drawRect(200f, 50f, 545f, 60f, p)
                p.color = 0xFFE86A1E.toInt(); c.drawRect(230f, 64f, 545f, 72f, p)
                p.color = pal.c3; c.drawRect(260f, 76f, 545f, 82f, p)
                p.color = pal.c4; c.drawRect(200f, 112f, 545f, 118f, p)
            }
            else -> {    // BLOCKS: black sign between the red uprights, a few blocks in the corners
                p.color = 0xFF0C0C0C.toInt(); c.drawRect(marquee, p)
                p.color = 0xFF1C1C1C.toInt(); c.drawRect(marquee.left + 8f, marquee.top + 8f, marquee.right - 8f, marquee.bottom - 8f, p)
                val cols = intArrayOf(0xFF2A5AD8.toInt(), 0xFFE0302A.toInt(), 0xFFF2E21E.toInt(), 0xFF3FA04A.toInt(), 0xFF8A3AC8.toInt())
                for (k in 0 until 5) { p.color = cols[k]
                    c.drawRect(62f + (k % 3) * 18f, 92f - (k / 3) * 18f, 78f + (k % 3) * 18f, 108f - (k / 3) * 18f, p)
                    c.drawRect(522f - (k % 3) * 18f, 92f - (k / 3) * 18f, 538f - (k % 3) * 18f, 108f - (k / 3) * 18f, p) }
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
            DIG -> {
                p.color = pal.c1; c.drawRect(r, p)
                p.color = pal.c2; c.drawRect(r.left + 6f, r.top + 6f, r.right - 6f, r.bottom - 6f, p)
                p.color = pal.c3; c.drawRect(r.left + 12f, r.top + 12f, r.right - 12f, r.bottom - 12f, p)
                p.color = pal.c4
                for (i in 0 until 4) c.drawCircle(r.centerX(), r.top + 40f + i * 50f, 12f, p)
            }
            CHASE -> {   // black strips: dotted yellow edge, green maze outlines, a gold card at the foot
                p.color = 0xFF0E0E0E.toInt(); c.drawRect(r, p)
                p.color = pal.c3; var y = r.top + 6f
                while (y < r.bottom - 4f) { c.drawCircle(r.left + 6f, y, 2f, p); c.drawCircle(r.right - 6f, y, 2f, p); y += 10f }
                val g = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeWidth = 3f; color = pal.c2 }
                c.drawRect(r.left + 18f, r.top + 70f, r.right - 30f, r.top + 110f, g); c.drawRect(r.left + 30f, r.top + 82f, r.right - 18f, r.top + 122f, g)
                p.color = 0xFF8A7A2A.toInt(); c.drawRect(r.left + 16f, r.bottom - 70f, r.right - 16f, r.bottom - 12f, p)
                p.color = 0xFF3A3410.toInt(); for (i in 0 until 4) c.drawRect(r.left + 22f, r.bottom - 62f + i * 12f, r.right - 22f, r.bottom - 58f + i * 12f, p)
            }
            SPY -> {     // black pillars with a red / yellow stripe
                p.color = 0xFF0C0C0C.toInt(); c.drawRect(r, p)
                p.color = 0xFF222222.toInt(); c.drawRect(r.left + 4f, r.top, r.left + 8f, r.bottom, p)
                p.color = pal.c4; c.drawRect(r.left, r.top + 110f, r.right, r.top + 116f, p)
                p.color = pal.c3; c.drawRect(r.left, r.top + 118f, r.right, r.top + 122f, p)
            }
            else -> {    // BLOCKS: yellow strips with a stack of coloured blocks at the bottom
                p.color = pal.c2; c.drawRect(r, p)
                val cols = intArrayOf(0xFF2A5AD8.toInt(), 0xFFE0302A.toInt(), 0xFF3FA04A.toInt(), 0xFF8A3AC8.toInt(), 0xFFE88A1E.toInt())
                for (row in 0 until 4) for (col in 0 until 4) {
                    if ((row * 3 + col) % 5 == 4) continue
                    p.color = cols[(row + col * 2) % 5]
                    val x = r.left + 4f + col * 17f; val yy = r.bottom - 22f - row * 17f
                    c.drawRect(x, yy, x + 15f, yy + 15f, p)
                }
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
            CHASE -> {   // black panel inside a dotted yellow border
                p.color = 0xFF0E0E0E.toInt(); c.drawRect(45f, 488f, 555f, 595f, p)
                p.color = pal.c3; var x = 52f
                while (x < 550f) { c.drawCircle(x, 494f, 2f, p); c.drawCircle(x, 589f, 2f, p); x += 10f }
                val g = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeWidth = 3f; color = pal.c2 }
                c.drawRect(250f, 500f, 350f, 584f, g)
            }
            SPY -> {
                p.color = 0xFF0A0A0A.toInt(); c.drawRect(45f, 490f, 555f, 595f, p)
                p.color = 0xFF14301C.toInt(); c.drawRect(300f, 500f, 545f, 504f, p); c.drawRect(300f, 582f, 545f, 586f, p)
            }
            else -> {    // BLOCKS: black panel with an orange line and blocks between the keys
                p.color = 0xFF0E0E0E.toInt(); c.drawRect(45f, 488f, 555f, 595f, p)
                val g = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeWidth = 3f; color = pal.c3 }
                c.drawRect(55f, 496f, 545f, 588f, g)
                val cols = intArrayOf(0xFFE88A1E.toInt(), 0xFF3FA04A.toInt(), 0xFF8A3AC8.toInt(), 0xFF2A5AD8.toInt())
                for ((k, bx) in listOf(165f, 295f, 425f).withIndex()) { p.color = cols[k]; c.drawRect(bx - 10f, 530f, bx + 2f, 542f, p); c.drawRect(bx + 2f, 530f, bx + 14f, 542f, p); p.color = cols[k + 1]; c.drawRect(bx - 4f, 542f, bx + 8f, 554f, p) }
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
        c.drawText(if (style == BLOCKS) "MOVE" else "◀  ▶", stick.x, stick.y + 40f, t)
        for (b in buttons) {
            val down = m and b.bit != 0
            p.color = 0xFF9A9A9A.toInt(); c.drawCircle(b.x, b.y + 8f, b.r + 2f, p)
            p.color = if (down) 0xFFC8C8C0.toInt() else 0xFFF6F4EE.toInt(); c.drawCircle(b.x, b.y + (if (down) 6f else 0f), b.r, p)
            t.color = if (style == SWARM) 0xFF1A2A3A.toInt() else 0xFFF4F4F4.toInt(); t.textSize = 16f
            t.textSize = if (b.label.length == 1) 30f else 16f
            c.drawText(b.label, b.x, b.y + b.r + 24f + (if (b.label.length == 1) 8f else 0f), t)
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
        for (bt in buttons) if (hypot(x - bt.x, y - bt.y) < bt.r + 21f) return bt.bit
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
