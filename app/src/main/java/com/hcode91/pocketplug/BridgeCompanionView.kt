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
 * The 1985 bridge computer's console under the TV, after a photo: dark grey top
 * over a cream base, the inset panel with red START / BACK, the grey bidding keys
 * in two staggered rows (PASS NT DBL RDBL, then the suits; hearts and diamonds
 * double as up / down) and the yellow PLAY YES / PLAY NO keys. The badge spot
 * on the left is the MENU plate. No maker's name or logo.
 *
 * Input bits as bbcbc.h: 0 Pass, 1 Spades, 2 Clubs, 3 Rdbl, 4 NT, 5 Hearts/Up,
 * 6 Play Yes, 7 Back, 8 Dbl, 9 Diamonds/Down, 10 Start, 11 Play No.
 */
class BridgeCompanionView(ctx: Context) : View(ctx) {
    var onInput: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    companion object {
        const val PASS = 1 shl 0; const val SPADES = 1 shl 1; const val CLUBS = 1 shl 2; const val RDBL = 1 shl 3
        const val NT = 1 shl 4; const val HEARTS = 1 shl 5; const val YES = 1 shl 6; const val BACK = 1 shl 7
        const val DBL = 1 shl 8; const val DIAMONDS = 1 shl 9; const val START = 1 shl 10; const val NO = 1 shl 11

        /** a real keyboard / gamepad */
        fun keyBit(code: Int): Int = when (code) {
            KeyEvent.KEYCODE_A -> PASS; KeyEvent.KEYCODE_Z -> SPADES; KeyEvent.KEYCODE_V -> CLUBS; KeyEvent.KEYCODE_F -> RDBL
            KeyEvent.KEYCODE_S -> NT; KeyEvent.KEYCODE_X, KeyEvent.KEYCODE_DPAD_UP -> HEARTS
            KeyEvent.KEYCODE_C, KeyEvent.KEYCODE_DPAD_DOWN -> DIAMONDS; KeyEvent.KEYCODE_D -> DBL
            KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_BUTTON_A, KeyEvent.KEYCODE_Y -> YES
            KeyEvent.KEYCODE_BUTTON_B, KeyEvent.KEYCODE_N -> NO
            KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_SPACE -> START
            KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_DEL -> BACK
            else -> 0
        }
    }

    // key positions in a 1000 x 520 design space, after the photo: START / BACK (red) on the
    // left of the inset panel, the grey bidding keys in two staggered rows, PLAY YES / NO yellow
    private class Key(val bit: Int, val x: Float, val y: Float, val color: Int, val above: String, val below: String = "")

    private val grey = 0xFFC2B9A6.toInt(); private val yellow = 0xFFF2A21E.toInt(); private val red = 0xFFD8302A.toInt()
    private val keys = listOf(
        Key(START, 250f, 150f, red, "Start"), Key(BACK, 290f, 270f, red, "Back"),
        Key(PASS, 440f, 125f, grey, "Pass"), Key(NT, 560f, 105f, grey, "NT"),
        Key(DBL, 680f, 85f, grey, "Dbl"), Key(RDBL, 800f, 65f, grey, "Rdbl"),
        Key(SPADES, 520f, 225f, grey, "♠"), Key(HEARTS, 640f, 205f, grey, "♥", "▲"),
        Key(DIAMONDS, 760f, 185f, grey, "♦", "▼"), Key(CLUBS, 880f, 165f, grey, "♣"),
        Key(YES, 640f, 330f, yellow, "Play", "Yes"), Key(NO, 900f, 270f, yellow, "Play", "No"),
    )
    private val KW = 96f; private val KH = 58f
    private var sc = 1f; private var ox = 0f; private var oy = 0f
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.DEFAULT_BOLD }
    private val pointers = HashMap<Int, Int>()
    private var mask = 0
    private val menu = RectF(40f, 40f, 200f, 110f)

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        sc = min(w / 1000f, h / 520f)
        ox = (w - 1000f * sc) / 2; oy = (h - 520f * sc) / 2
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0814.toInt())
        c.save(); c.translate(ox, oy); c.scale(sc, sc)
        // dark grey top over the cream base, the front lip cut away on the left
        p.color = 0xFFE4D8BC.toInt(); c.drawRoundRect(10f, 30f, 990f, 510f, 30f, 30f, p)
        p.color = 0xFF3A3A36.toInt(); c.drawRoundRect(10f, 10f, 990f, 410f, 30f, 30f, p)
        p.color = 0xFFD9CCAE.toInt(); c.drawRect(10f, 410f, 990f, 418f, p)
        // the inset key panel
        p.color = 0xFF2C2C2A.toInt(); c.drawRoundRect(195f, 30f, 970f, 395f, 14f, 14f, p)
        p.color = 0xFF34342F.toInt(); c.drawRoundRect(200f, 35f, 965f, 390f, 12f, 12f, p)
        // MENU plate where the badge sits on the left
        p.color = 0xFF4A4A44.toInt(); c.drawRoundRect(menu, 8f, 8f, p)
        t.color = 0xFFE8E4D8.toInt(); t.textSize = 30f; c.drawText("MENU", menu.centerX(), menu.centerY() + 11f, t)
        // power light
        p.color = 0xFF6A1410.toInt(); c.drawCircle(540f, 375f, 7f, p)
        val m = mask or externalMask
        for (k in keys) {
            val down = m and k.bit != 0
            val dy = if (down) 4f else 0f
            p.color = 0xFF1C1C1A.toInt(); c.drawRoundRect(k.x - KW / 2 - 6f, k.y - KH / 2 - 6f, k.x + KW / 2 + 6f, k.y + KH / 2 + 8f, 26f, 26f, p)
            p.color = if (down) blend(k.color) else k.color
            c.drawRoundRect(k.x - KW / 2, k.y - KH / 2 + dy, k.x + KW / 2, k.y + KH / 2 + dy, 24f, 24f, p)
            t.color = 0xFFF0ECE2.toInt(); t.textSize = if (k.above.length == 1) 30f else 24f
            c.drawText(k.above, k.x - 10f, k.y - KH / 2 - 12f, t)
            if (k.below.isNotEmpty()) { t.textSize = if (k.below.length == 1) 22f else 24f; c.drawText(k.below, k.x + 14f, k.y + KH / 2 + 32f, t) }
        }
        c.restore()
    }

    private fun blend(c: Int) = Color.rgb(Color.red(c) * 3 / 4, Color.green(c) * 3 / 4, Color.blue(c) * 3 / 4)

    private fun keyAt(x: Float, y: Float): Int {
        val dx = (x - ox) / sc; val dy = (y - oy) / sc
        for (k in keys) if (dx in k.x - KW / 2 - 14f..k.x + KW / 2 + 14f && dy in k.y - KH / 2 - 14f..k.y + KH / 2 + 14f) return k.bit
        return 0
    }

    private fun send() {
        mask = pointers.values.fold(0) { a, b -> a or b }
        onInput?.invoke(mask)
        invalidate()
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                val x = e.getX(i); val y = e.getY(i)
                if (menu.contains((x - ox) / sc, (y - oy) / sc)) { onMenu?.invoke(); return true }
                val b = keyAt(x, y)
                if (b != 0) { performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP); pointers[e.getPointerId(i)] = b; send() }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> { if (pointers.remove(e.getPointerId(e.actionIndex)) != null) send() }
            MotionEvent.ACTION_CANCEL -> { pointers.clear(); send() }
        }
        return true
    }
}
