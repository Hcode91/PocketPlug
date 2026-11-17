package com.hcode91.pocketplug

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import kotlin.concurrent.thread

/** Machine library: one card per supported plug-and-play system. */
class MainActivity : Activity() {

    private lateinit var list: LinearLayout
    private var pendingImport: MachineDef? = null

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = BG
        window.navigationBarColor = BG

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(BG)
            setPadding(dp(20), dp(28), dp(20), dp(12))
        }
        root.addView(TextView(this).apply {
            text = "PocketPlug"
            textSize = 34f
            typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD_ITALIC)
            setTextColor(Color.WHITE)
            setShadowLayer(18f, 0f, 0f, ACCENT)
        })
        root.addView(TextView(this).apply {
            text = "Plug & play TV games"
            textSize = 15f
            setTextColor(MUTED)
            setPadding(0, 0, 0, dp(18))
        })

        list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        root.addView(ScrollView(this).apply { addView(list) },
            LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f))
        root.addView(TextView(this).apply {
            text = "You need your own dumps of the systems' ROMs (MAME sets work: .zip, .7z or the raw .bin)."
            textSize = 12f
            setTextColor(MUTED)
            setPadding(0, dp(8), 0, 0)
        })
        setContentView(root)
    }

    override fun onResume() {
        super.onResume()
        rebuild()
    }

    private fun rebuild() {
        list.removeAllViews()
        for (m in Machines.all) list.addView(card(m))
    }

    private fun card(m: MachineDef): View {
        val ready = RomStore.isReady(this, m)
        val c = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(18), dp(16), dp(18), dp(16))
            background = GradientDrawable().apply {
                cornerRadius = dp(18).toFloat()
                colors = intArrayOf(0xFF2A1B5C.toInt(), 0xFF1A1238.toInt())
                orientation = GradientDrawable.Orientation.TL_BR
                setStroke(dp(1), if (ready) ACCENT else 0xFF3C2F6B.toInt())
            }
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT)
                .apply { bottomMargin = dp(14) }
        }
        c.addView(TextView(this).apply {
            text = m.title
            textSize = 22f
            typeface = Typeface.DEFAULT_BOLD
            setTextColor(Color.WHITE)
        })
        c.addView(TextView(this).apply {
            text = m.subtitle
            textSize = 14f
            setTextColor(0xFFCFC6F5.toInt())
        })
        c.addView(TextView(this).apply {
            text = "${m.chip} · ${m.region} · ${m.year} · ${m.id}"
            textSize = 12f
            setTextColor(MUTED)
            setPadding(0, dp(4), 0, dp(10))
        })
        c.addView(TextView(this).apply {
            text = if (ready) "● ROM ready" else "○ ROM needed: ${m.roms.joinToString { it.name }}"
            textSize = 13f
            setTextColor(if (ready) 0xFF6CF0A8.toInt() else 0xFFFFB86B.toInt())
            setPadding(0, 0, 0, dp(12))
        })
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.END }
        row.addView(button(if (ready) "Re-import" else "Import ROM", false) { startImport(m) })
        if (ready) {
            row.addView(View(this), LinearLayout.LayoutParams(dp(10), 1))
            row.addView(button("▶  Play", true) {
                startActivity(Intent(this, EmulatorActivity::class.java).putExtra("machine", m.id))
            })
        }
        c.addView(row)
        return c
    }

    private fun button(label: String, primary: Boolean, onClick: () -> Unit) = Button(this).apply {
        text = label
        isAllCaps = false
        textSize = 15f
        setTextColor(if (primary) Color.BLACK else Color.WHITE)
        background = GradientDrawable().apply {
            cornerRadius = dp(24).toFloat()
            if (primary) setColor(ACCENT) else { setColor(0x00000000); setStroke(dp(1), 0xFF8A7BD1.toInt()) }
        }
        setPadding(dp(20), 0, dp(20), 0)
        setOnClickListener { onClick() }
    }

    private fun startImport(m: MachineDef) {
        pendingImport = m
        val i = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
        }
        startActivityForResult(i, REQ_IMPORT)
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val m = pendingImport ?: return
        val uri = data?.data
        if (requestCode != REQ_IMPORT || resultCode != RESULT_OK || uri == null) return

        val status = TextView(this).apply { setPadding(dp(24), dp(20), dp(24), dp(8)); textSize = 15f }
        val dlg = AlertDialog.Builder(this).setTitle("Importing ${m.title}").setView(status).setCancelable(false).show()
        thread {
            val result = runCatching { RomStore.import(this, m, uri) { msg -> runOnUiThread { status.text = msg } } }
            runOnUiThread {
                dlg.dismiss()
                val msg = result.fold(
                    onSuccess = { r ->
                        when {
                            r.found.isEmpty() -> "None of the files for this machine were found in that file."
                            r.missing.isNotEmpty() -> "Still missing: ${r.missing.joinToString()}"
                            r.badCrc.isNotEmpty() -> "Imported, but the checksum doesn't match MAME's for: ${r.badCrc.joinToString()}. It may be a different dump and might not work."
                            else -> "ROM imported and verified."
                        }
                    },
                    onFailure = { "Import failed: ${it.message}" })
                AlertDialog.Builder(this).setMessage(msg).setPositiveButton("OK", null).show()
                rebuild()
            }
        }
    }

    companion object {
        const val REQ_IMPORT = 1
        val BG = 0xFF0E0B1A.toInt()
        val ACCENT = 0xFFFF4FD8.toInt()
        val MUTED = 0xFF9A90C2.toInt()
    }
}
