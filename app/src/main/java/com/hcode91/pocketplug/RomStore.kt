package com.hcode91.pocketplug

import android.content.Context
import android.net.Uri
import org.apache.commons.compress.archivers.sevenz.SevenZFile
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.util.zip.CRC32
import java.util.zip.ZipInputStream

/** Stores imported ROM sets under files/roms/<machine>/ and save states under files/states/. */
object RomStore {

    data class ImportResult(val found: List<String>, val missing: List<String>, val badCrc: List<String>)

    fun romDir(ctx: Context, m: MachineDef) = File(ctx.filesDir, "roms/${m.id}").apply { mkdirs() }
    fun romFile(ctx: Context, m: MachineDef, spec: RomSpec) = File(romDir(ctx, m), spec.name)
    fun stateFile(ctx: Context, m: MachineDef, slot: Int = 0) =
        File(ctx.filesDir, "states").apply { mkdirs() }.let { File(it, "${m.id}_$slot.state") }

    fun isReady(ctx: Context, m: MachineDef) = m.roms.all { romFile(ctx, m, it).length() == it.size }

    fun delete(ctx: Context, m: MachineDef) { romDir(ctx, m).deleteRecursively() }

    /**
     * Imports a ROM set from a .zip, .7z or a raw ROM file. Files are matched by
     * name first, then by size. Runs on a background thread.
     */
    fun import(ctx: Context, m: MachineDef, uri: Uri, progress: (String) -> Unit): ImportResult {
        val tmp = File(ctx.cacheDir, "import.tmp")
        progress("Copying file…")
        ctx.contentResolver.openInputStream(uri).use { input ->
            requireNotNull(input) { "Could not open the selected file" }
            FileOutputStream(tmp).use { input.copyTo(it, 1 shl 20) }
        }
        try {
            val magic = ByteArray(6)
            tmp.inputStream().use { it.read(magic) }
            val found = mutableSetOf<String>()

            fun take(entryName: String, size: Long, stream: InputStream) {
                val base = entryName.substringAfterLast('/').substringAfterLast('\\')
                val spec = m.roms.firstOrNull { it.name.equals(base, true) && it.name !in found }
                    ?: m.roms.firstOrNull { it.size == size && it.name !in found }
                    ?: return
                progress("Extracting ${spec.name}…")
                FileOutputStream(romFile(ctx, m, spec)).use { stream.copyTo(it, 1 shl 20) }
                found += spec.name
            }

            when {
                magic[0] == 'P'.code.toByte() && magic[1] == 'K'.code.toByte() -> {
                    ZipInputStream(tmp.inputStream().buffered()).use { zip ->
                        var e = zip.nextEntry
                        while (e != null) {
                            if (!e.isDirectory) take(e.name, e.size, zip)
                            e = zip.nextEntry
                        }
                    }
                }
                magic.contentEquals(byteArrayOf(0x37, 0x7a, 0xbc.toByte(), 0xaf.toByte(), 0x27, 0x1c)) -> {
                    SevenZFile.builder().setFile(tmp).get().use { z ->
                        var e = z.nextEntry
                        while (e != null) {
                            if (!e.isDirectory) take(e.name, e.size, z.getInputStream(e))
                            e = z.nextEntry
                        }
                    }
                }
                else -> {
                    // A bare ROM file: accept it if it fits one of the set's files
                    val name = queryName(ctx, uri) ?: "rom.bin"
                    tmp.inputStream().use { take(name, tmp.length(), it) }
                }
            }

            progress("Checking…")
            val bad = m.roms.filter { it.name in found && crc32(romFile(ctx, m, it)) != it.crc }.map { it.name }
            val missing = m.roms.map { it.name }.filter { it !in found && !romFile(ctx, m, m.roms.first { r -> r.name == it }).exists() }
            return ImportResult(found.toList(), missing, bad)
        } finally {
            tmp.delete()
        }
    }

    // ---------------- cartridges ----------------

    data class Cart(val file: File, val title: String, val crc: Long, val known: Boolean)

    fun cartDir(ctx: Context, m: MachineDef): File {
        val dir = File(ctx.filesDir, "carts/${m.cartLibrary}")
        // 0.2.x kept the 1292's games under its own id: move them to the shared library
        val old = File(ctx.filesDir, "carts/${m.id}")
        if (m.cartLibrary != m.id && old.isDirectory && !dir.exists()) old.renameTo(dir)
        return dir.apply { mkdirs() }
    }

