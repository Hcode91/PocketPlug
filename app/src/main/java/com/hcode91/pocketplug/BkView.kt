package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.os.Handler
import android.os.Looper
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View

/**
 * Soviet keyboard computer (BK-0010 / BK-0011 family), drawn from a photo of
 * the 0010-01: a black wedge case with a blank badge plate top left, a plain
 * black panel left of the keys, grey function keys, the red СТОП key, cream
 * character keys with the Cyrillic letter over the Latin one, and the arrow
 * cluster bottom right. A portable TV with a grey cabinet sits above it. The
 * 0011 models get a dark grey case.
 *
 * Keys send KOI-7 codes (the 1801VP014 matrix codes). ЗАГЛ / СТР pick capitals
 * or small letters (sticky); СУ makes the next key a control code; АР2 sends the
 * next key to the alternative vector. On the number and symbol keys a long press
 * gives the upper symbol. STOP (0x80) and АР2 (0x81) are codes above the
 * character set. Tap the badge plate for the programs menu, the TV knob for the
 * main menu.
 */
@SuppressLint("ViewConstructor")
class BkView(ctx: Context, private val eleven: Boolean) : View(ctx), DesktopSkin {

    override var onKey: ((Int, Boolean) -> Unit)? = null
    override var onMenu: (() -> Unit)? = null
    override var onDisks: (() -> Unit)? = null
    override var indicators = 0
    override var floppyLabel: String? = null
        set(v) { field = v; postInvalidate() }

    private enum class Kind { LETTER, SYMBOL, FUNC, STOP, SPACE, CTRL, AR2, CAPS, SMALL }
    /** Geometry in pixels of the reference photo's key area (x 655-2320, y 465-1105). */
    private class Key(
        val x0: Int, val y0: Int, val x1: Int, val y1: Int,
        val kind: Kind, val code: Int, val top: String = "", val bottom: String = "", val shifted: Int = -1,
    ) {
        val rect = RectF(); var down = false
    }

    private val keys = ArrayList<Key>()
    private fun fn(x0: Int, x1: Int, y0: Int, y1: Int, label: String, code: Int, kind: Kind = Kind.FUNC) {
        keys.add(Key(x0, y0, x1, y1, kind, code, label))
    }
    private fun sym(x0: Int, x1: Int, y0: Int, y1: Int, lower: Char, upper: Char) {
        keys.add(Key(x0, y0, x1, y1, Kind.SYMBOL, lower.code, upper.toString(), lower.toString(), upper.code))
    }
    /** A key in the letter area: base = the capital's code, small letters are base | 0x20. */
    private fun ltr(x0: Int, x1: Int, y0: Int, y1: Int, cyr: String, lat: String, base: Int) {
        keys.add(Key(x0, y0, x1, y1, Kind.LETTER, base, cyr, lat))
    }

