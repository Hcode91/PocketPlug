package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.min

/**
 * The 1983 cream home computer: a TV on top, the memory console's front
 * below it (tapping it opens the disk and cartridge menu; the light shows
 * the disk drive working),
 * then either the keyboard (cream keys, the six dark "smart keys", the
 * cursor cluster) or the hand controller (joystick disc, two side buttons,
 * 12-key keypad). The strip's switch flips between them. No maker's name or
 * logo is drawn.
 *
 * Keys send the keyboard matrix code (row * 8 + bit of MAME's Y0-Y12); SHIFT
 * and CONTROL latch for the next key when tapped alone. The controller sends
 * input bits: up 1, down 2, left 4, right 8, left button 0x10, right button
 * 0x20, keypad 0-9 # * = bits 8-19.
 */
class AdamView(ctx: Context, private val srcW: Int, private val srcH: Int) : View(ctx) {

    var onKey: ((Int, Boolean) -> Unit)? = null
    var onPad: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var onMedia: (() -> Unit)? = null
    var showPad = false
        set(v) { field = v; requestLayout(); onSizeChanged(width, height, width, height); invalidate() }
    var mediaLabel: String? = null
        set(v) { field = v; postInvalidate() }
    var diskLight = false
        set(v) { if (v != field) { field = v; postInvalidate() } }

