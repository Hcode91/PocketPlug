package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import kotlin.math.*

/**
 * Control panels for the 2004-2007 Japanese "play in front of the TV" games,
 * drawn as themes around each game's colours and props (no characters,
 * names or logos):
 *
 *  - "tamagotchi": a pale-blue egg handheld with a yellow four-petal pad and
 *                  two yellow buttons, stars and a ribbon (SPG2xx P1 bits).
 *  - "onmyou":     a dark shrine board with paper talismans, a yin-yang seal,
 *                  red and black; O / X buttons.
 *  - "punch":      wooden deck planks under red speed lines; O button.
 *  - "drums":      deep metallic blue with two taiko drum heads and two
 *                  flame-tipped sticks as the four hit pads.
 *  - "rainbow":    a pastel rainbow with sparkles and butterflies.
 *  - "hero":       silver with red stripes over a night city skyline.
 *  - "ninja":      the blue unit itself: X and O keys, the camera window and
 *                  an orange label, plus up / down; orange swirl behind it.
 *
 * The camera games ("onmyou", "punch", "rainbow", "hero") have a camera
 * window: drag the two reflective stickers (gloves, sword, hands) around it,
 * and tap one (or flick it up) to punch / swing with it. Sticker positions
 * go out as analogue sticks 0 and 1, punches as input bits 9 and 10.
 */
