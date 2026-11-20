package com.hcode91.pocketplug

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.view.View
import android.widget.LinearLayout
import java.io.File
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * Pictures for the machine list: each machine's own skin (the one it plays
 * in), drawn off screen with nothing on its display, cropped to the device
 * and cached as a PNG. Re-drawn when the app's version changes.
 */
object SkinPreview {
    private const val MAX_SIDE = 640

    private fun landscape(s: Skin) = s == Skin.ALTAIR || s == Skin.AIM65 || s == Skin.KIT || s == Skin.WIDE_HANDHELD

    private fun version(ctx: Context): Long =
        runCatching { ctx.packageManager.getPackageInfo(ctx.packageName, 0).longVersionCode }.getOrDefault(0L)

    fun cacheFile(ctx: Context, m: MachineDef): File =
        File(File(ctx.filesDir, "thumbs").apply { mkdirs() }, "${m.id}_${version(ctx)}.png")

    /** Removes pictures left over from older versions. */
    fun prune(ctx: Context) {
        val v = "_${version(ctx)}.png"
        File(ctx.filesDir, "thumbs").listFiles()?.forEach { if (!it.name.endsWith(v)) it.delete() }
    }

    fun cached(ctx: Context, m: MachineDef): Bitmap? {
        val f = cacheFile(ctx, m)
        return if (f.exists()) android.graphics.BitmapFactory.decodeFile(f.absolutePath) else null
    }

    private fun tv(ctx: Context) = TvView(ctx, 256, 240)

    private fun stack(ctx: Context, vararg parts: Pair<View, Float>) = StackLayout(ctx).apply {
        setBackgroundColor(0xFF0B0814.toInt())
        for ((v, r) in parts) add(v, r)
    }