    private val frame = Bitmap.createBitmap(srcW, srcH, Bitmap.Config.ARGB_8888)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }
    private val smooth = Paint(Paint.FILTER_BITMAP_FLAG)

    private enum class Kind { CHAR, WIDE, SMART, MOD }
    private class Key(val label: String, val code: Int, val units: Float, val kind: Kind, val sub: String = "") {
        val r = RectF(); var down = false
    }

    private fun k(l: String, c: Int, u: Float = 1f, kind: Kind = Kind.CHAR, sub: String = "") = Key(l, c, u, kind, sub)
    private val gap = -1   // spacer
    private val rows: List<List<Key>> = listOf(
        listOf(k("ESCAPE", 57, 1.25f, Kind.WIDE, "WP"), k("WILD", 0, 1.25f, Kind.WIDE, "CARD"),
            k("I", 1, 1f, Kind.SMART), k("II", 2, 1f, Kind.SMART), k("III", 3, 1f, Kind.SMART),
            k("IV", 4, 1f, Kind.SMART), k("V", 5, 1f, Kind.SMART), k("VI", 6, 1f, Kind.SMART),
            k("UNDO", 7, 1.25f, Kind.WIDE), k("BACK", 68, 1.25f, Kind.WIDE, "SPACE")),
        listOf(k("1", 8, sub = "!"), k("2", 9, sub = "@"), k("3", 10, sub = "#"), k("4", 11, sub = "$"),
            k("5", 12, sub = "%"), k("6", 13, sub = "_"), k("7", 14, sub = "&"), k("8", 15, sub = "*"),
            k("9", 40, sub = "("), k("0", 41, sub = ")"), k("-", 42, sub = "`"), k("=", 43, sub = "+"), k("^", 44, sub = "~")),
        listOf(k("TAB", 16, 1.4f, Kind.WIDE), k("Q", 17), k("W", 18), k("E", 19), k("R", 20), k("T", 21), k("Y", 22),
            k("U", 23), k("I", 48), k("O", 49), k("P", 50), k("[", 51, sub = "{"), k("]", 52, sub = "}"), k("\\", 56, sub = "|")),
        listOf(k("CONTROL", 72, 1.7f, Kind.MOD), k("A", 24), k("S", 25), k("D", 26), k("F", 27), k("G", 28), k("H", 29),
            k("J", 30), k("K", 31), k("L", 45), k(";", 46, sub = ":"), k("'", 47, sub = "\""), k("RETURN", 55, 1.6f, Kind.WIDE)),
        listOf(k("SHIFT", 80, 2.1f, Kind.MOD), k("Z", 32), k("X", 33), k("C", 34), k("V", 35), k("B", 36), k("N", 37),
            k("M", 38), k(",", 39, sub = "<"), k(".", 53, sub = ">"), k("/", 54, sub = "?"), k("SHIFT", 80, 2.1f, Kind.MOD)),
        listOf(k("LOCK", 88, 1.6f, Kind.WIDE), k(" ", 60, 9f, Kind.CHAR)),
    )
    // the cluster on the right: editing keys and the arrows round HOME
    private val cluster: List<Key> = listOf(
        k("MOVE", 61, 1f, Kind.WIDE, "COPY"), k("STORE", 62, 1f, Kind.WIDE, "GET"), k("CLEAR", 63, 1f, Kind.WIDE),
        k("INSERT", 69, 1f, Kind.WIDE), k("PRINT", 70, 1f, Kind.WIDE), k("DELETE", 71, 1f, Kind.WIDE),
        k("↑", 64), k("←", 67), k("HOME", 96, 1f, Kind.WIDE), k("→", 65), k("↓", 66))
    private val allKeys = rows.flatten() + cluster
    private val latched = HashSet<Int>()

    // layout
    private val tv = RectF(); private val screen = RectF()
    private val strip = RectF(); private val bay1 = RectF(); private val bay2 = RectF(); private val slot = RectF()
    private val switchR = RectF(); private val menuR = RectF()
    private val board = RectF()
    private var unit = 0f
    // controller
    private val padBody = RectF()
    private val disc = PointF(); private var discR = 0f
    private val fireL = RectF(); private val fireR = RectF()
    private val keypad = Array(12) { RectF() }
    private val keypadLabel = arrayOf("1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#")
    private val keypadBit = intArrayOf(1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 0, 10)   // index into bits 8-19 (0-9, #, *)
    private var stickId = -1; private var stickDir = 0
    private val padPointers = HashMap<Int, Int>()
    private var padMask = 0

    private val pointers = HashMap<Int, Key>()

    fun updateFrame(px: IntArray) {
        frame.setPixels(px, 0, srcW, 0, 0, srcW, srcH)
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        if (w == 0 || h == 0) return
        val tvW = w * 0.96f
        val tvH = min(tvW * 0.78f, h * 0.42f)
        tv.set((w - tvW) / 2f, h * 0.01f, (w + tvW) / 2f, h * 0.01f + tvH)
        val sh = tvH * 0.86f; val sw = sh * srcW / srcH * 1.0f
        val sw2 = min(sw, tvW * 0.8f); val sh2 = sw2 * srcH / srcW
        screen.set(tv.left + tvW * 0.05f, tv.centerY() - sh2 / 2f, tv.left + tvW * 0.05f + sw2, tv.centerY() + sh2 / 2f)
        val sTop = tv.bottom + h * 0.01f
        strip.set(w * 0.02f, sTop, w * 0.98f, sTop + h * 0.075f)
        val bh = strip.height() * 0.62f
        bay1.set(strip.left + strip.width() * 0.04f, strip.centerY() - bh / 2f, strip.left + strip.width() * 0.25f, strip.centerY() + bh / 2f)
        bay2.set(bay1.right + strip.width() * 0.02f, bay1.top, bay1.right + strip.width() * 0.23f, bay1.bottom)
        slot.set(strip.left + strip.width() * 0.52f, strip.top + strip.height() * 0.2f, strip.left + strip.width() * 0.7f, strip.top + strip.height() * 0.4f)
        switchR.set(strip.left + strip.width() * 0.74f, strip.centerY() - bh * 0.32f, strip.left + strip.width() * 0.88f, strip.centerY() + bh * 0.32f)
        menuR.set(strip.left + strip.width() * 0.9f, strip.centerY() - bh * 0.32f, strip.right - strip.width() * 0.015f, strip.centerY() + bh * 0.32f)
        board.set(w * 0.01f, strip.bottom + h * 0.012f, w * 0.99f, h * 0.995f)
        if (showPad) layoutPad() else layoutKeys()
    }

    private fun layoutKeys() {
        val mainUnits = 13.2f
        val clusterUnits = 3.25f
        unit = min(board.width() / (mainUnits + clusterUnits + 0.6f), board.height() / (rows.size + 0.6f))
        val ox = board.left + (board.width() - unit * (mainUnits + clusterUnits + 0.3f)) / 2f
        val oy = board.top + (board.height() - unit * rows.size) / 2f
        for ((ri, r) in rows.withIndex()) {
            var x = ox + if (ri == 1) unit * 0.15f else if (ri == 5) unit * 1.8f else 0f
            for (key in r) {
                key.r.set(x + unit * 0.05f, oy + ri * unit + unit * 0.05f, x + key.units * unit - unit * 0.05f, oy + (ri + 1) * unit - unit * 0.05f)
                x += key.units * unit
            }
        }
        val cx = ox + (mainUnits + 0.3f) * unit
        val pos = listOf(0f to 0f, 1f to 0f, 2f to 0f, 0f to 1f, 1f to 1f, 2f to 1f, 1f to 2f, 0f to 3f, 1f to 3f, 2f to 3f, 1f to 4f)
        for ((i, key) in cluster.withIndex()) {
            val (px, py) = pos[i]
            val x = cx + px * unit * 1.08f; val y = oy + py * unit + (if (py >= 2f) unit * 0.4f else 0f)
            key.r.set(x + unit * 0.05f, y + unit * 0.05f, x + unit * 1.03f, y + unit * 0.95f)
        }
    }

    private fun layoutPad() {
        val pw = min(board.width() * 0.62f, board.height() * 0.55f)
        val ph = min(board.height() * 0.98f, pw * 1.9f)
        padBody.set(board.centerX() - pw / 2f, board.centerY() - ph / 2f, board.centerX() + pw / 2f, board.centerY() + ph / 2f)
        discR = pw * 0.3f
        disc.set(padBody.centerX(), padBody.top + ph * 0.2f)
        fireL.set(padBody.left - pw * 0.13f, padBody.top + ph * 0.1f, padBody.left + pw * 0.02f, padBody.top + ph * 0.3f)
        fireR.set(padBody.right - pw * 0.02f, padBody.top + ph * 0.1f, padBody.right + pw * 0.13f, padBody.top + ph * 0.3f)
        val kw = pw * 0.24f; val kh = ph * 0.1f
        val kx = padBody.centerX() - kw * 1.6f; val ky = padBody.top + ph * 0.44f
        for (i in 0 until 12) {
            val c = i % 3; val r = i / 3
            keypad[i].set(kx + c * kw * 1.1f, ky + r * kh * 1.25f, kx + c * kw * 1.1f + kw, ky + r * kh * 1.25f + kh)
        }
    }

    // ---- drawing ----
    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF201C18.toInt())
        // TV
        paint.color = 0xFF2A2724.toInt(); c.drawRoundRect(tv, tv.width() * 0.03f, tv.width() * 0.03f, paint)
        paint.color = 0xFF000000.toInt(); c.drawRoundRect(RectF(screen).apply { inset(-screen.width() * 0.015f, -screen.width() * 0.015f) }, 14f, 14f, paint)
        c.drawBitmap(frame, null, screen, smooth)
        val kx = tv.right - tv.width() * 0.07f
        for (i in 0 until 3) { paint.color = 0xFF55504A.toInt(); c.drawCircle(kx, tv.top + tv.height() * (0.25f + i * 0.2f), tv.width() * 0.022f, paint) }
        // console front: cream, two tape bays, cartridge slot, switch, menu
        paint.color = 0xFFE9E1CC.toInt(); c.drawRoundRect(strip, strip.height() * 0.15f, strip.height() * 0.15f, paint)
        paint.shader = LinearGradient(0f, strip.top, 0f, strip.bottom, 0x00000000, 0x22000000, Shader.TileMode.CLAMP)
        c.drawRoundRect(strip, strip.height() * 0.15f, strip.height() * 0.15f, paint); paint.shader = null
        for (b in listOf(bay1, bay2)) {
            paint.color = 0xFFD8CFB8.toInt(); c.drawRect(RectF(b).apply { inset(-b.height() * 0.08f, -b.height() * 0.08f) }, paint)
            paint.color = 0xFF2B2A28.toInt(); c.drawRect(b, paint)
            paint.color = 0x55FFFFFF; c.drawRect(b.left + b.width() * 0.3f, b.top + b.height() * 0.35f, b.right - b.width() * 0.3f, b.top + b.height() * 0.6f, paint)
        }
        // the disk drive's busy light
        paint.color = if (diskLight) 0xFFFF3B2E.toInt() else 0xFF5A2420.toInt()
        c.drawCircle(bay2.right + strip.width() * 0.02f, bay2.top + bay2.height() * 0.2f, strip.height() * 0.06f, paint)
        paint.color = 0xFF111111.toInt(); c.drawRect(slot, paint)
        // the rainbow stripe (no name)
        paint.shader = LinearGradient(slot.left, 0f, slot.right, 0f,
            intArrayOf(0xFFE53935.toInt(), 0xFFFFB300.toInt(), 0xFF43A047.toInt(), 0xFF1E88E5.toInt(), 0xFF8E24AA.toInt()), null, Shader.TileMode.CLAMP)
        c.drawRect(slot.left, slot.bottom + strip.height() * 0.12f, slot.right, slot.bottom + strip.height() * 0.2f, paint)
        paint.shader = null
        text.textSize = strip.height() * 0.16f; text.color = 0xFF3A3630.toInt()
        c.drawText(mediaLabel ?: "DISKS · CARTRIDGES", (bay1.left + slot.right) / 2f, strip.bottom - strip.height() * 0.05f, text)
        drawPlate(c, switchR, if (showPad) "KEYBOARD" else "CONTROLLER")
        drawPlate(c, menuR, "MENU")
        if (showPad) drawPad(c) else drawKeyboard(c)
    }

    private fun drawPlate(c: Canvas, r: RectF, label: String) {
        paint.color = 0xFF4A453E.toInt(); c.drawRoundRect(r, r.height() * 0.3f, r.height() * 0.3f, paint)
        text.color = 0xFFF0EAD8.toInt(); text.textSize = r.height() * 0.36f
        val s = if (text.measureText(label) > r.width() * 0.9f) r.height() * 0.36f * r.width() * 0.9f / text.measureText(label) else r.height() * 0.36f
        text.textSize = s
        c.drawText(label, r.centerX(), r.centerY() + s * 0.36f, text)
    }

    private fun drawKeyboard(c: Canvas) {
        paint.color = 0xFFE4DCC6.toInt(); c.drawRoundRect(board, unit * 0.3f, unit * 0.3f, paint)
        paint.color = 0xFFCFC6AE.toInt(); c.drawRect(board.left, board.bottom - unit * 0.18f, board.right, board.bottom, paint)
        for (key in allKeys) drawKey(c, key)
    }

    private fun drawKey(c: Canvas, key: Key) {
        val down = key.down || latched.contains(key.code)
        val r = RectF(key.r)
        val (top, side, ink) = when (key.kind) {
            Kind.SMART -> Triple(0xFF3E3D45.toInt(), 0xFF26252B.toInt(), 0xFFF2F2F2.toInt())
            Kind.WIDE, Kind.MOD -> Triple(0xFFD6CDB6.toInt(), 0xFFAFA690.toInt(), 0xFF3A3630.toInt())
            else -> Triple(0xFFF3EDDE.toInt(), 0xFFC8BFA8.toInt(), 0xFF2E2B27.toInt())
        }
        paint.color = side; c.drawRoundRect(r, unit * 0.1f, unit * 0.1f, paint)
        val face = RectF(r.left + unit * 0.05f, r.top + unit * (if (down) 0.07f else 0.02f), r.right - unit * 0.05f, r.bottom - unit * (if (down) 0.05f else 0.12f))
        paint.color = if (down) blend(top, 0xFF000000.toInt(), 0.15f) else top
        c.drawRoundRect(face, unit * 0.08f, unit * 0.08f, paint)
        text.color = ink
        if (key.kind == Kind.WIDE || key.kind == Kind.MOD) {
            text.textSize = unit * 0.2f
            if (key.sub.isNotEmpty()) {
                c.drawText(key.label, face.centerX(), face.centerY() - unit * 0.03f, text)
                c.drawText(key.sub, face.centerX(), face.centerY() + unit * 0.19f, text)
            } else c.drawText(key.label, face.centerX(), face.centerY() + unit * 0.07f, text)
        } else if (key.kind == Kind.SMART) {
            text.textSize = unit * 0.34f
            c.drawText(key.label, face.centerX(), face.centerY() + unit * 0.12f, text)
        } else {
            text.textSize = unit * 0.36f
            if (key.sub.isNotEmpty()) {
                text.textSize = unit * 0.26f
                c.drawText(key.sub, face.centerX(), face.centerY() - unit * 0.04f, text)
                c.drawText(key.label, face.centerX(), face.centerY() + unit * 0.24f, text)
            } else c.drawText(key.label, face.centerX(), face.centerY() + unit * 0.12f, text)
        }
    }

    private fun blend(a: Int, b: Int, t: Float): Int {
        fun ch(s: Int) = (((a shr s) and 0xff) * (1 - t) + ((b shr s) and 0xff) * t).toInt()
        return (0xff shl 24) or (ch(16) shl 16) or (ch(8) shl 8) or ch(0)
    }

    private fun drawPad(c: Canvas) {
        paint.color = 0xFF2A2724.toInt(); c.drawRoundRect(board, board.width() * 0.03f, board.width() * 0.03f, paint)
        // side buttons
        for ((r, bit) in listOf(fireL to 0x10, fireR to 0x20)) {
            paint.color = if (padMask and bit != 0) 0xFFA0322B.toInt() else 0xFFD8443A.toInt()
            c.drawRoundRect(r, r.width() * 0.3f, r.width() * 0.3f, paint)
        }
        // body
        paint.color = 0xFF1A1A1B.toInt(); c.drawRoundRect(padBody, padBody.width() * 0.12f, padBody.width() * 0.12f, paint)
        stroke.color = 0xFF3A3A3C.toInt(); stroke.strokeWidth = padBody.width() * 0.01f
        c.drawRoundRect(padBody, padBody.width() * 0.12f, padBody.width() * 0.12f, stroke)
        // joystick disc with a short knob
        paint.color = 0xFF0D0D0E.toInt(); c.drawCircle(disc.x, disc.y, discR, paint)
        stroke.color = 0xFF333336.toInt(); c.drawCircle(disc.x, disc.y, discR * 0.85f, stroke)
        var dx = 0f; var dy = 0f
        if (stickDir and 4 != 0) dx = -1f; if (stickDir and 8 != 0) dx = 1f
        if (stickDir and 1 != 0) dy = -1f; if (stickDir and 2 != 0) dy = 1f
        paint.shader = RadialGradient(disc.x + dx * discR * 0.2f - discR * 0.1f, disc.y + dy * discR * 0.2f - discR * 0.1f, discR * 0.5f,
            0xFF4A4A4E.toInt(), 0xFF141416.toInt(), Shader.TileMode.CLAMP)
        c.drawCircle(disc.x + dx * discR * 0.25f, disc.y + dy * discR * 0.25f, discR * 0.38f, paint); paint.shader = null
        // keypad
        for (i in 0 until 12) {
            val r = keypad[i]
            val down = padMask and (1 shl (8 + keypadBit[i])) != 0
            paint.color = if (down) 0xFF8E8A80.toInt() else 0xFFC9C4B8.toInt()
            c.drawRoundRect(r, r.height() * 0.25f, r.height() * 0.25f, paint)
            text.color = 0xFF1E1E1E.toInt(); text.textSize = r.height() * 0.55f
            c.drawText(keypadLabel[i], r.centerX(), r.centerY() + text.textSize * 0.36f, text)
        }
    }

    // ---- touch ----
    private fun keyAt(x: Float, y: Float): Key? = allKeys.firstOrNull { it.r.contains(x, y) }

    private fun isMod(k: Key) = k.code == 72 || k.code == 80

    private fun press(k: Key) {
        k.down = true
        if (isMod(k)) { if (!latched.contains(k.code)) onKey?.invoke(k.code, true); return }
        onKey?.invoke(k.code, true)
    }

    private fun release(k: Key) {
        k.down = false
        if (isMod(k)) {
            // tapped alone: stays down for the next key
            if (!latched.remove(k.code)) latched.add(k.code) else onKey?.invoke(k.code, false)
            return
        }
        onKey?.invoke(k.code, false)
        if (latched.isNotEmpty()) { for (m in latched) onKey?.invoke(m, false); latched.clear() }
    }

    private fun dirFor(x: Float, y: Float): Int {
        val dx = x - disc.x; val dy = y - disc.y
        if (hypot(dx, dy) < discR * 0.2f) return 0
        val a = Math.toDegrees(atan2(dy.toDouble(), dx.toDouble()))
        var b = 0
        if (a > -67.5 && a < 67.5) b = b or 8
        if (a > 112.5 || a < -112.5) b = b or 4
        if (a > 22.5 && a < 157.5) b = b or 2
        if (a < -22.5 && a > -157.5) b = b or 1
        return b
    }

    private fun sendPad() {
        var m = stickDir
        for (v in padPointers.values) m = m or v
        if (m != padMask) {
            if (m and padMask.inv() != 0) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            padMask = m; onPad?.invoke(m)
        }
        invalidate()
    }

    private fun padButton(x: Float, y: Float): Int {
        if (RectF(fireL).apply { inset(-width() * 0.3f, -height() * 0.2f) }.contains(x, y)) return 0x10
        if (RectF(fireR).apply { inset(-width() * 0.3f, -height() * 0.2f) }.contains(x, y)) return 0x20
        for (i in 0 until 12) if (RectF(keypad[i]).apply { inset(-width() * 0.05f, -height() * 0.12f) }.contains(x, y)) return 1 shl (8 + keypadBit[i])
        return 0
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex; val id = e.getPointerId(i); val x = e.getX(i); val y = e.getY(i)
                if (RectF(menuR).apply { inset(-width() * 0.1f, -height() * 0.4f) }.contains(x, y)) { onMenu?.invoke(); return true }
                if (RectF(switchR).apply { inset(-width() * 0.05f, -height() * 0.4f) }.contains(x, y)) {
                    performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); showPad = !showPad; return true
                }
                if (strip.contains(x, y)) { onMedia?.invoke(); return true }
                if (showPad) {
                    val b = padButton(x, y)
                    if (b != 0) { padPointers[id] = b; sendPad(); return true }
                    if (stickId < 0 && hypot(x - disc.x, y - disc.y) < discR * 1.6f) { stickId = id; stickDir = dirFor(x, y); sendPad() }
                    return true
                }
                val k = keyAt(x, y) ?: return true
                performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
                pointers[id] = k
                press(k)
            }
            MotionEvent.ACTION_MOVE -> {
                if (stickId >= 0) {
                    val i = e.findPointerIndex(stickId)
                    if (i >= 0) { val d = dirFor(e.getX(i), e.getY(i)); if (d != stickDir) { stickDir = d; sendPad() } }
                }
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val id = e.getPointerId(e.actionIndex)
                if (id == stickId) { stickId = -1; stickDir = 0; sendPad(); return true }
                if (padPointers.remove(id) != null) { sendPad(); return true }
                pointers.remove(id)?.let { release(it) }
            }
            MotionEvent.ACTION_CANCEL -> {
                for (k in pointers.values) release(k)
                pointers.clear(); padPointers.clear(); stickId = -1; stickDir = 0; sendPad()
            }
        }
        invalidate()
        return true
    }
}
