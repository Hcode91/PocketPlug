package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View

/**
 * Mid-80s Soviet home computer built into its keyboard, plugged into a TV
 * (BK-0010 / BK-0011 family): a portable TV with a grey cabinet on top, the
 * low dark keyboard computer underneath with light keys, the red STOP key and
 * the row of control keys (КТ, ТАБ, СБР, РУС, ЛАТ, ЗБ). Placeholder look, no
 * names or logos; the 0011 models have a slightly lighter body.
 *
 * Keys send KOI-7 codes (down, then up). Letters are capitals unless НР (the
 * case shift) is latched. STOP (0x80) and АР2 (0x81) go to the core as codes
 * above the character set. Tap the strip on the back edge of the keyboard for
 * the programs menu and the TV's knob for the main menu.
 */
@SuppressLint("ViewConstructor")
class BkView(ctx: Context, private val eleven: Boolean) : View(ctx), DesktopSkin {

    override var onKey: ((Int, Boolean) -> Unit)? = null
    override var onMenu: (() -> Unit)? = null
    override var onDisks: (() -> Unit)? = null
    override var indicators = 0
    override var floppyLabel: String? = null
        set(v) { field = v; postInvalidate() }

    private enum class Kind { CHAR, FUNC, SHIFT, AR2, STOP, SPACE }
    private data class K(val label: String, val code: Int, val units: Float = 1f, val kind: Kind = Kind.CHAR, val shifted: Int = 0)
    private class Key(val k: K) { val rect = RectF(); var down = false }

    private fun c(ch: Char, sh: Char) = K(ch.toString(), ch.code, shifted = sh.code)
    private fun f(label: String, code: Int, u: Float = 1f) = K(label, code, u, Kind.FUNC)
    private val rows: List<List<K>> = listOf(
        listOf(K("СТОП", 0x80, 1.3f, Kind.STOP), f("КТ", 3), f("ТАБ", 9), f("СБР", 12), f("ПОВТ", 1), f("ИНД\nСУ", 2),
            f("БЛОК\nРЕД", 4), f("ШАГ", 0), f("РУС", 14), f("ЛАТ", 15), f("ЗБ", 0x18, 1.3f)),
        listOf(c(';', '+'), c('1', '!'), c('2', '"'), c('3', '#'), c('4', '$'), c('5', '%'), c('6', '&'),
            c('7', '\''), c('8', '('), c('9', ')'), c('0', '{'), c('-', '='), c('/', '?')),
        listOf(c('j', 'J'), c('c', 'C'), c('u', 'U'), c('k', 'K'), c('e', 'E'), c('n', 'N'), c('g', 'G'),
            c('[', '{'), c(']', '}'), c('z', 'Z'), c('h', 'H'), c(':', '*'), f("ВВОД", 10, 1.4f)),
        listOf(c('f', 'F'), c('y', 'Y'), c('w', 'W'), c('a', 'A'), c('p', 'P'), c('r', 'R'), c('o', 'O'),
            c('l', 'L'), c('d', 'D'), c('v', 'V'), c('\\', '|'), c('.', '>'), f("↑", 0x1a)),
        listOf(K("НР", 0, 1.4f, Kind.SHIFT), c('q', 'Q'), c('^', '~'), c('s', 'S'), c('m', 'M'), c('i', 'I'), c('t', 'T'),
            c('x', 'X'), c('b', 'B'), c('@', '`'), c(',', '<'), f("←", 8), f("↓", 0x1b), f("→", 0x19)),
        listOf(K("АР2", 0x81, 1.6f, Kind.AR2), K("", 0x20, 8.6f, Kind.SPACE), f("ВС", 0x13, 1.4f)),
    )
    private val keys = rows.map { r -> r.map { Key(it) } }
    private val allKeys = keys.flatten()
    private var lower = false
    private var ar2Latched = false

    private val frame = Bitmap.createBitmap(512, 256, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(512 * 256)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }
    private val smooth = Paint(Paint.FILTER_BITMAP_FLAG)

    private val tv = RectF(); private val screen = RectF(); private val knob = PointF(); private var knobR = 0f
    private val body = RectF(); private val keyArea = RectF(); private val slot = RectF()
    private val pointers = HashMap<Int, Key>()

