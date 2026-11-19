package com.hcode91.pocketplug

import org.apache.commons.compress.archivers.sevenz.SevenZFile
import org.apache.commons.compress.archivers.zip.ZipFile
import java.io.File
import java.io.InputStream
import java.nio.ByteBuffer
import java.nio.channels.SeekableByteChannel
import java.util.zip.CRC32
import java.util.zip.GZIPInputStream

/**
 * Works out which system a ROM file belongs to, from what's inside it rather
 * than its name: MAME sets by their chips' CRC32s (the index is built from
 * MAME's source by tools/mkromindex.py), MAME hard disks by their SHA1, and
 * console games, disc images and computer disks by their headers or, failing
 * that, their extensions. Pure JVM: the Android side only supplies files.
 */
class RomIndex private constructor(
    val names: Array<String>, val descs: Array<String>, val makers: Array<String>, val years: Array<String>,
    val kinds: CharArray, val parents: IntArray, val romCounts: IntArray,
    private val crcs: IntArray, private val sizes: IntArray, private val sets: IntArray,
    private val disks: Map<String, List<Int>>,
    val listNames: Array<String>, val listDescs: Array<String>,
    private val softDisks: Map<String, Pair<Int, String>>,
) {
    /** A disc / disk image in MAME's software lists: (list index, its name) */
    fun softDisk(sha1Hex: String) = softDisks[sha1Hex]

    private val byName = HashMap<String, Int>(names.size * 2).apply { names.forEachIndexed { i, n -> put(n, i) } }

    fun setNamed(n: String) = byName[n]

    /** Sets holding a chip with this CRC (and size, where the index knows it). */
    fun setsFor(crc: Int, size: Long): List<Int> {
        var lo = 0; var hi = crcs.size
        val key = crc.toLong() and 0xffffffffL
        while (lo < hi) {
            val mid = (lo + hi) ushr 1
            if ((crcs[mid].toLong() and 0xffffffffL) < key) lo = mid + 1 else hi = mid
        }
        val out = ArrayList<Int>()
        var i = lo
        while (i < crcs.size && crcs[i] == crc) {
            if (sizes[i] == 0 || size < 0 || sizes[i].toLong() == size) out += sets[i]
            i++
        }
        return out
    }

    fun setsForDisk(sha1Hex: String): List<Int> = disks[sha1Hex] ?: emptyList()

    companion object {
        fun load(gz: InputStream): RomIndex {
            val d = GZIPInputStream(gz.buffered()).readBytes()
            val b = ByteBuffer.wrap(d).order(java.nio.ByteOrder.LITTLE_ENDIAN)
            val magic = String(d, 0, 4, Charsets.US_ASCII)
            require(magic == "PPRX") { "not a ROM index" }
            val version = (d[4].toInt() and 0xff) or ((d[5].toInt() and 0xff) shl 8)
            b.position(6)
            fun str(): String { val l = b.get().toInt() and 0xff; val s = String(d, b.position(), l, Charsets.UTF_8); b.position(b.position() + l); return s }
            val n = b.int
            val names = Array(n) { "" }; val descs = Array(n) { "" }; val makers = Array(n) { "" }; val years = Array(n) { "" }
            val kinds = CharArray(n); val parents = IntArray(n); val counts = IntArray(n)
            for (i in 0 until n) {
                names[i] = str(); descs[i] = str(); makers[i] = str(); years[i] = str()
                kinds[i] = b.get().toInt().toChar()
                parents[i] = b.short.toInt() and 0xffff
                counts[i] = b.short.toInt() and 0xffff
            }
            val nr = b.int
            val crcs = IntArray(nr); val sizes = IntArray(nr); val sets = IntArray(nr)
            for (i in 0 until nr) { crcs[i] = b.int; sizes[i] = b.int; sets[i] = b.short.toInt() and 0xffff }
            val nd = b.int
            val disks = HashMap<String, MutableList<Int>>(nd * 2)
            for (i in 0 until nd) {
                val sha = ByteArray(20); b.get(sha)
                disks.getOrPut(sha.joinToString("") { "%02x".format(it) }) { ArrayList() } += b.short.toInt() and 0xffff
            }
            var listNames = emptyArray<String>(); var listDescs = emptyArray<String>()
            val soft = HashMap<String, Pair<Int, String>>()
            if (version >= 2) {
                val nl = b.short.toInt() and 0xffff
                listNames = Array(nl) { "" }; listDescs = Array(nl) { "" }
                for (i in 0 until nl) { listNames[i] = str(); listDescs[i] = str() }
                val ns = b.int
                for (i in 0 until ns) {
                    val sha = ByteArray(20); b.get(sha)
                    val li = b.short.toInt() and 0xffff
                    soft[sha.joinToString("") { "%02x".format(it) }] = li to str()
                }
            }
            return RomIndex(names, descs, makers, years, kinds, parents, counts, crcs, sizes, sets, disks, listNames, listDescs, soft)
        }
    }
}

