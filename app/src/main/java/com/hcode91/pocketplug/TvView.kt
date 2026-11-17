package com.hcode91.pocketplug

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RadialGradient
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.view.MotionEvent
import android.view.View

/**
 * A late-90s / early-2000s CRT television, the way these plug-and-play
 * systems were actually played. The emulated picture is shown at 4:3 on a
 * curved, scanlined screen.
 */
class TvView(ctx: Context, private val srcW: Int, private val srcH: Int) : View(ctx) {

    var onMenu: (() -> Unit)? = null
    var scanlines = true
    var powered = true

    private val frame = Bitmap.createBitmap(srcW, srcH, Bitmap.Config.ARGB_8888)
    private val frameLock = Any()

    private val cabinet = RectF()
    private val bezel = RectF()
    private val screen = RectF()
    private val panel = RectF()
    private val menuBtn = RectF()
    private val screenPath = Path()
    private val srcRect = Rect(0, 0, srcW, srcH)

    private val pCabinet = Paint(Paint.ANTI_ALIAS_FLAG)
    private val pBezel = Paint(Paint.ANTI_ALIAS_FLAG)
    private val pPicture = Paint(Paint.FILTER_BITMAP_FLAG)
    private val pScan = Paint().apply { color = 0x66000000; style = Paint.Style.STROKE }
    private val pVignette = Paint(Paint.ANTI_ALIAS_FLAG)
    private val pGlare = Paint(Paint.ANTI_ALIAS_FLAG)
    private val pWall = Paint()
    private val pText = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFFB9BCC6.toInt(); typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD_ITALIC); textAlign = Paint.Align.CENTER
    }
    private val pMisc = Paint(Paint.ANTI_ALIAS_FLAG)

    fun updateFrame(pixels: IntArray) {
        synchronized(frameLock) { frame.setPixels(pixels, 0, srcW, 0, 0, srcW, srcH) }
        postInvalidateOnAnimation()
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val pad = w * 0.04f
        // cabinet: screen (4:3) plus a control strip below it
        var cw = w - pad * 2
        var ch = cw * 0.86f
        if (ch > h - pad * 2) { ch = h - pad * 2; cw = ch / 0.86f }
        val cx = w / 2f
        val top = (h - ch) / 2f
        cabinet.set(cx - cw / 2, top, cx + cw / 2, top + ch)

        val border = cw * 0.055f
        val panelH = ch * 0.13f
        bezel.set(cabinet.left + border, cabinet.top + border, cabinet.right - border, cabinet.bottom - border - panelH)
        val inset = cw * 0.025f
        // keep the visible picture at 4:3 inside the bezel
        val maxW = bezel.width() - inset * 2
        val maxH = bezel.height() - inset * 2
        var sw = maxW
        var sh = sw * 3f / 4f
        if (sh > maxH) { sh = maxH; sw = sh * 4f / 3f }
        screen.set(bezel.centerX() - sw / 2, bezel.centerY() - sh / 2, bezel.centerX() + sw / 2, bezel.centerY() + sh / 2)
        val r = sw * 0.06f
        screenPath.reset()
        screenPath.addRoundRect(screen, r, r, Path.Direction.CW)

        panel.set(cabinet.left + border, bezel.bottom + border * 0.4f, cabinet.right - border, cabinet.bottom - border * 0.5f)
        val mh = panel.height() * 0.62f
        menuBtn.set(panel.right - mh * 3.2f, panel.centerY() - mh / 2, panel.right - mh * 0.6f, panel.centerY() + mh / 2)

        pWall.shader = RadialGradient(cx, h * 0.45f, w * 0.9f, 0xFF2B2140.toInt(), 0xFF0B0814.toInt(), Shader.TileMode.CLAMP)
        pCabinet.shader = LinearGradient(0f, cabinet.top, 0f, cabinet.bottom,
            intArrayOf(0xFF5A5D66.toInt(), 0xFF35373E.toInt(), 0xFF24252A.toInt()), floatArrayOf(0f, 0.5f, 1f), Shader.TileMode.CLAMP)
        pBezel.shader = LinearGradient(0f, bezel.top, 0f, bezel.bottom, 0xFF16171B.toInt(), 0xFF2A2B31.toInt(), Shader.TileMode.CLAMP)
        pVignette.shader = RadialGradient(screen.centerX(), screen.centerY(), sw * 0.62f,
            intArrayOf(0x00000000, 0x00000000, 0x99000000.toInt()), floatArrayOf(0f, 0.7f, 1f), Shader.TileMode.CLAMP)
        pGlare.shader = LinearGradient(screen.left, screen.top, screen.left + sw * 0.6f, screen.top + sh * 0.6f,
            0x22FFFFFF, 0x00FFFFFF, Shader.TileMode.CLAMP)
        pScan.strokeWidth = (sh / srcH) * 0.42f
        pText.textSize = panel.height() * 0.38f
    }

    override fun onDraw(c: Canvas) {
        c.drawRect(0f, 0f, width.toFloat(), height.toFloat(), pWall)

        // cabinet + soft shadow
        pMisc.color = 0x66000000
        c.drawRoundRect(cabinet.left + 6, cabinet.top + 14, cabinet.right + 6, cabinet.bottom + 14, cabinet.width() * 0.05f, cabinet.width() * 0.05f, pMisc)
        c.drawRoundRect(cabinet, cabinet.width() * 0.05f, cabinet.width() * 0.05f, pCabinet)
        pMisc.color = 0x30FFFFFF
        pMisc.style = Paint.Style.STROKE; pMisc.strokeWidth = 2f
        c.drawRoundRect(cabinet, cabinet.width() * 0.05f, cabinet.width() * 0.05f, pMisc)
        pMisc.style = Paint.Style.FILL
        c.drawRoundRect(bezel, bezel.width() * 0.04f, bezel.width() * 0.04f, pBezel)

        // picture
        c.save()
        c.clipPath(screenPath)
        if (powered) {
            synchronized(frameLock) { c.drawBitmap(frame, srcRect, screen, pPicture) }
            if (scanlines) {
                val step = screen.height() / srcH
                var y = screen.top + step * 0.75f
                while (y < screen.bottom) { c.drawLine(screen.left, y, screen.right, y, pScan); y += step }
            }
        } else {
            c.drawColor(0xFF101210.toInt())
        }
        c.drawRect(screen, pVignette)
        c.drawRect(screen, pGlare)
        c.restore()

        // control strip: brand, speaker grille, power LED, knobs, menu
        c.drawText("PocketPlug", panel.left + panel.width() * 0.17f, panel.centerY() + pText.textSize * 0.35f, pText)
        pMisc.color = 0xFF1B1C20.toInt()
        val gx0 = panel.left + panel.width() * 0.34f
        for (i in 0 until 6) {
            val x = gx0 + i * panel.height() * 0.22f
            c.drawRoundRect(x, panel.top + panel.height() * 0.25f, x + panel.height() * 0.09f, panel.bottom - panel.height() * 0.25f, 4f, 4f, pMisc)
        }
        val ledX = gx0 + 6 * panel.height() * 0.22f + panel.height() * 0.15f
        pMisc.color = if (powered) 0xFF4CFF7A.toInt() else 0xFF3A1010.toInt()
        c.drawCircle(ledX, panel.centerY(), panel.height() * 0.09f, pMisc)
        run {
            val kx = ledX + panel.height() * 0.6f
            pMisc.color = 0xFF17181C.toInt(); c.drawCircle(kx, panel.centerY(), panel.height() * 0.24f, pMisc)
            pMisc.color = 0xFF4A4C55.toInt(); c.drawCircle(kx, panel.centerY(), panel.height() * 0.19f, pMisc)
            pMisc.color = 0xFF9EA2AD.toInt(); c.drawRect(kx - 2, panel.centerY() - panel.height() * 0.17f, kx + 2, panel.centerY(), pMisc)
        }
        pMisc.color = 0xFF1B1C20.toInt()
        c.drawRoundRect(menuBtn, menuBtn.height() / 2, menuBtn.height() / 2, pMisc)
        val t = pText.textSize
        pText.textSize = menuBtn.height() * 0.5f
        pText.color = Color.WHITE
        c.drawText("MENU", menuBtn.centerX(), menuBtn.centerY() + pText.textSize * 0.36f, pText)
        pText.textSize = t
        pText.color = 0xFFB9BCC6.toInt()
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        if (e.actionMasked == MotionEvent.ACTION_UP) {
            val hit = RectF(menuBtn).apply { inset(-menuBtn.height() * 0.4f, -menuBtn.height() * 0.6f) }
            if (hit.contains(e.x, e.y)) onMenu?.invoke()
        }
        return true
    }
}
