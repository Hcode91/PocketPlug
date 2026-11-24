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
import android.widget.Toast
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.ImageView
import android.widget.PopupMenu
import android.graphics.Bitmap
import android.os.Handler
import android.os.Looper
import android.text.Editable
import android.text.TextUtils
import android.text.TextWatcher
import android.view.inputmethod.EditorInfo
import kotlin.concurrent.thread

/**
 * Machine library. Home: search, category filters, favourites, recently
 * played, then "Ready to play" and "Needs ROMs" as a grid of tiles with a
 * picture of each machine (its own skin). Tapping a tile opens its page:
 * description, ROM status, import and play.
 */
class MainActivity : Activity() {

    private lateinit var list: LinearLayout
    private lateinit var home: LinearLayout
    private var pendingImport: MachineDef? = null
    private var pendingBios = false
    private var detail: MachineDef? = null
    private var query = ""
    private var filter = ALL
    private val chips = ArrayList<TextView>()

    private val thumbs = HashMap<String, Bitmap>()
    private val thumbViews = HashMap<String, MutableList<ImageView>>()
    private val toRender = ArrayDeque<MachineDef>()
    private var rendering = false
    private val ui = Handler(Looper.getMainLooper())

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()
    private val prefs get() = getSharedPreferences("settings", MODE_PRIVATE)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = BG
        window.navigationBarColor = BG
        SkinPreview.prune(this)

