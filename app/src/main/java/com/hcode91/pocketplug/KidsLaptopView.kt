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
 * The children's learning laptops (GPL32612), drawn after the real ones without characters,
 * names or logos: the wide screen in the lid, the base with a row of menu keys, the kana
 * keyboard in the usual JIS order, the round cursor pad and a one-button mouse on a lead.
 *
 * The screen is the mouse pad: drag to move the pointer, tap to click. The mouse button can be
 * held to drag. Keys report matrix codes (row * 32 + column, see gpl32.h), worked out on the
 * orange Anpanman set's name entry. Keys whose code isn't known yet (OK, ★, small kana, shift,
 * space, volume, rock/paper/scissors) are drawn for the look but do nothing.
 *
 * caseStyle: "orange" (white base, orange lid), "yellow", "red", "pink" or "blue".
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

    private class Look(
        val lidTop: Int, val lidBottom: Int, val bezel: Int,
        val baseTop: Int, val baseBottom: Int, val trim: Int,
        val key: Int, val keySide: Int, val menuKey: Int, val menuInk: Int, val ok: Int,
        val mouseTop: Int, val mouseBottom: Int, val mouseButton: Int,
        val menuLabels: Boolean,
    )

    private fun c(v: Long) = v.toInt()
    private val look = when (caseStyle) {
        "yellow" -> Look(c(0xFFF7F7F4), c(0xFFE3E3DF), c(0xFFFFFFFF), c(0xFFFFE04A), c(0xFFF5C92A), c(0xFFF2B81E),
            c(0xFFFFFFFF), c(0xFFD8D2C2), c(0xFFF0603C), c(0xFFFFFFFF), c(0xFFE04A3A), c(0xFFFFE04A), c(0xFFF2C21E), c(0xFFF7F7F4), true)
        "red" -> Look(c(0xFF2A2A2C), c(0xFF151516), c(0xFF151516), c(0xFFE8231F), c(0xFFC4120F), c(0xFFB00E0C),
            c(0xFFFFFFFF), c(0xFFD4D0CC), c(0xFF5DB94A), c(0xFFFFFFFF), c(0xFF2F9AE0), c(0xFFE8231F), c(0xFFB8100D), c(0xFF1C1C1E), false)
        "pink" -> Look(c(0xFFFFFFFF), c(0xFFEDE6EA), c(0xFFFFFFFF), c(0xFFF8BBD4), c(0xFFEFA0C2), c(0xFFE58CB4),
            c(0xFFFFFFFF), c(0xFFDCCFD6), c(0xFF7B78D2), c(0xFFFFFFFF), c(0xFFF06AA6), c(0xFFF8BBD4), c(0xFFEFA0C2), c(0xFFFFFFFF), false)
        "blue" -> Look(c(0xFFF2F7FC), c(0xFFD9E6F4), c(0xFFF2F7FC), c(0xFF2F7FD4), c(0xFF1D63B6), c(0xFF1A58A4),
            c(0xFFFFFFFF), c(0xFFC9D2DE), c(0xFFD8443C), c(0xFFFFFFFF), c(0xFFF5A23A), c(0xFF2F7FD4), c(0xFF1D63B6), c(0xFFFFFFFF), false)
        else -> Look(c(0xFFFF9A2E), c(0xFFF07A14), c(0xFFFF9A2E), c(0xFFFFFFFF), c(0xFFEDECE8), c(0xFFF58220),
            c(0xFFFFF4A0), c(0xFFE2D27A), c(0xFFF2F2F2), c(0xFF8A8A8A), c(0xFFFFF4A0), c(0xFFFFFFFF), c(0xFFF58220), c(0xFFFFFFFF), true)
    }

    /** code < 0: drawn only. kind: 0 kana, 1 function, 2 menu, 3 OK */
    private class Key(val label: String, val code: Int, val units: Float = 1f, val kind: Int = 0) {
        val rect = RectF()
        var down = false
    }

    private fun kana(s: String, code: Int) = Key(s, code)
    private fun fn(s: String, code: Int, units: Float = 1f) = Key(s, code, units, 1)

    // the menu keys run left to right from matrix column 15 down to 7
    private val menuRow = listOf("パソコン", "こくご", "えいご", "さんすう", "おんがく", "ちえ", "たいけん", "おえかき", "ゲーム")
        .mapIndexed { i, s -> Key(if (look.menuLabels) s else "", 32 + 15 - i, 1f, 2) }
    private val rows: List<List<Key>> = listOf(
        menuRow,
        listOf(kana("ぬ", 170), kana("ふ", 171), kana("あ", 71), kana("う", 135), kana("え", 167), kana("お", 199),
            kana("や", 76), kana("ゆ", 141), kana("よ", 173), kana("わ", 78), kana("ほ", 235), kana("へ", 203), fn("けす", 13, 1.3f)),
        listOf(kana("た", 74), kana("て", 201), kana("い", 103), kana("す", 137), kana("か", 72), kana("ん", 174),
            kana("な", 75), kana("に", 138), kana("ら", 77), kana("せ", 200), fn("★゛゜", -1), Key("OK", -1, 1.3f, 3)),
        listOf(fn("シフト", -1, 1.2f), kana("ち", 106), kana("と", 233), kana("し", 105), kana("は", 107), kana("き", 104),
            kana("く", 136), kana("ま", 108), kana("の", 234), kana("り", 109), kana("れ", 205), kana("け", 168), kana("む", 172)),
        listOf(fn("かな/ローマじ", 14, 1.2f), kana("つ", 169), kana("さ", 73), kana("そ", 232), kana("ひ", 139), kana("こ", 231),
            kana("み", 140), kana("も", 236), kana("ね", 202), kana("る", 142), kana("め", 204), kana("ろ", 237), fn("ちいさい", -1)),
        listOf(fn("🔈", -1), fn("🔊", -1), Key("スペース", -1, 5f, 1), kana("を", 110), fn("✊", -1), fn("✌", -1), fn("✋", -1)),
    )
    private val arrows = listOf(Key("", 10), Key("", 9), Key("", 8), Key("", 7))   // up, left, right, down
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
    private val menuStrip = RectF()
    private val pad = RectF()
    private val power = RectF()
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
        val sw = lw * 0.9f
        val sh = sw * H / W
        lid.set((wf - lw) / 2, vh * 0.01f, (wf + lw) / 2, vh * 0.01f + sh + lw * 0.09f)
        screen.set(wf / 2 - sw / 2, lid.top + lw * 0.045f, wf / 2 + sw / 2, lid.top + lw * 0.045f + sh)
        menuPlate.set(lid.centerX() - lw * 0.07f, screen.bottom + lw * 0.008f, lid.centerX() + lw * 0.07f, lid.bottom - lw * 0.008f)
        // 13 key units across
        u = wf * 0.93f / 13.2f
        val kh = u
        val top = lid.bottom + u * 0.5f
        base.set(wf * 0.02f, top, wf * 0.98f, top + kh * rows.size + u * 4.1f)
        var y = base.top + u * 0.4f
        for ((ri, r) in rows.withIndex()) {
            val units = r.sumOf { it.units.toDouble() }.toFloat()
            var x = (wf - u * units) / 2
            if (ri == 0) {
                // the menu keys: flat keys on a strip, the power button on its left
                val mu = u * 1.12f
                x = (wf - mu * units) / 2 + u * 0.5f
                menuStrip.set(x - u * 0.25f, y - u * 0.08f, x + mu * units + u * 0.1f, y + kh * 0.66f)
                power.set(menuStrip.left - u * 0.95f, y, menuStrip.left - u * 0.3f, y + u * 0.6f)
                for (key in r) {
                    key.rect.set(x + mu * 0.08f, y, x + mu * 0.92f, y + kh * 0.58f)
                    x += mu
                }
                y += kh * 0.8f
                continue
            }
            for (key in r) {
                key.rect.set(x + u * 0.05f, y + kh * 0.05f, x + key.units * u - u * 0.05f, y + kh * 0.95f)
                x += key.units * u
            }
            y += kh
        }
        // the round cursor pad under the space bar
        val cy = y + u * 1.45f
        val cx = wf / 2 - u * 1.2f
        val pr = u * 1.15f
        pad.set(cx - pr * 1.5f, cy - pr * 1.15f, cx + pr * 1.5f, cy + pr * 1.15f)
        val a = pr * 0.62f
        arrows[0].rect.set(cx - a / 2, cy - pr, cx + a / 2, cy - pr + a)
        arrows[1].rect.set(cx - pr, cy - a / 2, cx - pr + a, cy + a / 2)
        arrows[2].rect.set(cx + pr - a, cy - a / 2, cx + pr, cy + a / 2)
        arrows[3].rect.set(cx - a / 2, cy + pr - a, cx + a / 2, cy + pr)
        // the mouse to the right of the pad, on the base's front edge
        val mh = u * 3.1f
        val mw = u * 2.1f
        mouse.set(base.right - mw - u * 0.5f, cy - mh / 2, base.right - u * 0.5f, cy + mh / 2)
        mouseButton.set(mouse.left + mw * 0.1f, mouse.top + mh * 0.07f, mouse.right - mw * 0.1f, mouse.top + mh * 0.42f)
    }

    override fun onDraw(cv: Canvas) {
        cv.drawColor(0xFF2B2A33.toInt())
        // lid
        paint.shader = LinearGradient(0f, lid.top, 0f, lid.bottom, look.lidTop, look.lidBottom, Shader.TileMode.CLAMP)
        cv.drawRoundRect(lid, u * 0.6f, u * 0.6f, paint)
        paint.shader = null
        paint.color = look.bezel
        val b = u * 0.12f
        cv.drawRoundRect(screen.left - b, screen.top - b, screen.right + b, screen.bottom + b, u * 0.2f, u * 0.2f, paint)
        paint.color = 0xFF000000.toInt()
        cv.drawRect(screen, paint)
        frame.setPixels(framePx, 0, W, 0, 0, W, H)
        cv.drawBitmap(frame, null, screen, pixelPaint)
        // the camera bump on the top edge
        paint.color = blend(look.lidTop, 0xFF888888.toInt(), 0.25f)
        cv.drawRoundRect(lid.centerX() - u * 0.6f, lid.top - u * 0.08f, lid.centerX() + u * 0.6f, lid.top + u * 0.25f, u * 0.1f, u * 0.1f, paint)
        // the emulator menu: a plate under the screen
        paint.color = 0x33000000
        cv.drawRoundRect(menuPlate, menuPlate.height() / 2, menuPlate.height() / 2, paint)
        text.color = 0xCCFFFFFF.toInt(); text.textSize = menuPlate.height() * 0.55f
        cv.drawText("MENU", menuPlate.centerX(), menuPlate.centerY() + text.textSize * 0.36f, text)
        // hinge and base
        paint.color = look.trim
        cv.drawRoundRect(lid.left + u * 2.2f, lid.bottom - u * 0.2f, lid.right - u * 2.2f, base.top + u * 0.35f, u * 0.2f, u * 0.2f, paint)
        paint.color = 0x55000000
        cv.drawRoundRect(base.left, base.top + u * 0.12f, base.right, base.bottom + u * 0.12f, u * 0.5f, u * 0.5f, paint)
        paint.shader = LinearGradient(0f, base.top, 0f, base.bottom, look.baseTop, look.baseBottom, Shader.TileMode.CLAMP)
        cv.drawRoundRect(base, u * 0.5f, u * 0.5f, paint)
        paint.shader = null
        stroke.color = look.trim; stroke.strokeWidth = u * 0.12f
        cv.drawRoundRect(base, u * 0.5f, u * 0.5f, stroke)
        // menu strip and power button
        paint.color = look.trim
        cv.drawRoundRect(menuStrip, menuStrip.height() / 2, menuStrip.height() / 2, paint)
        paint.color = look.key
        cv.drawOval(power, paint)
        stroke.color = 0xFFE0403A.toInt(); stroke.strokeWidth = u * 0.06f
        val pc = power.centerX(); val pcy = power.centerY(); val prr = power.width() * 0.22f
        cv.drawArc(pc - prr, pcy - prr, pc + prr, pcy + prr, -60f, 300f, false, stroke)
        cv.drawLine(pc, pcy - prr * 1.2f, pc, pcy, stroke)
        // cursor pad
        paint.color = 0xFFC8CACC.toInt()
        cv.drawRoundRect(pad, u * 0.2f, u * 0.2f, paint)
        val cx = pad.centerX(); val cy = pad.centerY(); val r = pad.height() * 0.42f
        paint.shader = RadialGradient(cx - r * 0.3f, cy - r * 0.3f, r * 1.4f, 0xFFFFFFFF.toInt(), 0xFFB9BBBE.toInt(), Shader.TileMode.CLAMP)
        cv.drawCircle(cx, cy, r, paint)
        paint.shader = null
        paint.color = if (arrows.any { it.down }) 0xFFAAAAAA.toInt() else 0xFFD8D9DB.toInt()
        cv.drawCircle(cx, cy, r * 0.42f, paint)
        for ((i, a) in arrows.withIndex()) drawArrow(cv, a, i)
        for (row in rows) for (key in row) drawKey(cv, key)
        drawMouse(cv)
    }

    private fun drawArrow(cv: Canvas, a: Key, dir: Int) {
        val p = Path()
        val r = a.rect; val s = r.width() * 0.32f
        val x = r.centerX(); val y = r.centerY()
        when (dir) {
            0 -> { p.moveTo(x, y - s); p.lineTo(x - s, y + s * 0.6f); p.lineTo(x + s, y + s * 0.6f) }
            1 -> { p.moveTo(x - s, y); p.lineTo(x + s * 0.6f, y - s); p.lineTo(x + s * 0.6f, y + s) }
            2 -> { p.moveTo(x + s, y); p.lineTo(x - s * 0.6f, y - s); p.lineTo(x - s * 0.6f, y + s) }
            else -> { p.moveTo(x, y + s); p.lineTo(x - s, y - s * 0.6f); p.lineTo(x + s, y - s * 0.6f) }
        }
        p.close()
        paint.color = if (a.down) 0xFF555555.toInt() else 0xFF8E9093.toInt()
        cv.drawPath(p, paint)
    }

    private fun drawKey(cv: Canvas, key: Key) {
        val r = key.rect
        val rad = min(r.width(), r.height()) * 0.2f
        val sink = if (key.down) r.height() * 0.06f else 0f
        val face = when (key.kind) { 2 -> look.menuKey; 3 -> look.ok; else -> look.key }
        paint.color = if (key.kind == 2) blend(face, 0xFF000000.toInt(), 0.25f) else look.keySide
        cv.drawRoundRect(r.left, r.top + sink, r.right, r.bottom, rad, rad, paint)
        paint.color = if (key.down) blend(face, 0xFF000000.toInt(), 0.18f) else face
        cv.drawRoundRect(r.left + r.width() * 0.03f, r.top + sink, r.right - r.width() * 0.03f, r.bottom - r.height() * 0.12f + sink * 0.5f, rad, rad, paint)
        if (key.label.isEmpty()) return
        text.color = when {
            key.kind == 2 -> look.menuInk
            key.kind == 3 -> if (look.ok == look.key) 0xFF33302A.toInt() else 0xFFFFFFFF.toInt()
            key.code < 0 -> 0xFF9A968E.toInt()
            else -> 0xFF33302A.toInt()
        }
        val n = key.label.codePointCount(0, key.label.length)
        val faceH = r.height() * 0.88f
        text.textSize = if (key.kind == 0) faceH * 0.52f else min(faceH * 0.36f, r.width() * 0.92f / n)
        val cy = (r.top + r.bottom - r.height() * 0.12f) / 2 + sink + text.textSize * 0.36f
        cv.drawText(key.label, r.centerX(), cy, text)
    }

    private fun drawMouse(cv: Canvas) {
        val m = mouse
        val w = m.width()
        stroke.color = 0xFFEDEDED.toInt(); stroke.strokeWidth = w * 0.04f
        val lead = Path().apply { moveTo(m.centerX(), m.top); cubicTo(m.centerX(), m.top - u * 1.5f, base.right + u * 0.2f, base.top + u * 6f, base.right - u * 0.1f, base.top + u * 3f) }
        cv.drawPath(lead, stroke)
        paint.color = 0x66000000
        cv.drawRoundRect(m.left + w * 0.04f, m.top + w * 0.06f, m.right + w * 0.04f, m.bottom + w * 0.06f, w * 0.4f, w * 0.4f, paint)
        paint.shader = LinearGradient(0f, m.top, 0f, m.bottom, look.mouseTop, look.mouseBottom, Shader.TileMode.CLAMP)
        cv.drawRoundRect(m, w * 0.4f, w * 0.4f, paint)
        paint.shader = null
        paint.color = if (buttons and 1 != 0) blend(look.mouseButton, 0xFF000000.toInt(), 0.2f) else look.mouseButton
        cv.drawRoundRect(mouseButton, w * 0.3f, w * 0.3f, paint)
        text.color = if (look.mouseButton == 0xFF1C1C1E.toInt()) 0xFFBBBBBB.toInt() else 0xFF8A857C.toInt()
        text.textSize = w * 0.15f
        cv.drawText("CLICK", mouseButton.centerX(), mouseButton.centerY() + text.textSize * 0.36f, text)
    }

    private fun blend(a: Int, b: Int, t: Float): Int {
        fun ch(s: Int) = (((a shr s) and 0xff) * (1 - t) + ((b shr s) and 0xff) * t).toInt() shl s
        return (0xFF shl 24) or ch(16) or ch(8) or ch(0)
    }

    private fun keyAt(x: Float, y: Float): Key? {
        for (key in keys) if (key.code >= 0 && RectF(key.rect).apply { inset(-u * 0.04f, -u * 0.04f) }.contains(x, y)) return key
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
