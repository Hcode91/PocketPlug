package com.hcode91.pocketplug

import android.content.ContentResolver
import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.DocumentsContract.Document
import org.apache.commons.compress.archivers.sevenz.SevenZFile
import org.apache.commons.compress.archivers.zip.ZipFile
import java.io.File
import java.io.FileInputStream
import java.io.InputStream
import java.nio.ByteBuffer
import java.nio.channels.SeekableByteChannel
import java.util.zip.CRC32

/**
 * Finds the files every machine needs anywhere under a folder the user picked
 * (subfolders too), by checksum and size rather than by name:
 *  - zips and 7z files are listed (their headers carry each file's CRC), and a
 *    zip inside a zip is opened too;
 *  - loose files are only read when their size is one a machine needs;
 *  - CHD images (whose bytes differ with the tool that made them) are matched
 *    by name and a CHD header.
 * What each file held is cached by (document, size, date), so a rescan only
 * reads files that are new or have changed.
 *
 * [install] copies what was found into PocketPlug. Loose files over
 * [LINK_ABOVE] can instead stay where they are: the machine then reads them
 * from the folder (see RomStore links).
 */
class RomScanner(private val ctx: Context, private val tree: Uri) {

    /** Where a needed file is: a loose file, or an entry of an archive (inner = an archive inside it). */
    data class Location(val docId: String, val docName: String, val docSize: Long, val entry: String? = null, val inner: String? = null)

    class MachineHits(val machine: MachineDef, val found: Map<String, Location>) {
        val missing get() = machine.roms.filter { it.name !in found }.map { it.name }
        val complete get() = missing.isEmpty()
    }

    private val cr: ContentResolver = ctx.contentResolver
    private val rootId = DocumentsContract.getTreeDocumentId(tree)
    private fun uri(id: String) = DocumentsContract.buildDocumentUriUsingTree(tree, id)
    private val tempDir = File(ctx.cacheDir, "scan").apply { mkdirs() }

    private class Doc(val id: String, val name: String, val size: Long, val modified: Long)

    // (crc, size) -> files that machines need; names of CHD-style files
    private val byCrc = HashMap<Long, MutableList<Pair<MachineDef, RomSpec>>>()
    private val sizes = HashSet<Long>()
    private val byName = HashMap<String, MutableList<Pair<MachineDef, RomSpec>>>()

    init {
        for (m in Machines.all) {
            for (r in m.roms) {   // cartridge consoles' BIOS files too
                if (r.size == RomSpec.ANY_SIZE) byName.getOrPut(r.name.lowercase()) { ArrayList() } += m to r
                else { byCrc.getOrPut(key(r.crc, r.size)) { ArrayList() } += m to r; sizes += r.size }
            }
        }
    }

    private fun key(crc: Long, size: Long) = ((crc and 0xffffffffL) shl 32) xor size

    // ---- the folder ----

    private fun walk(progress: (String) -> Unit): List<Doc> {
        val out = ArrayList<Doc>()
        val stack = ArrayDeque<String>().apply { add(rootId) }
        while (stack.isNotEmpty()) {
            val dir = stack.removeLast()
            cr.query(DocumentsContract.buildChildDocumentsUriUsingTree(tree, dir),
                arrayOf(Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE,
                    Document.COLUMN_SIZE, Document.COLUMN_LAST_MODIFIED), null, null, null)?.use { c ->
                while (c.moveToNext()) {
                    val id = c.getString(0)
                    if (c.getString(2) == Document.MIME_TYPE_DIR) stack += id
                    else out += Doc(id, c.getString(1) ?: "", if (c.isNull(3)) 0L else c.getLong(3), if (c.isNull(4)) 0L else c.getLong(4))
                }
            }
            progress("Looking through folders: ${out.size} files")
        }
        return out
    }

    private fun channel(id: String): SeekableByteChannel {
        val pfd = cr.openFileDescriptor(uri(id), "r") ?: throw java.io.IOException("can't open")
        val ch = FileInputStream(pfd.fileDescriptor).channel
        return object : SeekableByteChannel by ch {
            override fun close() { try { ch.close() } finally { pfd.close() } }
            override fun read(dst: ByteBuffer) = ch.read(dst)
        }
    }

    private fun stream(id: String): InputStream = cr.openInputStream(uri(id)) ?: throw java.io.IOException("can't open")

