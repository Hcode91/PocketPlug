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
import kotlin.concurrent.thread

/** Machine library: one card per supported plug-and-play system. */
class MainActivity : Activity() {

    private lateinit var list: LinearLayout
    private var pendingImport: MachineDef? = null
    private var pendingBios = false

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
            setPadding(0, 0, 0, dp(10))
        })
        root.addView(LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            addView(button("Find my ROMs", true) { findRoms() })
            addView(View(this@MainActivity), LinearLayout.LayoutParams(dp(10), 1))
            addView(button("Sort a folder", false) { pickSortFolder() })
            setPadding(0, 0, 0, dp(14))
        })

        list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        root.addView(ScrollView(this).apply { addView(list) },
            LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f))
        root.addView(TextView(this).apply {
            text = "You need your own ROM and cartridge dumps (MAME sets and software-list zips work, as do .7z and raw .bin files)."
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
            text = if (carts.isEmpty()) "○ No games yet - add cartridge dumps (.bin, .zip, .7z or a MAME software-list zip)"
                   else "● ${carts.size} game${if (carts.size == 1) "" else "s"} · tap to play, hold to remove"
            textSize = 13f
            setTextColor(if (carts.isEmpty()) 0xFFFFB86B.toInt() else 0xFF6CF0A8.toInt())
            setPadding(0, 0, 0, dp(8))
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
                        .setPositiveButton("Remove") { _, _ -> cart.file.delete(); rebuild() }
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
        const val REQ_IMPORT = 1
        const val REQ_SORT = 2
        const val REQ_FIND = 3
        val BG = 0xFF0E0B1A.toInt()
        val ACCENT = 0xFFFF4FD8.toInt()
        val MUTED = 0xFF9A90C2.toInt()
    }
}
