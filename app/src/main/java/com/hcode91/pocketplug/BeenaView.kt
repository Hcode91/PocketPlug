package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import kotlin.math.min

/**
 * The 2005 picture-book console under the TV, after a photo: the teal lid holds the storyware
 * book (tap its page to point the pen at the book; the tabs beside it turn the pages, "Shut"
 * closes it), the white base has the drawing tablet in the middle (the pen points at the TV
 * picture) and a pad at each side: a big red button and four coloured keys (white up, orange
 * down, blue left, green right). The icon plate on the lid is the MENU. No maker's name or logo.
 *
 * Input bits as beena.h: 0-4 left pad up/down/left/right/red, 5-9 right pad, 10 pen pressed.
 * onPen(stick, x, y): stick 2 = tablet, 3 = book, x/y 0..1. onPage(n): 0 = closed, 1-6.
 */
class BeenaView(ctx: Context) : View(ctx) {
    var onInput: ((Int) -> Unit)? = null
    var onPen: ((Int, Float, Float) -> Unit)? = null
    var onPage: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }
    var page = 0
        private set

    companion object {
        const val PEN = 1 shl 10
        fun keyBit(code: Int): Int = when (code) {
            KeyEvent.KEYCODE_DPAD_UP, KeyEvent.KEYCODE_W -> 1 shl 0
            KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.KEYCODE_S -> 1 shl 1
            KeyEvent.KEYCODE_DPAD_LEFT, KeyEvent.KEYCODE_A -> 1 shl 2
            KeyEvent.KEYCODE_DPAD_RIGHT, KeyEvent.KEYCODE_D -> 1 shl 3
            KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_SPACE, KeyEvent.KEYCODE_Z -> 1 shl 4
            KeyEvent.KEYCODE_I -> 1 shl 5
            KeyEvent.KEYCODE_K -> 1 shl 6
            KeyEvent.KEYCODE_J -> 1 shl 7
            KeyEvent.KEYCODE_L -> 1 shl 8
            KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_X -> 1 shl 9
            else -> 0
        }
    }

    // 1000 x 900 design space: lid 0-380, base 400-890
    private class Key(val bit: Int, val x: Float, val y: Float, val r: Float, val color: Int)
    private val white = 0xFFF4F4F0.toInt(); private val orange = 0xFFF26A1E.toInt()
    private val blue = 0xFF2A6FD6.toInt(); private val green = 0xFF6CC83A.toInt(); private val red = 0xFFE8303A.toInt()
    private fun pad(base: Int, x: Float) = listOf(
        Key(1 shl (base + 4), x, 520f, 62f, red),
        Key(1 shl (base + 0), x, 640f, 30f, white), Key(1 shl (base + 1), x, 760f, 30f, orange),
        Key(1 shl (base + 2), x - 60f, 700f, 30f, blue), Key(1 shl (base + 3), x + 60f, 700f, 30f, green),
    )
    private val keys = pad(0, 115f) + pad(5, 885f)
    private val tablet = RectF(235f, 440f, 765f, 801f)
    private val book = RectF(380f, 40f, 620f, 350f)
    private val menu = RectF(60f, 60f, 300f, 120f)
    private val tabs = (0..6).map { RectF(650f, 40f + it * 45f, 760f, 78f + it * 45f) }

    private var sc = 1f; private var ox = 0f; private var oy = 0f
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.DEFAULT_BOLD }
    private val keyPointers = HashMap<Int, Int>()
    private var penPointer = -1; private var penStick = 2
    private var penPos = PointF(-1f, -1f)
    private var mask = 0
    private var tabletShader: Shader? = null

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        sc = min(w / 1000f, h / 900f)
        ox = (w - 1000f * sc) / 2; oy = (h - 900f * sc) / 2
        tabletShader = LinearGradient(0f, tablet.top, 0f, tablet.bottom, 0xFF5CC8C8.toInt(), 0xFFE6E86A.toInt(), Shader.TileMode.CLAMP)
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0814.toInt())
        c.save(); c.translate(ox, oy); c.scale(sc, sc)
        // lid: white rim, teal face
        p.color = 0xFFE8ECEC.toInt(); c.drawRoundRect(10f, 0f, 990f, 390f, 50f, 50f, p)
        p.color = 0xFF1FB3C4.toInt(); c.drawRoundRect(24f, 12f, 976f, 380f, 42f, 42f, p)
        // MENU: the icon plate
        p.color = 0xFFF2F6F6.toInt(); c.drawRoundRect(menu, 30f, 30f, p)
        t.color = 0xFF1A8A98.toInt(); t.textSize = 30f; c.drawText("MENU", menu.centerX(), menu.centerY() + 11f, t)
        // volume plate and speaker dots
        p.color = 0xFFF2F6F6.toInt(); c.drawRoundRect(70f, 290f, 150f, 340f, 16f, 16f, p)
        t.textSize = 26f; c.drawText("+  −", 110f, 325f, t)
        p.color = 0xFF178E9C.toInt()
        for (i in 0 until 4) for (j in 0 until 6) c.drawCircle(200f + i * 40f, 170f + j * 30f, 3f, p)
        // book holder and the open page
        p.color = 0xFFE6EAEA.toInt(); c.drawRoundRect(book.left - 14f, book.top - 14f, book.right + 14f, book.bottom + 14f, 12f, 12f, p)
        if (page == 0) {
            p.color = 0xFF2E7FD0.toInt(); c.drawRect(book, p)
            t.color = 0xFFFFFFFF.toInt(); t.textSize = 30f; c.drawText("Book shut", book.centerX(), book.centerY() + 10f, t)
        } else {
            p.color = 0xFFFFF8E8.toInt(); c.drawRect(book, p)
            p.color = 0xFFE0D6BE.toInt(); c.drawRect(book.left, book.top, book.left + 8f, book.bottom, p)
            t.color = 0xFF8A7A5A.toInt(); t.textSize = 26f
            c.drawText("Page $page", book.centerX(), book.top + 40f, t)
            t.textSize = 20f; c.drawText("tap to point", book.centerX(), book.bottom - 20f, t)
        }
        if (penStick == 3 && penPos.x >= 0) {
            p.color = 0xAA1FB3C4.toInt()
            c.drawCircle(book.left + penPos.x * book.width(), book.top + penPos.y * book.height(), 10f, p)
        }
        // page tabs
        for ((i, r) in tabs.withIndex()) {
            p.color = if (i == page) 0xFFFFD84A.toInt() else 0xFFF2F6F6.toInt()
            c.drawRoundRect(r, 12f, 12f, p)
            t.color = 0xFF1A6A78.toInt(); t.textSize = 24f
            c.drawText(if (i == 0) "Shut" else "$i", r.centerX(), r.centerY() + 8f, t)
        }
        // pen holder on the right of the lid
        p.color = 0xFFF2F6F6.toInt(); c.drawRoundRect(800f, 50f, 850f, 340f, 24f, 24f, p)
        p.color = 0xFF1FB3C4.toInt(); c.drawRoundRect(812f, 70f, 838f, 320f, 13f, 13f, p)
        // base
        p.color = 0xFF1FB3C4.toInt(); c.drawRoundRect(10f, 410f, 990f, 895f, 60f, 60f, p)
        p.color = 0xFFF6F6F2.toInt(); c.drawRoundRect(10f, 400f, 990f, 880f, 60f, 60f, p)
        p.color = 0xFFE2E4E0.toInt(); c.drawRoundRect(tablet.left - 12f, tablet.top - 12f, tablet.right + 12f, tablet.bottom + 12f, 10f, 10f, p)
        p.shader = tabletShader; c.drawRect(tablet, p); p.shader = null
        p.color = 0x66FFFFFF
        for (i in 1 until 8) for (j in 1 until 6) c.drawCircle(tablet.left + i * tablet.width() / 8, tablet.top + j * tablet.height() / 6, 2.5f, p)
        if (penStick == 2 && penPos.x >= 0) {
            p.color = 0xAA0B5866.toInt()
            c.drawCircle(tablet.left + penPos.x * tablet.width(), tablet.top + penPos.y * tablet.height(), 10f, p)
        }
        val m = mask or externalMask
        for (k in keys) {
            val down = m and k.bit != 0
            val dy = if (down) 3f else 0f
            p.color = 0xFFD2D4D0.toInt(); c.drawCircle(k.x, k.y + 5f, k.r + 5f, p)
            p.color = if (down) darker(k.color) else k.color
            if (k.r > 40f) {
                c.drawCircle(k.x, k.y + dy, k.r, p)
                p.color = darker(k.color); c.drawCircle(k.x, k.y + dy, k.r * 0.62f, p)
                p.color = if (down) darker(k.color) else k.color; c.drawCircle(k.x, k.y + dy - 3f, k.r * 0.55f, p)
            } else c.drawRoundRect(k.x - k.r, k.y - k.r * 0.8f + dy, k.x + k.r, k.y + k.r * 0.8f + dy, 12f, 12f, p)
        }
        c.restore()
    }

    private fun darker(c: Int) = Color.rgb(Color.red(c) * 3 / 4, Color.green(c) * 3 / 4, Color.blue(c) * 3 / 4)

    private fun keyAt(dx: Float, dy: Float): Int {
        for (k in keys) { val r = k.r + 16f; if ((dx - k.x) * (dx - k.x) + (dy - k.y) * (dy - k.y) < r * r) return k.bit }
        return 0
    }

    private fun send() {
        mask = keyPointers.values.fold(0) { a, b -> a or b } or (if (penPointer >= 0) PEN else 0)
        onInput?.invoke(mask)
        invalidate()
    }

    private fun movePen(dx: Float, dy: Float) {
        val r = if (penStick == 2) tablet else book
        val x = ((dx - r.left) / r.width()).coerceIn(0f, 1f); val y = ((dy - r.top) / r.height()).coerceIn(0f, 1f)
        penPos.set(x, y)
        onPen?.invoke(penStick, x, y)
    }

    fun selectPage(n: Int) { page = n.coerceIn(0, 6); onPage?.invoke(page); invalidate() }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                val dx = (e.getX(i) - ox) / sc; val dy = (e.getY(i) - oy) / sc
                val id = e.getPointerId(i)
                if (menu.contains(dx, dy)) { onMenu?.invoke(); return true }
                for ((n, r) in tabs.withIndex()) if (r.contains(dx, dy)) {
                    performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP); selectPage(n); return true
                }
                val inTablet = RectF(tablet).apply { inset(-10f, -10f) }.contains(dx, dy)
                val inBook = page != 0 && book.contains(dx, dy)
                if (penPointer < 0 && (inTablet || inBook)) {
                    penPointer = id; penStick = if (inTablet) 2 else 3
                    movePen(dx, dy); send(); return true
                }
                val b = keyAt(dx, dy)
                if (b != 0) { performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP); keyPointers[id] = b; send() }
            }
            MotionEvent.ACTION_MOVE -> {
                val i = e.findPointerIndex(penPointer)
                if (penPointer >= 0 && i >= 0) { movePen((e.getX(i) - ox) / sc, (e.getY(i) - oy) / sc); invalidate() }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val id = e.getPointerId(e.actionIndex)
                if (id == penPointer) { penPointer = -1; send() }
                else if (keyPointers.remove(id) != null) send()
            }
            MotionEvent.ACTION_CANCEL -> { keyPointers.clear(); penPointer = -1; send() }
        }
        return true
    }
}
