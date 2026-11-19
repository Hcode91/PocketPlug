package com.hcode91.pocketplug

import android.content.ContentResolver
import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.DocumentsContract.Document
import java.io.File
import java.io.FileInputStream
import java.io.InputStream
import java.nio.ByteBuffer
import java.nio.channels.SeekableByteChannel

/**
 * Sorts a folder the user picked (through Android's folder picker) into
 * Sorted/<category>/<system>/ inside it, using [RomSorter]. Two steps: [scan]
 * works out where everything goes without changing anything, [move] does it.
 * Files it can't identify stay where they are; a file whose name is already
 * taken in its new folder goes to Sorted/Duplicates/ instead of replacing it.
 */
class FolderSorter(private val ctx: Context, private val tree: Uri) {

    class Doc(val id: String, val name: String, val size: Long, val parentId: String)
    class Plan(val doc: Doc, val result: SortResult?, val companionOf: Doc? = null)

    private val cr: ContentResolver = ctx.contentResolver
    private val rootId = DocumentsContract.getTreeDocumentId(tree)
    private fun uri(id: String) = DocumentsContract.buildDocumentUriUsingTree(tree, id)

    private fun children(parentId: String): List<Triple<String, String, Pair<String, Long>>> {
        val out = ArrayList<Triple<String, String, Pair<String, Long>>>()
        cr.query(DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId),
            arrayOf(Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE, Document.COLUMN_SIZE),
            null, null, null)?.use { c ->
            while (c.moveToNext()) out += Triple(c.getString(0), c.getString(1) ?: "", (c.getString(2) ?: "") to (if (c.isNull(3)) 0L else c.getLong(3)))
        }
        return out
    }

    /** Every file under the folder, leaving out what's already in Sorted/. */
    private fun walk(progress: (String) -> Unit): List<Doc> {
        val out = ArrayList<Doc>()
        val stack = ArrayDeque<String>().apply { add(rootId) }
        while (stack.isNotEmpty()) {
            val dir = stack.removeLast()
            for ((id, name, ms) in children(dir)) {
                if (ms.first == Document.MIME_TYPE_DIR) {
                    if (!(dir == rootId && name == SORTED)) stack += id
                } else out += Doc(id, name, ms.second, dir)
            }
            progress("Looking through folders: ${out.size} files")
        }
        return out
    }

    inner class DocSource(val doc: Doc) : SortSource {
        fun fd() = cr.openFileDescriptor(uri(doc.id), "r")
        override val name get() = doc.name
        override val size get() = doc.size
        override fun channel(): SeekableByteChannel {
            val pfd = cr.openFileDescriptor(uri(doc.id), "r") ?: throw java.io.IOException("can't open ${doc.name}")
            val ch = FileInputStream(pfd.fileDescriptor).channel
            // closing the channel closes the descriptor too
            return object : SeekableByteChannel by ch {
                override fun close() { try { ch.close() } finally { pfd.close() } }
                override fun read(dst: ByteBuffer) = ch.read(dst)
            }
        }
        override fun stream(): InputStream = cr.openInputStream(uri(doc.id)) ?: throw java.io.IOException("can't open ${doc.name}")
    }

    fun scan(sorter: RomSorter, progress: (String) -> Unit): List<Plan> {
        val docs = walk(progress)
        val plans = LinkedHashMap<String, Plan>()
        val bySibling = docs.groupBy { it.parentId }
        val taken = HashSet<String>()
        for ((i, d) in docs.withIndex()) {
            if (d.id in taken) continue
            progress("Checking ${i + 1} of ${docs.size}: ${d.name}")
            var r = try { sorter.identify(DocSource(d)) } catch (_: Exception) { null }
            // a .cue / .gdi / .m3u: its tracks go with it; a .cue's system from its first track
            if (r != null && r.companions.isNotEmpty()) {
                val sibs = bySibling[d.parentId].orEmpty()
                val parts = r.companions.mapNotNull { c -> sibs.firstOrNull { it.name.equals(c, true) } }
                if (r.folder == "Disc images" && parts.isNotEmpty()) {
                    val head = ByteArray(0x9400)
                    val n = try { DocSource(parts[0]).channel().use { ch -> val bb = ByteBuffer.wrap(head); while (bb.hasRemaining() && ch.read(bb) > 0) {}; bb.position() } } catch (_: Exception) { 0 }
                    sorter.systemForDiscData(head.copyOf(n), parts[0].size)?.let { sys -> r = SortResult("Games/$sys", sys, "its disc's own header", companions = r!!.companions) }
                }
                // tracks go with their sheet, even if they were looked at on their own first
                for (p in parts) { taken += p.id; plans[p.id] = Plan(p, r, companionOf = d) }
            }
            plans[d.id] = Plan(d, r)
        }
        return plans.values.toList()
    }

    private val dirCache = HashMap<String, String>()

    /** The folder at this path under the root, created as needed; returns its document id. */
    private fun dir(path: String): String {
        dirCache[path]?.let { return it }
        var cur = rootId
        var sofar = ""
        for (seg in path.split('/').filter { it.isNotEmpty() }) {
            sofar = if (sofar.isEmpty()) seg else "$sofar/$seg"
            val cached = dirCache[sofar]
            if (cached != null) { cur = cached; continue }
            val existing = children(cur).firstOrNull { it.second == seg && it.third.first == Document.MIME_TYPE_DIR }?.first
            cur = existing ?: DocumentsContract.createDocument(cr, uri(cur), Document.MIME_TYPE_DIR, seg)
                ?.let { DocumentsContract.getDocumentId(it) } ?: throw java.io.IOException("can't make $sofar")
            dirCache[sofar] = cur
        }
        return cur
    }

    class Moved(val moved: Int, val duplicates: Int, val failed: List<String>)

    fun move(plans: List<Plan>, progress: (String) -> Unit): Moved {
        var moved = 0; var dups = 0
        val failed = ArrayList<String>()
        val names = HashMap<String, MutableSet<String>>()   // folder id -> names in it
        val todo = plans.filter { it.result != null }
        for ((i, p) in todo.withIndex()) {
            val r = p.result!!
            progress("Moving ${i + 1} of ${todo.size}: ${p.doc.name}")
            try {
                var target = dir("$SORTED/${r.folder}")
                val inThere = names.getOrPut(target) { children(target).map { it.second.lowercase() }.toMutableSet() }
                var dup = false
                if (p.doc.name.lowercase() in inThere) {
                    target = dir("$SORTED/Duplicates/${r.folder}")
                    dup = true
                    val inDup = names.getOrPut(target) { children(target).map { it.second.lowercase() }.toMutableSet() }
                    if (p.doc.name.lowercase() in inDup) { failed += "${p.doc.name} (already in Duplicates)"; continue }
                }
                val res = DocumentsContract.moveDocument(cr, uri(p.doc.id), uri(p.doc.parentId), uri(target))
                if (res == null) { failed += p.doc.name; continue }
                names.getOrPut(target) { HashSet() } += p.doc.name.lowercase()
                if (dup) dups++ else moved++
            } catch (e: Exception) {
                failed += "${p.doc.name}: ${e.message ?: e.javaClass.simpleName}"
            }
        }
        return Moved(moved, dups, failed)
    }

    companion object {
        const val SORTED = "Sorted"

        @Volatile private var index: RomIndex? = null
        fun index(ctx: Context): RomIndex = index ?: synchronized(this) {
            index ?: ctx.assets.open("romindex.bin.gz").use { RomIndex.load(it) }.also { index = it }
        }

        fun sorter(ctx: Context) = RomSorter(index(ctx), File(ctx.cacheDir, "sort").apply { mkdirs() }) { src ->
            // the native CHD reader, through the file's descriptor
            (src as? FolderSorter.DocSource)?.let { ds ->
                ds.fd()?.use { pfd ->
                    Native.chdDiscHead("/proc/self/fd/${pfd.fd}")?.let { a -> if (a.isEmpty()) null else a[0].toInt().toChar() to a.copyOfRange(1, a.size) }
                }
            }
        }
    }
}
