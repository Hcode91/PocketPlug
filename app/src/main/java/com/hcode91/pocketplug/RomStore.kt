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