    fun cartStateFile(ctx: Context, m: MachineDef, cart: File) =
        File(ctx.filesDir, "states").apply { mkdirs() }.let { File(it, "${m.id}_${cart.nameWithoutExtension}.state") }

    fun listCarts(ctx: Context, m: MachineDef): List<Cart> =
        (cartDir(ctx, m).listFiles() ?: emptyArray()).filter { it.isFile }.map { f ->
            val crc = crc32(f)
            val title = m.knownCarts[crc]
            Cart(f, title ?: f.nameWithoutExtension, crc, title != null)
        }.sortedBy { it.title.lowercase() }

    data class CartImportResult(val added: List<String>, val duplicates: Int, val rejected: Int)

    /**
     * Imports every cartridge found in a file: a raw dump, a .zip / .7z of dumps,
     * or a whole MAME software-list zip (zips inside a zip). Duplicates (same CRC)
     * are skipped; files larger than the machine's cartridge size are rejected.
     */
    fun importCarts(ctx: Context, m: MachineDef, uri: Uri, progress: (String) -> Unit): CartImportResult {
        val tmp = File(ctx.cacheDir, "import.tmp")
        progress("Copying file…")
        ctx.contentResolver.openInputStream(uri).use { input ->
            requireNotNull(input) { "Could not open the selected file" }
            FileOutputStream(tmp).use { input.copyTo(it, 1 shl 20) }
        }
        val dir = cartDir(ctx, m)
        val have = listCarts(ctx, m).map { it.crc }.toMutableSet()
        val added = mutableListOf<String>()
        var dup = 0
        var rejected = 0

        fun addCart(name: String, data: ByteArray) {
            if (data.isEmpty() || data.size > m.cartMaxSize) { rejected++; return }
            val crc = CRC32().apply { update(data) }.value
            if (crc in have) { dup++; return }
            var base = name.substringAfterLast('/').substringAfterLast('\\').substringBeforeLast('.').ifBlank { "cart" }
            base = base.replace(Regex("[^A-Za-z0-9 _().-]"), "_")
            var f = File(dir, "$base.bin")
            var n = 2
            while (f.exists()) { f = File(dir, "$base ($n).bin"); n++ }
            f.writeBytes(data)
            have += crc
            added += m.knownCarts[crc] ?: base
            progress("Added ${added.size}…")
        }

        fun isZip(b: ByteArray) = b.size > 4 && b[0] == 'P'.code.toByte() && b[1] == 'K'.code.toByte()

        // A zip inside the archive is one software-list entry: name it after the zip.
        fun handleEntry(name: String, data: ByteArray) {
            if (isZip(data)) {
                val inner = mutableListOf<Pair<String, ByteArray>>()
                ZipInputStream(data.inputStream()).use { z ->
                    var e = z.nextEntry
                    while (e != null) { if (!e.isDirectory) inner += e.name to z.readBytes(); e = z.nextEntry }
                }
                val zipName = name.substringAfterLast('/').substringBeforeLast('.')
                if (inner.size == 1) addCart(zipName, inner[0].second)
                else inner.sortedBy { it.first }.forEach { addCart(it.first, it.second) }
            } else addCart(name, data)
        }

        try {
            val magic = ByteArray(6)
            tmp.inputStream().use { it.read(magic) }
            when {
                isZip(magic) -> ZipInputStream(tmp.inputStream().buffered()).use { zip ->
                    var e = zip.nextEntry
                    while (e != null) {
                        if (!e.isDirectory) handleEntry(e.name, zip.readBytes())
                        e = zip.nextEntry
                    }
                }
                magic.contentEquals(byteArrayOf(0x37, 0x7a, 0xbc.toByte(), 0xaf.toByte(), 0x27, 0x1c)) ->
                    SevenZFile.builder().setFile(tmp).get().use { z ->
                        var e = z.nextEntry
                        while (e != null) {
                            if (!e.isDirectory) handleEntry(e.name, z.getInputStream(e).readBytes())
                            e = z.nextEntry
                        }
                    }
                else -> addCart(queryName(ctx, uri) ?: "cart", tmp.readBytes())
            }
        } finally {
            tmp.delete()
        }
        return CartImportResult(added, dup, rejected)
    }

    private fun queryName(ctx: Context, uri: Uri): String? =
        ctx.contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
            if (it.moveToFirst()) it.getString(0) else null
        }

    fun crc32(f: File): Long {
        val crc = CRC32()
        val buf = ByteArray(1 shl 20)
        f.inputStream().use { s -> while (true) { val n = s.read(buf); if (n <= 0) break; crc.update(buf, 0, n) } }
        return crc.value
    }
}
