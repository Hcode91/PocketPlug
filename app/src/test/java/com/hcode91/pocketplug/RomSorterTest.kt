package com.hcode91.pocketplug

import org.junit.Assert.assertEquals
import org.junit.Test
import java.io.File

/** Runs the sorter over a folder of real files when SORT_TEST_DIR is set (local check, not CI). */
class RomSorterTest {
    private fun sorter(): RomSorter {
        val idx = File("src/main/assets/romindex.bin.gz").inputStream().use { RomIndex.load(it) }
        return RomSorter(idx, File(System.getProperty("java.io.tmpdir")))
    }

    @Test fun baseNames() {
        assertEquals("aa310", RomSorter.baseName("Aa310 (1).zip"))
        assertEquals("a3000", RomSorter.baseName("6189117c-A3000.zip"))
    }

    @Test fun folder() {
        val dir = System.getenv("SORT_TEST_DIR") ?: return
        val s = sorter()
        val out = StringBuilder()
        for (f in File(dir).listFiles()!!.sortedBy { it.name }) {
            if (!f.isFile) continue
            val r = try { s.identify(FileSource(f)) } catch (e: Exception) { null }
            out.append("${f.name} -> ${r?.folder ?: "(unknown)"}  [${r?.how ?: ""}]\n")
        }
        File(System.getenv("SORT_TEST_OUT") ?: "/tmp/sort.txt").writeText(out.toString())
    }
}
