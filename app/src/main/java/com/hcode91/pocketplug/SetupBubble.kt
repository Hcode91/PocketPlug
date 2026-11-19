package com.hcode91.pocketplug

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.text.TextUtils
import android.util.TypedValue
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import kotlin.math.abs

/**
 * "Getting started" steps for machines that need more than loading the ROM.
 * Floats over the skin as a small card; closing it shrinks it to a round "?"
 * bubble (drag it out of the way, tap it to read the steps again). "Hide" puts
 * the bubble away for that machine; the emulator menu brings it back.
 *
 * Touches outside the card / bubble go through to the machine underneath.
 */
@SuppressLint("ViewConstructor")
class SetupBubble(ctx: Context, title: String, steps: List<String>, startOpen: Boolean,
                  private val onHide: () -> Unit) : FrameLayout(ctx) {

    private fun dp(v: Float) = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, resources.displayMetrics)

    private val card = LinearLayout(ctx)
    private val bubble = TextView(ctx)

    init {
        // the card
        card.orientation = LinearLayout.VERTICAL
        card.background = GradientDrawable().apply {
            cornerRadius = dp(16f)
            setColor(0xEE1F1D1B.toInt())
            setStroke(dp(1f).toInt(), 0x55FFFFFF)
        }
        card.elevation = dp(8f)
        val pad = dp(14f).toInt()
        card.setPadding(pad, dp(10f).toInt(), pad, dp(8f).toInt())

        val head = LinearLayout(ctx).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        head.addView(TextView(ctx).apply {
            text = "Getting started: $title"
            setTextColor(Color.WHITE)
            textSize = 15f
            typeface = Typeface.DEFAULT_BOLD
            maxLines = 2
            ellipsize = TextUtils.TruncateAt.END
        }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        head.addView(TextView(ctx).apply {
            text = "✕"
            setTextColor(0xFFDDDDDD.toInt())
            textSize = 20f
            setPadding(dp(12f).toInt(), 0, dp(2f).toInt(), 0)
            contentDescription = "Close"
            setOnClickListener { show(false) }
        })
        card.addView(head)

        val list = LinearLayout(ctx).apply { orientation = LinearLayout.VERTICAL }
        steps.forEachIndexed { i, s ->
            val row = LinearLayout(ctx).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, dp(6f).toInt(), 0, 0) }
            row.addView(TextView(ctx).apply {
                text = "${i + 1}"
                setTextColor(0xFF1F1D1B.toInt())
                textSize = 12f
                typeface = Typeface.DEFAULT_BOLD
                gravity = Gravity.CENTER
                background = GradientDrawable().apply { shape = GradientDrawable.OVAL; setColor(0xFFF2B33D.toInt()) }
            }, LinearLayout.LayoutParams(dp(20f).toInt(), dp(20f).toInt()).apply { rightMargin = dp(8f).toInt(); topMargin = dp(1f).toInt() })
            row.addView(TextView(ctx).apply {
                text = s
                setTextColor(0xFFEDEAE4.toInt())
                textSize = 13.5f
                setLineSpacing(0f, 1.1f)
            }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            list.addView(row)
        }
        val scroll = object : ScrollView(ctx) {
            override fun onMeasure(w: Int, h: Int) {
                // never taller than ~45% of the screen: the machine stays visible
                val max = (resources.displayMetrics.heightPixels * 0.45f).toInt()
                super.onMeasure(w, MeasureSpec.makeMeasureSpec(max, MeasureSpec.AT_MOST))
            }
        }
        scroll.addView(list)
        card.addView(scroll, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))

        card.addView(TextView(ctx).apply {
            text = "Hide for this machine (menu brings it back)"
            setTextColor(0xFFF2B33D.toInt())
            textSize = 12.5f
            gravity = Gravity.END
            setPadding(0, dp(10f).toInt(), 0, dp(4f).toInt())
            setOnClickListener { visibility = View.GONE; this@SetupBubble.visibility = View.GONE; onHide() }
        })
        card.isClickable = true   // the card itself swallows touches

        val cw = minOf((resources.displayMetrics.widthPixels * 0.86f).toInt(), dp(420f).toInt())
        addView(card, LayoutParams(cw, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP or Gravity.CENTER_HORIZONTAL).apply {
            topMargin = dp(36f).toInt()
        })

        // the "?" bubble
        bubble.text = "?"
        bubble.setTextColor(0xFF1F1D1B.toInt())
        bubble.textSize = 20f
        bubble.typeface = Typeface.DEFAULT_BOLD
        bubble.gravity = Gravity.CENTER
        bubble.contentDescription = "Getting started steps"
        bubble.background = GradientDrawable().apply {
            shape = GradientDrawable.OVAL
            setColor(0xE6F2B33D.toInt())
            setStroke(dp(2f).toInt(), 0xCC1F1D1B.toInt())
        }
        bubble.elevation = dp(6f)
        val bs = dp(42f).toInt()
        addView(bubble, LayoutParams(bs, bs, Gravity.TOP or Gravity.END).apply {
            topMargin = dp(36f).toInt(); rightMargin = dp(10f).toInt()
        })
        bubble.setOnTouchListener(DragOrTap(bubble) { show(true) })

        show(startOpen)
    }

    fun show(open: Boolean) {
        visibility = View.VISIBLE
        card.visibility = if (open) View.VISIBLE else View.GONE
        bubble.visibility = if (open) View.GONE else View.VISIBLE
    }

    /** Drag the bubble anywhere; a tap without moving opens the card. */
    private inner class DragOrTap(private val v: View, private val onTap: () -> Unit) : OnTouchListener {
        private var dx = 0f; private var dy = 0f; private var sx = 0f; private var sy = 0f
        private var moved = false
        @SuppressLint("ClickableViewAccessibility")
        override fun onTouch(view: View, e: MotionEvent): Boolean {
            when (e.actionMasked) {
                MotionEvent.ACTION_DOWN -> { sx = e.rawX; sy = e.rawY; dx = v.translationX; dy = v.translationY; moved = false }
                MotionEvent.ACTION_MOVE -> {
                    val mx = e.rawX - sx; val my = e.rawY - sy
                    if (abs(mx) > dp(6f) || abs(my) > dp(6f)) moved = true
                    if (moved) { v.translationX = dx + mx; v.translationY = dy + my }
                }
                MotionEvent.ACTION_UP -> if (!moved) onTap()
            }
            return true
        }
    }
}