    /** The skin's view, built as the emulator builds it (without any of its callbacks). */
    private fun build(ctx: Context, m: MachineDef): View = when (m.skin) {
        Skin.DANCE_MAT -> stack(ctx, tv(ctx) to 0.92f, DanceMatView(ctx) to 1.12f)
        Skin.ABL_TV -> stack(ctx, tv(ctx) to 0.92f, AblPadView(ctx, m.badge) to 0.8f)
        Skin.JOYSTICK_TV -> stack(ctx, tv(ctx) to 0.92f, JoystickPadView(ctx) to 0.8f)
        Skin.KEYCHAIN -> KeychainGameView(ctx)
        Skin.HH -> HhView(ctx, HhSkin.forMachine(m.id)!!, 960, 350)
        Skin.FLIGHT_STICK -> stack(ctx, tv(ctx) to 0.875f, FlightStickView(ctx) to 1.1f)
        Skin.APVS_1292 -> {
            val console: View = if (m.badge == "1392") Apvs1392ConsoleView(ctx, m.badge, "")
                else ApvsConsoleView(ctx, m.badge.ifEmpty { "1292" }, "")
            val row = LinearLayout(ctx).apply {
                orientation = LinearLayout.HORIZONTAL
                for (pl in 0..1) addView(ApvsControllerView(ctx, pl, greyBody = m.badge != "1392"),
                    LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f))
            }
            stack(ctx, tv(ctx) to 0.92f, console to 0.34f, row to 0.92f)
        }
        Skin.APF -> {
            val row = LinearLayout(ctx).apply {
                orientation = LinearLayout.HORIZONTAL
                for (pl in 0..1) addView(ApfControllerView(ctx, pl), LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f))
            }
            stack(ctx, tv(ctx) to 0.75f, ApfConsoleView(ctx, "") to 0.34f, row to 0.95f)
        }
        Skin.TV_CONSOLE -> stack(ctx, tv(ctx) to 0.85f, Arcade3dConsoleView(ctx) to 0.42f, Arcade3dPadView(ctx) to 0.5f)
        Skin.EGG_PET -> EggPetView(ctx, if (m.badge == "angel" || m.badge == "umino") m.badge else "gen1")
        Skin.PHONE_CASE -> PhoneCaseView(ctx, m.id == "36pcase")
        Skin.ARCADE_CABINET -> ArcadeCabinetView(ctx, a6 = m.id == "a6plus")
        Skin.HANDHELD -> HandheldView(ctx)
        Skin.MINI_ARCADE -> MiniArcadeView(ctx)
        Skin.WIDE_HANDHELD -> WideHandheldView(ctx)
        Skin.CHESS -> ChessComputerView(ctx, m.badge)
        Skin.NIXIE_CLOCK -> NixieClockView(ctx)
        Skin.ADAM -> AdamView(ctx, 272, 208)
        Skin.TABLETOP -> TabletopView(ctx, 480, 160)
        Skin.ALTAIR -> AltairView(ctx, m.id == "al8800bt")
        Skin.AIM65 -> AimView(ctx)
        Skin.KIT -> KitBoardView(ctx, m.badge)
        Skin.UNIVERSITY_BOARD -> UniversityBoardView(ctx, m.id == "990189v")
        Skin.WORKSTATION -> WorkstationView(ctx)
        Skin.DESKTOP_3B1 -> Desktop3b1View(ctx)
        Skin.ATARI_XL -> AtariXlView(ctx, when (m.badge) {
            "130xe", "65xe", "800xe" -> AtariXlView.MODEL_XE
            "400" -> AtariXlView.MODEL_400
            "800" -> AtariXlView.MODEL_800
            "800xl" -> AtariXlView.MODEL_800XL
            "600xl" -> AtariXlView.MODEL_600XL
            else -> AtariXlView.MODEL_1200XL
        })
        Skin.ACORN -> AcornView(ctx, m.badge)
    }

    /** Draws, crops and caches the picture. Call on the main thread; null if the skin can't be drawn. */
    fun render(ctx: Context, m: MachineDef): Bitmap? = runCatching {
        val short = 720; val long = 1560
        val w = if (landscape(m.skin)) long else short
        val h = if (landscape(m.skin)) short else long
        val v = build(ctx, m)
        v.measure(View.MeasureSpec.makeMeasureSpec(w, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(h, View.MeasureSpec.EXACTLY))
        v.layout(0, 0, w, h)
        val full = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        v.draw(Canvas(full))
        val out = crop(full)
        if (out !== full) full.recycle()
        runCatching { cacheFile(ctx, m).outputStream().use { out.compress(Bitmap.CompressFormat.PNG, 100, it) } }
        out
    }.getOrNull()

    /** Trims the plain background around the device, then scales down. */
    private fun crop(b: Bitmap): Bitmap {
        val w = b.width; val h = b.height
        val px = IntArray(w * h)
        b.getPixels(px, 0, w, 0, 0, w, h)
        val bg = px[0]
        fun differs(c: Int) = abs(Color.red(c) - Color.red(bg)) > 20 || abs(Color.green(c) - Color.green(bg)) > 20 ||
            abs(Color.blue(c) - Color.blue(bg)) > 20
        var l = w; var t = h; var r = -1; var bt = -1
        var y = 0
        while (y < h) {
            var x = 0
            val row = y * w
            while (x < w) {
                if (differs(px[row + x])) {
                    if (x < l) l = x; if (x > r) r = x
                    if (y < t) t = y; if (y > bt) bt = y
                }
                x += 2
            }
            y += 2
        }
        if (r < 0) { l = 0; t = 0; r = w - 1; bt = h - 1 }
        val pad = (max(r - l, bt - t) * 0.03f).roundToInt()
        l = (l - pad).coerceAtLeast(0); t = (t - pad).coerceAtLeast(0)
        r = (r + pad).coerceAtMost(w - 1); bt = (bt + pad).coerceAtMost(h - 1)
        val cw = r - l + 1; val ch = bt - t + 1
        val s = minOf(1f, MAX_SIDE.toFloat() / max(cw, ch))
        val cropped = Bitmap.createBitmap(b, l, t, cw, ch)
        if (s >= 1f) return cropped
        val scaled = Bitmap.createScaledBitmap(cropped, (cw * s).roundToInt().coerceAtLeast(1), (ch * s).roundToInt().coerceAtLeast(1), true)
        if (scaled !== cropped) cropped.recycle()
        return scaled
    }

    /** Background colour of the picture (its top-left corner), to fill around it in a tile. */
    fun background(b: Bitmap): Int = b.getPixel(0, 0)
}
