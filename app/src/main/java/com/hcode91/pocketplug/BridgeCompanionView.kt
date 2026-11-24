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
 * The 1985 bridge computer's console under the TV (placeholder until photos):
 * the grey bidding keys (the four suits, NT, PASS, DBL, RDBL), the yellow
 * PLAY YES / PLAY NO keys and the red START and BACK keys, plus the cartridge
 * slot (tap: change cartridge) and a MENU plate. No maker's name or logo.
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

    private class Key(val label: String, val bit: Int, val color: Int, val ink: Int, val suit: Boolean = false) { val r = RectF() }

    private val grey = 0xFFB9BBBE.toInt(); private val yellow = 0xFFF2C230.toInt(); private val red = 0xFFD8382C.toInt()
    private val inkDark = 0xFF1E1E22.toInt(); private val inkRed = 0xFFC0202A.toInt()
    // two rows of four grey keys, then the yellow and red keys
    private val keys = listOf(
        Key("♠", SPADES, grey, inkDark, true), Key("♥", HEARTS, grey, inkRed, true),
        Key("♦", DIAMONDS, grey, inkRed, true), Key("♣", CLUBS, grey, inkDark, true),
        Key("NT", NT, grey, inkDark), Key("PASS", PASS, grey, inkDark),
        Key("DBL", DBL, grey, inkDark), Key("RDBL", RDBL, grey, inkDark),
        Key("PLAY\nYES", YES, yellow, inkDark), Key("PLAY\nNO", NO, yellow, inkDark),
        Key("START", START, red, 0xFFFFF4EC.toInt()), Key("BACK", BACK, red, 0xFFFFF4EC.toInt()),
    )
    private val body = RectF()
    private val slot = RectF()
    private val menu = RectF()
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val t = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.DEFAULT_BOLD }
    private val pointers = HashMap<Int, Int>()
    private var mask = 0

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        val bw = min(w * 0.96f, h * 1.6f)
        body.set((w - bw) / 2, h * 0.04f, (w + bw) / 2, h * 0.96f)
        val top = body.top + body.height() * 0.2f
        slot.set(body.left + bw * 0.25f, body.top + body.height() * 0.05f, body.right - bw * 0.25f, body.top + body.height() * 0.13f)
        menu.set(body.right - bw * 0.17f, slot.top, body.right - bw * 0.04f, slot.bottom)
        val cols = 4
        val kw = bw * 0.84f / cols
        val kh = (body.bottom - top) / 3f
        for ((i, k) in keys.withIndex()) {
            val row = i / cols; val col = i % cols
            val x = body.left + bw * 0.08f + col * kw
            val y = top + row * kh
            k.r.set(x + kw * 0.08f, y + kh * 0.1f, x + kw * 0.92f, y + kh * 0.86f)
        }
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0814.toInt())
        p.color = 0xFF2A2A2E.toInt(); c.drawRoundRect(body, body.width() * 0.04f, body.width() * 0.04f, p)
        p.color = 0xFF0E0E10.toInt(); c.drawRoundRect(slot, slot.height() * 0.3f, slot.height() * 0.3f, p)
        t.color = 0xFF8A8A90.toInt(); t.textSize = slot.height() * 0.45f
        c.drawText("CARTRIDGE", slot.centerX(), slot.centerY() + t.textSize * 0.36f, t)
        p.color = 0xFF55555C.toInt(); c.drawRoundRect(menu, 8f, 8f, p)
        t.color = 0xFFE8E8EC.toInt(); t.textSize = menu.height() * 0.45f
        c.drawText("MENU", menu.centerX(), menu.centerY() + t.textSize * 0.36f, t)
        val m = mask or externalMask
        for (k in keys) {
            val down = m and k.bit != 0
            val rr = k.r.height() * 0.18f
            p.color = 0x66000000; c.drawRoundRect(k.r.left, k.r.top + 6f, k.r.right, k.r.bottom + 6f, rr, rr, p)
            p.color = if (down) blend(k.color) else k.color
            c.drawRoundRect(k.r.left, k.r.top + (if (down) 4f else 0f), k.r.right, k.r.bottom + (if (down) 4f else 0f), rr, rr, p)
            t.color = k.ink
            val lines = k.label.split('\n')
            t.textSize = if (k.suit) k.r.height() * 0.6f else min(k.r.height() * 0.3f, k.r.width() * 1.5f / lines.maxOf { it.length })
            val cy = k.r.centerY() + (if (down) 4f else 0f)
            if (lines.size == 1) c.drawText(k.label, k.r.centerX(), cy + t.textSize * 0.36f, t)
            else { c.drawText(lines[0], k.r.centerX(), cy - t.textSize * 0.2f, t); c.drawText(lines[1], k.r.centerX(), cy + t.textSize * 0.95f, t) }
        }
    }

    private fun blend(c: Int) = Color.rgb(Color.red(c) * 3 / 4, Color.green(c) * 3 / 4, Color.blue(c) * 3 / 4)

    private fun keyAt(x: Float, y: Float): Int {
        for (k in keys) if (RectF(k.r).apply { inset(-8f, -8f) }.contains(x, y)) return k.bit
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
                if (menu.contains(x, y) || slot.contains(x, y)) { onMenu?.invoke(); return true }
                val b = keyAt(x, y)
                if (b != 0) { performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP); pointers[e.getPointerId(i)] = b; send() }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> { if (pointers.remove(e.getPointerId(e.actionIndex)) != null) send() }
            MotionEvent.ACTION_CANCEL -> { pointers.clear(); send() }
        }
        return true
    }
}
