package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.min

/**
 * The 8800b machines.
 *
 *  - Terminal (both models): a cream glass terminal with its black bezel
 *    and grey keyboard on the left, and on the right the blue rack: for the
 *    turnkey model the control plate (key lock, RUN/STOP, READY, SAFE, PROT,
 *    power rocker), then the dual 8" disk unit (tap it for disks), MENU.
 *  - Front panel (8800b only): the black panel with the status, data and
 *    address LEDs, the 16 address/data switches and the eight control
 *    switches (up / down), plus TERMINAL, DISKS and MENU.
 *
 * The core's frame is a status block (see altair.h). Terminal keys go out as
 * ASCII; SHIFT and CTRL latch until the next key, CAPS LOCK starts on. Panel
 * switches go out as input bits. No maker's name or logo is drawn.
 */
class AltairView(ctx: Context, private val turnkey: Boolean) : View(ctx) {

    var onKey: ((Int) -> Unit)? = null
    var onInput: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var onDisks: (() -> Unit)? = null
    var onPower: (() -> Unit)? = null
    /** 0: VT100 arrows, 1: ADM-3A arrows. */
    var vtMode = 0
    var diskLabels = arrayOfNulls<String>(2)
        set(v) { field = v; invalidate() }

    var showTerminal = turnkey
        set(v) { field = v; invalidate() }