    // ---- what a file holds: "crc size entry [inner]" lines (cached) ----

    private class Item(val crc: Long, val size: Long, val entry: String?, val inner: String?)

    private fun listArchive(ch: SeekableByteChannel, zip: Boolean, depth: Int, out: MutableList<Item>, outer: String?) {
        fun nested(name: String, size: Long, open: () -> InputStream) {
            val ext = name.lowercase().substringAfterLast('.', "")
            val tmp = File.createTempFile("scan", ".$ext", tempDir)
            try {
                open().use { i -> tmp.outputStream().use { i.copyTo(it, 1 shl 16) } }
                java.nio.file.Files.newByteChannel(tmp.toPath()).use { c -> listArchive(c, ext == "zip", depth + 1, out, name) }
            } catch (_: Exception) {
            } finally { tmp.delete() }
        }
        fun isArchive(n: String) = n.lowercase().let { it.endsWith(".zip") || it.endsWith(".7z") }
        if (zip) {
            ZipFile.builder().setSeekableByteChannel(ch).get().use { z ->
                for (e in z.entries) {
                    if (e.isDirectory) continue
                    if (depth == 0 && isArchive(e.name) && e.size in 1..(512L shl 20)) nested(e.name, e.size) { z.getInputStream(e) }
                    else if (e.crc >= 0) out += Item(e.crc, e.size, if (outer == null) e.name else outer, if (outer == null) null else e.name)
                }
            }
        } else {
            SevenZFile.builder().setSeekableByteChannel(ch).get().use { z ->
                for (e in z.entries) {
                    if (e.isDirectory) continue
                    if (depth == 0 && isArchive(e.name) && e.size in 1..(512L shl 20)) {
                        // 7z entries are read in order: copy this one out now
                        nested(e.name, e.size) { z.getInputStream(e) }
                    } else if (e.hasCrc) out += Item(e.crcValue, e.size, if (outer == null) e.name else outer, if (outer == null) null else e.name)
                }
            }
        }
    }

    private fun contents(d: Doc, cache: MutableMap<String, String>): List<Item> {
        val ck = "${d.id}\u0001${d.size}\u0001${d.modified}"
        cache[ck]?.let { return parse(it) }
        val lower = d.name.lowercase()
        val items = ArrayList<Item>()
        try {
            when {
                lower.endsWith(".zip") -> channel(d.id).use { listArchive(it, true, 0, items, null) }
                lower.endsWith(".7z") -> channel(d.id).use { listArchive(it, false, 0, items, null) }
                d.size in sizes -> {
                    val crc = CRC32()
                    stream(d.id).use { s -> val buf = ByteArray(1 shl 16); while (true) { val n = s.read(buf); if (n <= 0) break; crc.update(buf, 0, n) } }
                    items += Item(crc.value, d.size, null, null)
                }
            }
        } catch (_: Exception) { return emptyList() }
        cache[ck] = items.joinToString("\u0002") { "${it.crc}\u0003${it.size}\u0003${it.entry ?: ""}\u0003${it.inner ?: ""}" }
        return items
    }

    private fun parse(s: String): List<Item> = if (s.isEmpty()) emptyList() else s.split('\u0002').map {
        val p = it.split('\u0003')
        Item(p[0].toLong(), p[1].toLong(), p[2].ifEmpty { null }, p[3].ifEmpty { null })
    }

    private fun isChd(d: Doc): Boolean = try {
        stream(d.id).use { s -> val h = ByteArray(8); s.read(h) == 8 && String(h, Charsets.US_ASCII) == "MComprHD" }
    } catch (_: Exception) { false }

    // ---- scan ----

