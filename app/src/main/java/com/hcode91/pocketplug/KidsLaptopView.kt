package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.hypot
import kotlin.math.min

/**
 * The children's learning laptops (GPL32612): the wide screen in the lid, a kana keyboard in
 * the base and a one-button mouse on a lead. No characters, names or logos.
 *
 * The screen is the mouse pad: drag to move the pointer, tap to click. The mouse button can be
 * held to drag. Keys report matrix codes (row * 32 + column, see gpl32.h): the row of menu keys
 * above the kana, the kana in the toy's order (right to left from あ), delete, the kana/romaji
 * key and the four cursor keys.
 *
 * style picks the case colours: "orange", "yellow", "blue", "pink" or "white".
 */
class KidsLaptopView(ctx: Context, caseStyle: String) : View(ctx) {

    var onKey: ((Int, Boolean) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var onMouseMove: ((Float, Float) -> Unit)? = null
    /** bit 0: the mouse button */
    var onMouseButtons: ((Int) -> Unit)? = null

    companion object {
        const val W = 480
        const val H = 272
    }

    private val caseTop: Int
    private val caseBottom: Int
    private val bezel: Int
    private val keyFace = 0xFFFDFBF4.toInt()
    private val keySide = 0xFFC9C2B2.toInt()
    private val menuFaces = intArrayOf(0xFFE8443A.toInt(), 0xFFF08A2C.toInt(), 0xFFF5C02E.toInt(), 0xFF8BC34A.toInt(),
        0xFF3FB5A5.toInt(), 0xFF42A5F5.toInt(), 0xFF7E7FD8.toInt(), 0xFFB36AD0.toInt(), 0xFFEC6FA5.toInt())
    init {
        fun c(v: Long) = v.toInt()
        when (caseStyle) {
            "yellow" -> { caseTop = c(0xFFFFD84A); caseBottom = c(0xFFF2B92C); bezel = c(0xFFE84B3C) }
            "blue" -> { caseTop = c(0xFF7CC4F2); caseBottom = c(0xFF4C9BD9); bezel = c(0xFFFFFFFF) }
            "pink" -> { caseTop = c(0xFFF6B8D6); caseBottom = c(0xFFE58DB9); bezel = c(0xFFFFFFFF) }
            "white" -> { caseTop = c(0xFFF4F6F8); caseBottom = c(0xFFD8DEE4); bezel = c(0xFF2F8FD8) }
            else -> { caseTop = c(0xFFFFA23C); caseBottom = c(0xFFF08020); bezel = c(0xFFFFE070) }
        }
    }

    private class Key(val label: String, val code: Int, val units: Float = 1f, val face: Int = 0, val small: Boolean = false) {
        val rect = RectF()
        var down = false
    }

    private fun k(label: String, code: Int) = Key(label, code)
    private val menuRow = listOf("ゲーム", "おえかき", "たいけん", "ちえ", "おんがく", "さんすう", "えいご", "こくご", "パソコン")
        .mapIndexed { i, s -> Key(s, 32 + 7 + i, face = menuFaces[i], small = true) }
    private val rows: List<List<Key>> = listOf(
        menuRow,
        listOf(k("わ", 78), k("ら", 77), k("や", 76), k("ま", 108), k("は", 107), k("な", 75), k("た", 74), k("さ", 73), k("か", 72), k("あ", 71)),
        listOf(k("を", 110), k("り", 109), k("ゆ", 141), k("み", 140), k("ひ", 139), k("に", 138), k("ち", 106), k("し", 105), k("き", 104), k("い", 103)),
        listOf(k("ん", 174), k("る", 142), k("よ", 173), k("む", 172), k("ふ", 171), k("ぬ", 170), k("つ", 169), k("す", 137), k("く", 136), k("う", 135)),
        listOf(k("れ", 205), k("め", 204), k("へ", 203), k("ね", 202), k("て", 201), k("せ", 200), k("け", 168), k("え", 167), Key("けす", 13, 2f, 0xFFF4A6A0.toInt(), true)),
        listOf(k("ろ", 237), k("も", 236), k("ほ", 235), k("の", 234), k("と", 233), k("そ", 232), k("こ", 231), k("お", 199), Key("かな/ローマ字", 14, 2f, 0xFFB8DDF5.toInt(), true)),
    )
    private val arrows = listOf(Key("▲", 10), Key("◀", 9), Key("▶", 8), Key("▼", 7))
    private val keys = rows.flatten() + arrows

    private val frame = Bitmap.createBitmap(W, H, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(W * H)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.DEFAULT_BOLD }
    private val pixelPaint = Paint(Paint.FILTER_BITMAP_FLAG)

    private val lid = RectF()
    private val screen = RectF()
    private val base = RectF()
    private val pad = RectF()          // the cursor keys
    private val mouse = RectF()
    private val mouseButton = RectF()
    private val menuPlate = RectF()
    private var u = 1f

    private var padId = -1
    private var padX = 0f
    private var padY = 0f
    private var padMoved = false
    private var padDownAt = 0L
    private var buttons = 0
    private var buttonId = -1
    private val pointers = HashMap<Int, Key>()

    fun updateFrame(px: IntArray) {
        if (px.size < W * H) return
        System.arraycopy(px, 0, framePx, 0, W * H)
        postInvalidate()
    }

    override fun onSizeChanged(vw: Int, vh: Int, ow: Int, oh: Int) {
        val wf = vw.toFloat()
        val lw = wf * 0.96f
        val sw = lw * 0.88f
        val sh = sw * H / W
        lid.set((wf - lw) / 2, vh * 0.01f, (wf + lw) / 2, vh * 0.01f + sh + lw * 0.1f)
        screen.set(wf / 2 - sw / 2, lid.top + lw * 0.05f, wf / 2 + sw / 2, lid.top + lw * 0.05f + sh)
        menuPlate.set(lid.centerX() - lw * 0.08f, screen.bottom + lw * 0.012f, lid.centerX() + lw * 0.08f, lid.bottom - lw * 0.012f)
        // the keyboard: 10 kana columns, the menu row on top
        u = wf * 0.92f / 10f
        val kh = u * 1.02f
        val top = lid.bottom + wf * 0.02f
        base.set(wf * 0.02f, top, wf * 0.98f, top + kh * rows.size + u * 0.5f + u * 2.3f)
        var y = base.top + u * 0.25f
        for ((ri, r) in rows.withIndex()) {
            val units = r.sumOf { it.units.toDouble() }.toFloat()
            val ku = if (ri == 0) wf * 0.92f / units else u
            var x = (wf - ku * units) / 2
            val rh = if (ri == 0) kh * 0.9f else kh
            for (key in r) {
                key.rect.set(x + ku * 0.05f, y + rh * 0.06f, x + key.units * ku - ku * 0.05f, y + rh * 0.94f)
                x += key.units * ku
            }
            y += rh + if (ri == 0) u * 0.12f else 0f
        }
        // the cursor keys, a cross on the left of the front edge
        val cy = y + u * 1.05f
        val cx = base.left + u * 1.9f
        pad.set(cx - u * 1.5f, cy - u * 1.05f, cx + u * 1.5f, cy + u * 1.05f)
        val a = u * 0.68f
        arrows[0].rect.set(cx - a / 2, cy - a * 1.5f, cx + a / 2, cy - a * 0.5f)
        arrows[1].rect.set(cx - a * 1.55f, cy - a / 2, cx - a * 0.55f, cy + a / 2)
        arrows[2].rect.set(cx + a * 0.55f, cy - a / 2, cx + a * 1.55f, cy + a / 2)
        arrows[3].rect.set(cx - a / 2, cy + a * 0.5f, cx + a / 2, cy + a * 1.5f)
        // the mouse beside the base, front right
        val below = vh - base.bottom
        val mh = min(below * 0.8f, u * 3.6f).coerceAtLeast(u * 2.2f)
        val mw = mh * 0.7f
        val my = if (below > mh * 1.05f) base.bottom + (below - mh) / 2 else base.bottom - mh - u * 0.1f
        mouse.set(base.right - mw - u * 0.4f, my, base.right - u * 0.4f, my + mh)
        mouseButton.set(mouse.left + mw * 0.1f, mouse.top + mh * 0.06f, mouse.right - mw * 0.1f, mouse.top + mh * 0.42f)
    }

    override fun onDraw(cv: Canvas) {
        cv.drawColor(0xFF2B2A33.toInt())
        // lid
        paint.shader = LinearGradient(0f, lid.top, 0f, lid.bottom, caseTop, caseBottom, Shader.TileMode.CLAMP)
        cv.drawRoundRect(lid, u * 0.5f, u * 0.5f, paint)
        paint.shader = null
        paint.color = bezel
        val b = u * 0.18f
        cv.drawRoundRect(screen.left - b, screen.top - b, screen.right + b, screen.bottom + b, u * 0.25f, u * 0.25f, paint)
        paint.color = 0xFF000000.toInt()
        cv.drawRect(screen, paint)
        frame.setPixels(framePx, 0, W, 0, 0, W, H)
        cv.drawBitmap(frame, null, screen, pixelPaint)
        // the emulator menu: a small plate under the screen
        paint.color = 0x33000000
        cv.drawRoundRect(menuPlate, menuPlate.height() / 2, menuPlate.height() / 2, paint)
        text.color = 0xAAFFFFFF.toInt(); text.textSize = menuPlate.height() * 0.55f
        cv.drawText("MENU", menuPlate.centerX(), menuPlate.centerY() + text.textSize * 0.36f, text)
        // hinge and base
        paint.color = caseBottom
        cv.drawRect(lid.left + u, lid.bottom - u * 0.1f, lid.right - u, base.top + u * 0.1f, paint)
        paint.shader = LinearGradient(0f, base.top, 0f, base.bottom, caseTop, caseBottom, Shader.TileMode.CLAMP)
        cv.drawRoundRect(base, u * 0.45f, u * 0.45f, paint)
        paint.shader = null
        paint.color = 0x22000000
        cv.drawRoundRect(pad, pad.height() / 2, pad.height() / 2, paint)
        for (key in keys) drawKey(cv, key)
        text.color = 0x99000000.toInt(); text.textSize = u * 0.3f
        val hintX = (pad.right + mouse.left) / 2
        cv.drawText("drag on the screen to move", hintX, pad.centerY() - text.textSize * 0.2f, text)
        cv.drawText("tap it to click", hintX, pad.centerY() + text.textSize * 1.2f, text)
        drawMouse(cv)
    }

    private fun drawKey(cv: Canvas, key: Key) {
        val r = key.rect
        val rad = min(r.width(), r.height()) * 0.22f
        val sink = if (key.down) r.height() * 0.06f else 0f
        paint.color = keySide
        cv.drawRoundRect(r.left, r.top + sink, r.right, r.bottom, rad, rad, paint)
        paint.color = if (key.face != 0) key.face else keyFace
        if (key.down) paint.color = blend(paint.color, 0xFF000000.toInt(), 0.18f)
        cv.drawRoundRect(r.left + r.width() * 0.04f, r.top + sink, r.right - r.width() * 0.04f, r.bottom - r.height() * 0.12f + sink * 0.5f, rad, rad, paint)
        text.color = if (key.face != 0 && key.small && key.code in 39..47) 0xFFFFFFFF.toInt() else 0xFF33302A.toInt()
        text.textSize = if (key.small) min(r.height() * 0.3f, r.width() * 0.9f / key.label.length * 1.05f) else r.height() * 0.5f
        val cy = (r.top + r.bottom - r.height() * 0.12f) / 2 + sink + text.textSize * 0.36f
        cv.drawText(key.label, r.centerX(), cy, text)
    }

    private fun drawMouse(cv: Canvas) {
        val m = mouse
        val w = m.width()
        stroke.color = 0xFF8F8A80.toInt(); stroke.strokeWidth = w * 0.035f
        cv.drawLine(m.centerX(), m.top, m.centerX() - w * 0.3f, base.bottom - u * 0.2f, stroke)
        paint.color = 0x55000000
        cv.drawRoundRect(m.left + w * 0.03f, m.top + w * 0.04f, m.right + w * 0.03f, m.bottom + w * 0.04f, w * 0.45f, w * 0.45f, paint)
        paint.shader = LinearGradient(0f, m.top, 0f, m.bottom, caseTop, caseBottom, Shader.TileMode.CLAMP)
        cv.drawRoundRect(m, w * 0.45f, w * 0.45f, paint)
        paint.shader = null
        paint.color = if (buttons and 1 != 0) 0xFFD6D0C4.toInt() else 0xFFFFFFFF.toInt()
        cv.drawRoundRect(mouseButton, w * 0.3f, w * 0.3f, paint)
        text.color = 0xFF6F6A60.toInt(); text.textSize = w * 0.16f
        cv.drawText("CLICK", mouseButton.centerX(), mouseButton.centerY() + text.textSize * 0.36f, text)
    }

    private fun blend(a: Int, b: Int, t: Float): Int {
        fun ch(s: Int) = (((a shr s) and 0xff) * (1 - t) + ((b shr s) and 0xff) * t).toInt() shl s
        return (0xFF shl 24) or ch(16) or ch(8) or ch(0)
    }

    private fun keyAt(x: Float, y: Float): Key? {
        for (key in keys) if (RectF(key.rect).apply { inset(-u * 0.04f, -u * 0.04f) }.contains(x, y)) return key
        return null
    }

    private fun setButtons(v: Int) {
        if (v != buttons) { buttons = v; onMouseButtons?.invoke(v); invalidate() }
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                val id = e.getPointerId(i)
                val x = e.getX(i); val y = e.getY(i)
                if (e.actionMasked == MotionEvent.ACTION_DOWN && RectF(menuPlate).apply { inset(-u * 0.3f, -u * 0.3f) }.contains(x, y)) {
                    onMenu?.invoke(); return true
                }
                if (RectF(mouse).apply { inset(-u * 0.2f, -u * 0.2f) }.contains(x, y)) {
                    performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                    buttonId = id
                    setButtons(buttons or 1)
                    return true
                }
                if (padId < 0 && RectF(screen).apply { inset(-u * 0.3f, -u * 0.3f) }.contains(x, y)) {
                    padId = id; padX = x; padY = y; padMoved = false; padDownAt = e.eventTime
                    return true
                }
                val key = keyAt(x, y) ?: return true
                performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                pointers[id] = key
                key.down = true
                onKey?.invoke(key.code, true)
            }
            MotionEvent.ACTION_MOVE -> {
                if (padId >= 0) {
                    val i = e.findPointerIndex(padId)
                    if (i >= 0) {
                        val dx = e.getX(i) - padX; val dy = e.getY(i) - padY
                        if (!padMoved && hypot(dx, dy) > u * 0.2f) padMoved = true
                        if (padMoved) {
                            val s = W / screen.width()      // emulated screen pixels
                            onMouseMove?.invoke(dx * s, dy * s)
                            padX = e.getX(i); padY = e.getY(i)
                        }
                    }
                }
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val id = e.getPointerId(e.actionIndex)
                if (id == buttonId) { buttonId = -1; setButtons(buttons and 1.inv()); return true }
                if (id == padId) {
                    padId = -1
                    if (!padMoved && e.eventTime - padDownAt < 400) {
                        setButtons(buttons or 1)
                        postDelayed({ setButtons(buttons and 1.inv()) }, 100)
                    }
                    return true
                }
                pointers.remove(id)?.let { it.down = false; onKey?.invoke(it.code, false) }
            }
            MotionEvent.ACTION_CANCEL -> {
                for (key in pointers.values) { key.down = false; onKey?.invoke(key.code, false) }
                pointers.clear(); padId = -1; buttonId = -1
                setButtons(0)
            }
        }
        invalidate()
        return true
    }
}