    init {
        // row 0: function keys
        var y0 = 465; var y1 = 565
        fn(655, 810, y0, y1, "ПОВТ", 1); fn(820, 970, y0, y1, "КТ", 3)
        fn(980, 1130, y0, y1, "→|", 0x0b); fn(1140, 1290, y0, y1, "|←", 0x16); fn(1300, 1450, y0, y1, "⇒", 0x17)
        fn(1458, 1610, y0, y1, "ИНД\nСУ", 2); fn(1620, 1770, y0, y1, "БЛОК\nРЕД", 4); fn(1780, 1930, y0, y1, "ШАГ", 0)
        fn(1940, 2120, y0, y1, "СБР", 0x0c); fn(2130, 2320, y0, y1, "СТОП", 0x80, Kind.STOP)
        // row 1: digits
        y0 = 575; y1 = 672
        fn(660, 760, y0, y1, "↓", 0x0a)
        sym(765, 860, y0, y1, ';', '+')
        val digits = "1234567890"; val ups = "!\"#$%&'()0"
        var x = 870
        for (i in 0 until 10) { sym(x, x + 95, y0, y1, digits[i], ups[i]); x += 106 }
        sym(1935, 2030, y0, y1, '-', '='); sym(2040, 2140, y0, y1, ':', '*')
        fn(2150, 2320, y0, y1, "⇐", 0x18)
        // row 2
        y0 = 683; y1 = 780
        fn(655, 810, y0, y1, "ТАБ", 9)
        val r2 = listOf("Й" to "J", "Ц" to "C", "У" to "U", "К" to "K", "Е" to "E", "Н" to "N", "Г" to "G", "Ш" to "[", "Щ" to "]", "З" to "Z", "Х" to "H", "Ъ" to "}")
        val r2c = listOf(0x4a, 0x43, 0x55, 0x4b, 0x45, 0x4e, 0x47, 0x5b, 0x5d, 0x5a, 0x48, 0x5f)
        x = 815
        for (i in r2.indices) { ltr(x, x + 100, y0, y1, r2[i].first, r2[i].second, r2c[i]); x += 105 }
        sym(2095, 2195, y0, y1, '/', '?')
        fn(2210, 2320, y0, y1, "ВС", 0x13)
        // row 3
        y0 = 788; y1 = 885
        fn(655, 835, y0, y1, "СУ", 0, Kind.CTRL)
        val r3 = listOf("Ф" to "F", "Ы" to "Y", "В" to "W", "А" to "A", "П" to "P", "Р" to "R", "О" to "O", "Л" to "L", "Д" to "D", "Ж" to "V", "Э" to "\\")
        val r3c = listOf(0x46, 0x59, 0x57, 0x41, 0x50, 0x52, 0x4f, 0x4c, 0x44, 0x56, 0x5c)
        x = 845
        for (i in r3.indices) { ltr(x, x + 100, y0, y1, r3[i].first, r3[i].second, r3c[i]); x += 105 }
        sym(2010, 2115, y0, y1, '.', '>')
        fn(2130, 2320, y0, y1, "↵", 0x0a)
        // row 4
        y0 = 895; y1 = 992
        fn(655, 785, y0, y1, "ЗАГЛ", 0, Kind.CAPS); fn(790, 890, y0, y1, "СТР", 0, Kind.SMALL)
        val r4 = listOf("Я" to "Q", "Ч" to "¬", "С" to "S", "М" to "M", "И" to "I", "Т" to "T", "Ь" to "X", "Б" to "B", "Ю" to "@")
        val r4c = listOf(0x51, 0x5e, 0x53, 0x4d, 0x49, 0x54, 0x58, 0x42, 0x40)
        x = 895
        for (i in r4.indices) { ltr(x, x + 100, y0, y1, r4[i].first, r4[i].second, r4c[i]); x += 105 }
        sym(1855, 1955, y0, y1, ',', '<')
        // arrows
        fn(2000, 2100, 900, 1105, "←", 0x08); fn(2110, 2210, 900, 996, "↑", 0x1a)
        fn(2110, 2210, 1005, 1105, "↓", 0x1b); fn(2220, 2320, 900, 1105, "→", 0x19)
        // row 5
        y0 = 1003; y1 = 1100
        fn(655, 855, y0, y1, "РУС", 0x0e); fn(865, 995, y0, y1, "АР2", 0x81, Kind.AR2)
        keys.add(Key(1005, y0, 1775, y1, Kind.SPACE, 0x20))
        fn(1780, 1990, y0, y1, "ЛАТ", 0x0f)
    }

    private var small = false
    private var ctrlLatched = false
    private var ar2Latched = false