    // ---- state from the core ----
    private val lock = Any()
    private val inChars = IntArray(24 * 80)
    private val inStat = IntArray(8)
    private val inLed = IntArray(36)
    private var fresh = false
    private val chars = IntArray(24 * 80)
    private val stat = IntArray(8)
    private val led = IntArray(36)

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.SANS_SERIF }
    private val mono = Paint(Paint.ANTI_ALIAS_FLAG).apply { typeface = Typeface.MONOSPACE; textAlign = Paint.Align.LEFT }

    // laid out in a 2000 x 1000 space, scaled to fit
    private var sc = 1f
    private var ox = 0f
    private var oy = 0f

    // ---- terminal keyboard ----
    private class TKey(val x: Float, val y: Float, val w: Float, val label: String, val shiftLabel: String, val code: Int, val shiftCode: Int)
    private val tkeys = ArrayList<TKey>()
    private var shift = false
    private var ctrl = false
    private var caps = true
    private var heldKey: TKey? = null

    // ---- panel ----
    private var switches = 0
    private var momentary = 0
    private val pointers = HashMap<Int, Any>()

    init {
        val p = 90f; val x0 = 45f
        fun row(y: Float, items: List<Any>) {
            var x = x0
            for (it in items) {
                @Suppress("UNCHECKED_CAST")
                val t = it as Array<Any>
                val w = (t.getOrNull(5) as? Float) ?: 1f
                tkeys.add(TKey(x, y, 82f + (w - 1f) * p, t[0] as String, t[1] as String, t[2] as Int, t[3] as Int))
                x += w * p
            }
        }
        fun k(l: String, s: String, c: Int, sc: Int, w: Float = 1f) = arrayOf<Any>(l, s, c, sc, 0, w)
        fun ch(c: Char, s: Char) = k(c.toString(), s.toString(), c.code, s.code)
        fun letter(c: Char) = k(c.toString(), "", c.lowercaseChar().code, c.code)
        row(628f, listOf(k("ESC", "", 27, 27), ch('1', '!'), ch('2', '@'), ch('3', '#'), ch('4', '$'), ch('5', '%'),
            ch('6', '^'), ch('7', '&'), ch('8', '*'), ch('9', '('), ch('0', ')'), ch('-', '_'), ch('=', '+'),
            ch('`', '~'), k("BS", "", 8, 8)))
        row(700f, listOf(k("TAB", "", 9, 9, 1.5f)) + "QWERTYUIOP".map { letter(it) } +
            listOf(ch('[', '{'), ch(']', '}'), k("\\", "|", '\\'.code, '|'.code, 1.5f)))
        row(772f, listOf(k("CTRL", "", CTRL, CTRL, 1.75f)) + "ASDFGHJKL".map { letter(it) } +
            listOf(ch(';', ':'), ch('\'', '"'), k("RETURN", "", 13, 13, 2.25f)))
        row(844f, listOf(k("SHIFT", "", SHIFT, SHIFT, 2.25f)) + "ZXCVBNM".map { letter(it) } +
            listOf(ch(',', '<'), ch('.', '>'), ch('/', '?'), k("SHIFT", "", SHIFT, SHIFT, 2.75f)))
        row(916f, listOf(k("LOCK", "", CAPS, CAPS, 1.5f), k("", "", ' '.code, ' '.code, 9f),
            k("◀", "", LEFT, LEFT), k("▲", "", UP, UP), k("▼", "", DOWN, DOWN), k("▶", "", RIGHT, RIGHT)))
    }

    fun updateFrame(px: IntArray) {
        if (px.size < 80 * 27) return
        synchronized(lock) {
            System.arraycopy(px, 0, inChars, 0, 24 * 80)
            System.arraycopy(px, 24 * 80, inStat, 0, 8)
            System.arraycopy(px, 25 * 80, inLed, 0, 36)
            fresh = true
        }
        postInvalidate()
    }

    val running get() = stat[2] != 0

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        sc = min(w / 2000f, h / 1000f)
        ox = (w - 2000f * sc) / 2f
        oy = (h - 1000f * sc) / 2f
    }

    private fun X(v: Float) = ox + v * sc
    private fun Y(v: Float) = oy + v * sc
    private fun rect(l: Float, t: Float, r: Float, b: Float) = RectF(X(l), Y(t), X(r), Y(b))
    private fun rect(r: RectF) = rect(r.left, r.top, r.right, r.bottom)

    override fun onDraw(c: Canvas) {
        synchronized(lock) {
            if (fresh) {
                System.arraycopy(inChars, 0, chars, 0, chars.size)
                System.arraycopy(inStat, 0, stat, 0, 8)
                System.arraycopy(inLed, 0, led, 0, 36)
                fresh = false
            }
        }
        c.drawColor(0xFF161616.toInt())
        if (showTerminal) {
            drawTerminal(c)
            drawRack(c)
        } else drawPanel(c)
        if (fresh) postInvalidateDelayed(16)
    }

    // ================= terminal =================
    private val screenR = RectF(104f, 52f, 1346f, 566f)

    private fun drawTerminal(c: Canvas) {
        // housing
        paint.color = 0xFFD8D2C0.toInt(); c.drawRoundRect(rect(20f, 10f, 1430f, 990f), 26f * sc, 26f * sc, paint)
        paint.color = 0xFF232323.toInt(); c.drawRoundRect(rect(56f, 26f, 1394f, 596f), 16f * sc, 16f * sc, paint)
        paint.color = 0xFF2C2C2C.toInt(); c.drawRoundRect(rect(80f, 38f, 1370f, 582f), 30f * sc, 30f * sc, paint)
        // screen
        val sr = rect(screenR)
        paint.color = 0xFF0B0E0C.toInt(); c.drawRoundRect(sr, 26f * sc, 26f * sc, paint)
        val cw = (screenR.width() - 20f) / 80f
        val chH = (screenR.height() - 20f) / 24f
        mono.textSize = chH * 0.86f * sc
        mono.textScaleX = 1f
        val mw = mono.measureText("M")
        mono.textScaleX = cw * sc / mw
        val glyph = CharArray(1)
        val cx = stat[0]; val cy = stat[1]
        val blink = (System.currentTimeMillis() / 530) % 2 == 0L
        for (r in 0 until 24) {
            val y = screenR.top + 10f + r * chH
            for (col in 0 until 80) {
                val v = chars[r * 80 + col]
                val ch = v and 0x7f
                val rev = (v and 0x100) != 0
                val isCursor = r == cy && col == cx && blink
                val x = screenR.left + 10f + col * cw
                if (rev != isCursor) {
                    paint.color = PHOSPHOR
                    c.drawRect(X(x), Y(y + 1f), X(x + cw), Y(y + chH), paint)
                }
                if (ch > 32) {
                    mono.color = if (rev != isCursor) 0xFF0B0E0C.toInt() else if ((v and 0x200) != 0) 0xFFFFFFFF.toInt() else PHOSPHOR
                    glyph[0] = ch.toChar()
                    c.drawText(glyph, 0, 1, X(x), Y(y + chH * 0.8f), mono)
                }
                if ((v and 0x400) != 0) {
                    paint.color = PHOSPHOR
                    c.drawRect(X(x), Y(y + chH - 2f), X(x + cw), Y(y + chH - 0.5f), paint)
                }
            }
        }
        // glass
        paint.shader = RadialGradient(sr.centerX(), sr.centerY(), sr.width() * 0.7f, 0x00000000, 0x50000000, Shader.TileMode.CLAMP)
        c.drawRoundRect(sr, 26f * sc, 26f * sc, paint); paint.shader = null
        paint.color = 0x0EFFFFFF; c.drawRoundRect(rect(screenR.left + 30f, screenR.top + 12f, screenR.right - 300f, screenR.top + 90f), 30f * sc, 30f * sc, paint)
        // keyboard deck
        paint.color = 0xFFCAC3AF.toInt(); c.drawRoundRect(rect(34f, 612f, 1416f, 984f), 14f * sc, 14f * sc, paint)
        for (k in tkeys) drawTKey(c, k)
        postInvalidateDelayed(265)
    }

    private fun drawTKey(c: Canvas, k: TKey) {
        val held = heldKey === k || (k.code == SHIFT && shift) || (k.code == CTRL && ctrl)
        val r = RectF(k.x, k.y, k.x + k.w, k.y + 64f)
        paint.color = 0xFF3A3D40.toInt(); c.drawRoundRect(rect(r.left, r.top + 6f, r.right, r.bottom + 6f), 9f * sc, 9f * sc, paint)
        val dark = k.label.length > 1 && k.code != ' '.code
        var col = if (dark) 0xFF5E6266.toInt() else 0xFF8C9094.toInt()
        if (held) col = 0xFF4A4D50.toInt()
        paint.color = col
        val rr = rect(r).apply { if (held) offset(0f, 4f * sc) }
        c.drawRoundRect(rr, 9f * sc, 9f * sc, paint)
        paint.color = 0x22FFFFFF; c.drawRoundRect(RectF(rr.left + 6f * sc, rr.top + 5f * sc, rr.right - 6f * sc, rr.bottom - 12f * sc), 8f * sc, 8f * sc, paint)
        text.color = 0xFFF2F2F0.toInt()
        if (k.code == CAPS) {
            paint.color = if (caps) 0xFFFF4A2A.toInt() else 0xFF502018.toInt()
            c.drawCircle(rr.left + 16f * sc, rr.top + 16f * sc, 6f * sc, paint)
        }
        if (k.shiftLabel.isNotEmpty() && k.label.length == 1 && !k.label[0].isLetter()) {
            text.textSize = 24f * sc
            c.drawText(k.shiftLabel, rr.centerX(), rr.top + 26f * sc, text)
            c.drawText(k.label, rr.centerX(), rr.bottom - 14f * sc, text)
        } else {
            text.textSize = (if (k.label.length > 2) 20f else 30f) * sc
            c.drawText(k.label, rr.centerX(), rr.centerY() + text.textSize * 0.36f, text)
        }
    }

    // ================= right-hand rack =================
    private val rocker = RectF(1866f, 96f, 1940f, 226f)
    private val runStopR = RectF(1600f, 80f, 1680f, 160f)
    private val readyR = RectF(1694f, 80f, 1774f, 160f)
    private val safeR = RectF(1600f, 172f, 1680f, 252f)
    private val protR = RectF(1694f, 172f, 1774f, 252f)
    private val panelBtnR = RectF(1500f, 90f, 1940f, 250f)
    private val diskR = RectF(1470f, 330f, 1970f, 720f)
    private val menuR = RectF(1560f, 820f, 1880f, 920f)

    private fun drawRack(c: Canvas) {
        // blue cabinet
        paint.color = 0xFF4E8DC6.toInt(); c.drawRoundRect(rect(1450f, 10f, 1990f, 990f), 12f * sc, 12f * sc, paint)
        paint.color = 0xFF3F7AB2.toInt(); c.drawRect(rect(1466f, 26f, 1974f, 290f), paint)
        paint.color = 0xFF111214.toInt(); c.drawRect(rect(1476f, 36f, 1964f, 62f), paint)
        if (turnkey) {
            // control plate
            paint.color = 0xFFE7E1CF.toInt(); c.drawRoundRect(rect(1490f, 70f, 1956f, 262f), 6f * sc, 6f * sc, paint)
            // key lock
            paint.color = 0xFF1B1B1B.toInt(); c.drawRoundRect(rect(1508f, 104f, 1574f, 222f), 6f * sc, 6f * sc, paint)
            paint.color = 0xFFBFC2C6.toInt(); c.drawCircle(X(1541f), Y(163f), 22f * sc, paint)
            paint.color = 0xFF2A2A2A.toInt(); c.drawRect(rect(1538f, 147f, 1544f, 179f), paint)
            fun lamp(r: RectF, top: String, bottom: String, lit: Boolean) {
                paint.color = 0xFF404244.toInt(); c.drawRoundRect(rect(r.left - 4f, r.top - 4f, r.right + 4f, r.bottom + 4f), 6f * sc, 6f * sc, paint)
                paint.color = if (lit) 0xFFFFF4C8.toInt() else 0xFFE4E2DA.toInt()
                c.drawRoundRect(rect(r), 4f * sc, 4f * sc, paint)
                if (lit) { paint.color = 0x40FFE080; c.drawRoundRect(rect(r.left - 10f, r.top - 10f, r.right + 10f, r.bottom + 10f), 12f * sc, 12f * sc, paint) }
                text.color = 0xFF55585C.toInt(); text.textSize = 17f * sc; text.isFakeBoldText = true
                if (bottom.isEmpty()) c.drawText(top, X(r.centerX()), Y(r.centerY() + 6f), text)
                else {
                    c.drawText(top, X(r.centerX()), Y(r.centerY() - 6f), text)
                    stroke.color = 0xFF55585C.toInt(); stroke.strokeWidth = 1.5f * sc
                    c.drawLine(X(r.left + 16f), Y(r.centerY()), X(r.right - 16f), Y(r.centerY()), stroke)
                    c.drawText(bottom, X(r.centerX()), Y(r.centerY() + 20f), text)
                }
                text.isFakeBoldText = false
            }
            lamp(runStopR, "RUN", "STOP", running)
            lamp(readyR, "READY", "", running)
            lamp(safeR, "SAFE", "", !running)
            lamp(protR, "PROT", "PROT", stat[4] != 0)
            // power rocker (on)
            paint.color = 0xFF151515.toInt(); c.drawRoundRect(rect(rocker.left - 10f, rocker.top - 10f, rocker.right + 10f, rocker.bottom + 10f), 6f * sc, 6f * sc, paint)
            paint.color = 0xFFF4F2EC.toInt(); c.drawRoundRect(rect(rocker), 4f * sc, 4f * sc, paint)
            paint.color = 0xFFB02020.toInt(); c.drawRect(rect(rocker.left + 14f, rocker.top + 18f, rocker.right - 14f, rocker.top + 40f), paint)
            text.color = 0xFFF4F2EC.toInt(); text.textSize = 14f * sc
            c.drawText("ON", X(rocker.centerX()), Y(rocker.top + 34f), text)
            text.color = 0xFF404040.toInt(); text.textSize = 18f * sc
            c.drawText("OFF", X(rocker.centerX()), Y(rocker.bottom - 22f), text)
            text.color = 0xFF55585C.toInt(); text.textSize = 15f * sc
            c.drawText("POWER", X(1772f), Y(254f), text)
        } else {
            // back to the front panel
            paint.color = 0xFF16181B.toInt(); c.drawRoundRect(rect(panelBtnR), 10f * sc, 10f * sc, paint)
            for (i in 0 until 8) {
                val b = led[16 + 7 - i]
                paint.color = ledColor(b)
                c.drawCircle(X(1560f + i * 46f), Y(130f), 10f * sc, paint)
            }
            text.color = 0xFFE8EEF4.toInt(); text.textSize = 34f * sc; text.isFakeBoldText = true
            c.drawText("FRONT PANEL", X(panelBtnR.centerX()), Y(212f), text)
            text.isFakeBoldText = false
        }
        // dual disk unit
        paint.color = 0xFF3F7AB2.toInt(); c.drawRect(rect(1466f, 306f, 1974f, 736f), paint)
        paint.color = 0xFF1C1E22.toInt(); c.drawRoundRect(rect(diskR), 8f * sc, 8f * sc, paint)
        for (d in 0 until 2) {
            val t = 360f + d * 176f
            paint.color = 0xFF2C3036.toInt(); c.drawRoundRect(rect(1500f, t, 1940f, t + 140f), 6f * sc, 6f * sc, paint)
            paint.color = 0xFF050506.toInt(); c.drawRect(rect(1530f, t + 54f, 1880f, t + 70f), paint)
            // door lever
            paint.color = 0xFF9A9EA4.toInt(); c.drawRoundRect(rect(1680f, t + 82f, 1760f, t + 128f), 6f * sc, 6f * sc, paint)
            val lit = stat[3] != 0 && diskLabels[d] != null
            paint.color = if (lit) 0xFFFF3020.toInt() else 0xFF4A1410.toInt()
            c.drawCircle(X(1910f), Y(t + 30f), 9f * sc, paint)
            text.color = 0xFFE0E4EA.toInt(); text.textSize = 22f * sc
            text.textAlign = Paint.Align.LEFT
            val label = diskLabels[d] ?: "empty"
            c.drawText("${'A' + d}:  $label".take(28), X(1530f), Y(t + 38f), text)
            text.textAlign = Paint.Align.CENTER
        }
        // menu
        paint.color = 0xFF1C1E22.toInt(); c.drawRoundRect(rect(menuR), 12f * sc, 12f * sc, paint)
        text.color = 0xFFE8EEF4.toInt(); text.textSize = 40f * sc; text.isFakeBoldText = true
        c.drawText("MENU", X(menuR.centerX()), Y(menuR.centerY() + 14f), text)
        text.isFakeBoldText = false
    }

    // ================= front panel =================
    private fun ledX(i: Int) = 560f + i * 88f          // A15 at i = 0
    private val statusNames = arrayOf("INTE", "PROT", "MEMR", "INP", "MI", "OUT", "HLTA", "STACK", "WO", "INT")
    private val ctlTop = arrayOf("STOP", "SINGLE STEP", "EXAMINE", "DEPOSIT", "RESET", "PROTECT", "DISPLAY ACC", "INPUT ACC")
    private val ctlBottom = arrayOf("RUN", "SLOW", "EXAMINE NEXT", "DEPOSIT NEXT", "EXT CLR", "UNPROTECT", "LOAD ACC", "OUTPUT ACC")
    private val ctlBitsTop = intArrayOf(16, 18, 19, 21, 23, 25, 27, 29)
    private val ctlBitsBottom = intArrayOf(17, 18, 20, 22, 24, 26, 28, 30)
    private fun ctlX(i: Int) = 640f + i * 168f
    private val termBtnR = RectF(70f, 560f, 430f, 680f)
    private val diskBtnR = RectF(70f, 710f, 430f, 810f)
    private val pMenuR = RectF(70f, 840f, 430f, 940f)

    private fun ledColor(b: Int): Int {
        if (b <= 0) return 0xFF3A0C0A.toInt()
        val v = b.coerceIn(0, 255)
        return Color.rgb(58 + v * 197 / 255, 12 + v * 40 / 255, 10 + v * 20 / 255)
    }

    private fun drawLed(c: Canvas, x: Float, y: Float, b: Int) {
        paint.color = 0xFF0A0A0A.toInt(); c.drawCircle(X(x), Y(y), 17f * sc, paint)
        paint.color = ledColor(b); c.drawCircle(X(x), Y(y), 12f * sc, paint)
        if (b > 40) { paint.color = Color.argb(b / 4, 255, 60, 30); c.drawCircle(X(x), Y(y), 22f * sc, paint) }
    }

    private fun label(c: Canvas, s: String, x: Float, y: Float, size: Float = 20f) {
        text.color = 0xFFE9EEF2.toInt(); text.textSize = size * sc
        c.drawText(s, X(x), Y(y), text)
    }

    private fun drawPanel(c: Canvas) {
        paint.color = 0xFF101114.toInt(); c.drawRoundRect(rect(10f, 10f, 1990f, 990f), 16f * sc, 16f * sc, paint)
        paint.color = 0xFF2E6FB0.toInt(); c.drawRect(rect(10f, 92f, 1990f, 104f), paint)
        c.drawRect(rect(10f, 520f, 1990f, 528f), paint)
        // status LEDs (left), data LEDs over A7-A0
        for (i in 0 until 10) {
            val x = 62f + i * 48f
            drawLed(c, x, 210f, led[24 + i])
            label(c, statusNames[i], x, if (i % 2 == 0) 172f else 252f, 14f)
        }
        for (i in 0 until 8) {
            val x = ledX(8 + i)
            drawLed(c, x, 210f, led[16 + 7 - i])
            label(c, "D${7 - i}", x, 170f)
        }
        label(c, "DATA", ledX(11) + 44f, 132f, 22f)
        label(c, "STATUS", 278f, 132f, 22f)
        // wait / hlda and address
        drawLed(c, 90f, 380f, led[34]); label(c, "WAIT", 90f, 340f)
        drawLed(c, 190f, 380f, led[35]); label(c, "HLDA", 190f, 340f)
        for (i in 0 until 16) {
            val x = ledX(i)
            drawLed(c, x, 380f, led[15 - i])
            label(c, "A${15 - i}", x, 340f)
        }
        label(c, "ADDRESS", ledX(7) + 44f, 300f, 22f)
        // octal grouping lines over the switches
        stroke.color = 0xFF2E6FB0.toInt(); stroke.strokeWidth = 3f * sc
        for (g in 0 until 6) {
            val hi = 15 - g * 3
            val lo = maxOf(hi - 2, 0)
            val xl = ledX(15 - hi) - 30f; val xr = ledX(15 - lo) + 30f
            c.drawLine(X(xl), Y(410f), X(xr), Y(410f), stroke)
        }
        // address / data switches
        for (i in 0 until 16) {
            val bit = 15 - i
            drawToggle(c, ledX(i), 470f, (switches shr bit) and 1 != 0, (bit / 3) % 2 == 0)
            label(c, "$bit", ledX(i), 560f, 18f)
        }
        label(c, "SENSE SW.", ledX(3) + 44f, 600f, 18f)
        label(c, "DATA", ledX(11) + 44f, 600f, 18f)
        // control switches
        for (i in 0 until 8) {
            val x = ctlX(i)
            label(c, ctlTop[i], x, 700f, 17f)
            val up = momentary and (1 shl ctlBitsTop[i]) != 0
            val down = momentary and (1 shl ctlBitsBottom[i]) != 0 && ctlBitsTop[i] != ctlBitsBottom[i]
            drawRocker(c, x, 800f, if (up) -1 else if (down) 1 else 0)
            label(c, ctlBottom[i], x, 910f, 17f)
        }
        // buttons
        fun btn(r: RectF, s: String) {
            paint.color = 0xFF24272C.toInt(); c.drawRoundRect(rect(r), 12f * sc, 12f * sc, paint)
            stroke.color = 0xFF2E6FB0.toInt(); stroke.strokeWidth = 3f * sc; c.drawRoundRect(rect(r), 12f * sc, 12f * sc, stroke)
            text.color = 0xFFE9EEF2.toInt(); text.textSize = 34f * sc; text.isFakeBoldText = true
            c.drawText(s, X(r.centerX()), Y(r.centerY() + 12f), text)
            text.isFakeBoldText = false
        }
        btn(termBtnR, "TERMINAL")
        btn(diskBtnR, "DISKS")
        btn(pMenuR, "MENU")
        // disk light
        drawLed(c, 400f, 760f, if (stat[3] != 0) 255 else 0)
    }

    private fun drawToggle(c: Canvas, x: Float, y: Float, on: Boolean, blue: Boolean) {
        paint.color = 0xFF2A2C30.toInt(); c.drawRoundRect(rect(x - 26f, y - 46f, x + 26f, y + 46f), 6f * sc, 6f * sc, paint)
        paint.color = if (blue) 0xFF4E8DC6.toInt() else 0xFFE8E8E4.toInt()
        val t = if (on) y - 44f else y - 4f
        c.drawRoundRect(rect(x - 20f, t, x + 20f, t + 48f), 6f * sc, 6f * sc, paint)
        paint.color = 0x30000000; c.drawRect(rect(x - 20f, if (on) t + 36f else t, x + 20f, if (on) t + 48f else t + 12f), paint)
    }

    private fun drawRocker(c: Canvas, x: Float, y: Float, pos: Int) {
        paint.color = 0xFF2A2C30.toInt(); c.drawRoundRect(rect(x - 34f, y - 70f, x + 34f, y + 70f), 8f * sc, 8f * sc, paint)
        paint.color = 0xFFE8E8E4.toInt()
        val off = pos * 30f
        c.drawRoundRect(rect(x - 26f, y - 34f + off, x + 26f, y + 34f + off), 8f * sc, 8f * sc, paint)
        paint.color = 0x26000000
        c.drawRect(rect(x - 26f, y - 4f + off, x + 26f, y + 4f + off), paint)
    }

    // ================= touch =================
    private fun sendPanel() {
        onInput?.invoke(switches or momentary)
        invalidate()
    }

    private fun tap() = performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)

    private fun sendChar(code: Int) {
        when (code) {
            UP, DOWN, LEFT, RIGHT -> {
                if (vtMode == 0) {
                    onKey?.invoke(27); onKey?.invoke('['.code)
                    onKey?.invoke(when (code) { UP -> 'A'; DOWN -> 'B'; RIGHT -> 'C'; else -> 'D' }.code)
                } else onKey?.invoke(when (code) { UP -> 11; DOWN -> 10; RIGHT -> 12; else -> 8 })
            }
            else -> onKey?.invoke(code)
        }
    }

    private fun pressTKey(k: TKey) {
        tap()
        when (k.code) {
            SHIFT -> { shift = !shift; invalidate(); return }
            CTRL -> { ctrl = !ctrl; invalidate(); return }
            CAPS -> { caps = !caps; invalidate(); return }
        }
        var code = if (shift) k.shiftCode else k.code
        if (caps && code in 'a'.code..'z'.code) code -= 32
        if (ctrl && code in 0x40..0x7f) code = code and 0x1f
        sendChar(code)
        shift = false; ctrl = false
        heldKey = k
        invalidate()
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                val vx = (e.getX(i) - ox) / sc; val vy = (e.getY(i) - oy) / sc
                val id = e.getPointerId(i)
                if (showTerminal) touchTerminal(id, vx, vy) else touchPanel(id, vx, vy)
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> release(e.getPointerId(e.actionIndex))
            MotionEvent.ACTION_CANCEL -> { for (id in pointers.keys.toList()) release(id) }
        }
        return true
    }

    private fun release(id: Int) {
        when (val p = pointers.remove(id)) {
            is TKey -> if (heldKey === p) { heldKey = null; invalidate() }
            is Int -> { momentary = momentary and p.inv(); sendPanel() }
        }
    }

    private fun touchTerminal(id: Int, vx: Float, vy: Float) {
        for (k in tkeys) if (vx >= k.x - 4f && vx < k.x + k.w + 4f && vy >= k.y - 4f && vy < k.y + 68f) { pointers[id] = k; pressTKey(k); return }
        if (menuR.contains(vx, vy)) { onMenu?.invoke(); return }
        if (diskR.contains(vx, vy)) { onDisks?.invoke(); return }
        if (turnkey) {
            fun pulse(bit: Int) { tap(); val m = 1 shl bit; momentary = momentary or m; pointers[id] = m; sendPanel() }
            when {
                runStopR.contains(vx, vy) -> pulse(if (running) 16 else 17)
                readyR.contains(vx, vy) -> pulse(23)
                protR.contains(vx, vy) -> pulse(if (stat[4] != 0) 26 else 25)
                RectF(rocker.left - 20f, rocker.top - 20f, rocker.right + 20f, rocker.bottom + 20f).contains(vx, vy) -> { tap(); onPower?.invoke() }
            }
        } else if (panelBtnR.contains(vx, vy)) { tap(); showTerminal = false }
    }

    private fun touchPanel(id: Int, vx: Float, vy: Float) {
        if (termBtnR.contains(vx, vy)) { tap(); showTerminal = true; return }
        if (diskBtnR.contains(vx, vy)) { onDisks?.invoke(); return }
        if (pMenuR.contains(vx, vy)) { onMenu?.invoke(); return }
        if (vy in 410f..540f) {
            for (i in 0 until 16) if (kotlin.math.abs(vx - ledX(i)) < 44f) {
                tap(); switches = switches xor (1 shl (15 - i)); sendPanel(); return
            }
        }
        if (vy in 720f..890f) {
            for (i in 0 until 8) if (kotlin.math.abs(vx - ctlX(i)) < 80f) {
                tap()
                val bit = if (vy < 800f) ctlBitsTop[i] else ctlBitsBottom[i]
                val m = 1 shl bit
                momentary = momentary or m
                pointers[id] = m
                sendPanel()
                return
            }
        }
    }

    companion object {
        const val SHIFT = -10
        const val CTRL = -11
        const val CAPS = -12
        const val UP = -20
        const val DOWN = -21
        const val LEFT = -22
        const val RIGHT = -23
        private const val PHOSPHOR = 0xFFD9F2E4.toInt()
    }
}