    fun scan(progress: (String) -> Unit): List<MachineHits> {
        val cacheFile = File(ctx.filesDir, "scan_cache.txt")
        val cache = HashMap<String, String>()
        if (cacheFile.exists()) cacheFile.readLines().forEach { l -> val i = l.indexOf('\u0004'); if (i > 0) cache[l.substring(0, i)] = l.substring(i + 1) }
        val docs = walk(progress)
        val found = HashMap<String, HashMap<String, Location>>()   // machine -> spec -> where
        for ((i, d) in docs.withIndex()) {
            val lower = d.name.lowercase()
            if (!(lower.endsWith(".zip") || lower.endsWith(".7z") || d.size in sizes || lower in byName)) continue
            progress("Checking ${i + 1} of ${docs.size}: ${d.name}")
            byName[lower]?.let { needs ->
                if (!lower.endsWith(".chd") || isChd(d))
                    for ((m, r) in needs) found.getOrPut(m.id) { HashMap() }.putIfAbsent(r.name, Location(d.id, d.name, d.size))
            }
            for (it in contents(d, cache)) {
                val needs = byCrc[key(it.crc, it.size)] ?: continue
                for ((m, r) in needs) {
                    val loc = Location(d.id, d.name, d.size, it.entry, it.inner)
                    val have = found.getOrPut(m.id) { HashMap() }
                    // prefer a loose file over one in an archive (it can be linked)
                    if (r.name !in have || (have[r.name]!!.entry != null && loc.entry == null)) have[r.name] = loc
                }
            }
        }
        // keep only cache entries for files still there
        val live = docs.map { "${it.id}\u0001${it.size}\u0001${it.modified}" }.toHashSet()
        cacheFile.writeText(cache.filterKeys { it in live }.entries.joinToString("\n") { "${it.key}\u0004${it.value}" })
        return Machines.all.mapNotNull { m -> found[m.id]?.let { MachineHits(m, it) } }
    }

    // ---- install ----

    class Installed(val machines: List<String>, val failed: List<String>, val linked: Int)

    fun install(hits: List<MachineHits>, linkLarge: Boolean, progress: (String) -> Unit): Installed {
        val done = ArrayList<String>(); val failed = ArrayList<String>()
        var linked = 0
        val todo = hits.flatMap { h -> h.found.map { Triple(h.machine, it.key, it.value) } }
        for ((n, t) in todo.withIndex()) {
            val (m, specName, loc) = t
            val spec = m.roms.first { it.name == specName }
            progress("Setting up ${n + 1} of ${todo.size}: ${m.title} - $specName")
            try {
                val dest = RomStore.romFile(ctx, m, spec)
                if (loc.entry == null && linkLarge && loc.docSize > LINK_ABOVE) {
                    RomStore.link(ctx, m, spec, uri(loc.docId))
                    linked++
                    continue
                }
                RomStore.unlink(ctx, m, spec)
                val tmp = File(dest.path + ".part")
                when {
                    loc.entry == null -> stream(loc.docId).use { i -> tmp.outputStream().use { i.copyTo(it, 1 shl 20) } }
                    else -> extract(loc, tmp)
                }
                if (!tmp.renameTo(dest)) throw java.io.IOException("can't write $specName")
            } catch (e: Exception) {
                failed += "${m.title}: $specName (${e.message ?: e.javaClass.simpleName})"
            }
        }
        for (h in hits) if (RomStore.isReady(ctx, h.machine)) done += h.machine.title
        return Installed(done, failed, linked)
    }

    private fun extract(loc: Location, out: File) {
        val zip = loc.docName.lowercase().endsWith(".zip")
        channel(loc.docId).use { ch -> extractFrom(ch, zip, loc.entry!!, loc.inner, out) }
    }

    private fun extractFrom(ch: SeekableByteChannel, zip: Boolean, entry: String, inner: String?, out: File) {
        fun take(open: () -> InputStream) {
            if (inner == null) open().use { i -> out.outputStream().use { i.copyTo(it, 1 shl 20) } }
            else {
                val ext = entry.lowercase().substringAfterLast('.', "")
                val tmp = File.createTempFile("scan", ".$ext", tempDir)
                try {
                    open().use { i -> tmp.outputStream().use { i.copyTo(it, 1 shl 16) } }
                    java.nio.file.Files.newByteChannel(tmp.toPath()).use { c -> extractFrom(c, ext == "zip", inner, null, out) }
                } finally { tmp.delete() }
            }
        }
        if (zip) ZipFile.builder().setSeekableByteChannel(ch).get().use { z ->
            val e = z.getEntry(entry) ?: throw java.io.IOException("$entry is gone")
            take { z.getInputStream(e) }
        } else SevenZFile.builder().setSeekableByteChannel(ch).get().use { z ->
            val e = z.entries.firstOrNull { it.name == entry } ?: throw java.io.IOException("$entry is gone")
            take { z.getInputStream(e) }
        }
    }

    companion object {
        const val LINK_ABOVE = 100L shl 20
        const val PREF_FOLDER = "rom_folder"
    }
}
