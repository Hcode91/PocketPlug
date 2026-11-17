package com.hcode91.pocketplug

import android.content.Context
import android.view.ViewGroup

/**
 * Stacks children vertically, each wanting (width * ratio) of height, and
 * centres the stack. If the screen is too short, everything is scaled down
 * evenly so the TV and the controller always both fit.
 */
class StackLayout(ctx: Context) : ViewGroup(ctx) {
    private val ratios = mutableListOf<Float>()

    fun add(v: android.view.View, heightRatio: Float) {
        ratios += heightRatio
        addView(v)
    }

    private fun heights(w: Int, h: Int): IntArray {
        val want = ratios.map { it * w }
        val scale = minOf(1f, h / want.sum())
        return want.map { (it * scale).toInt() }.toIntArray()
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val w = MeasureSpec.getSize(widthMeasureSpec)
        val h = MeasureSpec.getSize(heightMeasureSpec)
        val hs = heights(w, h)
        for (i in 0 until childCount)
            getChildAt(i).measure(MeasureSpec.makeMeasureSpec(w, MeasureSpec.EXACTLY), MeasureSpec.makeMeasureSpec(hs[i], MeasureSpec.EXACTLY))
        setMeasuredDimension(w, h)
    }

    override fun onLayout(changed: Boolean, l: Int, t: Int, r: Int, b: Int) {
        val w = r - l
        val hs = heights(w, b - t)
        var y = ((b - t) - hs.sum()) / 2
        for (i in 0 until childCount) {
            getChildAt(i).layout(0, y, w, y + hs[i])
            y += hs[i]
        }
    }
}