/** One file to identify: its name, size and ways to read it. */
interface SortSource {
    val name: String
    val size: Long
    fun channel(): SeekableByteChannel
    fun stream(): InputStream
}

class FileSource(private val f: File, override val name: String = f.name) : SortSource {
    override val size get() = f.length()
    override fun channel(): SeekableByteChannel = java.nio.file.Files.newByteChannel(f.toPath())
    override fun stream(): InputStream = f.inputStream()
}

/**
 * Where a file goes: [folder] is a path under the sorted folder ("Computers/Archimedes 310 [aa310]").
 * [how] says what identified it. [companions] are files that belong with it (a .cue's tracks).
 */
data class SortResult(val folder: String, val label: String, val how: String, val machine: String? = null,
                      val companions: List<String> = emptyList(), val title: String? = null)

/**
 * [discHead] reads the first sectors of the disc inside a CD / DVD / GD-ROM CHD
 * (native code on Android): a kind ('c', 'd', 'g') and the sectors, laid out as
 * in an image file; null where that isn't available.
 */
class RomSorter(private val index: RomIndex, private val tempDir: File,
                private val discHead: ((SortSource) -> Pair<Char, ByteArray>?)? = null) {

    private data class Entry(val name: String, val size: Long, val crc: Int?)

    fun identify(src: SortSource): SortResult? {
        val lower = src.name.lowercase()
        val ext = lower.substringAfterLast('.', "")
        return when (ext) {
            "zip", "7z" -> identifyArchive(src, ext)
            "chd" -> identifyChd(src)
            "cue", "gdi", "m3u" -> identifyPlaylist(src, ext)
            else -> identifyLoose(src, ext)
        }
    }

    // ---- archives: MAME sets, or a console game zipped up ----

    private fun listArchive(ch: SeekableByteChannel, ext: String, depth: Int): List<Entry> {
        val out = ArrayList<Entry>()
        if (ext == "zip") {
            ZipFile.builder().setSeekableByteChannel(ch).get().use { z ->
                for (e in z.entries) {
                    if (e.isDirectory) continue
                    val n = e.name.substringAfterLast('/')
                    val inner = n.lowercase().substringAfterLast('.', "")
                    if (depth == 0 && (inner == "zip" || inner == "7z") && e.size in 1..(512L shl 20)) {
                        val tmp = File.createTempFile("sort", ".$inner", tempDir)
                        try {
                            z.getInputStream(e).use { i -> tmp.outputStream().use { i.copyTo(it) } }
                            java.nio.file.Files.newByteChannel(tmp.toPath()).use { c -> out += listArchive(c, inner, 1).map { it.copy(name = "$n/${it.name}") } }
                        } catch (_: Exception) {
                        } finally { tmp.delete() }
                    } else out += Entry(n, e.size, if (e.crc >= 0) e.crc.toInt() else null)
                }
            }
        } else {
            SevenZFile.builder().setSeekableByteChannel(ch).get().use { z ->
                val entries = z.entries.filter { !it.isDirectory }
                for (e in entries) {
                    val n = e.name.substringAfterLast('/')
                    val inner = n.lowercase().substringAfterLast('.', "")
                    if (depth == 0 && (inner == "zip" || inner == "7z") && e.size in 1..(512L shl 20)) {
                        val tmp = File.createTempFile("sort", ".$inner", tempDir)
                        try {
                            z.getInputStream(e).use { i -> tmp.outputStream().use { i.copyTo(it) } }
                            java.nio.file.Files.newByteChannel(tmp.toPath()).use { c -> out += listArchive(c, inner, 1).map { it.copy(name = "$n/${it.name}") } }
                        } catch (_: Exception) {
                        } finally { tmp.delete() }
                    } else out += Entry(n, e.size, if (e.hasCrc) e.crcValue.toInt() else null)
                }
            }
        }
        return out
    }

    private fun identifyArchive(src: SortSource, ext: String): SortResult? {
        val entries = try { src.channel().use { listArchive(it, ext, 0) } } catch (_: Exception) { return null }
        if (entries.isEmpty()) return null
        val base = baseName(src.name)
        // MAME: the set with the most chips here; ties go to the set named like the file,
        // then to whole machines over device ROMs, then to the fuller match
        val score = HashMap<Int, Int>()
        for (e in entries) {
            val c = e.crc ?: continue
            for (s in index.setsFor(c, e.size).toSet()) score[s] = (score[s] ?: 0) + 1
        }
        // a zip holding other sets' zips (A3000.zip with a3000.zip inside) is named by the inner one
        val innerNames = entries.mapNotNull { if ('/' in it.name) baseName(it.name.substringBefore('/')) else null }.toSet()
        if (score.isNotEmpty()) {
            // the set the file is named after wins if it explains nearly as much
            val top = score.values.max()
            // (or, for a bundle of several sets, if all of its own chips are there)
            val named = score.entries.firstOrNull { index.names[it.key] == base && (it.value * 5 >= top * 4 || it.value >= index.romCounts[it.key] && it.value >= 2) }
                ?: score.entries.firstOrNull { index.names[it.key] in innerNames && it.value * 5 >= top * 4 }
            if (named != null && (named.value >= 2 || entries.count { it.crc != null } <= 2))
                return mameResult(named.key, "matched ${named.value} chip${if (named.value == 1) "" else "s"}")
            val best = score.entries.maxWithOrNull(compareBy<Map.Entry<Int, Int>> { it.value }
                .thenBy { if (index.names[it.key] == base || index.names[it.key] in innerNames) 1 else 0 }
                .thenBy { if (index.kinds[it.key] == 'D') 0 else 1 }
                .thenBy { it.value.toDouble() / maxOf(1, index.romCounts[it.key]) }
                .thenBy { if (index.parents[it.key] == 0xffff) 1 else 0 })!!
            // one stray chip shared with hundreds of sets isn't an identification
            if (best.value >= 2 || entries.count { it.crc != null } <= 2) return mameResult(best.key, "matched ${best.value} chip${if (best.value == 1) "" else "s"}")
        }
        index.setNamed(base)?.let { return mameResult(it, "named like the MAME set") }
        // a console game in a zip: by the files inside
        val systems = entries.mapNotNull { systemForExtension(it.name.lowercase().substringAfterLast('.', ""), it.size) }.distinct()
        if (systems.size == 1) return SortResult("Games/${systems[0]}", systems[0], "the files inside are ${systems[0]} games")
        return null
    }

    private fun mameResult(set: Int, how: String): SortResult {
        val name = index.names[set]
        val desc = index.descs[set].ifEmpty { name }
        val folder = when (index.kinds[set]) {
            'A' -> "Arcade"
            'K' -> "Computers/${clean(desc)} [$name]"
            'C' -> "Consoles and plug-and-play/${clean(desc)} [$name]"
            'S' -> "Other systems/${clean(desc)} [$name]"
            else -> "Devices and BIOS"
        }
        val label = if (index.kinds[set] == 'D') "$desc ($name, shared ROMs)" else "$desc ($name)"
        return SortResult(folder, label, how, name)
    }

    // ---- CHD: MAME's hard disks by SHA1, GD-ROMs and other discs by their metadata ----

    private fun identifyChd(src: SortSource): SortResult? {
        val h = ByteArray(124)
        val meta = try {
            src.channel().use { ch ->
                ch.read(ByteBuffer.wrap(h))
                if (String(h, 0, 8, Charsets.US_ASCII) != "MComprHD") return null
                if (be32(h, 12) != 5) return SortResult("Disc images", "CHD (old version)", "a CHD older than version 5")
                val sha = h.copyOfRange(84, 104).joinToString("") { "%02x".format(it) }
                // a disc in MAME's software lists: the exact game
                index.softDisk(sha)?.let { (li, disk) ->
                    val sys = listSystem(li)
                    return SortResult("Games/$sys", sys, "its SHA1 is in MAME's ${index.listNames[li]} list", title = disk)
                }
                val owners = index.setsForDisk(sha).distinct()
                if (owners.size == 1) return mameResult(owners[0], "a MAME disk image")
                if (owners.size > 1) {
                    // one disk several machines use (riscos311_apps: A3020, A4000, A4, A5000)
                    val kind = owners.map { index.kinds[it] }.groupingBy { it }.eachCount().maxBy { it.value }.key
                    val top = when (kind) { 'A' -> "Arcade"; 'K' -> "Computers"; 'C' -> "Consoles and plug-and-play"; else -> "Other systems" }
                    val who = owners.joinToString(", ") { index.names[it] }
                    return SortResult("$top/Shared disks", "Hard disk for $who", "a MAME disk image several machines use")
                }
                // metadata tags: GDDD hard disk, CHGD GD-ROM, CHT2/CHTR CD, DVD
                val tags = ArrayList<String>()
                var at = be64(h, 48)
                val mh = ByteBuffer.allocate(16)
                var n = 0
                while (at != 0L && n++ < 32) {
                    mh.clear(); ch.position(at); ch.read(mh)
                    val a = mh.array()
                    tags += String(a, 0, 4, Charsets.US_ASCII)
                    at = be64(a, 8)
                }
                tags
            }
        } catch (_: Exception) { return null }
        // otherwise read the disc itself: its boot sector or ISO 9660 system name
        if ("CHT2" in meta || "CHTR" in meta || "CHCD" in meta || "DVD " in meta) {
            discHead?.let { read ->
                val got = try { read(src) } catch (_: Exception) { null }
                if (got != null) {
                    val size = if (got.first == 'd') 4_700_000_000L else 700_000_000L
                    discSystem(got.second, size)?.let { return SortResult("Games/$it", it, "the disc inside the CHD") }
                }
            }
        }
        return when {
            "CHGD" in meta -> SortResult("Games/Sega Dreamcast", "Sega Dreamcast", "a GD-ROM in a CHD")
            "DVD " in meta -> SortResult("Disc images/DVD", "DVD image", "a DVD in a CHD")
            "CHT2" in meta || "CHTR" in meta || "CHCD" in meta -> SortResult("Disc images/CD", "CD image", "a CD in a CHD")
            "GDDD" in meta -> SortResult("Disc images/Hard disks", "Hard disk image", "a hard disk in a CHD")
            else -> null
        }
    }

    /** Folder name for a software list: the console, without "CD-ROMs" and the like */
    private fun listSystem(li: Int): String {
        val n = index.listNames[li]
        known[n]?.let { return it }
        return clean(index.listDescs[li].replace(Regex("\\s*(CD-ROMs?|GD-ROMs?|DVD-ROMs?|DVDs?|discs?|hard disks?|HDDs?|software|images?)\\b.*$", RegexOption.IGNORE_CASE), "")
            .ifEmpty { n })
    }

    private val known = mapOf(
        "psx" to "Sony PlayStation", "saturn" to "Sega Saturn", "dc" to "Sega Dreamcast",
        "megacd" to "Sega Mega-CD", "megacdj" to "Sega Mega-CD", "segacd" to "Sega Mega-CD",
        "pcecd" to "NEC PC Engine CD", "pcfx" to "NEC PC-FX", "cdi" to "Philips CD-i",
        "fmtowns_cd" to "Fujitsu FM Towns", "cd32" to "Commodore Amiga CD32", "cdtv" to "Commodore CDTV",
        "neocd" to "SNK Neo Geo CD", "3do" to "Panasonic 3DO", "pippin" to "Apple Pippin",
        "jaguar_cd" to "Atari Jaguar CD", "amiga_cd" to "Commodore Amiga", "pc98_cd" to "NEC PC-98",
        "mac_cdrom" to "Apple Macintosh", "vsmile_cd" to "VTech V.Smile Pro", "ibm5170_cdrom" to "IBM PC compatible",
    )

    // ---- .cue / .gdi / .m3u: the sheet decides, its files go with it ----

    private fun identifyPlaylist(src: SortSource, ext: String): SortResult? {
        val text = try { src.stream().use { String(it.readBytes().copyOf(minOf(src.size, 65536L).toInt()), Charsets.ISO_8859_1) } } catch (_: Exception) { return null }
        val files = when (ext) {
            "cue" -> Regex("FILE\\s+\"([^\"]+)\"|FILE\\s+(\\S+)", RegexOption.IGNORE_CASE).findAll(text).map { it.groupValues[1].ifEmpty { it.groupValues[2] } }.toList()
            "gdi" -> text.lines().drop(1).mapNotNull { l -> Regex("\"([^\"]+)\"|^\\s*\\d+\\s+\\d+\\s+\\d+\\s+\\d+\\s+(\\S+)").find(l)?.let { m -> m.groupValues[1].ifEmpty { m.groupValues[2] } } }
            else -> text.lines().map { it.trim() }.filter { it.isNotEmpty() && !it.startsWith("#") }
        }.map { it.substringAfterLast('/').substringAfterLast('\\') }
        if (ext == "gdi") return SortResult("Games/Sega Dreamcast", "Sega Dreamcast", "a GD-ROM sheet (.gdi)", companions = files)
        return SortResult("Disc images", "Disc image", "a ${ext} sheet", companions = files)
    }

    /** For a .cue: the system from its first data track, read by the caller. */
    fun systemForDiscData(head: ByteArray, size: Long): String? = discSystem(head, size)

    // ---- single files ----

    private fun identifyLoose(src: SortSource, ext: String): SortResult? {
        // a MAME ROM chip on its own (small files only: the CRC needs the whole file)
        if (src.size in 1..(64L shl 20) && ext !in discExts) {
            val crc = try { src.stream().use { crc32(it) } } catch (_: Exception) { null }
            if (crc != null) {
                val sets = index.setsFor(crc, src.size)
                if (sets.isNotEmpty()) {
                    val best = sets.maxWithOrNull(compareBy<Int> { if (index.kinds[it] == 'D') 0 else 1 }.thenBy { if (index.parents[it] == 0xffff) 1 else 0 })!!
                    if (sets.size <= 8 || systemForExtension(ext, src.size) == null) return mameResult(best, "a MAME ROM chip")
                }
            }
        }
        val head = ByteArray(0x10000)
        val got = try { src.channel().use { readAt(it, 0, head) } } catch (_: Exception) { 0 }
        if (ext in discExts || ext == "bin" && src.size > (16L shl 20)) {
            // discs: ISO 9660 at sector 16 (2048-byte or raw 2352-byte sectors) and the boot sectors
            val sec = ByteArray(0x9400)
            val n = try { src.channel().use { readAt(it, 0, sec) } } catch (_: Exception) { 0 }
            discSystem(sec.copyOf(n), src.size)?.let { return SortResult("Games/$it", it, "the disc's own header") }
        }
        headerSystem(head.copyOf(got), ext, src.size)?.let { return SortResult("Games/$it", it, "the file's header") }
        systemForExtension(ext, src.size)?.let { return SortResult("Games/$it", it, "its .$ext extension") }
        return null
    }

    private fun readAt(ch: SeekableByteChannel, pos: Long, dst: ByteArray): Int {
        ch.position(pos)
        val bb = ByteBuffer.wrap(dst)
        while (bb.hasRemaining()) if (ch.read(bb) <= 0) break
        return bb.position()
    }

    private fun headerSystem(h: ByteArray, ext: String, size: Long): String? {
        fun at(off: Int, s: String) = h.size >= off + s.length && String(h, off, s.length, Charsets.ISO_8859_1) == s
        if (at(0, "NES\u001a")) return "Nintendo NES"
        if (at(0, "FDS\u001a") || at(1, "*NINTENDO-HVC*")) return "Nintendo Famicom Disk System"
        if (h.size > 0x134 && (h[0x104].toInt() and 0xff) == 0xce && (h[0x105].toInt() and 0xff) == 0xed) // the boot logo
            return if ((h[0x143].toInt() and 0x80) != 0) "Nintendo Game Boy Color" else "Nintendo Game Boy"
        if (h.size > 0xc0 && (h[4].toInt() and 0xff) == 0x24 && (h[5].toInt() and 0xff) == 0xff && (h[6].toInt() and 0xff) == 0xae) return "Nintendo Game Boy Advance"
        if (at(0, "\u00807\u0012@") || at(0, "7\u0080@\u0012") || at(0, "@\u00127\u0080")) return "Nintendo 64"
        if (at(0x100, "SEGA") && !at(0x100, "SEGA SEGASATURN")) return if (at(0x100, "SEGA 32X")) "Sega 32X" else "Sega Mega Drive"
        if (at(0x7ff0, "TMR SEGA") || at(0x3ff0, "TMR SEGA") || at(0x1ff0, "TMR SEGA"))
            return if (ext == "gg") "Sega Game Gear" else "Sega Master System"
        if (at(0, "LYNX")) return "Atari Lynx"
        if (at(1, "ATARI7800")) return "Atari 7800"
        if (at(0, "\u0096\u0002") && ext == "atr") return "Atari 8-bit"
        if (at(0, "DOS") && (ext == "adf" || size == 901120L)) return "Commodore Amiga"
        if (at(0, "UAE-1ADF") || at(0, "UAE--ADF")) return "Commodore Amiga"
        if (at(0, "CAPS")) return "Commodore Amiga"                 // .ipf
        if (at(0, "C64 CARTRIDGE")) return "Commodore 64"
        if (at(0, "ZXTape!")) return "Sinclair ZX Spectrum"
        if (at(0, "MV - CPC") || at(0, "EXTENDED CPC DSK")) return "Amstrad CPC"
        if (at(0, "WOZ1") || at(0, "WOZ2") || at(0, "2IMG")) return "Apple II"
        if (at(0, "UEF File!")) return "Acorn BBC Micro and Electron"
        if (at(0, "HXCPICFE")) return null                          // .hfe: Acorn, Amiga, ST ... can't tell
        if (at(0, "NCSD") || at(0x100, "NCSD")) return "Nintendo 3DS"
        if (at(0, "PBP\u0000")) return "Sony PlayStation Portable"
        if (at(0, "CISO")) return "Sony PlayStation Portable"
        if (at(0, "WBFS") || at(0, "RVZ\u0001") || at(0, "WIA\u0001")) return "Nintendo GameCube and Wii"
        if (h.size > 0x20 && be32(h, 0x18) == 0x5D1C9EA3) return "Nintendo Wii"
        if (h.size > 0x20 && be32(h, 0x1c) == 0xC2339F3D.toInt()) return "Nintendo GameCube"
        return null
    }

    private fun discSystem(d: ByteArray, size: Long): String? {
        fun at(off: Int, s: String) = d.size >= off + s.length && String(d, off, s.length, Charsets.ISO_8859_1) == s
        // the first sector (raw images have a 16-byte sync / header first)
        for (o in intArrayOf(0, 16)) {
            if (at(o, "SEGA SEGASATURN")) return "Sega Saturn"
            if (at(o, "SEGA SEGAKATANA")) return "Sega Dreamcast"
            if (at(o, "SEGADISCSYSTEM") || at(o, "SEGABOOTDISC")) return "Sega Mega-CD"
            if (at(o + 0x10, "PC Engine CD-ROM SYSTEM") || at(o + 0x20, "PC Engine CD-ROM SYSTEM")) return "NEC PC Engine CD"
        }
        if (d.size > 0x20 && be32(d, 0x18) == 0x5D1C9EA3) return "Nintendo Wii"
        if (d.size > 0x20 && be32(d, 0x1c) == 0xC2339F3D.toInt()) return "Nintendo GameCube"
        // ISO 9660's volume descriptor at sector 16: the system identifier
        for (pvd in intArrayOf(16 * 2048, 16 * 2352 + 24, 16 * 2352 + 16)) {
            if (at(pvd + 1, "CD001")) {
                val sys = String(d, pvd + 8, 32, Charsets.ISO_8859_1).trim()
                return when {
                    sys.startsWith("PLAYSTATION") -> if (size > 900L shl 20) "Sony PlayStation 2" else "Sony PlayStation"
                    sys.startsWith("PSP GAME") -> "Sony PlayStation Portable"
                    sys.startsWith("SEGA SEGASATURN") -> "Sega Saturn"
                    sys.startsWith("CD-RTOS") -> "Philips CD-i"
                    else -> null
                }
            }
        }
        return null
    }

    /** Last resort: what the extension says, where it's not ambiguous. */
    private fun systemForExtension(ext: String, size: Long): String? = when (ext) {
        "nes", "unf", "unif" -> "Nintendo NES"
        "fds" -> "Nintendo Famicom Disk System"
        "sfc", "smc", "fig", "swc" -> "Nintendo SNES"
        "n64", "z64", "v64" -> "Nintendo 64"
        "gb" -> "Nintendo Game Boy"
        "gbc" -> "Nintendo Game Boy Color"
        "gba" -> "Nintendo Game Boy Advance"
        "nds", "dsi" -> "Nintendo DS"
        "3ds", "cci", "cia", "cxi" -> "Nintendo 3DS"
        "xci", "nsp" -> "Nintendo Switch"
        "gcm", "gcz", "rvz", "wbfs", "wia" -> "Nintendo GameCube and Wii"
        "vb" -> "Nintendo Virtual Boy"
        "min" -> "Nintendo Pokemon Mini"
        "md", "gen", "smd" -> "Sega Mega Drive"
        "32x" -> "Sega 32X"
        "sms" -> "Sega Master System"
        "gg" -> "Sega Game Gear"
        "sg" -> "Sega SG-1000"
        "pce", "sgx" -> "NEC PC Engine"
        "a26" -> "Atari 2600"
        "a52" -> "Atari 5200"
        "a78" -> "Atari 7800"
        "lnx", "lyx" -> "Atari Lynx"
        "j64", "jag" -> "Atari Jaguar"
        "atr", "atx", "xfd", "xex", "car", "cas" -> "Atari 8-bit"
        "st", "msa", "stx" -> "Atari ST"
        "ws" -> "Bandai WonderSwan"
        "wsc" -> "Bandai WonderSwan Color"
        "ngp" -> "SNK Neo Geo Pocket"
        "ngc" -> "SNK Neo Geo Pocket Color"
        "col" -> "Coleco ColecoVision"
        "int" -> "Mattel Intellivision"
        "vec" -> "GCE Vectrex"
        "d64", "d71", "d81", "t64", "prg", "crt", "g64", "p00" -> "Commodore 64"
        "tzx", "z80", "sna", "szx" -> "Sinclair ZX Spectrum"
        "cdt" -> "Amstrad CPC"
        "woz", "nib", "2mg", "po" -> "Apple II"
        "ssd", "dsd", "uef" -> "Acorn BBC Micro and Electron"
        // .adf is Amiga (880 KB) or Acorn; Amiga ones start with "DOS" and are caught above
        "adf" -> if (size == 901120L || size == 1802240L) "Commodore Amiga" else "Acorn Archimedes"
        "adl", "apd", "jfd" -> "Acorn Archimedes"
        "ipf" -> "Commodore Amiga"
        "pbp", "cso" -> "Sony PlayStation Portable"
        "vpk" -> "Sony PlayStation Vita"
        "vtx" -> "VideoNow"
        else -> null
    }

    companion object {
        val discExts = setOf("iso", "img", "gcm", "cdi", "nrg", "mdf", "ccd")

        fun crc32(i: InputStream): Int {
            val c = CRC32()
            val buf = ByteArray(1 shl 16)
            while (true) { val n = i.read(buf); if (n <= 0) break; c.update(buf, 0, n) }
            return c.value.toInt()
        }

        fun be32(b: ByteArray, o: Int) = ((b[o].toInt() and 0xff) shl 24) or ((b[o + 1].toInt() and 0xff) shl 16) or
            ((b[o + 2].toInt() and 0xff) shl 8) or (b[o + 3].toInt() and 0xff)

        fun be64(b: ByteArray, o: Int) = (be32(b, o).toLong() shl 32) or (be32(b, o + 4).toLong() and 0xffffffffL)

        /** "Aa310 (1).zip" -> "aa310" */
        fun baseName(n: String) = n.substringAfterLast('/').substringBeforeLast('.').lowercase()
            .replace(Regex("\\s*\\(\\d+\\)$"), "").replace(Regex("^[0-9a-f]{8}-"), "").trim()

        /** Folder names: no characters Android's storage refuses */
        fun clean(s: String) = s.replace(Regex("[\\\\/:*?\"<>|]"), "-").replace(Regex("\\s+"), " ").trim().take(80)
    }
}
