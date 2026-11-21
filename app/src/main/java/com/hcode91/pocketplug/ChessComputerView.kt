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
 *  - "almeria": a wooden board with an LED in each square's corner and the
 *    black 16/32 bit display module under it: the 2-line LCD and its six keys
 *    (ENT, CL and four arrows, in a diamond).
 *
 *  - "vcc": the black-rimmed brushed-gold plate of the (Advanced) Voice Chess
 *    Challenger: the board with marbled dark squares, and beside it the red
 *    4-digit LED display (FROM / TO) and the 4 x 4 keypad. The board has no
 *    sensors; moves are keyed in, pieces are moved by hand.
 *
 *  - "avrmax" / "atm18": the Elektor AVR-Max boards, a green PCB with the 4-digit
 *    red LED display (or the 4 x 20 LCD module) and the 11 tact switches; the board
 *    above is only for moving the pieces by hand.
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
    private val almeria = style == "almeria"
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
    private val modular1 = style == "modular1"
    private val keys = Array(18) { RectF() }
    // modular1 keypad: 3 rows of 6, left to right
    private val m1Bits = intArrayOf(0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x10000, 0x20000, 0x40, 0x80, 0x4000, 0x8000)
    private val lcdSeg = IntArray(32)
    private val arb = style == "arb"
    private val arbBits = intArrayOf(0x100, 0x200, 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01)
    private val arbLed = IntArray(12)
    private val vcc = style == "vcc"
    // RE, speaker, CL, EN / LV, DM, PB, PV / A1-D4 / E5-H8 (vcc.h input bits)
    private val vccBits = intArrayOf(0x10000, 0x10, 0x100, 0x1000, 0x2, 0x20, 0x200, 0x2000, 0x4, 0x40, 0x400, 0x4000, 0x8, 0x80, 0x800, 0x8000)
    private val elektorLcd = style == "atm18"
    private val elektor = style == "avrmax" || elektorLcd
    // A1 B2 C3 / D4 E5 F6 / G7 H8 - / FN CL GO (avrmax.h input bits)
    private val elkBits = intArrayOf(0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80, 0, 0x100, 0x200, 0x400)
    private val lcd4 = IntArray(640)
    private val modLeft = RectF(); private val modMid = RectF(); private val modRight = RectF()
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
        if (modular1 || vcc || elektor) for (i in 0 until 32) lcdSeg[i] = px[130 + i]
        if (elektorLcd && px.size >= 840) for (i in 0 until 640) lcd4[i] = px[200 + i]
        if (arb) for (i in 0 until 12) arbLed[i] = px[130 + i]
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
        } else if (arb) {
            // the board on the left, the label strip with keys and LEDs on the right
            val stripW = w * 0.2f
            val bw = min(w - stripW - w * 0.08f, (h - toolH) * 0.9f)
            sq = bw / 8f
            board.set(w * 0.05f, h * 0.03f + sq * 0.3f, w * 0.05f + bw, h * 0.03f + sq * 0.3f + bw)
            panel.set(board.right + sq * 0.35f, board.top - sq * 0.3f, w - w * 0.02f, board.bottom + sq * 0.3f)
            val kx = panel.left + panel.width() * 0.62f; val kr = sq * 0.13f
            val ys = floatArrayOf(0.13f, 0.20f, 0.28f, 0.33f, 0.38f, 0.43f, 0.48f, 0.53f, 0.61f, 0.66f)
            for (i in 0 until 10) {
                val cy = board.top + (board.height()) * ys[i]
                keys[i].set(kx - kr, cy - kr, kx + kr, cy + kr)
            }
        } else if (elektor) {
            // the hand board on top, the PCB under it: display on the left, 3 x 4 switches on the right
            val bw = min(w * 0.84f, (h - toolH) * 0.6f)
            sq = bw / 8f
            board.set((w - bw) / 2, h * 0.02f + sq * 0.3f, (w - bw) / 2 + bw, h * 0.02f + sq * 0.3f + bw)
            panel.set(w * 0.03f, board.bottom + sq * 0.55f, w * 0.97f, min(h - toolH - h * 0.015f, board.bottom + sq * 5.2f))
            lcdR.set(panel.left + panel.width() * 0.04f, panel.top + panel.height() * 0.3f, panel.left + panel.width() * 0.6f, panel.top + panel.height() * (if (elektorLcd) 0.62f else 0.66f))
            val kp = RectF(panel.left + panel.width() * 0.66f, panel.top + panel.height() * 0.08f, panel.right - panel.width() * 0.03f, panel.bottom - panel.height() * 0.06f)
            val kw = kp.width() / 3f; val kh = kp.height() / 4f
            val ks = min(kw, kh) * 0.36f
            for (i in 0 until 12) {
                val cx = kp.left + kw * (i % 3 + 0.5f); val cy = kp.top + kh * (i / 3 + 0.58f)
                keys[i].set(cx - ks, cy - ks, cx + ks, cy + ks)
            }
        } else if (vcc) {
            // board on top; under it the display and the keypad, side by side
            val bw = min(w * 0.88f, (h - toolH) * 0.64f)
            sq = bw / 8f
            board.set((w - bw) / 2, h * 0.02f + sq * 0.35f, (w - bw) / 2 + bw, h * 0.02f + sq * 0.35f + bw)
            panel.set(board.left - sq * 0.4f, board.bottom + sq * 0.55f, board.right + sq * 0.4f, board.bottom + sq * 4.1f)
            lcdR.set(panel.left + panel.width() * 0.05f, panel.top + panel.height() * 0.22f, panel.left + panel.width() * 0.43f, panel.top + panel.height() * 0.5f)
            val kp = RectF(panel.left + panel.width() * 0.5f, panel.top + panel.height() * 0.06f, panel.right - panel.width() * 0.04f, panel.bottom - panel.height() * 0.06f)
            val kw = kp.width() / 4f; val kh = kp.height() / 4f
            for (i in 0 until 16) {
                val cx = kp.left + kw * (i % 4 + 0.5f); val cy = kp.top + kh * (i / 4 + 0.5f)
                keys[i].set(cx - kw * 0.42f, cy - kh * 0.34f, cx + kw * 0.42f, cy + kh * 0.34f)
            }
        } else if (modular1) {
            val bw = min(w * 0.86f, (h - toolH) * 0.7f)
            sq = bw / 8f
            board.set((w - bw) / 2, h * 0.03f + sq * 0.35f, (w - bw) / 2 + bw, h * 0.03f + sq * 0.35f + bw)
            panel.set(board.left - sq * 0.6f, board.bottom + sq * 0.55f, board.right + sq * 0.6f, board.bottom + sq * 2.6f)
            val third = panel.width() / 3f
            modLeft.set(panel.left, panel.top, panel.left + third - 6f, panel.bottom)
            modMid.set(panel.left + third + 3f, panel.top, panel.left + 2 * third - 3f, panel.bottom)
            modRight.set(panel.left + 2 * third + 6f, panel.top, panel.right, panel.bottom)
            lcdR.set(modMid.centerX() - modMid.width() * 0.3f, modMid.top + modMid.height() * 0.28f, modMid.centerX() + modMid.width() * 0.3f, modMid.top + modMid.height() * 0.58f)
            val kp = RectF(modRight.left + modRight.width() * 0.08f, modRight.top + modRight.height() * 0.2f, modRight.right - modRight.width() * 0.06f, modRight.bottom - modRight.height() * 0.14f)
            val kw = kp.width() / 6f; val kh = kp.height() / 3f
            for (i in 0 until 18) {
                val cx = kp.left + kw * (i % 6 + 0.5f); val cy = kp.top + kh * (i / 6 + 0.5f)
                keys[i].set(cx - kw * 0.43f, cy - kh * 0.4f, cx + kw * 0.43f, cy + kh * 0.4f)
            }
        } else if (almeria) {
            val bw = min(w * 0.92f, (h - toolH) * 0.74f)
            sq = bw / 8f
            board.set((w - bw) / 2, h * 0.02f, (w - bw) / 2 + bw, h * 0.02f + bw)
            panel.set(board.left - sq * 0.3f, board.bottom + sq * 0.5f, board.right + sq * 0.3f, board.bottom + sq * 2.35f)
            lcdR.set(panel.left + panel.width() * 0.06f, panel.top + panel.height() * 0.3f, panel.left + panel.width() * 0.56f, panel.top + panel.height() * 0.72f)
            // diamond keypad: ENT, UP, CL on top; LEFT, RIGHT in the middle; DOWN at the bottom
            val kw = panel.width() * 0.085f; val kh = panel.height() * 0.22f
            val kx = panel.left + panel.width() * 0.79f; val ky = panel.top + panel.height() * 0.5f
            val pos = arrayOf(floatArrayOf(-1.15f, -1.15f), floatArrayOf(0f, -1.15f), floatArrayOf(1.15f, -1.15f),
                floatArrayOf(-1.15f, 0f), floatArrayOf(1.15f, 0f), floatArrayOf(0f, 1.15f))
            for (i in 0 until 8) {
                if (i >= 6) { keys[i].setEmpty(); continue }
                val cx = kx + pos[i][0] * kw; val cy = ky + pos[i][1] * kh
                keys[i].set(cx - kw * 0.45f, cy - kh * 0.45f, cx + kw * 0.45f, cy + kh * 0.45f)
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
        if (elektor) drawElektor(c) else if (vcc) drawVcc(c) else if (arb) drawArb(c) else if (accord) drawAccord(c) else if (almeria) drawAlmeria(c) else if (modular1) drawModular1(c) else drawAcademy(c)
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

    private fun drawLcd(c: Canvas, bg: Int, on: Int) {
        paint.color = bg; c.drawRect(lcdR, paint)
        val cw = lcdR.width() / (16 * 6 + 1); val ch = lcdR.height() / (2 * 9 + 1)
        for (line in 0 until 2) for (pos in 0 until 16) for (r in 0 until 8) {
            val bits = lcd[(line * 16 + pos) * 8 + r]
            for (col in 0 until 5) {
                val lit = (bits shr (4 - col)) and 1 != 0
                paint.color = if (lit) on else 0x14000000
                val x = lcdR.left + (1 + pos * 6 + col) * cw; val y = lcdR.top + (1 + line * 9 + r) * ch
                c.drawRect(x, y, x + cw * 0.9f, y + ch * 0.9f, paint)
            }
        }
    }

    private fun drawSevenSeg(c: Canvas, r: RectF, seg: Int, on: Int, off: Int) {
        val h = r.height(); val w = r.width(); val t = h * 0.11f
        val x0 = r.left + w * 0.12f; val x1 = r.right - w * 0.2f; val y0 = r.top; val ym = r.top + h / 2; val y1 = r.bottom
        val sk = w * 0.08f   // italic slant
        fun bar(ax: Float, ay: Float, bx: Float, by: Float, lit: Boolean) {
            paint.color = if (lit) on else off
            val dx = sk * (1 - (ay - y0) / h); val dx2 = sk * (1 - (by - y0) / h)
            c.drawRoundRect(minOf(ax + dx, bx + dx2), ay, maxOf(ax + dx, bx + dx2) + (if (ax == bx) t else 0f), by + (if (ay == by) t else 0f), t / 2, t / 2, paint)
        }
        val b = { i: Int -> seg shr i and 1 != 0 }
        bar(x0 + t, y0, x1 - t, y0, b(0))               // a
        bar(x1 - t, y0 + t, x1 - t, ym - t / 2, b(1))   // b
        bar(x1 - t, ym + t / 2, x1 - t, y1 - t, b(2))   // c
        bar(x0 + t, y1 - t, x1 - t, y1 - t, b(3))       // d
        bar(x0, ym + t / 2, x0, y1 - t, b(4))           // e
        bar(x0, y0 + t, x0, ym - t / 2, b(5))           // f
        bar(x0 + t, ym - t / 2, x1 - t, ym - t / 2, b(6)) // g
        paint.color = if (b(7)) on else off; c.drawCircle(r.right - w * 0.06f, y1 - t / 2, t * 0.6f, paint)
    }

    private fun drawArb(c: Canvas) {
        c.drawColor(0xFF2A1A12.toInt())
        // walnut frame
        paint.color = 0xFF6A3A22.toInt()
        c.drawRect(board.left - sq * 0.35f, board.top - sq * 0.35f, panel.right, board.bottom + sq * 0.35f, paint)
        paint.color = 0xFF5A311C.toInt(); c.drawRect(panel.left - sq * 0.05f, board.top - sq * 0.35f, panel.left, board.bottom + sq * 0.35f, paint)
        drawSquares(c, 0xFFE8CFA6.toInt(), 0xFF8A4A28.toInt())
        for (i in 0 until 64) {
            val x = i % 8; val y = i / 8
            led(c, board.left + (x + 0.88f) * sq, board.top + (7 - y + 0.88f) * sq, sq * 0.06f, squareLed[i])
        }
        drawPieces(c)
        // the strip: keys, their LEDs and labels
        val kx = keys[0].centerX(); val kr = keys[0].width() / 2
        text.textAlign = Paint.Align.LEFT; text.color = 0xFFF2E6D6.toInt(); text.textSize = sq * 0.15f
        val names = arrayOf("RESET", "HALT", "New Game", "Change Level", "Change Color", "Change Board", "Self Play", "Monitor", "Variable", "Hint")
        val nums = arrayOf("", "", "0", "1", "2", "3", "4", "5", "6 W", "B")
        for (i in 0 until 10) {
            val k = keys[i]
            val down = keyMask and arbBits[i] != 0
            paint.color = 0xFF1A1A1A.toInt(); c.drawCircle(k.centerX(), k.centerY(), kr * 1.15f, paint)
            paint.color = if (down) 0xFFB0A898.toInt() else 0xFFE8E2D4.toInt(); c.drawCircle(k.centerX(), k.centerY() + (if (down) 2f else 0f), kr, paint)
            if (i < 2) c.drawText(names[i], panel.left + panel.width() * 0.06f, k.centerY() + text.textSize * 0.35f, text)
            else {
                led(c, panel.left + panel.width() * 0.4f, k.centerY(), sq * 0.055f, arbLed[i - 2])
                c.drawText(nums[i], panel.left + panel.width() * 0.04f, k.centerY() + text.textSize * 0.35f, text)
                text.textSize = sq * 0.12f
                c.drawText(names[i], kx + kr * 1.6f, k.centerY() + text.textSize * 0.35f, text)
                text.textSize = sq * 0.15f
            }
        }
        // status LEDs: Your Move (green), Sargon's Move (yellow), Check, Change Board
        val st = arrayOf("Your Move", "Computer Move", "Check", "Change Board")
        val stc = arrayOf(0xFF40E040.toInt(), 0xFFFFD020.toInt(), 0xFFFF2A20.toInt(), 0xFFFF2A20.toInt())
        for (i in 0 until 4) {
            val cy = board.top + board.height() * (0.74f + i * 0.055f)
            val lx = panel.left + panel.width() * 0.4f
            val on = arbLed[8 + i].coerceIn(0, 255) / 255f
            paint.color = blend(Color.rgb(Color.red(stc[i]) / 7, Color.green(stc[i]) / 7, Color.blue(stc[i]) / 7), stc[i], on)
            c.drawCircle(lx, cy, sq * 0.055f, paint)
            text.textSize = sq * 0.12f; c.drawText(st[i], lx + sq * 0.15f, cy + text.textSize * 0.35f, text)
        }
        text.textAlign = Paint.Align.CENTER
    }

    private fun drawElektor(c: Canvas) {
        c.drawColor(0xFF2E3238.toInt())
        // a plain wooden board for moving the pieces
        paint.color = 0xFF5C3A1E.toInt(); c.drawRoundRect(board.left - sq * 0.3f, board.top - sq * 0.3f, board.right + sq * 0.3f, board.bottom + sq * 0.3f, sq * 0.1f, sq * 0.1f, paint)
        drawSquares(c, 0xFFE8D0A0.toInt(), 0xFF9C6234.toInt())
        drawPieces(c)
        // the green PCB with its traces and mounting holes
        paint.color = 0xFF16602E.toInt(); c.drawRoundRect(panel, sq * 0.08f, sq * 0.08f, paint)
        stroke.color = 0xFF2A8044.toInt(); stroke.strokeWidth = 3f
        for (i in 0 until 9) { val y = panel.top + panel.height() * (0.75f + i * 0.025f); c.drawLine(panel.left + panel.width() * 0.05f, y, panel.left + panel.width() * 0.6f, y, stroke) }
        paint.color = 0xFFC8C8C0.toInt()
        for ((hx, hy) in listOf(0.02f to 0.06f, 0.98f to 0.06f, 0.02f to 0.94f, 0.98f to 0.94f)) c.drawCircle(panel.left + panel.width() * hx, panel.top + panel.height() * hy, sq * 0.08f, paint)
        // the ATmega in its socket between display and switches
        paint.color = 0xFF1A1A1A.toInt()
        c.drawRect(panel.left + panel.width() * 0.61f, panel.top + panel.height() * 0.06f, panel.left + panel.width() * 0.645f, panel.bottom - panel.height() * 0.08f, paint)
        // a row of resistors under the display
        for (i in 0 until 8) {
            val x = panel.left + panel.width() * (0.1f + i * 0.055f); val y = panel.top + panel.height() * 0.86f
            paint.color = 0xFF8EB4D8.toInt(); c.drawRoundRect(x - sq * 0.07f, y - sq * 0.18f, x + sq * 0.07f, y + sq * 0.18f, sq * 0.05f, sq * 0.05f, paint)
        }
        if (elektorLcd) {
            // LCD module: dark frame, yellow-green panel, 4 x 20 characters
            paint.color = 0xFF0E0E0E.toInt(); c.drawRoundRect(lcdR.left - sq * 0.12f, lcdR.top - sq * 0.12f, lcdR.right + sq * 0.12f, lcdR.bottom + sq * 0.12f, sq * 0.05f, sq * 0.05f, paint)
            paint.color = 0xFF9DB040.toInt(); c.drawRect(lcdR, paint)
            val cw = lcdR.width() / (20 * 6 + 1); val ch = lcdR.height() / (4 * 9 + 1)
            for (line in 0 until 4) for (pos in 0 until 20) for (r in 0 until 8) {
                val bits = lcd4[(line * 20 + pos) * 8 + r]
                for (col in 0 until 5) {
                    paint.color = if ((bits shr (4 - col)) and 1 != 0) 0xFF1C2410.toInt() else 0x10000000
                    val x = lcdR.left + (1 + pos * 6 + col) * cw; val y = lcdR.top + (1 + line * 9 + r) * ch
                    c.drawRect(x, y, x + cw * 0.92f, y + ch * 0.92f, paint)
                }
            }
        } else {
            // four big red digits, each in its own grey block
            val dw = lcdR.width() / 4f
            for (d in 0 until 4) {
                val cell = RectF(lcdR.left + dw * d + 3f, lcdR.top, lcdR.left + dw * (d + 1) - 3f, lcdR.bottom)
                paint.color = 0xFF9A9A98.toInt(); c.drawRect(cell, paint)
                paint.color = 0xFF2A0C0C.toInt(); c.drawRect(cell.left + dw * 0.06f, cell.top + dw * 0.08f, cell.right - dw * 0.06f, cell.bottom - dw * 0.08f, paint)
                var seg = 0
                for (k in 0 until 8) if (lcdSeg[d * 8 + k] > 0) seg = seg or (1 shl k)
                drawSevenSeg(c, RectF(cell.left + dw * 0.14f, cell.top + dw * 0.16f, cell.right - dw * 0.06f, cell.bottom - dw * 0.16f), seg, 0xFFFF2A20.toInt(), 0xFF46100C.toInt())
            }
        }
        // the tact switches with their silk-screen labels
        val labels = arrayOf("A1", "B2", "C3", "D4", "E5", "F6", "G7", "H8", "", "FN", "CL", "GO")
        for (i in 0 until 12) {
            val k = keys[i]
            if (elkBits[i] == 0) continue
            val down = keyMask and elkBits[i] != 0
            text.color = 0xFFF0F0F0.toInt(); text.textSize = k.height() * 0.36f
            c.drawText(labels[i], k.centerX(), k.top - k.height() * 0.12f, text)
            paint.color = 0xFFB8B8B4.toInt(); c.drawRect(k, paint)
            paint.color = 0xFF8A8A86.toInt(); for ((px, py) in listOf(0.15f to 0.15f, 0.85f to 0.15f, 0.15f to 0.85f, 0.85f to 0.85f)) c.drawCircle(k.left + k.width() * px, k.top + k.height() * py, k.width() * 0.06f, paint)
            paint.color = if (down) 0xFF101010.toInt() else 0xFF3A2A22.toInt()
            c.drawCircle(k.centerX(), k.centerY(), k.width() * 0.3f, paint)
        }
    }

    private fun drawVcc(c: Canvas) {
        c.drawColor(0xFF3A2A1E.toInt())   // a wooden table
        // the black rim and the brushed gold plate
        val rim = RectF(panel.left - sq * 0.25f, board.top - sq * 0.6f, panel.right + sq * 0.25f, panel.bottom + sq * 0.3f)
        paint.color = 0xFF0E0E0E.toInt(); c.drawRoundRect(rim, sq * 0.35f, sq * 0.35f, paint)
        val plate = RectF(rim.left + sq * 0.2f, rim.top + sq * 0.2f, rim.right - sq * 0.2f, rim.bottom - sq * 0.2f)
        paint.shader = LinearGradient(plate.left, plate.top, plate.right, plate.bottom,
            intArrayOf(0xFFCBBB92.toInt(), 0xFFE2D5B2.toInt(), 0xFFBFAE84.toInt(), 0xFFD9CBA6.toInt()), null, Shader.TileMode.CLAMP)
        c.drawRoundRect(plate, sq * 0.12f, sq * 0.12f, paint)
        paint.shader = null
        stroke.color = 0x22000000; stroke.strokeWidth = 1f
        var y = plate.top + 3f
        while (y < plate.bottom) { c.drawLine(plate.left + 4f, y, plate.right - 4f, y, stroke); y += 5f }
        // light squares are the plate itself; dark squares black with a grey marbling
        val rnd = java.util.Random(7)
        for (yy in 0 until 8) for (x in 0 until 8) {
            if ((x + yy) % 2 == 1) continue
            val l = board.left + x * sq; val t = board.top + (7 - yy) * sq
            paint.color = 0xFF1A1A18.toInt(); c.drawRect(l, t, l + sq, t + sq, paint)
            paint.color = 0x55B8B2A0
            for (k in 0 until 14) {
                val px = l + rnd.nextFloat() * sq; val py = t + rnd.nextFloat() * sq
                c.drawCircle(px, py, sq * (0.02f + rnd.nextFloat() * 0.05f), paint)
            }
        }
        stroke.color = 0xFF2A2620.toInt(); stroke.strokeWidth = 2f; c.drawRect(board, stroke)
        drawPieces(c)
        text.color = 0xFF2A2620.toInt(); text.textSize = sq * 0.2f
        for (i in 0 until 8) {
            c.drawText("${i + 1}", board.left - sq * 0.2f, board.top + (7 - i + 0.58f) * sq, text)
            c.drawText("${i + 1}", board.right + sq * 0.2f, board.top + (7 - i + 0.58f) * sq, text)
            c.drawText("${'a' + i}", board.left + (i + 0.5f) * sq, board.bottom + sq * 0.3f, text)
        }
        // the red LED display in its black window, FROM / TO under it
        val win = RectF(lcdR.left - sq * 0.12f, lcdR.top - sq * 0.12f, lcdR.right + sq * 0.12f, lcdR.bottom + sq * 0.12f)
        paint.color = 0xFF0A0A0A.toInt(); c.drawRoundRect(win, sq * 0.06f, sq * 0.06f, paint)
        paint.color = 0xFF1C0606.toInt(); c.drawRect(lcdR, paint)
        val dw = lcdR.width() / 4.4f
        for (d in 0 until 4) {
            var seg = 0
            for (k in 0 until 7) if (lcdSeg[d * 8 + k] > 0) seg = seg or (1 shl k)
            val x0 = lcdR.left + dw * (d + (if (d >= 2) 0.4f else 0f))
            val cell = RectF(x0 + dw * 0.1f, lcdR.top + lcdR.height() * 0.14f, x0 + dw * 0.95f, lcdR.bottom - lcdR.height() * 0.14f)
            drawSevenSeg(c, cell, seg, 0xFFFF2A1C.toInt(), 0xFF3A0C0A.toInt())
        }
        // the two dots between the pairs (1.7 upper, 0.7 lower)
        val dx = lcdR.left + dw * 2.2f
        paint.color = if (lcdSeg[8 + 7] > 0) 0xFFFF2A1C.toInt() else 0xFF3A0C0A.toInt(); c.drawCircle(dx, lcdR.top + lcdR.height() * 0.36f, dw * 0.07f, paint)
        paint.color = if (lcdSeg[7] > 0) 0xFFFF2A1C.toInt() else 0xFF3A0C0A.toInt(); c.drawCircle(dx, lcdR.bottom - lcdR.height() * 0.2f, dw * 0.07f, paint)
        text.color = 0xFF2A2620.toInt(); text.textSize = sq * 0.2f
        c.drawText("FROM", lcdR.left + dw, win.bottom + sq * 0.3f, text)
        c.drawText("TO", lcdR.left + dw * 3.4f, win.bottom + sq * 0.3f, text)
        // the keypad: black keys with gold lettering
        val labels = arrayOf("RE", "", "CL", "EN", "LV", "DM", "PB", "PV", "A1", "b2", "C3", "d4", "E5", "F6", "G7", "H8")
        for (i in 0 until 16) {
            val k = keys[i]
            val down = keyMask and vccBits[i] != 0
            paint.color = 0xFF050505.toInt(); c.drawRoundRect(k.left - 2f, k.top - 2f, k.right + 2f, k.bottom + 4f, k.height() * 0.3f, k.height() * 0.3f, paint)
            paint.color = if (down) 0xFF0C0C0C.toInt() else 0xFF232323.toInt()
            val o = if (down) 2f else 0f
            c.drawRoundRect(k.left, k.top + o, k.right, k.bottom + o, k.height() * 0.3f, k.height() * 0.3f, paint)
            text.color = 0xFFE0C46A.toInt(); text.textSize = k.height() * 0.44f
            if (i == 1) {
                // speaker key: a speaker with sound waves
                val cx = k.centerX(); val cy = k.centerY() + o; val u = k.height() * 0.12f
                paint.color = 0xFFE0C46A.toInt()
                c.drawRect(cx - u * 2.6f, cy - u, cx - u * 1.6f, cy + u, paint)
                val path = Path().apply { moveTo(cx - u * 1.6f, cy - u); lineTo(cx - u * 0.2f, cy - u * 2.2f); lineTo(cx - u * 0.2f, cy + u * 2.2f); lineTo(cx - u * 1.6f, cy + u); close() }
                c.drawPath(path, paint)
                stroke.color = 0xFFE0C46A.toInt(); stroke.strokeWidth = u * 0.5f
                for (r in 1..2) c.drawArc(cx - u * r * 1.2f + u * 0.4f, cy - u * r * 1.2f, cx + u * r * 1.2f + u * 0.4f, cy + u * r * 1.2f, -45f, 90f, false, stroke)
            } else c.drawText(labels[i], k.centerX(), k.centerY() + o + text.textSize * 0.36f, text)
        }
    }

    private fun drawModular1(c: Canvas) {
        c.drawColor(0xFF6B4A2E.toInt())   // a wooden table
        // the silver board
        paint.color = 0xFFB8BABD.toInt()
        c.drawRoundRect(board.left - sq * 0.6f, board.top - sq * 0.45f, board.right + sq * 0.6f, board.bottom + sq * 0.6f, sq * 0.06f, sq * 0.06f, paint)
        paint.color = 0xFF2A2A2A.toInt(); c.drawRect(board.left - 3f, board.top - 3f, board.right + 3f, board.bottom + 3f, paint)
        drawSquares(c, 0xFFC4C6C8.toInt(), 0xFF2B2B2B.toInt())
        for (i in 0 until 64) {
            val x = i % 8; val y = i / 8
            led(c, board.left + (x + 0.88f) * sq, board.top + (7 - y + 0.88f) * sq, sq * 0.05f, squareLed[i])
        }
        drawPieces(c)
        text.color = 0xFF2A2A2A.toInt(); text.textSize = sq * 0.22f
        for (i in 0 until 8) {
            c.drawText("${i + 1}", board.left - sq * 0.3f, board.top + (7 - i + 0.58f) * sq, text)
            c.drawText("${i + 1}", board.right + sq * 0.3f, board.top + (7 - i + 0.58f) * sq, text)
            c.drawText("${'A' + i}", board.left + (i + 0.5f) * sq, board.bottom + sq * 0.38f, text)
        }
        // the three black modules
        for (m in listOf(modLeft, modMid, modRight)) {
            paint.color = 0xFF1A1816.toInt(); c.drawRoundRect(m, sq * 0.08f, sq * 0.08f, paint)
            paint.color = 0xFF2A2724.toInt()
            c.drawRoundRect(m.left + m.width() * 0.06f, m.top + m.height() * 0.08f, m.right - m.width() * 0.06f, m.bottom - m.height() * 0.1f, sq * 0.1f, sq * 0.1f, paint)
        }
        // module rails on the left one
        paint.color = 0xFF3A3632.toInt()
        for (i in 0 until 3) c.drawRect(modLeft.right - modLeft.width() * (0.3f - i * 0.06f), modLeft.top + modLeft.height() * 0.45f, modLeft.right - modLeft.width() * (0.27f - i * 0.06f), modLeft.bottom - modLeft.height() * 0.2f, paint)
        // the 4-digit LCD
        paint.color = 0xFF0C0C0C.toInt(); c.drawRoundRect(lcdR.left - 8f, lcdR.top - 8f, lcdR.right + 8f, lcdR.bottom + 8f, 6f, 6f, paint)
        paint.color = 0xFFB9C0A8.toInt(); c.drawRect(lcdR, paint)
        val dw = lcdR.width() / 4f
        for (d in 0 until 4) {
            var seg = 0
            for (k in 0 until 8) if (lcdSeg[d * 8 + k] != 0) seg = seg or (1 shl k)
            // digit 0 is the rightmost
            val cell = RectF(lcdR.left + dw * (3 - d) + dw * 0.12f, lcdR.top + lcdR.height() * 0.14f, lcdR.left + dw * (4 - d) - dw * 0.08f, lcdR.bottom - lcdR.height() * 0.14f)
            drawSevenSeg(c, cell, seg, 0xFF1C1F18.toInt(), 0x14000000)
        }
        // keypad
        val l1 = arrayOf("INFO", "POS", "LEV", "MEM", "CL", "ENT", "A", "B", "C", "D", "E", "F", "RES", "RES", "G", "H", "←", "→")
        val l2 = arrayOf("", "", "", "", "", "", "1", "♟2", "♞3", "♝4", "♜5", "♛6", "", "", "♚7", "8", "□9", "■0")
        for (i in 0 until 18) {
            val k = keys[i]
            val down = keyMask and m1Bits[i] != 0
            paint.color = if (down) 0xFF0A0A0A.toInt() else 0xFF161616.toInt()
            c.drawRoundRect(k, k.width() * 0.12f, k.width() * 0.12f, paint)
            stroke.color = 0xFF3C3C3C.toInt(); stroke.strokeWidth = 2f; c.drawRoundRect(k, k.width() * 0.12f, k.width() * 0.12f, stroke)
            text.color = 0xFFF0F0F0.toInt()
            if (l2[i].isEmpty()) { text.textSize = k.height() * 0.32f; c.drawText(l1[i], k.centerX(), k.centerY() + text.textSize * 0.36f, text) }
            else {
                text.textSize = k.height() * 0.3f; c.drawText(l1[i], k.centerX() - k.width() * 0.18f, k.top + k.height() * 0.4f, text)
                text.textSize = k.height() * 0.28f; c.drawText(l2[i], k.centerX() + k.width() * 0.12f, k.bottom - k.height() * 0.14f, text)
            }
        }
    }

    private fun drawAlmeria(c: Canvas) {
        c.drawColor(0xFF140E0A.toInt())
        // wooden board frame
        paint.color = 0xFF5C3418.toInt()
        c.drawRoundRect(board.left - sq * 0.4f, board.top - sq * 0.08f, board.right + sq * 0.4f, board.bottom + sq * 0.42f, sq * 0.1f, sq * 0.1f, paint)
        drawSquares(c, 0xFFE6CB98.toInt(), 0xFF8E5328.toInt())
        for (i in 0 until 64) {
            val x = i % 8; val y = i / 8
            led(c, board.left + (x + 0.88f) * sq, board.top + (7 - y + 0.88f) * sq, sq * 0.055f, squareLed[i])
        }
        drawPieces(c)
        text.color = 0xFFE8D2B0.toInt(); text.textSize = sq * 0.22f
        for (i in 0 until 8) {
            c.drawText("${i + 1}", board.left - sq * 0.2f, board.top + (7 - i + 0.55f) * sq, text)
            c.drawText("${'A' + i}", board.left + (i + 0.5f) * sq, board.bottom + sq * 0.3f, text)
        }
        // the black display module
        paint.color = 0xFF151515.toInt(); c.drawRoundRect(panel, sq * 0.12f, sq * 0.12f, paint)
        stroke.color = 0xFF2C2C2C.toInt(); stroke.strokeWidth = sq * 0.03f; c.drawRoundRect(panel, sq * 0.12f, sq * 0.12f, stroke)
        // recessed keypad area
        val pad = RectF(panel.left + panel.width() * 0.62f, panel.top + panel.height() * 0.1f, panel.right - panel.width() * 0.03f, panel.bottom - panel.height() * 0.1f)
        paint.color = 0xFF0C0C0C.toInt(); c.drawRoundRect(pad, sq * 0.08f, sq * 0.08f, paint)
        paint.color = 0xFF0A0A0A.toInt(); c.drawRoundRect(lcdR.left - 10f, lcdR.top - 10f, lcdR.right + 10f, lcdR.bottom + 10f, 6f, 6f, paint)
        drawLcd(c, 0xFFB4BBA6.toInt(), 0xFF1B1D18.toInt())
        // speaker slots beside the LCD
        paint.color = 0xFF050505.toInt()
        for (i in 0 until 5) {
            val sy = lcdR.top + i * lcdR.height() / 4.5f
            c.drawRoundRect(panel.left + panel.width() * 0.015f, sy, panel.left + panel.width() * 0.045f, sy + lcdR.height() * 0.08f, 3f, 3f, paint)
        }
        val labels = arrayOf("ENT", "↑", "CL", "←", "→", "↓")
        for (i in 0 until 6) {
            val k = keys[i]
            val down = keyMask and (1 shl i) != 0
            paint.color = when (i) {
                0 -> if (down) 0xFF0A5A2A.toInt() else 0xFF14924A.toInt()
                2 -> if (down) 0xFF7A1010.toInt() else 0xFFD02424.toInt()
                else -> if (down) 0xFF0E0E0E.toInt() else 0xFF2A2A2A.toInt()
            }
            c.drawRoundRect(k, k.width() * 0.12f, k.width() * 0.12f, paint)
            text.color = 0xFFF0F0F0.toInt()
            text.textSize = if (i == 0 || i == 2) k.height() * 0.4f else k.height() * 0.62f
            c.drawText(labels[i], k.centerX(), k.centerY() + text.textSize * 0.36f, text)
        }
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
        val n = if (modular1) 18 else if (vcc) 16 else if (elektor) 12 else if (arb) 10 else 8
        for (i in 0 until n) {
            val r = RectF(keys[i]).apply { if (modular1 || vcc) inset(-width() * 0.06f, -height() * 0.12f) else if (elektor) inset(-width() * 0.25f, -height() * 0.25f) else inset(-width() * 0.2f, -height() * 0.4f) }
            if (r.contains(x, y)) { val b = if (modular1) m1Bits[i] else if (vcc) vccBits[i] else if (elektor) elkBits[i] else if (arb) arbBits[i] else 1 shl i; if (b != 0) return b }
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