        home = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(BG)
            setPadding(dp(16), dp(20), dp(16), 0)
        }
        // title bar with the overflow menu
        home.addView(LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            addView(TextView(this@MainActivity).apply {
                text = "PocketPlug"
                textSize = 28f
                typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD_ITALIC)
                setTextColor(Color.WHITE)
                setShadowLayer(18f, 0f, 0f, ACCENT)
            }, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
            addView(TextView(this@MainActivity).apply {
                text = "⋮"
                textSize = 26f
                setTextColor(Color.WHITE)
                gravity = Gravity.CENTER
                setPadding(dp(14), dp(4), dp(6), dp(4))
                setOnClickListener { v -> overflow(v) }
            })
        })
        // search
        home.addView(EditText(this).apply {
            hint = "Search ${Machines.all.size} machines"
            setHintTextColor(MUTED)
            setTextColor(Color.WHITE)
            textSize = 16f
            isSingleLine = true
            imeOptions = EditorInfo.IME_ACTION_SEARCH
            setPadding(dp(16), dp(10), dp(16), dp(10))
            background = GradientDrawable().apply { cornerRadius = dp(22).toFloat(); setColor(0xFF1F1740.toInt()) }
            addTextChangedListener(object : TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}
                override fun onTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}
                override fun afterTextChanged(e: Editable?) { query = e?.toString()?.trim() ?: ""; renderList() }
            })
        }, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { topMargin = dp(10) })
        // category filters
        val chipRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, dp(10), 0, dp(10)) }
        for (name in listOf(ALL, FAVS) + CATEGORIES) {
            val t = TextView(this).apply {
                text = name
                textSize = 14f
                setPadding(dp(14), dp(7), dp(14), dp(7))
                setOnClickListener { filter = name; styleChips(); renderList() }
            }
            chips += t
            chipRow.addView(t, LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { marginEnd = dp(8) })
        }
        styleChips()
        home.addView(HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false; addView(chipRow) })

        list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(0, 0, 0, dp(24)) }
        home.addView(ScrollView(this).apply { addView(list) },
            LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f))
        setContentView(home)

        // Started for a machine: go straight to it. When it can't start yet (files missing, or a
        // cartridge to choose), its page stays open instead.
        if (savedInstanceState == null && launchFrom(intent)) finish()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        if (launchFrom(intent)) finish()
    }

    private fun launchFrom(i: Intent?): Boolean {
        if (i?.action != ACTION_LAUNCH_GAME) return false
        val wanted = i.getStringExtra("game_id")?.trim().orEmpty()
        if (wanted.isEmpty()) return false
        val set = wanted.substringBefore('/').trim()
        val cartTitle = wanted.substringAfter('/', "").trim()
        val m = Machines.all.firstOrNull { it.id.equals(set, ignoreCase = true) }
        if (m == null) {
            AlertDialog.Builder(this).setTitle("Machine not found")
                .setMessage("There's no machine with the set name \"$set\".")
                .setPositiveButton("OK", null).show()
            return false
        }
        if (!RomStore.isReady(this, m)) {
            detail = m
            Toast.makeText(this, "Import ${m.title}'s files first", Toast.LENGTH_LONG).show()
            return false
        }
        if (m.media != MediaKind.CARTRIDGE) {
            startActivity(Intent(this, EmulatorActivity::class.java).putExtra("machine", m.id))
            return true
        }
        val carts = RomStore.listCarts(this, m)
        if (m.builtInGame.isNotEmpty() && (cartTitle.equals(m.builtInGame, ignoreCase = true) || (cartTitle.isEmpty() && carts.isEmpty()))) {
            startActivity(Intent(this, EmulatorActivity::class.java).putExtra("machine", m.id).putExtra("title", m.builtInGame))
            return true
        }
        val cart = when {
            cartTitle.isNotEmpty() -> carts.firstOrNull {
                it.title.equals(cartTitle, ignoreCase = true) || it.file.nameWithoutExtension.equals(cartTitle, ignoreCase = true)
            }
            carts.size == 1 -> carts[0]
            else -> null
        }
        if (cart == null) {
            // Show the machine's cartridges to pick from.
            detail = m
            if (cartTitle.isNotEmpty()) Toast.makeText(this, "No cartridge called \"$cartTitle\" for ${m.title}", Toast.LENGTH_LONG).show()
            return false
        }
        startActivity(Intent(this, EmulatorActivity::class.java)
            .putExtra("machine", m.id).putExtra("cart", cart.file.absolutePath).putExtra("title", cart.title))
        return true
    }

    override fun onResume() {
        super.onResume()
        rebuild()
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (detail != null) showHome() else @Suppress("DEPRECATION") super.onBackPressed()
    }

    private fun rebuild() {
        val d = detail
        if (d != null) showDetail(d) else renderList()
    }

    private fun styleChips() {
        for (c in chips) {
            val on = c.text == filter
            c.setTextColor(if (on) Color.BLACK else Color.WHITE)
            c.background = GradientDrawable().apply {
                cornerRadius = dp(18).toFloat()
                if (on) setColor(ACCENT) else { setColor(0x00000000); setStroke(dp(1), 0xFF4A3D80.toInt()) }
            }
        }
    }

    private fun overflow(anchor: View) {
        val pm = PopupMenu(this, anchor)
        pm.menu.add(0, 1, 0, "Find my ROMs")
        pm.menu.add(0, 2, 1, "Sort a folder")
        pm.menu.add(0, 3, 2, "About")
        pm.setOnMenuItemClickListener {
            when (it.itemId) { 1 -> findRoms(); 2 -> pickSortFolder(); else -> about() }
            true
        }
        pm.show()
    }

    private fun about() {
        val ver = runCatching { packageManager.getPackageInfo(packageName, 0).versionName }.getOrDefault("")
        AlertDialog.Builder(this)
            .setTitle("PocketPlug $ver")
            .setMessage("Plug & play TV games, handhelds, pets and old computers, each in its own skin.\n\n" +
                "You need your own ROM and cartridge dumps (MAME sets and software-list zips work, as do .7z and raw .bin files). " +
                "\"Find my ROMs\" sets up every machine it can from one folder.\n\n" +
                "Free software under the GNU GPL; see the project's LICENSES folder for the parts it's built from.")
            .setPositiveButton("OK", null)
            .show()
    }

    // ---- favourites and recently played ----
    private fun favourites(): Set<String> = prefs.getStringSet("favourites", emptySet()) ?: emptySet()
    private fun isFav(m: MachineDef) = m.id in favourites()
    private fun toggleFav(m: MachineDef) {
        val f = favourites().toMutableSet()
        if (!f.remove(m.id)) f.add(m.id)
        prefs.edit().putStringSet("favourites", f).apply()
    }
    private fun recent(): List<MachineDef> =
        (prefs.getString("recent", "") ?: "").split(',').filter { it.isNotEmpty() }.mapNotNull { Machines.byId(it) }

    private fun playable(m: MachineDef): Boolean =
        if (m.media == MediaKind.CARTRIDGE) RomStore.isReady(this, m) && (m.builtInGame.isNotEmpty() || RomStore.listCarts(this, m).isNotEmpty())
        else RomStore.isReady(this, m)

    private fun play(m: MachineDef) {
        if (m.media == MediaKind.CARTRIDGE) { showDetail(m); return }
        startActivity(Intent(this, EmulatorActivity::class.java).putExtra("machine", m.id))
    }

    // ---- the list ----
    private fun matches(m: MachineDef): Boolean {
        if (filter == FAVS && !isFav(m)) return false
        if (filter != ALL && filter != FAVS && m.category != filter) return false
        if (query.isEmpty()) return true
        val q = query.lowercase()
        if (listOf(m.title, m.subtitle, m.id, m.chip, m.year, m.category).any { it.lowercase().contains(q) }) return true
        // a game inside a multi-game unit finds the unit
        return q.length >= 3 && GameLists.forMachine(m.id)?.games?.any { !it.startsWith("# ") && it.lowercase().contains(q) } == true
    }

    private fun columns() = maxOf(2, resources.configuration.screenWidthDp / 180)

    private fun renderList() {
        list.removeAllViews()
        thumbViews.clear()
        val shown = Machines.all.filter { matches(it) }
        val ready = shown.filter { playable(it) }
        val needs = shown.filter { !playable(it) }
        if (query.isEmpty() && filter == ALL) {
            val favs = Machines.all.filter { isFav(it) }
            if (favs.isNotEmpty()) shelf("★  Favourites", favs)
            val rec = recent()
            if (rec.isNotEmpty()) shelf("Recently played", rec)
        }
        if (shown.isEmpty()) {
            list.addView(TextView(this).apply {
                text = if (filter == FAVS && query.isEmpty()) "No favourites yet: tap the ☆ on a machine." else "Nothing matches."
                setTextColor(MUTED); textSize = 15f; setPadding(dp(4), dp(24), dp(4), dp(24))
            })
            return
        }
        if (ready.isNotEmpty()) { heading("Ready to play", ready.size); grid(ready) }
        if (needs.isNotEmpty()) { heading("Needs ROMs", needs.size); grid(needs) }
        pumpThumbs()
    }

    private fun heading(title: String, n: Int) {
        list.addView(TextView(this).apply {
            text = "$title  ·  $n"
            textSize = 17f
            typeface = Typeface.DEFAULT_BOLD
            setTextColor(Color.WHITE)
            setPadding(dp(2), dp(14), 0, dp(8))
        })
    }

    private fun shelf(title: String, ms: List<MachineDef>) {
        list.addView(TextView(this).apply {
            text = title; textSize = 17f; typeface = Typeface.DEFAULT_BOLD; setTextColor(Color.WHITE)
            setPadding(dp(2), dp(6), 0, dp(8))
        })
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        for (m in ms) row.addView(tile(m, compact = true), LinearLayout.LayoutParams(dp(132), LinearLayout.LayoutParams.WRAP_CONTENT).apply { marginEnd = dp(10) })
        list.addView(HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false; addView(row) })
    }

    private fun grid(ms: List<MachineDef>) {
        val cols = columns()
        for (i in ms.indices step cols) {
            val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
            for (j in 0 until cols) {
                val lp = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f).apply {
                    if (j < cols - 1) marginEnd = dp(10)
                }
                if (i + j < ms.size) row.addView(tile(ms[i + j], compact = false), lp)
                else row.addView(View(this), lp)
            }
            list.addView(row, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { bottomMargin = dp(10) })
        }
    }

    private fun tile(m: MachineDef, compact: Boolean): View {
        val ready = playable(m)
        val t = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            background = GradientDrawable().apply {
                cornerRadius = dp(16).toFloat()
                colors = intArrayOf(0xFF2A1B5C.toInt(), 0xFF1A1238.toInt())
                orientation = GradientDrawable.Orientation.TL_BR
                setStroke(dp(1), if (ready) 0xFF7A3FA8.toInt() else 0xFF3C2F6B.toInt())
            }
            setPadding(dp(6), dp(6), dp(6), dp(10))
            setOnClickListener { showDetail(m) }
        }
        val frame = FrameLayout(this)
        val img = ImageView(this).apply {
            scaleType = ImageView.ScaleType.FIT_CENTER
            setPadding(dp(4), dp(4), dp(4), dp(4))
            background = GradientDrawable().apply { cornerRadius = dp(12).toFloat(); setColor(0xFF120D24.toInt()) }
            clipToOutline = true
        }
        bindThumb(m, img)
        frame.addView(img, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, dp(if (compact) 96 else 120)))
        // favourite star
        frame.addView(TextView(this).apply {
            text = if (isFav(m)) "★" else "☆"
            textSize = 20f
            setTextColor(if (isFav(m)) 0xFFFFD34F.toInt() else 0xCCFFFFFF.toInt())
            setShadowLayer(6f, 0f, 0f, Color.BLACK)
            setPadding(dp(8), dp(2), dp(8), dp(6))
            setOnClickListener { toggleFav(m); renderList() }
        }, FrameLayout.LayoutParams(FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.TOP or Gravity.END))
        if (ready) frame.addView(TextView(this).apply {
            text = "▶"
            textSize = if (compact) 14f else 16f
            gravity = Gravity.CENTER
            setTextColor(Color.BLACK)
            background = GradientDrawable().apply { shape = GradientDrawable.OVAL; setColor(ACCENT) }
            setOnClickListener { play(m) }
        }, FrameLayout.LayoutParams(dp(if (compact) 34 else 42), dp(if (compact) 34 else 42), Gravity.BOTTOM or Gravity.END).apply {
            setMargins(0, 0, dp(6), dp(6))
        })
        t.addView(frame)
        t.addView(TextView(this).apply {
            text = m.title
            textSize = if (compact) 13f else 14f
            typeface = Typeface.DEFAULT_BOLD
            setTextColor(Color.WHITE)
            maxLines = 2
            ellipsize = TextUtils.TruncateAt.END
            setPadding(dp(4), dp(8), dp(4), 0)
        })
        t.addView(TextView(this).apply {
            text = "${m.year} · ${m.category}"
            textSize = 11f
            setTextColor(MUTED)
            maxLines = 1
            ellipsize = TextUtils.TruncateAt.END
            setPadding(dp(4), dp(2), dp(4), 0)
        })
        if (!compact) t.addView(TextView(this).apply {
            text = if (ready) "● Ready" else if (m.media == MediaKind.CARTRIDGE && RomStore.isReady(this@MainActivity, m)) "○ Add games" else "○ Needs ROMs"
            textSize = 11f
            setTextColor(if (ready) 0xFF6CF0A8.toInt() else 0xFFFFB86B.toInt())
            setPadding(dp(4), dp(3), dp(4), 0)
        })
        return t
    }

    // ---- machine pictures ----
    private fun bindThumb(m: MachineDef, img: ImageView) {
        thumbViews.getOrPut(m.id) { ArrayList() }.add(img)
        val b = thumbs[m.id]
        if (b != null) { img.setImageBitmap(b); return }
        img.setImageDrawable(null)
        if (toRender.none { it.id == m.id }) toRender.addLast(m)
    }

    private fun showThumb(m: MachineDef, b: Bitmap) {
        thumbs[m.id] = b
        thumbViews[m.id]?.forEach { it.setImageBitmap(b) }
    }

    /** Loads cached pictures, drawing the missing ones one at a time between frames. */
    private fun pumpThumbs() {
        if (rendering) return
        val m = toRender.removeFirstOrNull() ?: return
        if (thumbs.containsKey(m.id)) { pumpThumbs(); return }
        rendering = true
        thread {
            val cached = SkinPreview.cached(this, m)
            ui.post {
                val b = cached ?: SkinPreview.render(this, m)
                if (b != null) showThumb(m, b)
                rendering = false
                ui.postDelayed({ pumpThumbs() }, if (cached != null) 0L else 16L)
            }
        }
    }

    /** The games built into a multi-game unit, from its own menu. Long lists start folded, with a filter. */
    private fun gamesCard(m: MachineDef): View? {
        val e = GameLists.forMachine(m.id) ?: return null
        val total = e.games.count { !it.startsWith("# ") }
        val c = cardShell(false)
        c.addView(TextView(this).apply {
            text = "Games  ·  $total"; textSize = 20f; typeface = Typeface.DEFAULT_BOLD; setTextColor(Color.WHITE)
        })
        if (e.note.isNotEmpty()) c.addView(TextView(this).apply {
            text = e.note; textSize = 13f; setTextColor(MUTED); setPadding(0, dp(4), 0, 0)
        })
        val body = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(0, dp(8), 0, 0) }
        var q = ""
        var expanded = total <= 24
        fun fill() {
            body.removeAllViews()
            var n = 0; var shown = 0
            var section: String? = null; var sectionShown = false
            for (g in e.games) {
                if (g.startsWith("# ")) { section = g.substring(2); sectionShown = false; continue }
                n++
                if (q.isNotEmpty() && !g.lowercase().contains(q)) continue
                if (!expanded && q.isEmpty() && shown >= 24) break
                if (section != null && !sectionShown) {
                    val title = section
                    body.addView(TextView(this).apply {
                        text = title; textSize = 15f; typeface = Typeface.DEFAULT_BOLD; setTextColor(ACCENT)
                        setPadding(0, dp(10), 0, dp(2))
                    })
                    sectionShown = true
                }
                val label = if (e.numbered) "$n.  $g" else g
                body.addView(TextView(this).apply {
                    text = label
                    textSize = 14f; setTextColor(0xFFE6E0FF.toInt()); setPadding(0, dp(3), 0, dp(3))
                })
                shown++
            }
            if (shown == 0) body.addView(TextView(this).apply { text = "No game matches."; setTextColor(MUTED); textSize = 14f })
            if (!expanded && q.isEmpty() && total > shown) body.addView(button("Show all $total", false) { expanded = true; fill() },
                LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { topMargin = dp(10) })
        }
        if (total > 24) c.addView(EditText(this).apply {
            hint = "Find a game"
            setHintTextColor(MUTED); setTextColor(Color.WHITE); textSize = 15f; isSingleLine = true
            setPadding(dp(14), dp(8), dp(14), dp(8))
            background = GradientDrawable().apply { cornerRadius = dp(18).toFloat(); setColor(0xFF120D24.toInt()) }
            addTextChangedListener(object : TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, a: Int, b: Int, c2: Int) {}
                override fun onTextChanged(s: CharSequence?, a: Int, b: Int, c2: Int) {}
                override fun afterTextChanged(ed: Editable?) { q = ed?.toString()?.trim()?.lowercase() ?: ""; fill() }
            })
        }, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { topMargin = dp(10) })
        fill()
        c.addView(body)
        return c
    }

    // ---- a machine's page ----
    private fun showHome() {
        detail = null
        setContentView(home)
        renderList()
    }

    private fun showDetail(m: MachineDef) {
        detail = m
        val page = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(16), dp(16), dp(24))
        }
        page.addView(LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            addView(TextView(this@MainActivity).apply {
                text = "‹  Back"
                textSize = 17f
                setTextColor(Color.WHITE)
                setPadding(dp(4), dp(8), dp(16), dp(8))
                setOnClickListener { showHome() }
            }, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
            addView(TextView(this@MainActivity).apply {
                text = if (isFav(m)) "★  Favourite" else "☆  Favourite"
                textSize = 15f
                setTextColor(if (isFav(m)) 0xFFFFD34F.toInt() else Color.WHITE)
                setPadding(dp(12), dp(8), dp(4), dp(8))
                setOnClickListener { toggleFav(m); showDetail(m) }
            })
        })
        val img = ImageView(this).apply {
            scaleType = ImageView.ScaleType.FIT_CENTER
            setPadding(dp(8), dp(8), dp(8), dp(8))
            background = GradientDrawable().apply { cornerRadius = dp(18).toFloat(); setColor(0xFF120D24.toInt()) }
        }
        thumbViews.getOrPut(m.id) { ArrayList() }.add(img)
        val b = thumbs[m.id]
        if (b != null) img.setImageBitmap(b) else { toRender.addFirst(m); pumpThumbs() }
        page.addView(img, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(260)).apply { bottomMargin = dp(14) })
        page.addView(card(m))
        gamesCard(m)?.let { page.addView(it) }
        setContentView(ScrollView(this).apply { setBackgroundColor(BG); addView(page) })
    }

    private fun card(m: MachineDef): View {
        if (m.media == MediaKind.CARTRIDGE) return cartCard(m)
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
            text = if (m.roms.isEmpty()) "● Built in - no ROM needed"
                else if (ready) "● ROM ready" else "○ ROM needed: ${m.roms.joinToString { it.name }}"
            textSize = 13f
            setTextColor(if (ready) 0xFF6CF0A8.toInt() else 0xFFFFB86B.toInt())
            setPadding(0, 0, 0, dp(12))
        })
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.END }
        if (m.roms.isNotEmpty()) row.addView(button(if (ready) "Re-import" else "Import ROM", false) { startImport(m) })
        if (ready) {
            if (m.roms.isNotEmpty()) row.addView(View(this), LinearLayout.LayoutParams(dp(10), 1))
            row.addView(button("▶  Play", true) {
                startActivity(Intent(this, EmulatorActivity::class.java).putExtra("machine", m.id))
            })
        }
        c.addView(row)
        return c
    }

    private fun cardShell(highlight: Boolean) = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(dp(18), dp(16), dp(18), dp(16))
        background = GradientDrawable().apply {
            cornerRadius = dp(18).toFloat()
            colors = intArrayOf(0xFF2A1B5C.toInt(), 0xFF1A1238.toInt())
            orientation = GradientDrawable.Orientation.TL_BR
            setStroke(dp(1), if (highlight) ACCENT else 0xFF3C2F6B.toInt())
        }
        layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT)
            .apply { bottomMargin = dp(14) }
    }

    /** Card for a cartridge console: its game library plus "Add games". */
    private fun cartCard(m: MachineDef): View {
        val carts = RomStore.listCarts(this, m)
        val c = cardShell(carts.isNotEmpty())
        c.addView(TextView(this).apply {
            text = m.title; textSize = 22f; typeface = Typeface.DEFAULT_BOLD; setTextColor(Color.WHITE)
        })
        c.addView(TextView(this).apply { text = m.subtitle; textSize = 14f; setTextColor(0xFFCFC6F5.toInt()) })
        c.addView(TextView(this).apply {
            text = "${m.chip} · ${m.region} · ${m.year} · ${m.id}"
            textSize = 12f; setTextColor(MUTED); setPadding(0, dp(4), 0, dp(10))
        })
        val biosReady = RomStore.isReady(this, m)
        if (m.roms.isNotEmpty()) c.addView(TextView(this).apply {
            text = if (biosReady) "● BIOS ready" else "○ BIOS needed first: ${m.roms.joinToString { it.name }}"
            textSize = 13f
            setTextColor(if (biosReady) 0xFF6CF0A8.toInt() else 0xFFFFB86B.toInt())
            setPadding(0, 0, 0, dp(4))
        })
        c.addView(TextView(this).apply {
            text = if (carts.isEmpty() && m.builtInGame.isNotEmpty()) "○ Add cartridge dumps (.bin, .zip, .7z or a MAME software-list zip) for more games"
                   else if (carts.isEmpty() && m.cartDisc) "○ No games yet - add a game disc (.zip of its .cue and .bin, or a .bin / .iso)"
                   else if (carts.isEmpty() && m.cartWholeZip) "○ No games yet - add a game as a .zip of its files (its .exe and data folder)"
                   else if (carts.isEmpty()) "○ No games yet - add cartridge dumps (.bin, .zip, .7z or a MAME software-list zip)"
                   else "● ${carts.size} game${if (carts.size == 1) "" else "s"} · tap to play, hold to remove"
            textSize = 13f
            setTextColor(if (carts.isEmpty() && m.builtInGame.isEmpty()) 0xFFFFB86B.toInt() else 0xFF6CF0A8.toInt())
            setPadding(0, 0, 0, dp(8))
        })
        if (m.builtInGame.isNotEmpty()) c.addView(TextView(this).apply {
            text = "▶  ${m.builtInGame}   (built in)"
            textSize = 16f
            setTextColor(Color.WHITE)
            setPadding(dp(10), dp(11), dp(10), dp(11))
            background = GradientDrawable().apply { cornerRadius = dp(10).toFloat(); setColor(0x22FFFFFF) }
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT)
                .apply { bottomMargin = dp(6) }
            setOnClickListener {
                if (!biosReady) {
                    Toast.makeText(this@MainActivity, "Import the BIOS first (${m.roms.joinToString { it.name }})", Toast.LENGTH_LONG).show()
                    return@setOnClickListener
                }
                startActivity(Intent(this@MainActivity, EmulatorActivity::class.java).putExtra("machine", m.id).putExtra("title", m.builtInGame))
            }
        })
        for (cart in carts) {
            c.addView(TextView(this).apply {
                text = "▶  ${cart.title}" + if (cart.known) "" else "   (unknown dump)"
                textSize = 16f
                setTextColor(Color.WHITE)
                setPadding(dp(10), dp(11), dp(10), dp(11))
                background = GradientDrawable().apply { cornerRadius = dp(10).toFloat(); setColor(0x22FFFFFF) }
                layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT)
                    .apply { bottomMargin = dp(6) }
                setOnClickListener {
                    if (!biosReady) {
                        Toast.makeText(this@MainActivity, "Import the BIOS first (${m.roms.joinToString { it.name }})", Toast.LENGTH_LONG).show()
                        return@setOnClickListener
                    }
                    startActivity(Intent(this@MainActivity, EmulatorActivity::class.java)
                        .putExtra("machine", m.id).putExtra("cart", cart.file.absolutePath).putExtra("title", cart.title))
                }
                setOnLongClickListener {
                    AlertDialog.Builder(this@MainActivity).setMessage("Remove ${cart.title}?")
                        .setPositiveButton("Remove") { _, _ -> cart.file.delete(); java.io.File(cart.file.path + ".d").deleteRecursively(); rebuild() }
                        .setNegativeButton("Cancel", null).show()
                    true
                }
            })
        }
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.END; setPadding(0, dp(6), 0, 0) }
        if (m.roms.isNotEmpty()) {
            row.addView(button(if (biosReady) "Re-import BIOS" else "Import BIOS", !biosReady) { startImport(m, bios = true) })
            row.addView(View(this), LinearLayout.LayoutParams(dp(10), 1))
        }
        row.addView(button("Add games", biosReady && carts.isEmpty()) { startImport(m) })
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

    private fun startImport(m: MachineDef, bios: Boolean = false) {
        pendingImport = m
        pendingBios = bios
        val i = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
        }
        startActivityForResult(i, REQ_IMPORT)
    }

    // ---- finding every machine's files in a folder ----

    private fun savedRomFolder(): android.net.Uri? {
        val s = getSharedPreferences("settings", MODE_PRIVATE).getString(RomScanner.PREF_FOLDER, null) ?: return null
        val u = android.net.Uri.parse(s)
        return u.takeIf { contentResolver.persistedUriPermissions.any { p -> p.uri == u && p.isReadPermission } }
    }

    private fun findRoms() {
        val saved = savedRomFolder()
        if (saved == null) { pickRomFolder(); return }
        val name = saved.lastPathSegment?.substringAfterLast(':')?.ifEmpty { "your folder" } ?: "your folder"
        AlertDialog.Builder(this)
            .setTitle("Find my ROMs")
            .setMessage("Look through $name (and its subfolders) again? Only new or changed files are read.")
            .setPositiveButton("Rescan") { _, _ -> scanRomFolder(saved) }
            .setNeutralButton("Pick another folder") { _, _ -> pickRomFolder() }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun pickRomFolder() {
        AlertDialog.Builder(this)
            .setTitle("Find my ROMs")
            .setMessage("Pick the folder your ROMs are in. Every machine's files are found in it and its subfolders " +
                "by their checksums, so names and folders don't matter: MAME zips, 7z files, loose files and CHD images.\n\n" +
                "Nothing in the folder changes. PocketPlug remembers it, so next time it's one tap to look again.")
            .setPositiveButton("Pick folder") { _, _ ->
                startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).addFlags(
                    Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION), REQ_FIND)
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun scanRomFolder(tree: android.net.Uri) {
        val status = TextView(this).apply { setPadding(dp(24), dp(20), dp(24), dp(8)); textSize = 14f }
        val dlg = AlertDialog.Builder(this).setTitle("Finding ROMs").setView(status).setCancelable(false).show()
        thread {
            val sc = RomScanner(this, tree)
            val res = runCatching { sc.scan { msg -> status.post { status.text = msg } } }
            runOnUiThread {
                dlg.dismiss()
                res.onFailure { AlertDialog.Builder(this).setMessage("Couldn't read the folder: ${it.message}").setPositiveButton("OK", null).show() }
                res.onSuccess { showScanResult(sc, it) }
            }
        }
    }

    private fun showScanResult(sc: RomScanner, hits: List<RomScanner.MachineHits>) {
        // machines already set up only count if something of theirs changed place
        val fresh = hits.filter { !RomStore.isReady(this, it.machine) }
        val complete = fresh.filter { it.complete }
        val partial = fresh.filter { !it.complete }
        val ready = hits.filter { RomStore.isReady(this, it.machine) }
        val big = complete.flatMap { h -> h.found.values }.any { it.entry == null && it.docSize > RomScanner.LINK_ABOVE }
        val text = buildString {
            if (complete.isEmpty() && partial.isEmpty()) append("Nothing new: ")
            append(if (hits.isEmpty()) "no files for any machine were found.\n" else "")
            if (complete.isNotEmpty()) {
                append("Ready to set up (${complete.size}):\n")
                for (h in complete) append("   ✓ ${h.machine.title}\n")
            }
            if (partial.isNotEmpty()) {
                append("\nFound some files, still missing others:\n")
                for (h in partial) {
                    append("   ◐ ${h.machine.title}\n")
                    append("        missing: ${h.missing.joinToString()}\n")
                }
            }
            if (ready.isNotEmpty()) append("\nAlready set up: ${ready.joinToString { it.machine.title }}\n")
            if (big) append("\nFiles over 100 MB can stay in your folder instead of being copied (saves space; the machine stops working if you move them).")
        }
        val tv = TextView(this).apply { this.text = text; textSize = 13f; setPadding(dp(20), dp(12), dp(20), dp(12)); setTextIsSelectable(true) }
        val b = AlertDialog.Builder(this).setTitle("Find my ROMs").setView(ScrollView(this).apply { addView(tv) })
        // partial machines get their files too, so a later folder can finish them
        val todo = complete + partial
        if (todo.isNotEmpty()) {
            b.setPositiveButton("Set up ${complete.size}" + if (partial.isNotEmpty()) " (+${partial.size} partly)" else "") { _, _ -> installScan(sc, todo, big) }
            b.setNegativeButton("Cancel", null)
        } else b.setPositiveButton("OK", null)
        b.show()
    }

    private fun installScan(sc: RomScanner, todo: List<RomScanner.MachineHits>, askLink: Boolean) {
        fun go(link: Boolean) {
            val status = TextView(this).apply { setPadding(dp(24), dp(20), dp(24), dp(8)); textSize = 14f }
            val dlg = AlertDialog.Builder(this).setTitle("Setting up").setView(status).setCancelable(false).show()
            thread {
                val res = runCatching { sc.install(todo, link) { msg -> status.post { status.text = msg } } }
                runOnUiThread {
                    dlg.dismiss()
                    val msg = res.fold(onSuccess = { r ->
                        buildString {
                            append(if (r.machines.isEmpty()) "No machine is complete yet." else "Ready to play: ${r.machines.joinToString()}.")
                            if (r.linked > 0) append("\n${r.linked} large file${if (r.linked == 1) "" else "s"} left in your folder.")
                            if (r.failed.isNotEmpty()) append("\n\nCouldn't set up:\n" + r.failed.take(20).joinToString("\n"))
                        }
                    }, onFailure = { "Stopped: ${it.message}" })
                    AlertDialog.Builder(this).setTitle("Done").setMessage(msg).setPositiveButton("OK", null).show()
                    rebuild()
                }
            }
        }
        if (!askLink) { go(false); return }
        AlertDialog.Builder(this)
            .setTitle("Large files")
            .setMessage("Some files are over 100 MB. Copy them into PocketPlug too, or leave them in your folder?")
            .setPositiveButton("Leave in folder") { _, _ -> go(true) }
            .setNegativeButton("Copy them") { _, _ -> go(false) }
            .show()
    }

    @Deprecated("Deprecated in Java")
    // ---- sorting a folder of ROM files ----

    private fun pickSortFolder() {
        AlertDialog.Builder(this)
            .setTitle("Sort a folder of ROMs")
            .setMessage("Pick the folder your ROMs are in (Download, say). Each file is identified by what's inside it, " +
                "not its name: MAME sets by their chips, console games and discs by their headers.\n\n" +
                "Nothing moves until you've seen the list. Sorted files go into a Sorted folder inside the one you pick, " +
                "one folder per system; files it can't identify stay where they are.")
            .setPositiveButton("Pick folder") { _, _ ->
                startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).addFlags(
                    Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION), REQ_SORT)
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun sortFolder(tree: android.net.Uri) {
        runCatching { contentResolver.takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION) }
        val status = TextView(this).apply { setPadding(dp(24), dp(20), dp(24), dp(8)); textSize = 14f }
        val dlg = AlertDialog.Builder(this).setTitle("Identifying files").setView(status).setCancelable(false).show()
        thread {
            val fs = FolderSorter(this, tree)
            val res = runCatching {
                status.post { status.text = "Loading the ROM index..." }
                val sorter = FolderSorter.sorter(this)
                fs.scan(sorter) { msg -> status.post { status.text = msg } }
            }
            runOnUiThread {
                dlg.dismiss()
                res.onFailure { AlertDialog.Builder(this).setMessage("Couldn't read the folder: ${it.message}").setPositiveButton("OK", null).show() }
                res.onSuccess { showSortPlan(fs, it) }
            }
        }
    }

    private fun showSortPlan(fs: FolderSorter, plans: List<FolderSorter.Plan>) {
        val known = plans.filter { it.result != null }
        val unknown = plans.filter { it.result == null }
        if (known.isEmpty()) {
            AlertDialog.Builder(this).setTitle("Nothing to sort")
                .setMessage(if (plans.isEmpty()) "The folder is empty." else "None of the ${plans.size} files could be identified.")
                .setPositiveButton("OK", null).show()
            return
        }
        val text = buildString {
            append("${known.size} file${if (known.size == 1) "" else "s"} identified")
            if (unknown.isNotEmpty()) append(", ${unknown.size} left where they are")
            append(".\n")
            // by folder, then each file and why
            for ((folder, ps) in known.groupBy { it.result!!.folder }.toSortedMap(String.CASE_INSENSITIVE_ORDER)) {
                append("\n${FolderSorter.SORTED}/$folder\n")
                for (p in ps.sortedBy { it.doc.name.lowercase() }) {
                    val r = p.result!!
                    append("   ${p.doc.name}")
                    if (r.title != null && !p.doc.name.startsWith(r.title)) append("  = ${r.title}")
                    if (p.companionOf != null) append("  (with ${p.companionOf.name})")
                    else if (r.machine != null && Machines.all.any { it.id == r.machine }) append("  ★ PocketPlug")
                    append("\n")
                }
            }
            if (unknown.isNotEmpty()) {
                append("\nNot identified (staying put):\n")
                for (p in unknown.sortedBy { it.doc.name.lowercase() }.take(300)) append("   ${p.doc.name}\n")
                if (unknown.size > 300) append("   ... and ${unknown.size - 300} more\n")
            }
        }
        val tv = TextView(this).apply { this.text = text; textSize = 12f; setPadding(dp(20), dp(12), dp(20), dp(12)); setTextIsSelectable(true) }
        AlertDialog.Builder(this)
            .setTitle("Where they'll go")
            .setView(ScrollView(this).apply { addView(tv) })
            .setPositiveButton("Move ${known.size}") { _, _ -> runSortMove(fs, plans) }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun runSortMove(fs: FolderSorter, plans: List<FolderSorter.Plan>) {
        val status = TextView(this).apply { setPadding(dp(24), dp(20), dp(24), dp(8)); textSize = 14f }
        val dlg = AlertDialog.Builder(this).setTitle("Sorting").setView(status).setCancelable(false).show()
        thread {
            val res = runCatching { fs.move(plans) { msg -> status.post { status.text = msg } } }
            runOnUiThread {
                dlg.dismiss()
                val msg = res.fold(onSuccess = { r ->
                    buildString {
                        append("Moved ${r.moved} file${if (r.moved == 1) "" else "s"} into ${FolderSorter.SORTED}.")
                        if (r.duplicates > 0) append("\n${r.duplicates} had the same name as a file already there: they're in ${FolderSorter.SORTED}/Duplicates.")
                        if (r.failed.isNotEmpty()) append("\n\nCouldn't move:\n" + r.failed.take(30).joinToString("\n"))
                    }
                }, onFailure = { "Sorting stopped: ${it.message}" })
                AlertDialog.Builder(this).setTitle("Done").setMessage(msg).setPositiveButton("OK", null).show()
            }
        }
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQ_FIND) {
            val tree = data?.data
            if (resultCode == RESULT_OK && tree != null) {
                runCatching { contentResolver.takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
                getSharedPreferences("settings", MODE_PRIVATE).edit().putString(RomScanner.PREF_FOLDER, tree.toString()).apply()
                scanRomFolder(tree)
            }
            return
        }
        if (requestCode == REQ_SORT) {
            if (resultCode == RESULT_OK) data?.data?.let { sortFolder(it) }
            return
        }
        val m = pendingImport ?: return
        val uri = data?.data
        if (requestCode != REQ_IMPORT || resultCode != RESULT_OK || uri == null) return

        val status = TextView(this).apply { setPadding(dp(24), dp(20), dp(24), dp(8)); textSize = 15f }
        val dlg = AlertDialog.Builder(this).setTitle("Importing ${m.title}").setView(status).setCancelable(false).show()
        thread {
            if (m.media == MediaKind.CARTRIDGE && !pendingBios) {
                val res = runCatching { RomStore.importCarts(this, m, uri) { msg -> runOnUiThread { status.text = msg } } }
                runOnUiThread {
                    dlg.dismiss()
                    val msg = res.fold(
                        onSuccess = { r ->
                            buildString {
                                append(if (r.added.isEmpty()) "No new games found." else "Added ${r.added.size} game${if (r.added.size == 1) "" else "s"}.")
                                if (r.duplicates > 0) append("\n${r.duplicates} already in your library.")
                                if (r.rejected > 0) append("\n${r.rejected} file(s) skipped (not a cartridge for this system).")
                            }
                        },
                        onFailure = { "Import failed: ${it.message}" })
                    AlertDialog.Builder(this).setMessage(msg).setPositiveButton("OK", null).show()
                    rebuild()
                }
                return@thread
            }
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
        /**
         * The request another app (BacklogQuest's home-screen tiles) sends to start a machine straight
         * away. Extra "game_id": the machine's set name (e.g. "168pcase"), and for a cartridge machine
         * optionally "/" and the cartridge's title (e.g. "1292apvs/Grand Prix").
         */
        const val ACTION_LAUNCH_GAME = "com.hcode91.pocket.LAUNCH_GAME"
        const val ALL = "All"
        const val FAVS = "★ Favourites"
        const val REQ_IMPORT = 1
        const val REQ_SORT = 2
        const val REQ_FIND = 3
        val BG = 0xFF0E0B1A.toInt()
        val ACCENT = 0xFFFF4FD8.toInt()
        val MUTED = 0xFF9A90C2.toInt()
    }
}