    override fun updateFrame(px: IntArray) {
        if (px.size < framePx.size) return
        System.arraycopy(px, 0, framePx, 0, framePx.size)
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        var tw = w * 0.96f
        var sw = tw * 0.78f
        var sh = sw * 0.75f
        if (sh > h * 0.5f) { val k = h * 0.5f / sh; sh *= k; sw *= k; tw *= k }
        val tl = (w - tw) / 2; val tt = h * 0.012f
        tv.set(tl, tt, tl + tw, tt + sh + tw * 0.1f)
        screen.set(tl + tw * 0.04f, tt + tw * 0.05f, tl + tw * 0.04f + sw, tt + tw * 0.05f + sh)
        knobR = tw * 0.04f
        knob.set((screen.right + tv.right) / 2, screen.top + sh * 0.3f)
        body.set(w * 0.01f, tv.bottom + h * 0.02f, w * 0.99f, h * 0.995f)
        slot.set(body.left + body.width() * 0.25f, body.top + body.height() * 0.025f, body.right - body.width() * 0.25f, body.top + body.height() * 0.07f)
        keyArea.set(body.left + body.width() * 0.02f, body.top + body.height() * 0.1f, body.right - body.width() * 0.02f, body.bottom - body.height() * 0.03f)
        val maxUnits = rows.maxOf { r -> r.sumOf { it.units.toDouble() }.toFloat() }
        val unit = keyArea.width() / (maxUnits + 0.3f)
        val rowH = keyArea.height() / (rows.size + 0.3f)
        var y = keyArea.top + rowH * 0.15f
        for ((ri, r) in rows.withIndex()) {
            val total = r.sumOf { it.units.toDouble() }.toFloat()
            var x = keyArea.centerX() - total * unit / 2
            for ((ki, k) in r.withIndex()) {
                keys[ri][ki].rect.set(x + unit * 0.05f, y + rowH * 0.07f, x + k.units * unit - unit * 0.05f, y + rowH * 0.93f)
                x += k.units * unit
            }
            y += rowH
        }
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF1A1715.toInt())
        val tw = tv.width()
        // the TV: grey plastic cabinet, black screen surround, a channel knob and a speaker grille
        paint.color = 0x55000000
        c.drawRoundRect(tv.left + tw * 0.01f, tv.top + tw * 0.012f, tv.right + tw * 0.01f, tv.bottom + tw * 0.012f, tw * 0.03f, tw * 0.03f, paint)
        paint.shader = LinearGradient(0f, tv.top, 0f, tv.bottom, 0xFF8E8C88.toInt(), 0xFF5E5C58.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(tv, tw * 0.03f, tw * 0.03f, paint); paint.shader = null
        paint.color = 0xFF101010.toInt()
        c.drawRoundRect(screen.left - tw * 0.015f, screen.top - tw * 0.015f, screen.right + tw * 0.015f, screen.bottom + tw * 0.015f, tw * 0.04f, tw * 0.04f, paint)
        frame.setPixels(framePx, 0, 512, 0, 0, 512, 256)
        c.drawBitmap(frame, null, screen, smooth)
        paint.shader = RadialGradient(screen.centerX(), screen.centerY(), screen.width() * 0.7f, 0x00FFFFFF, 0x30000000, Shader.TileMode.CLAMP)
        c.drawRect(screen, paint); paint.shader = null
        paint.color = 0xFF2A2826.toInt(); c.drawCircle(knob.x, knob.y, knobR, paint)
        paint.color = 0xFF4A4744.toInt(); c.drawCircle(knob.x, knob.y, knobR * 0.75f, paint)
        stroke.color = 0xFFBDB8B0.toInt(); stroke.strokeWidth = knobR * 0.12f
        c.drawLine(knob.x, knob.y, knob.x, knob.y - knobR * 0.7f, stroke)
        paint.color = 0xFF3A3836.toInt()
        for (i in 0 until 7) {
            val gy = knob.y + knobR * 2.2f + i * knobR * 0.5f
            if (gy < screen.bottom) c.drawRect(knob.x - knobR, gy, knob.x + knobR, gy + knobR * 0.2f, paint)
        }

        // the keyboard computer: a low wedge, dark (0010) or dark grey (0011)
        val bw = body.width()
        paint.shader = LinearGradient(0f, body.top, 0f, body.bottom, if (eleven) 0xFF4A4C50.toInt() else 0xFF303034.toInt(),
            if (eleven) 0xFF2E3034.toInt() else 0xFF1A1A1E.toInt(), Shader.TileMode.CLAMP)
        c.drawRoundRect(body, bw * 0.02f, bw * 0.02f, paint); paint.shader = null
        stroke.color = 0xFF55565A.toInt(); stroke.strokeWidth = bw * 0.003f
        c.drawRoundRect(body, bw * 0.02f, bw * 0.02f, stroke)
        // the expansion edge at the back: tap for the programs menu
        paint.color = 0xFF121214.toInt(); c.drawRoundRect(slot, slot.height() / 2, slot.height() / 2, paint)
        text.color = 0xFFB8B8B8.toInt(); text.textSize = slot.height() * 0.55f
        c.drawText(floppyLabel?.take(30) ?: "programs", slot.centerX(), slot.centerY() + text.textSize * 0.35f, text)
        paint.color = 0xFFE03020.toInt(); c.drawCircle(body.right - bw * 0.05f, slot.centerY(), bw * 0.008f, paint)

        for (k in allKeys) {
            val latched = (k.k.kind == Kind.SHIFT && lower) || (k.k.kind == Kind.AR2 && ar2Latched)
            val d = k.down || latched
            val r = k.rect
            val base = when (k.k.kind) {
                Kind.STOP -> 0xFFC8321E.toInt()
                Kind.CHAR, Kind.SPACE -> 0xFFDCD8CE.toInt()
                else -> 0xFF9A968E.toInt()
            }
            paint.color = 0xFF0A0A0A.toInt()
            c.drawRoundRect(r.left, r.top + r.height() * 0.06f, r.right, r.bottom + r.height() * 0.04f, r.height() * 0.1f, r.height() * 0.1f, paint)
            paint.color = if (d) darken(base) else base
            val dy = if (d) r.height() * 0.05f else 0f
            c.drawRoundRect(r.left, r.top + dy, r.right, r.bottom - r.height() * 0.04f + dy, r.height() * 0.1f, r.height() * 0.1f, paint)
            text.color = if (k.k.kind == Kind.STOP) 0xFFF4F0E8.toInt() else 0xFF1C1C1C.toInt()
            val lines = k.k.label.split("\n")
            if (k.k.kind == Kind.CHAR) {
                val letter = k.k.label[0].isLetter()
                if (letter) {
                    text.textSize = r.height() * 0.4f
                    c.drawText(k.k.label.uppercase(), r.centerX(), r.centerY() + text.textSize * 0.35f, text)
                } else {
                    text.textSize = r.height() * 0.28f
                    c.drawText(k.k.shifted.toChar().toString(), r.centerX(), r.top + r.height() * 0.38f, text)
                    c.drawText(k.k.label, r.centerX(), r.top + r.height() * 0.78f, text)
                }
            } else {
                text.textSize = r.height() * (if (lines.size > 1) 0.2f else if (k.k.label.length > 3) 0.22f else 0.28f)
                for ((i, l) in lines.withIndex())
                    c.drawText(l, r.centerX(), r.centerY() + text.textSize * (0.35f + (i - (lines.size - 1) / 2f) * 1.1f), text)
            }
        }
    }

