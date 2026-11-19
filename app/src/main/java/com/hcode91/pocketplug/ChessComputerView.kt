package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import kotlin.math.min

/**
 * Tabletop chess computers: the board with its pieces and square LEDs, the
 * machine's own keys and displays, and a small board panel (pick up / put
 * down pieces, spawn new ones, remove, reset, clear) like MAME's sensorboard
 * interface. The core sends the board, LEDs and LCD in its frame (see the
 * cores' headers); taps go back as key codes (ChessBoardKey in chessboard.h).
 *
 *  - "academy": a wooden board with a red LED in each square's corner, and
 *    under it the brown panel with the 2-line LCD and 8 keys with LEDs.
 *  - "accord":  a grey board with LEDs beside the ranks and under the files,
 *    and the button strip on the right with its status LEDs.
 *
 * No maker's name or logo is drawn (the Academy's LCD shows what its ROM writes).
 */
class ChessComputerView(ctx: Context, private val style: String) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    var onBoard: ((Int) -> Unit)? = null      // a ChessBoardKey code
    var onMenu: (() -> Unit)? = null

    companion object {
        const val CB_SQUARE = 0x00
        const val CB_SPAWN = 0x40
        const val CB_REMOVE = 0x50
        const val CB_RESET = 0x51
        const val CB_CLEAR = 0x52
        // white pawn, knight, bishop, rook, queen, king (pieces 1-6; 7-12 black)
        private val GLYPH = arrayOf("", "♟", "♞", "♝", "♜", "♛", "♚")
    }

    private val accord = style == "accord"
    private val pieces = IntArray(64)
    private var hand = 0
    private var handPos = -1
    private val squareLed = IntArray(64)
    private val leds = IntArray(24)
    private val lcd = IntArray(256)
    private var keyMask = 0

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD) }
    private val pieceText = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER }

    // layout
    private val board = RectF()
    private var sq = 0f
    private val panel = RectF()
    private val keys = Array(8) { RectF() }
    private val spawn = Array(12) { RectF() }
    private val handR = RectF()
    private val removeR = RectF(); private val resetR = RectF(); private val clearR = RectF(); private val menuR = RectF()
    private val lcdR = RectF()
    private val pointers = HashMap<Int, Int>()   // pointer -> key bit

    fun updateFrame(px: IntArray) {
        if (px.size < 416) return
        for (i in 0 until 64) pieces[i] = px[i]
        hand = px[64]; handPos = px[65] - 1
        for (i in 0 until 64) squareLed[i] = px[66 + i]
        for (i in 0 until 24) leds[i] = px[130 + i]
        for (i in 0 until 256) lcd[i] = px[160 + i]
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        val toolH = min(w * 0.3f, h * 0.2f)
        if (accord) {
            // board on the left, the button strip on the right (as on the real one)
            val stripW = w * 0.17f
            val bw = min(w - stripW - w * 0.06f, h - toolH - h * 0.04f)
            sq = bw / 8f
            board.set(w * 0.05f, h * 0.02f, w * 0.05f + bw, h * 0.02f + bw)
            panel.set(board.right, board.top - sq * 0.15f, w.toFloat() - w * 0.01f, board.bottom + sq * 0.15f)
            for (i in 0 until 8) {
                val cy = board.top + sq * (i + 0.5f)
                keys[i].set(board.right + stripW * 0.12f, cy - sq * 0.22f, board.right + stripW * 0.68f, cy + sq * 0.22f)
            }
        } else {
            val bw = min(w * 0.94f, (h - toolH) * 0.72f)
            sq = bw / 8f
            board.set((w - bw) / 2, h * 0.015f, (w - bw) / 2 + bw, h * 0.015f + bw)
            panel.set(board.left - sq * 0.25f, board.bottom + sq * 0.45f, board.right + sq * 0.25f, board.bottom + sq * 2.2f)
            lcdR.set(panel.left + panel.width() * 0.03f, panel.top + panel.height() * 0.18f, panel.left + panel.width() * 0.4f, panel.top + panel.height() * 0.62f)
            val kx0 = panel.left + panel.width() * 0.45f; val kw = panel.width() * 0.52f / 8f
            for (i in 0 until 8) {
                val cx = kx0 + kw * (i + 0.5f)
                keys[i].set(cx - kw * 0.28f, panel.top + panel.height() * 0.3f, cx + kw * 0.28f, panel.top + panel.height() * 0.58f)
            }
        }
        // the board panel along the bottom: hand, spawn pieces, remove / reset / clear, menu
        val ty = h - toolH
        val cell = min(w / 13f, toolH / 2.3f)
        handR.set(w * 0.03f, ty + cell * 0.2f, w * 0.03f + cell * 1.6f, ty + cell * 1.8f)
        for (i in 0 until 12) {
            val col = i % 6; val row = i / 6
            val x0 = handR.right + cell * 0.5f + col * cell * 1.05f
            spawn[i].set(x0, ty + cell * 0.1f + row * cell * 1.05f, x0 + cell, ty + cell * 1.1f + row * cell * 1.05f)
        }
        val bx = spawn[5].right + cell * 0.4f; val bw2 = w - bx - w * 0.03f
        removeR.set(bx, ty + cell * 0.1f, bx + bw2 * 0.48f, ty + cell * 0.95f)
        menuR.set(bx + bw2 * 0.52f, ty + cell * 0.1f, bx + bw2, ty + cell * 0.95f)
        resetR.set(bx, ty + cell * 1.15f, bx + bw2 * 0.48f, ty + cell * 2.0f)
        clearR.set(bx + bw2 * 0.52f, ty + cell * 1.15f, bx + bw2, ty + cell * 2.0f)
    }

    // ---- drawing ----
    override fun onDraw(c: Canvas) {
        if (accord) drawAccord(c) else drawAcademy(c)
        drawBoardPanel(c)
    }

    private fun drawPiece(c: Canvas, p: Int, cx: Float, cy: Float, size: Float, alpha: Int = 255) {
        if (p !in 1..12) return
        val black = p > 6
        val g = GLYPH[if (black) p - 6 else p] + "︎"
        pieceText.textSize = size
        val y = cy + size * 0.35f
        pieceText.style = Paint.Style.FILL
        pieceText.color = if (black) Color.argb(alpha, 18, 18, 18) else Color.argb(alpha, 250, 250, 248)
        c.drawText(g, cx, y, pieceText)
        pieceText.style = Paint.Style.STROKE; pieceText.strokeWidth = size * 0.035f
        pieceText.color = if (black) Color.argb(alpha, 210, 210, 210) else Color.argb(alpha, 20, 20, 20)
        c.drawText(g, cx, y, pieceText)
        pieceText.style = Paint.Style.FILL
    }

    private fun drawSquares(c: Canvas, light: Int, dark: Int) {
        for (y in 0 until 8) for (x in 0 until 8) {
            val l = board.left + x * sq; val t = board.top + (7 - y) * sq
            paint.color = if ((x + y) % 2 == 1) light else dark
            c.drawRect(l, t, l + sq, t + sq, paint)
        }
    }

    private fun drawPieces(c: Canvas) {
        for (i in 0 until 64) {
            val p = pieces[i]; if (p == 0) continue
            val x = i % 8; val y = i / 8
            val cx = board.left + (x + 0.5f) * sq; val cy = board.top + (7 - y + 0.5f) * sq
            drawPiece(c, p, cx, cy, sq * 0.82f, if (i == handPos) 90 else 255)
        }
    }

    private fun led(c: Canvas, cx: Float, cy: Float, r: Float, level: Int, red: Boolean = true) {
        val on = level.coerceIn(0, 255)
        val base = if (red) Color.rgb(70, 8, 8) else Color.rgb(8, 50, 8)
        val lit = if (red) Color.rgb(255, 40, 30) else Color.rgb(60, 255, 60)
        paint.color = blend(base, lit, on / 255f)
        c.drawCircle(cx, cy, r, paint)
        if (on > 60) { paint.color = Color.argb(on / 3, Color.red(lit), Color.green(lit), Color.blue(lit)); c.drawCircle(cx, cy, r * 2.2f, paint) }
    }

    private fun blend(a: Int, b: Int, t: Float) = Color.rgb(
        (Color.red(a) + (Color.red(b) - Color.red(a)) * t).toInt(),
        (Color.green(a) + (Color.green(b) - Color.green(a)) * t).toInt(),
        (Color.blue(a) + (Color.blue(b) - Color.blue(a)) * t).toInt())

    private fun drawAcademy(c: Canvas) {
        c.drawColor(0xFF2A1A10.toInt())
        // wooden case around the board
        paint.color = 0xFF7A4520.toInt()
        c.drawRoundRect(board.left - sq * 0.35f, board.top - sq * 0.05f, board.right + sq * 0.35f, panel.bottom + sq * 0.1f, sq * 0.15f, sq * 0.15f, paint)
        drawSquares(c, 0xFFE3C48F.toInt(), 0xFF9A5A2C.toInt())
        // square LEDs in the bottom-right corner
        for (i in 0 until 64) {
            val x = i % 8; val y = i / 8
            led(c, board.left + (x + 0.88f) * sq, board.top + (7 - y + 0.88f) * sq, sq * 0.055f, squareLed[i])
        }
        drawPieces(c)
        text.color = 0xFFE8D2B0.toInt(); text.textSize = sq * 0.22f
        for (i in 0 until 8) {
            c.drawText("${i + 1}", board.left - sq * 0.17f, board.top + (7 - i + 0.55f) * sq, text)
            c.drawText("${'A' + i}", board.left + (i + 0.5f) * sq, board.bottom + sq * 0.3f, text)
        }
        // control panel: dark brown strip, LCD, keys
        paint.color = 0xFF4A2A14.toInt(); c.drawRoundRect(panel, sq * 0.1f, sq * 0.1f, paint)
        paint.color = 0xFF1E1A16.toInt(); c.drawRoundRect(lcdR.left - 6f, lcdR.top - 6f, lcdR.right + 6f, lcdR.bottom + 6f, 6f, 6f, paint)
        paint.color = 0xFFB9BDA8.toInt(); c.drawRect(lcdR, paint)
        // 2 x 16 characters of 5 x 8 dots
        val cw = lcdR.width() / (16 * 6 + 1); val ch = lcdR.height() / (2 * 9 + 1)
        for (line in 0 until 2) for (pos in 0 until 16) for (r in 0 until 8) {
            val bits = lcd[(line * 16 + pos) * 8 + r]
            for (col in 0 until 5) {
                val on = (bits shr (4 - col)) and 1 != 0
                paint.color = if (on) 0xFF1B1D18.toInt() else 0x18000000
                val x = lcdR.left + (1 + pos * 6 + col) * cw; val y = lcdR.top + (1 + line * 9 + r) * ch
                c.drawRect(x, y, x + cw * 0.9f, y + ch * 0.9f, paint)
            }
        }
        text.color = 0xFFD8C8B0.toInt(); text.textSize = panel.height() * 0.09f
        c.drawText("MULTI INFO LCD", lcdR.centerX(), lcdR.bottom + panel.height() * 0.2f, text)
        val names = arrayOf("TRAIN", "INFO", "MEM", "POS", "LEV", "FCT", "ENT", "CL")
        val icons = arrayOf("♟", "♞", "♝", "♜", "♛", "♚", "", "")
        for (i in 0 until 8) {
            val k = keys[i]
            val red = i >= 6
            led(c, k.centerX() - k.width() * 0.25f, k.top - panel.height() * 0.1f, k.width() * 0.13f, leds[i], true)
            led(c, k.centerX() + k.width() * 0.25f, k.top - panel.height() * 0.1f, k.width() * 0.13f, leds[8 + i], false)
            text.textSize = panel.height() * 0.09f; text.color = 0xFFE0D0B8.toInt()
            c.drawText(names[i], k.centerX(), k.top - panel.height() * 0.18f, text)
            val down = keyMask and (1 shl i) != 0
            paint.color = if (red) (if (down) 0xFF8A0A0A.toInt() else 0xFFD02020.toInt()) else (if (down) 0xFF101010.toInt() else 0xFF2E2E2E.toInt())
            c.drawRoundRect(k, k.width() * 0.15f, k.width() * 0.15f, paint)
            if (icons[i].isNotEmpty()) { pieceText.color = 0xFFE0D0B8.toInt(); pieceText.textSize = panel.height() * 0.16f; c.drawText(icons[i] + "︎", k.centerX(), k.bottom + panel.height() * 0.2f, pieceText) }
        }
        c.drawText("NEW GAME", (keys[6].centerX() + keys[7].centerX()) / 2, keys[7].bottom + panel.height() * 0.2f, text)
    }

    private fun drawAccord(c: Canvas) {
        c.drawColor(0xFF101010.toInt())
        paint.color = 0xFF1C1C1E.toInt()
        c.drawRect(board.left - sq * 0.45f, board.top - sq * 0.1f, board.right, board.bottom + sq * 0.5f, paint)
        drawSquares(c, 0xFFCFCECB.toInt(), 0xFF6A6966.toInt())
        drawPieces(c)
        // rank LEDs on the left, file LEDs underneath
        text.color = 0xFFCFCECB.toInt(); text.textSize = sq * 0.17f
        val side = arrayOf("Mate", "Store", "Check", "", "", "", "Error", "")
        for (i in 0 until 8) {
            val cy = board.top + (7 - i + 0.5f) * sq
            c.drawText("${i + 1}", board.left - sq * 0.25f, cy - sq * 0.12f, text)
            led(c, board.left - sq * 0.25f, cy + sq * 0.08f, sq * 0.06f, leds[8 + i])
            if (side[i].isNotEmpty()) { text.textSize = sq * 0.12f; c.drawText(side[i], board.left - sq * 0.25f, cy + sq * 0.32f, text); text.textSize = sq * 0.17f }
            val cx = board.left + (i + 0.5f) * sq
            c.drawText("${'A' + i}", cx - sq * 0.15f, board.bottom + sq * 0.33f, text)
            led(c, cx + sq * 0.1f, board.bottom + sq * 0.27f, sq * 0.06f, leds[16 + i])
        }
        // button strip: dark grey with diagonal hatching
        paint.color = 0xFF2B2B2D.toInt(); c.drawRect(panel, paint)
        stroke.color = 0xFF3A3A3D.toInt(); stroke.strokeWidth = 2f
        var x = panel.left - panel.height()
        while (x < panel.right) { c.drawLine(x, panel.bottom, x + panel.height(), panel.top, stroke); x += sq * 0.12f }
        val labels = arrayOf("GO", "TAKE BACK", "SOUND", "SET LEVEL", "NEW GAME", "SET UP", "VERIFY", "BLACK/WHITE")
        val icons = arrayOf("", "♚", "♛", "♝", "♞", "♜", "♟", "")
        for (i in 0 until 8) {
            val k = keys[i]
            val down = keyMask and (1 shl i) != 0
            paint.color = when (i) { 0 -> if (down) 0xFF8A2A10.toInt() else 0xFFD24A1E.toInt(); else -> if (down) 0xFF0E0E0E.toInt() else 0xFF3A3A3C.toInt() }
            c.drawRoundRect(k, k.height() / 2, k.height() / 2, paint)
            if (i == 7) {
                paint.color = 0xFFEDEDED.toInt(); c.drawRoundRect(k.left + 4, k.top + 4, k.centerX(), k.bottom - 4, k.height() / 2, k.height() / 2, paint)
            }
            if (icons[i].isNotEmpty()) { pieceText.color = 0xFFE8E8E8.toInt(); pieceText.textSize = k.height() * 0.8f; c.drawText(icons[i] + "︎", k.centerX(), k.centerY() + k.height() * 0.28f, pieceText) }
            text.color = 0xFFCFCECB.toInt(); text.textSize = sq * 0.1f
            c.drawText(labels[i], k.centerX(), k.bottom + sq * 0.13f, text)
        }
        // status LEDs at the right edge: Setup, Verify, Black (green), White (red)
        val st = arrayOf("White", "Black", "Verify", "Setup")
        for (i in 0 until 4) {
            val cy = board.top + (7 - i + 0.5f) * sq
            val lx = panel.right - panel.width() * 0.13f
            led(c, lx, cy, sq * 0.06f, leds[i], i == 0)
            text.textSize = sq * 0.1f; c.drawText(st[i], lx, cy - sq * 0.12f, text)
        }
    }

    private fun drawBoardPanel(c: Canvas) {
        paint.color = 0xFF1A1A1A.toInt()
        c.drawRect(0f, handR.top - handR.height() * 0.15f, width.toFloat(), height.toFloat(), paint)
        // hand
        paint.color = 0xFF3A3A3A.toInt(); c.drawRoundRect(handR, 8f, 8f, paint)
        text.color = 0xFFBBBBBB.toInt(); text.textSize = handR.height() * 0.2f
        c.drawText("HAND", handR.centerX(), handR.bottom - handR.height() * 0.06f, text)
        if (hand != 0) drawPiece(c, hand, handR.centerX(), handR.centerY() - handR.height() * 0.08f, handR.height() * 0.7f)
        for (i in 0 until 12) {
            val r = spawn[i]
            paint.color = if (i < 6) 0xFF5A5A5A.toInt() else 0xFFB0B0B0.toInt()
            c.drawRoundRect(r, 6f, 6f, paint)
            drawPiece(c, i + 1, r.centerX(), r.centerY(), r.height() * 0.85f)
        }
        for ((r, s) in listOf(removeR to "REMOVE", resetR to "RESET", clearR to "CLEAR", menuR to "MENU")) {
            paint.color = 0xFF3C3C3C.toInt(); c.drawRoundRect(r, 8f, 8f, paint)
            text.color = 0xFFE0E0E0.toInt(); text.textSize = r.height() * 0.36f
            c.drawText(s, r.centerX(), r.centerY() + text.textSize * 0.36f, text)
        }
    }

    // ---- touch ----
    private fun keyAt(x: Float, y: Float): Int {
        for (i in 0 until 8) {
            val r = RectF(keys[i]).apply { inset(-width() * 0.2f, -height() * 0.4f) }
            if (r.contains(x, y)) return 1 shl i
        }
        return 0
    }

    private fun boardCode(x: Float, y: Float): Int {
        if (board.contains(x, y)) {
            val fx = ((x - board.left) / sq).toInt().coerceIn(0, 7)
            val ry = 7 - ((y - board.top) / sq).toInt().coerceIn(0, 7)
            return CB_SQUARE + ry * 8 + fx
        }
        for (i in 0 until 12) if (spawn[i].contains(x, y)) return CB_SPAWN + i
        if (removeR.contains(x, y) || handR.contains(x, y)) return CB_REMOVE
        if (resetR.contains(x, y)) return CB_RESET
        if (clearR.contains(x, y)) return CB_CLEAR
        if (menuR.contains(x, y)) return -2
        return -1
    }

    private fun updateKeys() {
        var m = 0
        for (v in pointers.values) m = m or v
        if (m != keyMask) {
            if (m and keyMask.inv() != 0) performHapticFeedback(HapticFeedbackConstants.KEYBOARD_TAP)
            keyMask = m
            onInput?.invoke(m)
            invalidate()
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex; val x = e.getX(i); val y = e.getY(i)
                val k = keyAt(x, y)
                if (k != 0) { pointers[e.getPointerId(i)] = k; updateKeys() }
                else {
                    val code = boardCode(x, y)
                    if (code == -2) onMenu?.invoke()
                    else if (code >= 0) { performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY); onBoard?.invoke(code) }
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> { pointers.remove(e.getPointerId(e.actionIndex)); updateKeys() }
            MotionEvent.ACTION_CANCEL -> { pointers.clear(); updateKeys() }
        }
        return true
    }
}