class LetsTvView(ctx: Context, private val theme: String) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    var onAnalog: ((Int, Float, Float) -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    companion object {
        const val PUNCH_L = 1 shl 9
        const val PUNCH_R = 1 shl 10

        fun hasCamera(theme: String) = theme == "onmyou" || theme == "punch" || theme == "rainbow" || theme == "hero" || theme == "ninja"

        fun keyBit(theme: String, code: Int): Int = when (theme) {
            "tamagotchi" -> when (code) {
                KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> 0x40
                KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> 0x20
                KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_Z -> 0x10
                KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_ESCAPE, KeyEvent.KEYCODE_X -> 0x80
                else -> 0
            }
            "ninja" -> when (code) {
                KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_Z -> 1 shl 16
                KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_ESCAPE, KeyEvent.KEYCODE_X -> 1 shl 17
                KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> 1 shl 18
                KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> 1 shl 19
                KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_A -> PUNCH_L
                KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_D -> PUNCH_R
                else -> 0
            }
            "drums" -> when (code) {
                KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_A, KeyEvent.KEYCODE_DPAD_LEFT -> 0x02
                KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_D, KeyEvent.KEYCODE_DPAD_RIGHT -> 0x01
                KeyEvent.KEYCODE_BUTTON_X, KeyEvent.KEYCODE_Q -> 0x08
                KeyEvent.KEYCODE_BUTTON_Y, KeyEvent.KEYCODE_E -> 0x04
                else -> 0
            }
            else -> when (code) {
                KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_Z -> 0x01
                KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_ESCAPE, KeyEvent.KEYCODE_X -> 0x02
                KeyEvent.KEYCODE_BUTTON_L1, KeyEvent.KEYCODE_A -> PUNCH_L
                KeyEvent.KEYCODE_BUTTON_R1, KeyEvent.KEYCODE_D -> PUNCH_R
                else -> 0
            }
        }
    }

    private class Btn(val bit: Int, val label: String, var cx: Float = 0f, var cy: Float = 0f, var r: Float = 0f, val fill: Int, val ink: Int)

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }
    private val path = Path()
    private val btns = mutableListOf<Btn>()
    private val cam = RectF()
    private val stickers = if (theme == "onmyou") arrayOf(PointF(0f, 0.1f), PointF(0f, 0.1f)) else arrayOf(PointF(-0.45f, 0.1f), PointF(0.45f, 0.1f))
    private val pointerBtn = HashMap<Int, Int>()
    private val pointerSticker = HashMap<Int, Int>()
    private val downAt = HashMap<Int, PointF>()
    private val downTime = HashMap<Int, Long>()
    private var touchMask = 0
    private val nStickers = if (theme == "onmyou") 1 else 2
    private var punchMask = 0
    private val punchEnd = LongArray(2)

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        btns.clear()
        val W = w.toFloat(); val H = h.toFloat()
        when (theme) {
            "tamagotchi" -> {
                val cy = H * 0.55f
                val r = H * 0.075f
                btns += Btn(0x40, "▲", W * 0.3f, cy - r * 1.35f, r, 0xFFF6D44B.toInt(), 0xFF8A6A10.toInt())
                btns += Btn(0x20, "▼", W * 0.3f, cy + r * 1.35f, r, 0xFFF6D44B.toInt(), 0xFF8A6A10.toInt())
                btns += Btn(0x10, "", W * 0.68f, cy - H * 0.09f, H * 0.065f, 0xFFF6D44B.toInt(), 0)
                btns += Btn(0x80, "", W * 0.76f, cy + H * 0.08f, H * 0.065f, 0xFFF6D44B.toInt(), 0)
            }
            "ninja" -> {
                // camera window up top, the unit's keys along the bottom
                cam.set(W * 0.06f, H * 0.05f, W * 0.94f, H * 0.62f)
                btns += Btn(1 shl 17, "✕", W * 0.3f, H * 0.83f, H * 0.07f, 0xFF20304A.toInt(), 0xFFE8EEF8.toInt())
                btns += Btn(1 shl 16, "○", W * 0.5f, H * 0.83f, H * 0.07f, 0xFF20304A.toInt(), 0xFFE8EEF8.toInt())
                btns += Btn(1 shl 18, "▲", W * 0.8f, H * 0.75f, H * 0.055f, 0xFFF08A1C.toInt(), Color.WHITE)
                btns += Btn(1 shl 19, "▼", W * 0.8f, H * 0.91f, H * 0.055f, 0xFFF08A1C.toInt(), Color.WHITE)
            }
            "drums" -> {
                btns += Btn(0x02, "", W * 0.27f, H * 0.5f, min(W * 0.2f, H * 0.32f), 0xFFE9DCC0.toInt(), 0)
                btns += Btn(0x01, "", W * 0.73f, H * 0.5f, min(W * 0.2f, H * 0.32f), 0xFFE9DCC0.toInt(), 0)
                btns += Btn(0x08, "", W * 0.08f, H * 0.86f, H * 0.075f, 0xFFD8381E.toInt(), 0)
                btns += Btn(0x04, "", W * 0.92f, H * 0.86f, H * 0.075f, 0xFFD8381E.toInt(), 0)
            }
            else -> {
                // camera window across the panel, buttons underneath
                cam.set(W * 0.08f, H * 0.08f, W * 0.92f, H * 0.7f)
                val by = H * 0.85f
                when (theme) {
                    "onmyou" -> {
                        btns += Btn(0x02, "✕", W * 0.24f, by, H * 0.07f, 0xFF1C1414.toInt(), 0xFFF2E8D0.toInt())
                        btns += Btn(0x01, "○", W * 0.76f, by, H * 0.07f, 0xFFB3201B.toInt(), 0xFFF2E8D0.toInt())
                        // hold: the drive raised up to the unit (opens the in-battle menu)
                        btns += Btn(PUNCH_L, "⇧", W * 0.5f, by, H * 0.06f, 0xFFF2E8D0.toInt(), 0xFF1C1414.toInt())
                    }
                    "punch" -> btns += Btn(0x01, "○", W * 0.5f, by, H * 0.075f, 0xFFF4C430.toInt(), 0xFF7A1E10.toInt())
                }
            }
        }
    }

    // ---- drawing ----

    override fun onDraw(c: Canvas) {
        val held = touchMask or externalMask or punchMask
        when (theme) {
            "tamagotchi" -> drawTamagotchi(c)
            "onmyou" -> drawOnmyou(c)
            "punch" -> drawPunch(c)
            "drums" -> drawDrums(c, held)
            "rainbow" -> drawRainbow(c)
            "hero" -> drawHero(c)
            "ninja" -> drawNinja(c)
        }
        if (hasCamera(theme)) drawCamera(c, held)
        if (theme != "drums") for (b in btns) drawButton(c, b, held and b.bit != 0)
        // punches fade out on their own
        val now = System.currentTimeMillis()
        if (punchMask != 0) {
            for (g in 0..1) if (punchMask and (PUNCH_L shl g) != 0 && now >= punchEnd[g]) { punchMask = punchMask and (PUNCH_L shl g).inv(); send() }
            postInvalidateDelayed(16)
        }
    }

    private fun star(c: Canvas, x: Float, y: Float, r: Float, color: Int) {
        path.reset()
        for (i in 0 until 10) {
            val a = -PI / 2 + i * PI / 5
            val rr = if (i % 2 == 0) r else r * 0.45f
            val px = x + (cos(a) * rr).toFloat(); val py = y + (sin(a) * rr).toFloat()
            if (i == 0) path.moveTo(px, py) else path.lineTo(px, py)
        }
        path.close(); paint.color = color; c.drawPath(path, paint)
    }

    private fun sparkle(c: Canvas, x: Float, y: Float, r: Float, color: Int) {
        path.reset()
        path.moveTo(x, y - r); path.quadTo(x, y, x + r, y); path.quadTo(x, y, x, y + r); path.quadTo(x, y, x - r, y); path.quadTo(x, y, x, y - r)
        paint.color = color; c.drawPath(path, paint)
    }

    private fun drawTamagotchi(c: Canvas) {
        val W = width.toFloat(); val H = height.toFloat()
        c.drawColor(0xFFF3E3C6.toInt())
        // the egg, wider than tall like the handheld
        val egg = RectF(W * 0.06f, H * 0.08f, W * 0.94f, H * 0.95f)
        paint.color = 0x33000000; c.drawOval(egg.left + 6, egg.top + 14, egg.right + 6, egg.bottom + 14, paint)
        paint.shader = RadialGradient(W * 0.4f, H * 0.35f, W * 0.6f, 0xFFB5DDF2.toInt(), 0xFF6FB1DC.toInt(), Shader.TileMode.CLAMP)
        c.drawOval(egg, paint); paint.shader = null
        stroke.color = 0xFF4F8EBA.toInt(); stroke.strokeWidth = W * 0.008f; c.drawOval(egg, stroke)
        // little window and ribbon
        paint.color = 0xFFF7F4EC.toInt()
        c.drawRoundRect(W * 0.42f, H * 0.2f, W * 0.58f, H * 0.38f, W * 0.02f, W * 0.02f, paint)
        paint.color = 0xFF8E9A86.toInt(); c.drawRect(W * 0.445f, H * 0.23f, W * 0.555f, H * 0.35f, paint)
        paint.color = 0xFFFAFAF6.toInt()
        path.reset(); path.moveTo(W * 0.3f, H * 0.83f); path.quadTo(W * 0.5f, H * 0.76f, W * 0.72f, H * 0.82f)
        path.lineTo(W * 0.72f, H * 0.89f); path.quadTo(W * 0.5f, H * 0.83f, W * 0.3f, H * 0.9f); path.close()
        c.drawPath(path, paint)
        star(c, W * 0.2f, H * 0.3f, W * 0.025f, 0xFFF7E26B.toInt())
        star(c, W * 0.82f, H * 0.32f, W * 0.022f, 0xFFF4B6C2.toInt())
        star(c, W * 0.88f, H * 0.6f, W * 0.02f, 0xFFF7E26B.toInt())
        star(c, W * 0.14f, H * 0.62f, W * 0.018f, 0xFFF4B6C2.toInt())
        star(c, W * 0.6f, H * 0.76f, W * 0.018f, 0xFFF7E26B.toInt())
        // the four-petal pad behind the up / down buttons
        val cx = W * 0.3f; val cy = H * 0.55f; val pr = H * 0.075f
        paint.color = 0xFFE2BE33.toInt()
        c.drawOval(cx - pr * 2.3f, cy - pr * 0.55f, cx - pr * 0.6f, cy + pr * 0.55f, paint)
        c.drawOval(cx + pr * 0.6f, cy - pr * 0.55f, cx + pr * 2.3f, cy + pr * 0.55f, paint)
    }

    private fun drawOnmyou(c: Canvas) {
        val W = width.toFloat(); val H = height.toFloat()
        paint.shader = LinearGradient(0f, 0f, 0f, H, 0xFF2A1612.toInt(), 0xFF120A08.toInt(), Shader.TileMode.CLAMP)
        c.drawRect(0f, 0f, W, H, paint); paint.shader = null
        // talismans hanging at the sides
        for ((i, x) in listOf(W * 0.015f, W * 0.94f).withIndex()) {
            paint.color = 0xFFF1E6C8.toInt(); c.drawRect(x, H * 0.05f, x + W * 0.045f, H * 0.62f, paint)
            paint.color = 0xFFB3201B.toInt()
            c.drawRect(x + W * 0.008f, H * 0.08f, x + W * 0.037f, H * 0.11f, paint)
            stroke.color = 0xFF1C1414.toInt(); stroke.strokeWidth = W * 0.004f
            for (k in 0 until 5) c.drawLine(x + W * 0.012f, H * (0.16f + k * 0.08f) + i * 4, x + W * 0.033f, H * (0.2f + k * 0.08f), stroke)
        }
        // yin-yang seal behind the buttons
        val cx = W * 0.5f; val cy = H * 0.85f; val r = H * 0.12f
        paint.color = 0xFFF2E8D0.toInt(); c.drawCircle(cx, cy, r, paint)
        paint.color = 0xFF1C1414.toInt(); c.drawArc(cx - r, cy - r, cx + r, cy + r, 90f, 180f, true, paint)
        c.drawCircle(cx, cy - r / 2, r / 2, paint)
        paint.color = 0xFFF2E8D0.toInt(); c.drawCircle(cx, cy + r / 2, r / 2, paint)
        c.drawCircle(cx, cy - r / 2, r / 7, paint)
        paint.color = 0xFF1C1414.toInt(); c.drawCircle(cx, cy + r / 2, r / 7, paint)
        stroke.color = 0xFFB3201B.toInt(); stroke.strokeWidth = r * 0.08f; c.drawCircle(cx, cy, r, stroke)
    }

    private fun drawPunch(c: Canvas) {
        val W = width.toFloat(); val H = height.toFloat()
        c.drawColor(0xFFF5A9A0.toInt())
        // red speed lines out from the centre
        paint.color = 0xFFE0281E.toInt()
        val cx = W / 2; val cy = H * 0.4f; val R = max(W, H)
        for (i in 0 until 36) {
            val a = i * PI * 2 / 36 + 0.04 * (i % 3)
            path.reset()
            path.moveTo(cx + (cos(a) * R * 0.35f).toFloat(), cy + (sin(a) * R * 0.35f).toFloat())
            path.lineTo(cx + (cos(a - 0.05) * R).toFloat(), cy + (sin(a - 0.05) * R).toFloat())
            path.lineTo(cx + (cos(a + 0.05) * R).toFloat(), cy + (sin(a + 0.05) * R).toFloat())
            path.close(); c.drawPath(path, paint)
        }
        // deck planks along the bottom
        val top = H * 0.74f
        paint.color = 0xFF8A5A2E.toInt(); c.drawRect(0f, top, W, H, paint)
        stroke.color = 0xFF5E3B1C.toInt(); stroke.strokeWidth = H * 0.006f
        for (k in 1 until 4) c.drawLine(0f, top + (H - top) * k / 4, W, top + (H - top) * k / 4, stroke)
        for (k in 0 until 6) c.drawLine(W * (k * 0.19f + 0.07f), top + (H - top) * (k % 4) / 4, W * (k * 0.19f + 0.07f), top + (H - top) * ((k % 4) + 1) / 4, stroke)
    }

    private fun drawDrums(c: Canvas, held: Int) {
        val W = width.toFloat(); val H = height.toFloat()
        paint.shader = LinearGradient(0f, 0f, W, H, intArrayOf(0xFF1D2A52.toInt(), 0xFF3B4C80.toInt(), 0xFF2A1F4A.toInt()), null, Shader.TileMode.CLAMP)
        c.drawRect(0f, 0f, W, H, paint); paint.shader = null
        paint.color = 0x22FFFFFF
        for (k in 0 until 40) c.drawCircle((k * 97 % 100) / 100f * W, (k * 53 % 100) / 100f * H, 1.5f + (k % 3), paint)
        for (b in btns) {
            val down = held and b.bit != 0
            if (b.bit == 0x01 || b.bit == 0x02) {
                // a taiko: red-lacquered rim, studs, skin
                paint.color = 0xFF7A1A12.toInt(); c.drawCircle(b.cx, b.cy, b.r * 1.12f, paint)
                paint.color = 0xFFC9A64A.toInt()
                for (k in 0 until 16) { val a = k * PI / 8; c.drawCircle(b.cx + (cos(a) * b.r * 1.06f).toFloat(), b.cy + (sin(a) * b.r * 1.06f).toFloat(), b.r * 0.025f, paint) }
                paint.shader = RadialGradient(b.cx, b.cy - b.r * 0.2f, b.r, if (down) 0xFFD8C9A6.toInt() else 0xFFF3E8CE.toInt(), 0xFFC9B48A.toInt(), Shader.TileMode.CLAMP)
                c.drawCircle(b.cx, b.cy, b.r * (if (down) 0.96f else 1f), paint); paint.shader = null
                // the three-comma crest, plain
                paint.color = 0x55A02018
                for (k in 0 until 3) { val a = k * 2 * PI / 3; c.drawCircle(b.cx + (cos(a) * b.r * 0.25f).toFloat(), b.cy + (sin(a) * b.r * 0.25f).toFloat(), b.r * 0.16f, paint) }
            } else {
                // a drumstick's flame tip (the rim hits)
                val dir = if (b.cx < W / 2) 1f else -1f
                stroke.color = 0xFF8C1C12.toInt(); stroke.strokeWidth = b.r * 0.35f; stroke.strokeCap = Paint.Cap.ROUND
                c.drawLine(b.cx, b.cy, b.cx + dir * b.r * 2.6f, b.cy - b.r * 1.6f, stroke)
                stroke.strokeCap = Paint.Cap.BUTT
                paint.shader = RadialGradient(b.cx, b.cy, b.r, if (down) 0xFFFFE08A.toInt() else 0xFFFF9A3C.toInt(), 0xFFD8381E.toInt(), Shader.TileMode.CLAMP)
                c.drawCircle(b.cx, b.cy, b.r, paint); paint.shader = null
            }
        }
    }

    private fun drawRainbow(c: Canvas) {
        val W = width.toFloat(); val H = height.toFloat()
        paint.shader = LinearGradient(0f, 0f, W, 0f, intArrayOf(0xFFA6E36B.toInt(), 0xFFF06EB4.toInt(), 0xFFFFF07A.toInt(), 0xFF7FD3F5.toInt()), null, Shader.TileMode.CLAMP)
        c.drawRect(0f, 0f, W, H, paint); paint.shader = null
        paint.color = 0x55FFFFFF; c.drawRect(0f, 0f, W, H, paint)
        for (k in 0 until 18) sparkle(c, (k * 61 % 100) / 100f * W, (k * 37 % 100) / 100f * H, 6f + (k % 4) * 4f, Color.WHITE)
        // two butterflies
        for ((x, y, s) in listOf(Triple(W * 0.18f, H * 0.84f, H * 0.07f), Triple(W * 0.82f, H * 0.84f, H * 0.07f))) {
            paint.color = 0xFFE83E8C.toInt()
            c.drawOval(x - s * 1.1f, y - s * 0.9f, x - s * 0.05f, y + s * 0.05f, paint)
            c.drawOval(x + s * 0.05f, y - s * 0.9f, x + s * 1.1f, y + s * 0.05f, paint)
            paint.color = 0xFFF7A8CF.toInt()
            c.drawOval(x - s * 0.8f, y, x - s * 0.05f, y + s * 0.7f, paint)
            c.drawOval(x + s * 0.05f, y, x + s * 0.8f, y + s * 0.7f, paint)
            paint.color = 0xFF7A2A5C.toInt(); c.drawRoundRect(x - s * 0.08f, y - s * 0.7f, x + s * 0.08f, y + s * 0.6f, s * 0.08f, s * 0.08f, paint)
            paint.color = 0xFF3FB07A.toInt(); c.drawCircle(x, y - s * 0.15f, s * 0.12f, paint)
        }
    }

    private fun drawHero(c: Canvas) {
        val W = width.toFloat(); val H = height.toFloat()
        paint.shader = LinearGradient(0f, 0f, 0f, H, 0xFF0E1530.toInt(), 0xFF2A3A6A.toInt(), Shader.TileMode.CLAMP)
        c.drawRect(0f, 0f, W, H, paint); paint.shader = null
        paint.color = 0x55FFFFFF
        for (k in 0 until 30) c.drawCircle((k * 89 % 100) / 100f * W, (k * 23 % 70) / 100f * H, 1.5f, paint)
        // skyline
        paint.color = 0xFF0A0F22.toInt()
        var x = 0f; var k = 0
        while (x < W) { val bw = W * (0.05f + (k * 7 % 5) * 0.012f); val bh = H * (0.12f + (k * 13 % 7) * 0.025f); c.drawRect(x, H - bh, x + bw, H, paint); x += bw + W * 0.005f; k++ }
        paint.color = 0xFFF2D46A.toInt()
        for (i in 0 until 25) c.drawRect((i * 41 % 100) / 100f * W, H * (0.88f + (i % 4) * 0.025f), (i * 41 % 100) / 100f * W + 4f, H * (0.88f + (i % 4) * 0.025f) + 5f, paint)
        // silver frame with red stripes round the camera window
        val f = RectF(cam).apply { inset(-W * 0.04f, -H * 0.04f) }
        paint.shader = LinearGradient(f.left, f.top, f.right, f.bottom, intArrayOf(0xFFE8ECF0.toInt(), 0xFF9AA4AE.toInt(), 0xFFDDE2E8.toInt()), null, Shader.TileMode.CLAMP)
        c.drawRoundRect(f, W * 0.04f, W * 0.04f, paint); paint.shader = null
        paint.color = 0xFFD0202A.toInt()
        c.drawRect(f.left, f.top + f.height() * 0.25f, f.left + W * 0.025f, f.bottom - f.height() * 0.25f, paint)
        c.drawRect(f.right - W * 0.025f, f.top + f.height() * 0.25f, f.right, f.bottom - f.height() * 0.25f, paint)
    }

    private fun drawNinja(c: Canvas) {
        val W = width.toFloat(); val H = height.toFloat()
        c.drawColor(0xFFF4F1EA.toInt())
        // orange swirl behind everything
        stroke.color = 0xFFF08A1C.toInt(); stroke.strokeWidth = H * 0.05f
        c.drawArc(-W * 0.2f, -H * 0.3f, W * 1.2f, H * 1.3f, 200f, 140f, false, stroke)
        stroke.color = 0xFF2E6FD8.toInt(); stroke.strokeWidth = H * 0.02f
        c.drawArc(-W * 0.1f, -H * 0.1f, W * 1.1f, H * 1.4f, 190f, 100f, false, stroke)
        // the blue unit as the base the keys sit on, with its orange label and lens
        val u = RectF(W * 0.04f, H * 0.68f, W * 0.96f, H * 0.98f)
        paint.color = 0x44000000; c.drawRoundRect(u.left + 6, u.top + 8, u.right + 6, u.bottom + 8, W * 0.03f, W * 0.03f, paint)
        paint.shader = LinearGradient(u.left, u.top, u.right, u.bottom, 0xFF3A62C8.toInt(), 0xFF1C3A8E.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(u, W * 0.03f, W * 0.03f, paint); paint.shader = null
        paint.color = 0xFFF08A1C.toInt(); c.drawRoundRect(u.left + W * 0.03f, u.top + u.height() * 0.3f, u.left + W * 0.14f, u.bottom - u.height() * 0.3f, W * 0.01f, W * 0.01f, paint)
        val lx = u.left + W * 0.66f; val ly = u.centerY(); val lr = u.height() * 0.22f
        paint.color = 0xFF0C1426.toInt(); c.drawRoundRect(lx - lr, ly - lr, lx + lr, ly + lr, lr * 0.3f, lr * 0.3f, paint)
        paint.color = 0xFF3A4A6A.toInt(); c.drawCircle(lx, ly, lr * 0.55f, paint)
        paint.color = 0x88FFFFFF.toInt(); c.drawCircle(lx - lr * 0.2f, ly - lr * 0.2f, lr * 0.14f, paint)
    }

    private fun drawCamera(c: Canvas, held: Int) {
        val W = width.toFloat()
        val frame = when (theme) { "onmyou" -> 0xFFB3201B.toInt(); "punch" -> 0xFF5E3B1C.toInt(); "rainbow" -> 0xFFFFFFFF.toInt(); "ninja" -> 0xFFF08A1C.toInt(); else -> 0xFF9AA4AE.toInt() }
        paint.color = 0xCC0B0B12.toInt(); c.drawRoundRect(cam, W * 0.03f, W * 0.03f, paint)
        stroke.color = frame; stroke.strokeWidth = W * 0.008f; c.drawRoundRect(cam, W * 0.03f, W * 0.03f, stroke)
        // faint grid, the camera's 32 x 31 pixels in eights
        stroke.color = 0x22FFFFFF; stroke.strokeWidth = 1f
        for (k in 1 until 8) {
            c.drawLine(cam.left + cam.width() * k / 8, cam.top, cam.left + cam.width() * k / 8, cam.bottom, stroke)
            c.drawLine(cam.left, cam.top + cam.height() * k / 8, cam.right, cam.top + cam.height() * k / 8, stroke)
        }
        text.textSize = cam.height() * 0.07f; text.color = 0x88FFFFFF.toInt()
        c.drawText(when (theme) { "onmyou" -> "swing down to strike  ·  hold ⇧ for the menu"; "punch", "ninja" -> "drag the gloves  ·  tap to punch"; else -> "drag your hands" }, cam.centerX(), cam.bottom - cam.height() * 0.05f, text)
        for (g in 0 until nStickers) {
            val p = stickers[g]
            val x = cam.centerX() + p.x * cam.width() / 2; val y = cam.centerY() + p.y * cam.height() / 2
            val hit = held and (PUNCH_L shl g) != 0
            val r = cam.height() * (if (hit) 0.13f else 0.1f)
            paint.shader = RadialGradient(x, y, r * 1.8f, 0xAAFFFFFF.toInt(), 0x00FFFFFF, Shader.TileMode.CLAMP)
            c.drawCircle(x, y, r * 1.8f, paint); paint.shader = null
            paint.color = if (g == 0) 0xFFE8E8F0.toInt() else 0xFFD8DCE6.toInt()
            c.drawRoundRect(x - r, y - r, x + r, y + r, r * 0.25f, r * 0.25f, paint)
            paint.color = 0x66FFFFFF; c.drawRect(x - r * 0.7f, y - r * 0.7f, x, y, paint)
            text.textSize = r * 0.8f; text.color = 0xFF30384A.toInt()
            if (nStickers > 1) c.drawText(if (g == 0) "L" else "R", x, y + r * 0.3f, text)
        }
    }

    private fun drawButton(c: Canvas, b: Btn, down: Boolean) {
        paint.color = 0x44000000; c.drawCircle(b.cx + b.r * 0.06f, b.cy + b.r * 0.1f, b.r, paint)
        val base = b.fill
        paint.shader = RadialGradient(b.cx - b.r * 0.3f, b.cy - b.r * 0.35f, b.r * 1.4f,
            if (down) darker(base) else lighter(base), if (down) darker(darker(base)) else base, Shader.TileMode.CLAMP)
        c.drawCircle(b.cx, b.cy + (if (down) b.r * 0.04f else 0f), b.r, paint); paint.shader = null
        if (b.label.isNotEmpty()) {
            text.textSize = b.r * 1.0f; text.color = b.ink
            c.drawText(b.label, b.cx, b.cy + text.textSize * 0.36f, text)
        }
    }

    private fun lighter(c: Int) = Color.rgb(min(255, Color.red(c) + 40), min(255, Color.green(c) + 40), min(255, Color.blue(c) + 40))
    private fun darker(c: Int) = Color.rgb(Color.red(c) * 3 / 4, Color.green(c) * 3 / 4, Color.blue(c) * 3 / 4)

    // ---- touch ----

    private fun btnAt(x: Float, y: Float): Int {
        for (b in btns) if (hypot(x - b.cx, y - b.cy) < b.r * 1.25f) return b.bit
        return 0
    }

    private fun stickerAt(x: Float, y: Float): Int {
        if (!hasCamera(theme) || !RectF(cam).apply { inset(-30f, -30f) }.contains(x, y)) return -1
        var best = -1; var bd = Float.MAX_VALUE
        for (g in 0 until nStickers) {
            val sx = cam.centerX() + stickers[g].x * cam.width() / 2; val sy = cam.centerY() + stickers[g].y * cam.height() / 2
            val d = hypot(x - sx, y - sy)
            if (d < bd) { bd = d; best = g }
        }
        return best
    }

    private fun moveSticker(g: Int, x: Float, y: Float) {
        stickers[g].x = ((x - cam.centerX()) / (cam.width() / 2)).coerceIn(-1f, 1f)
        stickers[g].y = ((y - cam.centerY()) / (cam.height() / 2)).coerceIn(-1f, 1f)
        onAnalog?.invoke(g, stickers[g].x, stickers[g].y)
    }

    private fun punch(g: Int) {
        if (theme == "onmyou") return
        punchMask = punchMask or (PUNCH_L shl g)
        punchEnd[g] = System.currentTimeMillis() + 180
        performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
        send()
        postInvalidate()
    }

    private fun send() {
        var m = 0
        for (v in pointerBtn.values) m = m or v
        if (m and touchMask.inv() != 0) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
        touchMask = m
        onInput?.invoke(m or punchMask)
        invalidate()
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        val i = e.actionIndex
        val id = e.getPointerId(i)
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val x = e.getX(i); val y = e.getY(i)
                val b = btnAt(x, y)
                if (b != 0) { pointerBtn[id] = b; send(); return true }
                val g = stickerAt(x, y)
                if (g >= 0) { pointerSticker[id] = g; downAt[id] = PointF(x, y); downTime[id] = e.eventTime; moveSticker(g, x, y); invalidate() }
            }
            MotionEvent.ACTION_MOVE -> {
                for (j in 0 until e.pointerCount) {
                    val pid = e.getPointerId(j)
                    val x = e.getX(j); val y = e.getY(j)
                    pointerSticker[pid]?.let { g ->
                        val d0 = downAt[pid]
                        // a quick flick upwards is a punch, wherever the sticker is
                        if (d0 != null && d0.y - y > cam.height() * 0.25f && e.eventTime - (downTime[pid] ?: 0L) < 250) { punch(g); downAt[pid] = PointF(x, y); downTime[pid] = e.eventTime }
                        moveSticker(g, x, y)
                    }
                    if (pointerBtn.containsKey(pid)) pointerBtn[pid] = btnAt(x, y)
                }
                send()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                pointerSticker.remove(id)?.let { g ->
                    val d0 = downAt[id]
                    val t0 = downTime[id] ?: 0L
                    if (d0 != null && hypot(e.getX(i) - d0.x, e.getY(i) - d0.y) < 20f && e.eventTime - t0 < 300) punch(g)
                }
                downAt.remove(id); downTime.remove(id)
                pointerBtn.remove(id)
                if (e.actionMasked == MotionEvent.ACTION_CANCEL) { pointerBtn.clear(); pointerSticker.clear() }
                send()
            }
        }
        return true
    }
}