    private val frame = Bitmap.createBitmap(512, 256, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(512 * 256)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL) }
    private val smooth = Paint(Paint.FILTER_BITMAP_FLAG)

    private val tv = RectF(); private val screen = RectF(); private val knob = PointF(); private var knobR = 0f
    private val body = RectF(); private val keyArea = RectF(); private val badge = RectF(); private val leftPanel = RectF()
    private val pointers = HashMap<Int, Key>()
    private val handler = Handler(Looper.getMainLooper())
    private var longFired: Key? = null

    override fun updateFrame(px: IntArray) {
        if (px.size < framePx.size) return
        System.arraycopy(px, 0, framePx, 0, framePx.size)
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        // keyboard: as wide as the view, keys a little taller than the photo's when there's room
        val bw = w * 0.99f
        val keysW = bw * 0.86f
        val keysH = minOf(keysW / (1665f / 640f) * 1.35f, h * 0.48f)
        val bandTop = keysH * 0.3f; val bandBottom = keysH * 0.22f
        val bh = keysH + bandTop + bandBottom
        body.set((w - bw) / 2, h - bh - h * 0.01f, (w + bw) / 2, h - h * 0.01f)
        keyArea.set(body.right - keysW - bw * 0.015f, body.top + bandTop, body.right - bw * 0.015f, body.top + bandTop + keysH)
        leftPanel.set(body.left + bw * 0.015f, keyArea.top - keysH * 0.02f, keyArea.left - bw * 0.01f, keyArea.bottom + keysH * 0.02f)
        badge.set(body.left + bw * 0.03f, body.top + bandTop * 0.22f, body.left + bw * 0.2f, body.top + bandTop * 0.8f)
        val sx = keysW / 1665f; val sy = keysH / 640f
        for (k in keys) k.rect.set(keyArea.left + (k.x0 - 655) * sx, keyArea.top + (k.y0 - 465) * sy, keyArea.left + (k.x1 - 655) * sx, keyArea.top + (k.y1 - 465) * sy)

        // TV above, filling the space left
        val avail = body.top - h * 0.02f
        var tw = w * 0.96f
        var sw = tw * 0.78f
        var sh = sw * 0.75f
        if (sh + tw * 0.1f > avail) { val k = avail / (sh + tw * 0.1f); sh *= k; sw *= k; tw *= k }
        val tl = (w - tw) / 2; val tt = (avail - (sh + tw * 0.1f)) / 2 + h * 0.005f
        tv.set(tl, tt, tl + tw, tt + sh + tw * 0.1f)
        screen.set(tl + tw * 0.04f, tt + tw * 0.05f, tl + tw * 0.04f + sw, tt + tw * 0.05f + sh)
        knobR = tw * 0.04f
        knob.set((screen.right + tv.right) / 2, screen.top + sh * 0.3f)
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF1A1715.toInt())
        val tw = tv.width()
        // the TV
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

        // the case: black wedge with a lighter top edge and a front lip
        val bw = body.width()
        val top = if (eleven) 0xFF4A4A4E.toInt() else 0xFF242426.toInt()
        val bot = if (eleven) 0xFF333336.toInt() else 0xFF141415.toInt()
        paint.color = 0x66000000
        c.drawRoundRect(body.left, body.top + bw * 0.006f, body.right, body.bottom + bw * 0.006f, bw * 0.012f, bw * 0.012f, paint)
        paint.shader = LinearGradient(0f, body.top, 0f, body.bottom, top, bot, Shader.TileMode.CLAMP)
        c.drawRoundRect(body, bw * 0.012f, bw * 0.012f, paint); paint.shader = null
        stroke.color = 0xFF3C3C40.toInt(); stroke.strokeWidth = bw * 0.002f
        c.drawLine(body.left + bw * 0.01f, keyArea.top - keyArea.height() * 0.06f, body.right - bw * 0.01f, keyArea.top - keyArea.height() * 0.06f, stroke)
        paint.color = 0x22000000
        c.drawRect(body.left + bw * 0.01f, keyArea.bottom + keyArea.height() * 0.06f, body.right - bw * 0.01f, body.bottom - bw * 0.008f, paint)
        // the well the keys sit in, and the plain panel to its left
        paint.color = 0xFF0C0C0D.toInt()
        c.drawRect(keyArea.left - bw * 0.006f, keyArea.top - bw * 0.006f, keyArea.right + bw * 0.006f, keyArea.bottom + bw * 0.006f, paint)
        paint.color = if (eleven) 0xFF3E3E42.toInt() else 0xFF1C1C1E.toInt()
        c.drawRect(leftPanel, paint)
        // badge plate (blank): tap for programs
        paint.shader = LinearGradient(badge.left, 0f, badge.right, 0f, 0xFF2E2E30.toInt(), 0xFF3A3A3C.toInt(), Shader.TileMode.CLAMP)
        c.drawRect(badge, paint); paint.shader = null
        stroke.color = 0xFFB8B8B8.toInt(); stroke.strokeWidth = bw * 0.0015f
        c.drawLine(badge.left, badge.centerY() - badge.height() * 0.1f, badge.right, badge.centerY() - badge.height() * 0.1f, stroke)
        text.color = 0xFFC8C8C8.toInt(); text.textSize = badge.height() * 0.28f
        c.drawText(floppyLabel?.take(18) ?: "programs", badge.centerX(), badge.bottom - badge.height() * 0.16f, text)

        for (k in keys) drawKey(c, k)
    }

    private fun drawKey(c: Canvas, k: Key) {
        val latched = (k.kind == Kind.CTRL && ctrlLatched) || (k.kind == Kind.AR2 && ar2Latched) ||
            (k.kind == Kind.CAPS && !small) || (k.kind == Kind.SMALL && small)
        val d = k.down || latched
        val r = k.rect
        val base = when (k.kind) {
            Kind.STOP -> 0xFFC8282A.toInt()
            Kind.LETTER, Kind.SYMBOL, Kind.SPACE -> 0xFFE6E1D2.toInt()
            else -> 0xFF8E8E8C.toInt()
        }
        val rad = r.height() * 0.06f
        paint.color = 0xFF050505.toInt()
        c.drawRoundRect(r, rad, rad, paint)
        // keycap: darker skirt, lighter top face
        val inset = minOf(r.width(), r.height()) * 0.08f
        val dy = if (d) r.height() * 0.04f else 0f
        paint.color = darken(if (d) darken(base) else base)
        c.drawRoundRect(r.left + inset * 0.3f, r.top + inset * 0.3f + dy, r.right - inset * 0.3f, r.bottom - inset * 0.2f, rad, rad, paint)
        paint.color = if (d) darken(base) else base
        val face = RectF(r.left + inset, r.top + inset * 0.6f + dy, r.right - inset, r.bottom - inset * 1.4f + dy)
        c.drawRoundRect(face, rad, rad, paint)

        text.color = if (k.kind == Kind.STOP) 0xFF2A0A0A.toInt() else 0xFF1C1C1C.toInt()
        when (k.kind) {
            Kind.LETTER -> {
                text.textSize = face.height() * 0.32f
                c.drawText(k.top, face.left + face.width() * 0.3f, face.top + face.height() * 0.38f, text)
                text.textSize = face.height() * 0.36f
                text.textSkewX = -0.2f
                c.drawText(k.bottom, face.left + face.width() * 0.55f, face.top + face.height() * 0.85f, text)
                text.textSkewX = 0f
            }
            Kind.SYMBOL -> {
                text.textSize = face.height() * 0.3f
                c.drawText(k.top, face.centerX(), face.top + face.height() * 0.36f, text)
                c.drawText(k.bottom, face.centerX(), face.top + face.height() * 0.82f, text)
            }
            Kind.SPACE -> {}
            else -> {
                val lines = k.top.split("\n")
                val longest = maxOf(1, lines.maxOf { it.length })
                val arrow = longest <= 2 && !k.top[0].isLetter()
                val want = face.height() * (if (arrow) 0.55f else if (lines.size > 1) 0.26f else 0.3f)
                text.textSize = minOf(want, face.width() * 1.5f / longest)
                for ((i, l) in lines.withIndex())
                    c.drawText(l, face.centerX(), face.centerY() + text.textSize * (0.35f + (i - (lines.size - 1) / 2f) * 1.05f), text)
            }
        }
    }

    private fun darken(c: Int) = Color.rgb(Color.red(c) * 8 / 10, Color.green(c) * 8 / 10, Color.blue(c) * 8 / 10)

    private fun send(code: Int) {
        var cd = code
        if (ctrlLatched && cd in 0x40..0x7f) cd = cd and 0x1f
        onKey?.invoke(cd, true)
        onKey?.invoke(cd, false)
        if (ctrlLatched) { ctrlLatched = false; invalidate() }
        if (ar2Latched) { ar2Latched = false; onKey?.invoke(0x81, false); invalidate() }
    }

    /** Key down: modifiers act at once; symbol keys wait to see if it's a long press. */
    private fun press(k: Key) {
        when (k.kind) {
            Kind.CTRL -> ctrlLatched = !ctrlLatched
            Kind.AR2 -> { ar2Latched = !ar2Latched; onKey?.invoke(0x81, ar2Latched) }
            Kind.CAPS -> small = false
            Kind.SMALL -> small = true
            Kind.STOP -> { onKey?.invoke(0x80, true); onKey?.invoke(0x80, false) }
            Kind.SYMBOL -> {
                longFired = null
                handler.postDelayed({
                    if (k.down) { longFired = k; send(k.shifted); performHapticFeedback(HapticFeedbackConstants.LONG_PRESS) }
                }, 400)
            }
            Kind.LETTER -> send(if (small) k.code or 0x20 else k.code)
            else -> send(k.code)
        }
    }

    private fun release(k: Key) {
        if (k.kind == Kind.SYMBOL && longFired !== k) send(k.code)
        if (longFired === k) longFired = null
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        val i = e.actionIndex
        val id = e.getPointerId(i)
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val x = e.getX(i); val y = e.getY(i)
                if (Math.hypot((x - knob.x).toDouble(), (y - knob.y).toDouble()) < knobR * 1.6) { onMenu?.invoke(); return true }
                if (badge.contains(x, y)) { onDisks?.invoke(); return true }
                val k = keys.firstOrNull { it.rect.contains(x, y) } ?: return true
                k.down = true; pointers[id] = k
                performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                press(k)
                invalidate()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                pointers.remove(id)?.let { it.down = false; release(it); invalidate() }
            }
            MotionEvent.ACTION_CANCEL -> {
                for (k in pointers.values) k.down = false
                pointers.clear(); invalidate()
            }
        }
        return true
    }
}