    private fun darken(c: Int) = Color.rgb(Color.red(c) * 7 / 10, Color.green(c) * 7 / 10, Color.blue(c) * 7 / 10)

    private fun press(k: Key) {
        when (k.k.kind) {
            Kind.SHIFT -> { lower = !lower; return }
            Kind.AR2 -> {
                ar2Latched = !ar2Latched
                onKey?.invoke(0x81, ar2Latched)
                return
            }
            Kind.STOP -> { onKey?.invoke(0x80, true); onKey?.invoke(0x80, false); return }
            else -> {}
        }
        var code = k.k.code
        if (k.k.kind == Kind.CHAR) {
            val letter = k.k.label[0].isLetter()
            code = if (letter) (if (lower) k.k.code else k.k.shifted) else (if (lower) k.k.shifted else k.k.code)
        }
        onKey?.invoke(code, true)
        onKey?.invoke(code, false)
        if (ar2Latched) { ar2Latched = false; onKey?.invoke(0x81, false) }
        if (lower && k.k.kind == Kind.CHAR && !k.k.label[0].isLetter()) lower = false
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        val i = e.actionIndex
        val id = e.getPointerId(i)
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val x = e.getX(i); val y = e.getY(i)
                if (Math.hypot((x - knob.x).toDouble(), (y - knob.y).toDouble()) < knobR * 1.6) { onMenu?.invoke(); return true }
                if (y >= body.top && y < keyArea.top) { onDisks?.invoke(); return true }
                val k = allKeys.firstOrNull { it.rect.contains(x, y) } ?: return true
                k.down = true; pointers[id] = k
                performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                press(k)
                invalidate()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                pointers.remove(id)?.let { it.down = false; invalidate() }
                if (e.actionMasked == MotionEvent.ACTION_CANCEL) { for (k in pointers.values) k.down = false; pointers.clear() }
            }
        }
        return true
    }
}
