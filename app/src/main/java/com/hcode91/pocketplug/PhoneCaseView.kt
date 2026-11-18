package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.*
import android.os.Handler
import android.os.Looper
import android.view.MotionEvent
import android.view.View
import kotlin.math.*

/**
 * Game-console phone case (the 168-in-1 type): a black textured case with the
 * colour screen at the top, a D-pad, red A/B buttons on a slanted panel, two
 * small round SELECT / START buttons, two slanted TURBO buttons, a speaker grille
 * and the camera bump (tap it for the menu).
 *
 * Input bits follow the VT369 controller: A 0x01, B 0x02, Select 0x04, Start 0x08,
 * Up 0x10, Down 0x20, Left 0x40, Right 0x80.
 */
class PhoneCaseView(ctx: Context) : View(ctx) {

    var onInput: ((Int) -> Unit)? = null
    var onMenu: (() -> Unit)? = null
    var externalMask = 0
        set(v) { field = v; postInvalidate() }

    private val frame = Bitmap.createBitmap(256, 240, Bitmap.Config.ARGB_8888)
    private val framePx = IntArray(256 * 240)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; color = 0xFF8A8D92.toInt() }
    private val nearest = Paint().apply { isFilterBitmap = false }

    private val case = RectF()
    private val screen = RectF()
    private val dpad = PointF(); private var dpadR = 0f
    private val btnA = PointF(); private val btnB = PointF(); private var abR = 0f
    private val btnSel = PointF(); private val btnStart = PointF(); private var smallR = 0f
    private val turboB = RectF(); private val turboA = RectF()
    private val camera = RectF()
    private val grille = RectF()
    private var texture: Bitmap? = null

    // pointer id -> held bits
    private val pointers = HashMap<Int, Int>()
    private var touchMask = 0
    private var turboMask = 0       // bits held through TURBO buttons
    private var turboPhase = false
    private val handler = Handler(Looper.getMainLooper())
    private val turboTick = object : Runnable {
        override fun run() {
            turboPhase = !turboPhase
            publish()
            if (turboMask != 0) handler.postDelayed(this, 50)
        }
    }

    fun updateFrame(px: IntArray) {
        if (px.size < 256 * 240) return
        System.arraycopy(px, 0, framePx, 0, 256 * 240)
        postInvalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {
        // case proportions roughly like a large phone (0.48 : 1)
        var cw = w * 0.96f
        var ch = cw / 0.48f
        if (ch > h * 0.98f) { ch = h * 0.98f; cw = ch * 0.48f }
        val l = (w - cw) / 2; val t = (h - ch) / 2
        case.set(l, t, l + cw, t + ch)
        val sw = cw * 0.84f
        val sh = sw * 240f / 256f
        screen.set(case.centerX() - sw / 2, t + cw * 0.12f, case.centerX() + sw / 2, t + cw * 0.12f + sh)
        val cy = screen.bottom + cw * 0.36f
        dpadR = cw * 0.17f
        dpad.set(l + cw * 0.27f, cy)
        abR = cw * 0.075f
        btnB.set(l + cw * 0.66f, cy + cw * 0.04f)
        btnA.set(l + cw * 0.84f, cy - cw * 0.05f)
        smallR = cw * 0.04f
        btnSel.set(l + cw * 0.52f, screen.bottom + cw * 0.13f)
        btnStart.set(l + cw * 0.64f, screen.bottom + cw * 0.13f)
        val py = cy + cw * 0.26f
        turboB.set(l + cw * 0.40f, py - cw * 0.035f, l + cw * 0.52f, py + cw * 0.035f)
        turboA.set(l + cw * 0.56f, py - cw * 0.035f, l + cw * 0.68f, py + cw * 0.035f)
        camera.set(l + cw * 0.58f, case.bottom - cw * 0.42f, case.right - cw * 0.06f, case.bottom - cw * 0.05f)
        grille.set(l + cw * 0.08f, case.bottom - cw * 0.30f, l + cw * 0.28f, case.bottom - cw * 0.10f)
        texture = makeTexture(max(8, (cw / 6).toInt()))
    }

    private fun makeTexture(size: Int): Bitmap {
        // fine diagonal weave like the case's matte finish
        val bm = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
        val c = Canvas(bm)
        c.drawColor(0xFF15161A.toInt())
        val p = Paint(Paint.ANTI_ALIAS_FLAG).apply { strokeWidth = 1f }
        var i = -size
        while (i < size * 2) {
            p.color = if ((i / 3) % 2 == 0) 0xFF1D1F24.toInt() else 0xFF111215.toInt()
            c.drawLine(i.toFloat(), 0f, (i + size).toFloat(), size.toFloat(), p)
            i += 3
        }
        return bm
    }

    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0814.toInt())
        val cw = case.width()
        val r = cw * 0.11f
        // case body
        paint.color = Color.WHITE
        texture?.let { paint.shader = BitmapShader(it, Shader.TileMode.REPEAT, Shader.TileMode.REPEAT) }
        c.drawRoundRect(case, r, r, paint)
        paint.shader = null
        stroke.color = 0xFF2C2E34.toInt(); stroke.strokeWidth = cw * 0.012f
        c.drawRoundRect(case, r, r, stroke)
        // side buttons
        paint.color = 0xFF202226.toInt()
        c.drawRoundRect(case.right - cw * 0.01f, case.top + cw * 0.55f, case.right + cw * 0.015f, case.top + cw * 0.75f, 6f, 6f, paint)

        // screen frame and picture
        val bz = cw * 0.035f
        paint.color = 0xFF050506.toInt()
        c.drawRoundRect(screen.left - bz, screen.top - bz, screen.right + bz, screen.bottom + bz, bz, bz, paint)
        stroke.color = 0xFF3A3C42.toInt(); stroke.strokeWidth = cw * 0.004f
        c.drawRoundRect(screen.left - bz, screen.top - bz, screen.right + bz, screen.bottom + bz, bz, bz, stroke)
        frame.setPixels(framePx, 0, 256, 0, 0, 256, 240)
        c.drawBitmap(frame, null, screen, nearest)
        paint.color = Color.WHITE
        paint.shader = LinearGradient(screen.left, screen.top, screen.right, screen.bottom, 0x18FFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP)
        c.drawRect(screen, paint); paint.shader = null

        val held = touchMask or externalMask or (if (turboPhase) turboMask else 0)
        drawDpad(c, held)
        // A / B on a slanted dark panel
        c.save()
        c.rotate(-25f, (btnA.x + btnB.x) / 2, (btnA.y + btnB.y) / 2)
        val pw = hypot(btnA.x - btnB.x, btnA.y - btnB.y) + abR * 2.8f
        paint.color = 0xFF2A2C31.toInt()
        val pcx = (btnA.x + btnB.x) / 2; val pcy = (btnA.y + btnB.y) / 2
        c.drawRoundRect(pcx - pw / 2, pcy - abR * 1.4f, pcx + pw / 2, pcy + abR * 1.4f, abR * 1.4f, abR * 1.4f, paint)
        c.restore()
        drawRound(c, btnB, abR, held and 0x02 != 0, 0xFFE0473C.toInt(), 0xFFA02A22.toInt())
        drawRound(c, btnA, abR, held and 0x01 != 0, 0xFFE0473C.toInt(), 0xFFA02A22.toInt())
        // small round SELECT / START
        drawRound(c, btnSel, smallR, held and 0x04 != 0, 0xFFC9CBD0.toInt(), 0xFF8E9096.toInt())
        drawRound(c, btnStart, smallR, held and 0x08 != 0, 0xFFC9CBD0.toInt(), 0xFF8E9096.toInt())
        text.textSize = cw * 0.026f
        c.drawText("SELECT", btnSel.x, btnSel.y + smallR * 2.3f, text)
        c.drawText("START", btnStart.x, btnStart.y + smallR * 2.3f, text)
        // slanted TURBO pills on a dark recess
        paint.color = 0xFF24262B.toInt()
        c.drawRoundRect(turboB.left - cw * 0.03f, turboB.top - cw * 0.03f, turboA.right + cw * 0.03f, turboA.bottom + cw * 0.03f, cw * 0.06f, cw * 0.06f, paint)
        drawPill(c, turboB, turboMask and 0x02 != 0)
        drawPill(c, turboA, turboMask and 0x01 != 0)
        c.drawText("TURBO B", turboB.centerX(), turboB.bottom + cw * 0.06f, text)
        c.drawText("TURBO A", turboA.centerX(), turboA.bottom + cw * 0.06f, text)
        drawGrille(c)
        drawCamera(c)
    }

    private fun drawRound(c: Canvas, p: PointF, r: Float, down: Boolean, top: Int, bottom: Int) {
        paint.color = 0x88000000.toInt()
        c.drawCircle(p.x, p.y + r * 0.12f, r * 1.06f, paint)
        paint.color = Color.WHITE
        paint.shader = RadialGradient(p.x - r * 0.3f, p.y - r * 0.35f, r * 1.4f,
            if (down) intArrayOf(bottom, bottom) else intArrayOf(top, bottom), null, Shader.TileMode.CLAMP)
        c.drawCircle(p.x, p.y + (if (down) r * 0.06f else 0f), r * (if (down) 0.95f else 1f), paint)
        paint.shader = null
    }

    private fun drawPill(c: Canvas, r: RectF, down: Boolean) {
        c.save()
        c.rotate(-25f, r.centerX(), r.centerY())
        paint.color = if (down) 0xFF8E9096.toInt() else 0xFFB8BBC1.toInt()
        c.drawRoundRect(r, r.height() / 2, r.height() / 2, paint)
        c.restore()
    }

    private fun drawDpad(c: Canvas, held: Int) {
        val r = dpadR; val arm = r * 0.36f
        paint.color = 0xFF08090B.toInt()
        c.drawCircle(dpad.x, dpad.y, r * 1.08f, paint)
        paint.color = 0xFF1E2024.toInt()
        c.drawRoundRect(dpad.x - arm, dpad.y - r, dpad.x + arm, dpad.y + r, arm * 0.3f, arm * 0.3f, paint)
        c.drawRoundRect(dpad.x - r, dpad.y - arm, dpad.x + r, dpad.y + arm, arm * 0.3f, arm * 0.3f, paint)
        // pressed arms light up slightly
        paint.color = 0xFF34373D.toInt()
        if (held and 0x10 != 0) c.drawRect(dpad.x - arm, dpad.y - r, dpad.x + arm, dpad.y - arm, paint)
        if (held and 0x20 != 0) c.drawRect(dpad.x - arm, dpad.y + arm, dpad.x + arm, dpad.y + r, paint)
        if (held and 0x40 != 0) c.drawRect(dpad.x - r, dpad.y - arm, dpad.x - arm, dpad.y + arm, paint)
        if (held and 0x80 != 0) c.drawRect(dpad.x + arm, dpad.y - arm, dpad.x + r, dpad.y + arm, paint)
        // arrows and the centre dimple
        paint.color = 0xFF3D4047.toInt()
        val a = arm * 0.45f
        fun tri(x: Float, y: Float, dx: Float, dy: Float) {
            val p = Path(); p.moveTo(x + dx * a, y + dy * a)
            p.lineTo(x - dy * a * 0.8f, y + dx * a * 0.8f); p.lineTo(x + dy * a * 0.8f, y - dx * a * 0.8f); p.close()
            c.drawPath(p, paint)
        }
        tri(dpad.x, dpad.y - r * 0.68f, 0f, -1f); tri(dpad.x, dpad.y + r * 0.68f, 0f, 1f)
        tri(dpad.x - r * 0.68f, dpad.y, -1f, 0f); tri(dpad.x + r * 0.68f, dpad.y, 1f, 0f)
        stroke.color = 0xFF34373D.toInt(); stroke.strokeWidth = arm * 0.12f
        c.drawCircle(dpad.x, dpad.y, arm * 0.55f, stroke)
    }

    private fun drawGrille(c: Canvas) {
        stroke.color = 0xFF050506.toInt(); stroke.strokeWidth = grille.width() * 0.07f
        stroke.strokeCap = Paint.Cap.ROUND
        for (k in 0 until 7) {
            val f = k / 6f
            val x0 = grille.left + grille.width() * f * 0.7f
            c.drawLine(x0, grille.bottom - grille.height() * f * 0.3f, x0 + grille.width() * 0.3f, grille.top + grille.height() * (1 - f) * 0.3f, stroke)
        }
        stroke.strokeCap = Paint.Cap.BUTT
    }

    private fun drawCamera(c: Canvas) {
        val r = camera.width() * 0.2f
        paint.color = 0xFF0A0B0D.toInt()
        c.drawRoundRect(camera, r, r, paint)
        paint.color = 0xFF8FA6C9.toInt()
        val inner = RectF(camera.left + camera.width() * 0.06f, camera.top + camera.height() * 0.06f, camera.right - camera.width() * 0.06f, camera.bottom - camera.height() * 0.06f)
        c.drawRoundRect(inner, r * 0.8f, r * 0.8f, paint)
        val lr = camera.width() * 0.2f
        for ((fx, fy) in listOf(0.3f to 0.3f, 0.7f to 0.5f, 0.3f to 0.72f)) {
            val x = camera.left + camera.width() * fx; val y = camera.top + camera.height() * fy
            paint.color = 0xFF2B2E33.toInt(); c.drawCircle(x, y, lr * 1.12f, paint)
            paint.color = Color.WHITE
            paint.shader = RadialGradient(x - lr * 0.3f, y - lr * 0.3f, lr, intArrayOf(0xFFD7D9DD.toInt(), 0xFF5C6068.toInt(), 0xFF1A1C20.toInt()), floatArrayOf(0f, 0.5f, 1f), Shader.TileMode.CLAMP)
            c.drawCircle(x, y, lr, paint); paint.shader = null
        }
        paint.color = 0xFFE9E6D6.toInt()
        c.drawCircle(camera.left + camera.width() * 0.72f, camera.top + camera.height() * 0.2f, lr * 0.25f, paint)
    }

    // ---- touch ----
    private fun hit(x: Float, y: Float): Int {
        val ddx = x - dpad.x; val ddy = y - dpad.y
        if (hypot(ddx, ddy) < dpadR * 1.25f) {
            if (hypot(ddx, ddy) < dpadR * 0.18f) return 0
            val ang = Math.toDegrees(atan2(ddy.toDouble(), ddx.toDouble()))
            var m = 0
            if (ang > -157.5 && ang < -22.5) m = m or 0x10
            if (ang > 22.5 && ang < 157.5) m = m or 0x20
            if (ang > 112.5 || ang < -112.5) m = m or 0x40
            if (ang > -67.5 && ang < 67.5) m = m or 0x80
            return m
        }
        if (hypot(x - btnA.x, y - btnA.y) < abR * 1.5f) return 0x01
        if (hypot(x - btnB.x, y - btnB.y) < abR * 1.5f) return 0x02
        if (hypot(x - btnSel.x, y - btnSel.y) < smallR * 2.2f) return 0x04
        if (hypot(x - btnStart.x, y - btnStart.y) < smallR * 2.2f) return 0x08
        if (RectF(turboB).apply { inset(-turboB.width() * 0.2f, -turboB.height()) }.contains(x, y)) return 0x200
        if (RectF(turboA).apply { inset(-turboA.width() * 0.2f, -turboA.height()) }.contains(x, y)) return 0x100
        return 0
    }

    private fun publish() {
        val m = touchMask or (if (turboPhase) turboMask else 0)
        onInput?.invoke(m)
        invalidate()
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = e.actionIndex
                val x = e.getX(i); val y = e.getY(i)
                if (camera.contains(x, y) && e.actionMasked == MotionEvent.ACTION_DOWN) { onMenu?.invoke(); return true }
                val b = hit(x, y)
                if (b != 0) performHapticFeedback(android.view.HapticFeedbackConstants.VIRTUAL_KEY)
                pointers[e.getPointerId(i)] = b
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until e.pointerCount) {
                    val id = e.getPointerId(i)
                    val old = pointers[id] ?: continue
                    // only the D-pad slides between directions
                    if (old and 0xf0 != 0 || old == 0) {
                        val b = hit(e.getX(i), e.getY(i))
                        if (b and 0xf0 != 0 || b == 0) pointers[id] = b
                    }
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> pointers.remove(e.getPointerId(e.actionIndex))
            MotionEvent.ACTION_CANCEL -> pointers.clear()
        }
        var m = 0; var turbo = 0
        for (v in pointers.values) {
            if (v and 0x100 != 0) turbo = turbo or 0x01
            if (v and 0x200 != 0) turbo = turbo or 0x02
            m = m or (v and 0xff)
        }
        val turboWas = turboMask
        touchMask = m; turboMask = turbo
        if (turbo != 0 && turboWas == 0) { turboPhase = true; handler.removeCallbacks(turboTick); handler.postDelayed(turboTick, 50) }
        publish()
        return true
    }
}
