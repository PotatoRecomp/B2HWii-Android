package com.shiinobu.basara;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.util.DisplayMetrics;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * The settings, one section at a time, drawn into a column the caller owns: the launcher's page (under
 * its category tabs) or the in-game menu. Each change is saved to the app's preferences and written to
 * Config.toml at once, then `onChanged` runs (the launcher redraws its summary, the game applies what
 * it can while playing).
 *
 * In the game, settings that are only read when the game starts carry a red "restart" warning: they
 * are saved, and apply the next time the game starts.
 */
final class SettingsMenu {
    private static final String TAG = "Basara";

    static final int DISPLAY = 0, GRAPHICS = 1, PERFORMANCE = 2, CONTROLS = 3, SAVE_DATA = 4, CHEATS = 5,
            TEXTURES = 6, SYSTEM = 7, DEVELOPER = 8;
    private static final String[] NAMES = {"Display", "Graphics", "Performance", "Controls", "Save Data",
        "Cheats", "Textures", "System", "Developer"};
    private static final int[] ICONS = {R.drawable.ic_display, R.drawable.ic_graphics, R.drawable.ic_performance,
        R.drawable.ic_controls, R.drawable.ic_save, R.drawable.ic_cheats, R.drawable.ic_textures,
        R.drawable.ic_system, R.drawable.ic_developer};

    static String name(int section) { return Tr.t(NAMES[section]); }
    static int icon(int section) { return ICONS[section]; }
    /** Sections a player should know are risky (warning colour on their tab). */
    static boolean risky(int section) { return section == CHEATS; }
    /** A tab shown greyed out that cannot be opened: Cheats in a build without them (-PbasaraCheats). */
    static boolean disabled(int section) { return section == CHEATS && !BuildConfig.CHEATS; }

    /**
     * The game's menu holds every setting, System last; Textures is a group at the end of Graphics so
     * the tabs fit one row. Cheats is listed in every build, greyed out without BuildConfig.CHEATS.
     */
    static int[] gameSections() {
        return new int[] {DISPLAY, GRAPHICS, PERFORMANCE, CONTROLS, SAVE_DATA, CHEATS, SYSTEM};
    }

    /** The game's own rows for System (game file, updates). */
    interface SystemRows { void fill(SettingsMenu menu); }

    private final Activity activity;
    private final LinearLayout rows;
    private final boolean inGame;
    private final Runnable onChanged;
    private AppSettings settings;
    private int section = DISPLAY;
    SystemRows systemRows;

    SettingsMenu(Activity activity, LinearLayout rows, boolean inGame, Runnable onChanged) {
        this.activity = activity;
        this.rows = rows;
        this.inGame = inGame;
        this.onChanged = onChanged;
        this.settings = new AppSettings(activity);
    }

    int section() { return section; }

    private void save() {
        try {
            AppFiles.writeConfig(activity, settings);
        } catch (Exception e) {
            Log.e(TAG, "could not save the settings", e);
        }
    }

    /** After a change: save, tell the owner, and draw the section again with the new values. */
    void changed() {
        save();
        if (onChanged != null) onChanged.run();
        show(section);
    }

    /** Draws `index` again in place (the views swap before the next frame, so nothing jumps). */
    void show(int index) {
        if ((index == TEXTURES || index == GRAPHICS) && section != index && !hdBusy) hdCacheBytes = -1;  // count again on entry
        section = index;
        settings = new AppSettings(activity);  // the other process may have written it
        rows.removeAllViews();
        switch (index) {
            case DISPLAY: showDisplay(); break;
            case GRAPHICS:
                showGraphics();
                group(Tr.t("Textures"));
                showTextures();
                break;
            case PERFORMANCE: showPerformance(); break;
            case CONTROLS: showControls(); break;
            case SAVE_DATA: showSaveData(); break;
            case CHEATS: showCheats(); break;
            case TEXTURES: showTextures(); break;
            default:
                if (systemRows != null) systemRows.fill(this);
                break;
        }
    }

    /**
     * A setting read once when the game starts: the card already says so in red. It is saved now and
     * applies after Restart (beside Resume) or the next time the app opens.
     */
    private void confirmRestart(String name, String now, String before, Runnable revert) {
        changed();
    }

    // --- sections --------------------------------------------------------------------------------

    private void showDisplay() {
        choiceRow(Tr.t("Language"), Tr.t("The menus' language. Auto follows the phone's region: Indonesian "
                        + "in Indonesia, English elsewhere."),
                new String[] {Tr.t("Auto"), "English", "Bahasa Indonesia"}, settings.language(), true, which -> {
                    settings.setLanguage(which);
                    Tr.init(activity);
                    changed();  // drawn again in the new language (the side list on the next open)
                });
        choiceRow(Tr.t("Aspect Ratio"), Tr.t("The shape of the picture."),
                new String[] {Tr.t("Original 4:3"), "16:9", Tr.t("Fill Screen")}, settings.aspectMode(), true,
                which -> settings.setAspectMode(which));
        restart(null);
        toggleRow(Tr.t("Real Widescreen"), Tr.t("16:9 and Fill Screen show more of the field to the sides instead of "
                        + "stretching it. It also wakes more enemies, so slower phones run heavier and the sound "
                        + "can break up. Off: stretched, as in 1.0."),
                settings.realWidescreen(), true, on -> {
                    settings.setRealWidescreen(on);
                    confirmRestart(Tr.t("Real Widescreen"), on ? "on" : "off", on ? "off" : "on",
                            () -> settings.setRealWidescreen(!on));
                }, false);
        resolutionRow();
        displayResolutionRow();
        choiceRow(Tr.t("Camera Distance"), Tr.t("How far the camera stays from your character in battle. Farther shows "
                        + "more of the field. Off in LAN play."),
                new String[] {Tr.t("Default"), "1.5x", "2x"}, settings.cameraDistance(), true,
                which -> settings.setCameraDistance(which));
        heldToggle("show_fps", Tr.t("Performance Overlay"), Tr.t("A see-through bar at the top of the game with FPS and phone stats."),
                settings.showFps(), on -> settings.setShowFps(on));
        final String[] items = {Tr.t("FPS"), Tr.t("CPU %"), Tr.t("GPU %"), Tr.t("Battery"), Tr.t("Temperature °C"), Tr.t("Details (ms, draws, RAM)"),
                Tr.t("Frametime Graph")};
        final int mask = settings.overlayItems();
        final boolean[] lit = new boolean[items.length];
        for (int i = 0; i < 6; ++i) lit[i] = (mask & (1 << i)) != 0;
        lit[6] = settings.frameGraph();
        multiRow(Tr.t("Overlay Items"), settings.showFps()
                        ? Tr.t("Tap to show or hide each one. FPS counts the frames the game really draws a second. "
                                + "CPU is how busy the game's thread is: near 100% means the phone's CPU is the limit.")
                        : Tr.t("Turn Performance Overlay on to choose."),
                items, lit, settings.showFps(), which -> {
                    if (which == 6) settings.setFrameGraph(!settings.frameGraph());
                    else settings.setOverlayItems(settings.overlayItems() ^ (1 << which));
                });
    }

    /** Render Resolution, shared with the launcher's quick settings. */
    void resolutionRow() {
        final float[] scales = AppSettings.RESOLUTION_SCALES;
        final String[] labels = new String[scales.length];
        final int selected = resolutionIndex(settings);
        for (int i = 0; i < scales.length; ++i) labels[i] = AppSettings.resolutionLabel(scales[i]);
        sliderRow(Tr.t("Render Resolution"), labels, selected, true,
                which -> settings.setResolutionScale(scales[which]));
    }

    /** Not held by the Low-End preset: the player may change it. */
    private boolean free(String key) {
        return !settings.lowEndHolds(key);
    }

    /** `about`, plus why it cannot be changed while the Low-End preset holds it. */
    private String held(String key, String about) {
        return free(key) ? about : about + "\n" + Tr.t("Set by the Low-End Preset: turn the preset off to change it.");
    }

    /** A toggle the Low-End preset may hold: then it shows its value, dimmed, and cannot be tapped. */
    private void heldToggle(String key, String title, String about, boolean on, Toggle toggle) {
        if (free(key)) toggleRow(title, about, on, toggle);
        else card(title, held(key, about), Ui.toggle(activity, on), false, false);
    }

    /** The real screen size (the app's own metrics leave out the system bars). */
    static DisplayMetrics screenMetrics(Activity activity) {
        final DisplayMetrics m = new DisplayMetrics();
        activity.getWindowManager().getDefaultDisplay().getRealMetrics(m);
        return m;
    }

    /** Lines of the picture sent to the screen: Display Resolution when it is below the screen's. */
    static int outputLines(Activity activity, AppSettings settings) {
        final DisplayMetrics m = screenMetrics(activity);
        final int shortSide = Math.min(m.widthPixels, m.heightPixels);
        final int lines = settings.displayLines();
        return lines > 0 && lines < shortSide * 0.95f ? lines : shortSide;
    }

    static int resolutionIndex(AppSettings settings) {
        final float[] scales = AppSettings.RESOLUTION_SCALES;
        for (int i = 0; i < scales.length; ++i) {
            if (scales[i] <= 0f ? settings.resolutionScale() <= 0f
                    : Math.abs(scales[i] - settings.resolutionScale()) < 0.01f) {
                return i;
            }
        }
        return 3;
    }


    static final String[] UPSCALERS = {"Off", "FSR Light", "FSR Full", "SGSR"};

    /** Display Resolution: the size of the picture the game sends to the screen (GameActivity). */
    private void displayResolutionRow() {
        final int[] lines = AppSettings.DISPLAY_LINES;
        final String[] labels = new String[lines.length];
        int selected = 0;
        for (int i = 0; i < lines.length; ++i) {
            labels[i] = lines[i] == 0 ? Tr.t("Screen") : lines[i] + "p";
            if (lines[i] == settings.displayLines()) selected = i;
        }
        final DisplayMetrics m = screenMetrics(activity);
        final int shortSide = Math.min(m.widthPixels, m.heightPixels);
        restart(null);
        choiceRow(Tr.t("Display Resolution"), held("display_lines", Tr.t("The picture the game sends to the screen, which stretches it "
                        + "for free. Lower takes GPU work off every frame, menus too: steadier fps on a slow "
                        + "phone, blurrier picture. This screen: ") + Math.max(m.widthPixels, m.heightPixels)
                        + "×" + shortSide + Tr.t(" (a size at or above it changes nothing).")),
                labels, selected, free("display_lines"), which -> settings.setDisplayLines(lines[which]));
    }

    /** Off / FSR Light / FSR Full / SGSR, shared with the launcher's quick settings. */
    void upscalerRow() {
        final String[] labels = new String[UPSCALERS.length];
        for (int i = 0; i < labels.length; ++i) labels[i] = Tr.t(UPSCALERS[i]);
        choiceRow(Tr.t("Upscaler"), held("fsr_on", Tr.t("Keeps a picture rendered below the screen's resolution sharp when it is "
                        + "stretched. FSR Light sharpens it and costs almost nothing. FSR Full adds FSR1's edge-aware "
                        + "upscale: sharpest, but several ms on a small phone GPU. SGSR: Qualcomm's edge-aware "
                        + "upscale in one pass, lighter than FSR Full.")),
                labels, settings.fsr(), free("fsr_on"), which -> settings.setFsr(which));
    }

    private void showGraphics() {
        upscalerRow();
        choiceRow(Tr.t("Texture Detail"), held("texture_filter", Tr.t("Keeps the ground, walls and characters sharp at low Render "
                        + "Resolution and when seen at an angle. Sharpest costs a little more on a small phone GPU.")),
                new String[] {Tr.t("Original"), Tr.t("Sharp"), Tr.t("Sharpest")}, settings.textureFilter(),
                free("texture_filter"),
                which -> settings.setTextureFilter(which));
        restart(null);
        choiceRow(Tr.t("Graphics Compatibility"), Tr.t("Fixes for phone GPUs whose drivers break the 3D models "
                        + "(scrambled textures, stretched characters). Auto uses them where needed."),
                new String[] {Tr.t("Auto"), Tr.t("On"), Tr.t("Off")}, settings.gpuCompat(), true,
                which -> settings.setGpuCompat(which));
        restart(null);
        int rendererIndex = 0;
        for (int i = 0; i < AppSettings.RENDERER_ORDER.length; i++) {
            if (AppSettings.RENDERER_ORDER[i] == settings.renderer()) rendererIndex = i;
        }
        choiceRow(Tr.t("Renderer"), Tr.t("Auto picks one for this phone (the Vulkan renderer on most). Dawn: the "
                        + "WebGPU renderer of this version. WebGPU 1.0: the renderer of version 1.0, which some "
                        + "phones run smoother; it has none of the renderer changes made after 1.0, so Graphics Compatibility "
                        + "and the GPU-time Dynamic Resolution do nothing there (not on PowerVR, Adreno 5xx or "
                        + "Android 10: they use Auto). Vulkan: "
                        + "the game talks to the Vulkan driver directly. OpenGL ES: Dawn on OpenGL ES, for phones "
                        + "whose Vulkan driver shows a black screen or closes the game."),
                new String[] {Tr.t("Auto"), "Dawn", "WebGPU 1.0", Tr.t("Vulkan"), Tr.t("OpenGL ES")}, rendererIndex,
                true, index -> {
                    final int which = AppSettings.RENDERER_ORDER[index];
                    final int before = settings.renderer();
                    settings.setRenderer(which);
                    confirmRestart(Tr.t("Renderer"), AppSettings.rendererName(which), AppSettings.rendererName(before),
                            () -> settings.setRenderer(before));
                }, false);
        showGpuDrivers();
    }

    /** System's report tools (launcher only): share a run's log, the device info, reset and clear. */
    void reportRows(boolean gameRunning) {
        toggleRow(Tr.t("Recording-Friendly Audio"), Tr.t("For screen recordings: some phones play the game's sound "
                        + "on a fast path that recordings miss, so the video comes out silent. On keeps the sound on "
                        + "the regular path, with a little more delay. Applies when the game starts again."),
                settings.recordingAudio(), on -> settings.setRecordingAudio(on));
        final boolean files = AppFiles.hasFileAccess(activity);
        actionRow(Tr.t("Share Log"), files
                        ? Tr.t("Sends the log of the game runs you pick (the one that crashed first) with the device "
                                + "info, as one zip: to Discord, Telegram, WhatsApp or anywhere else.")
                        : Tr.t("Needs \"All files access\" for this app, to read the logs."),
                files ? Tr.t("Share") : null, () -> LogShare.show(activity));
        actionRow(Tr.t("Device Info"), LogShare.deviceInfo(activity).trim(), Tr.t("Copy"),
                () -> LogShare.copyDeviceInfo(activity));
        actionRow(Tr.t("Reset and Clear"), Tr.t("Reset all settings, clear the HD texture and shader caches, "
                        + "the testing options or the logs, in one place."),
                Tr.t("Choose"), () -> DevTools.showReset(activity, gameRunning, () -> show(SYSTEM)));
    }

    /** Everything about the game's textures: packs, dump, the key report and HD Textures. */
    private void showTextures() {
        showCustomTextures();
        showTextureTools();
        showHdTextures();
    }

    /**
     * The HD cache size, counted (or the cache deleted) off the UI thread: it holds thousands of files on
     * shared storage, and walking them on every redraw made the section stutter. -1: not counted yet.
     */
    private static long hdCacheBytes = -1;
    private static boolean hdBusy;

    private void hdCacheWork(java.io.File dir, boolean clear) {
        if (hdBusy) return;
        hdBusy = true;
        new Thread(() -> {
            if (clear) AppFiles.deleteRecursively(dir);
            final long bytes = clear ? 0 : AppFiles.folderSize(dir);
            activity.runOnUiThread(() -> {
                hdBusy = false;
                hdCacheBytes = bytes;
                if ((section == TEXTURES || section == GRAPHICS) && !activity.isFinishing()) show(section);
            });
        }, "hd-cache-size").start();
    }

    /** HD Textures: upscaled while playing, with their storage and cache. */
    private void showHdTextures() {
        restart(null);
        choiceRow(Tr.t("HD Textures"), held("hd_textures", Tr.t("Makes the game's textures twice as sharp: each one is upscaled in the "
                        + "background the first time it appears (blurry, then sharp) and kept for next time. Uses "
                        + "more RAM and storage.")
                        + (weakPhone() ? Tr.t(" This phone has little RAM or renders below 1x: the gain is small.") : "")),
                new String[] {Tr.t("Off"), Tr.t("Live")}, settings.hdTextures(), free("hd_textures"), which -> {
                    final int before = settings.hdTextures();
                    settings.setHdTextures(which);
                    confirmRestart(Tr.t("HD Textures"), which == 1 ? Tr.t("Live") : "off", before == 1 ? Tr.t("Live") : "off",
                            () -> settings.setHdTextures(before));
                }, false);
        final int[] sizes = AppSettings.HD_CACHE_SIZES;
        int size = 1;
        for (int i = 0; i < sizes.length; ++i) if (sizes[i] == settings.hdCacheMb()) size = i;
        restart(null);
        choiceRow(Tr.t("HD Texture Storage"), settings.hdTextures() != 0
                        ? Tr.t("The most storage the sharper textures may take; once full, no new ones are made.")
                        : Tr.t("Turn HD Textures on to choose."),
                new String[] {Tr.t("512 MB"), Tr.t("1 GB"), Tr.t("2 GB")}, size, settings.hdTextures() != 0,
                which -> settings.setHdCacheMb(sizes[which]));
        final java.io.File hdCache = AppFiles.hdCacheDir();
        if (hdCache != null && hdCacheBytes < 0) hdCacheWork(hdCache, false);
        final long used = Math.max(0, hdCacheBytes);
        valueRow(Tr.t("HD Texture Cache"), hdCache == null ? "–"
                        : hdBusy || hdCacheBytes < 0 ? "…"
                        : String.format(java.util.Locale.ROOT, "%.0f MB", used / 1048576.0),
                used > 0 && !hdBusy && !inGame ? Tr.t("Clear") : null, this::confirmClearHdCache);
    }

    /** Texture mods from the Textures folder: on or off, then each mod ticked and put in order. */
    private void showCustomTextures() {
        final String folder = folderName("Textures");
        final boolean files = AppFiles.hasFileAccess(activity);
        restart(null);
        toggleRow(Tr.t("Custom Textures"), files
                        ? Tr.t("Uses your own textures (tex1_*.png or .dds, Dolphin-style names) from ") + folder
                                + Tr.t(" in place of the game's. A very large pack uses more RAM.")
                        : Tr.t("Needs \"All files access\" for this app, to read ") + folder + ".",
                settings.customTextures(), files, on -> settings.setCustomTextures(on));
        if (!files) return;
        final java.io.File root = AppFiles.textureDir();
        final java.util.List<TexturePacks.Pack> packs = TexturePacks.load(settings, root);
        group(Tr.t("Mods") + (inGame ? " *" : ""));
        if (packs.isEmpty()) {
            final TextView none = Ui.text(activity, Tr.t("No mods yet. Put each mod in its own folder in ") + folder
                    + Tr.t(" (how: README.txt there)."), 12, Ui.SECONDARY);
            none.setPadding(0, dp(2), 0, dp(4));
            rows.addView(none, Ui.matchWidth(0));
        }
        final boolean enabled = settings.customTextures();
        for (int i = 0; i < packs.size(); ++i) packRow(packs, i, enabled);
        TexturePacks.countMissing(activity, root, packs, () -> {
            if (section == TEXTURES || section == GRAPHICS) show(section);
        });
        valueRow(Tr.t("Mods Folder"), folder, Tr.t("Rescan"), () -> TexturePacks.recount());
    }

    /** One mod: its tick, name, valid file count, and arrows to move it up or down the order. */
    private void packRow(java.util.List<TexturePacks.Pack> packs, int index, boolean enabled) {
        final TexturePacks.Pack pack = packs.get(index);
        final LinearLayout row = new LinearLayout(activity);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(3), 0, dp(3));
        row.addView(Ui.toggle(activity, pack.on && enabled));
        final boolean loose = pack.name.equals(TexturePacks.LOOSE);
        final TextView name = Ui.text(activity, loose ? Tr.t("(Loose files)") : pack.name, 13, Ui.TEXT);
        name.setSingleLine(true);
        name.setEllipsize(android.text.TextUtils.TruncateAt.MIDDLE);
        final LinearLayout.LayoutParams nameParams = new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        nameParams.leftMargin = dp(8);
        row.addView(name, nameParams);
        final int count = TexturePacks.count(pack.name);
        final TextView files = Ui.text(activity, count < 0 ? "…"
                : count == 0 ? Tr.t("⚠ no valid names")
                : String.format(java.util.Locale.ROOT, Tr.t("%d files"), count), 12,
                count == 0 ? Ui.WARNING : Ui.SECONDARY);
        files.setSingleLine(true);
        row.addView(files);
        for (int dir = -1; dir <= 1; dir += 2) {
            final int to = index + dir;
            final boolean can = enabled && to >= 0 && to < packs.size();
            final TextView arrow = Ui.chip(activity, dir < 0 ? "▲" : "▼", false, can, v -> {
                java.util.Collections.swap(packs, index, to);
                TexturePacks.save(settings, packs);
                changed();
            });
            arrow.setPadding(0, 0, 0, 0);
            final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(dp(30), dp(28));
            params.leftMargin = dp(dir < 0 ? 8 : 2);
            row.addView(arrow, params);
        }
        if (enabled) {
            row.setClickable(true);
            row.setOnClickListener(v -> {
                pack.on = !pack.on;
                TexturePacks.save(settings, packs);
                changed();
            });
        } else {
            row.setAlpha(0.4f);
        }
        rows.addView(row, Ui.matchWidth(0));
    }

    /** Tools for pack makers and testers: texture dump and the texture key report. */
    private void showTextureTools() {
        final String folder = folderName("Textures");
        restart(null);
        toggleRow(Tr.t("Dump Textures"), settings.customTextures()
                        ? Tr.t("Saves each texture the game uses to ") + folder + Tr.t("/dump, named the way its "
                                + "replacement must be: edit one and put it in ") + folder + Tr.t(". Slows loading and fills "
                                + "storage: turn it off when done.")
                        : Tr.t("Turn Custom Textures on to choose."),
                settings.dumpTextures(), settings.customTextures(), on -> settings.setDumpTextures(on));
        restart(null);
        toggleRow(Tr.t("Report Texture Keys"), Tr.t("Lists the name of every texture the game uses in the log (only "
                        + "the names, no pictures), so testers can check that texture packs match the game."),
                settings.textureKeyReport(), on -> settings.setTextureKeyReport(on));
    }

    /** Player-installed Vulkan drivers (GpuDrivers): choose one, install a .zip, remove one. */
    private void showGpuDrivers() {
        if (!GpuDrivers.supported()) return;
        final java.util.List<GpuDrivers.Driver> drivers = GpuDrivers.installed(activity);
        final String[] options = new String[drivers.size() + 1];
        options[0] = Tr.t("System");
        int selected = 0;
        for (int i = 0; i < drivers.size(); ++i) {
            options[i + 1] = drivers.get(i).label();
            if (drivers.get(i).id.equals(settings.gpuDriver())) selected = i + 1;
        }
        final String before = settings.gpuDriver();
        final String beforeName = options[selected];
        restart(null);
        choiceRow(Tr.t("GPU Driver"), Tr.t("System is the phone's own driver. A driver you install (a Turnip .zip "
                        + "made for Dolphin or Yuzu, for Snapdragon / Adreno) can be faster or fix broken graphics. "
                        + "Mali drivers (malisx2) run on the Vulkan renderer, which starts by itself for them. "
                        + "If it fails or the game closes while starting, it goes back to System by itself."),
                options, selected, true, which -> {
                    settings.setGpuDriver(which == 0 ? "" : drivers.get(which - 1).id);
                    confirmRestart(Tr.t("GPU Driver"), options[which], beforeName,
                            () -> settings.setGpuDriver(before));
                }, false);
        // 1.2 has no launcher settings: installing and removing live here too, used from the next start.
        actionRow(Tr.t("Install GPU Driver"), Tr.t("Choose a driver .zip (it has a meta.json inside), for example "
                        + "from Download."),
                Tr.t("Install"), this::installGpuDriver);
        for (GpuDrivers.Driver driver : drivers) {
            final String about = (driver.description.isEmpty() ? "" : driver.description + " ")
                    + (driver.author.isEmpty() ? "" : Tr.t("By ") + driver.author + ".");
            actionRow(driver.label(), about.trim(), Tr.t("Remove"), () -> {
                if (driver.id.equals(settings.gpuDriver())) settings.setGpuDriver("");
                GpuDrivers.remove(activity, driver);
            });
        }
    }

    private void installGpuDriver() {
        if (!AppFiles.hasFileAccess(activity)) {
            askFileAccess(() -> android.widget.Toast.makeText(activity, Tr.t("Allow file access to choose a "
                    + "driver .zip."), android.widget.Toast.LENGTH_LONG).show());
            return;
        }
        FileBrowser.showFiles(activity, FileBrowser.downloads(), Tr.t("Choose a GPU driver"),
                Tr.t("A driver .zip with a meta.json inside (Turnip for Adreno)."),
                f -> f.getName().toLowerCase(java.util.Locale.ROOT).endsWith(".zip"),
                Tr.t("No folders or .zip files here."), file -> {
                    try {
                        final GpuDrivers.Driver driver = GpuDrivers.install(activity, file);
                        android.widget.Toast.makeText(activity, Tr.t("Installed ") + driver.label()
                                + Tr.t(". Choose it under GPU Driver."), android.widget.Toast.LENGTH_LONG).show();
                    } catch (java.io.IOException e) {
                        new Sheet(activity, Tr.t("Not a driver"), e.getMessage())
                                .action(Tr.t("OK"), null, true, false, null).show();
                    }
                    changed();
                });
    }

    private void showPerformance() {
        // Every phone; a low or mid-low one has it on from its first start.
        lowEndRow();
        choiceRow(Tr.t("Frameskip"), held("frameskip", Tr.t("The game moves one step per drawn frame, so a slow phone plays in slow "
                        + "motion. Auto skips drawing a frame when it falls behind: closer to full speed, "
                        + "choppier picture.")),
                new String[] {Tr.t("Off"), Tr.t("Auto")}, settings.frameskip() ? 1 : 0,
                !settings.fpsLock30() && free("frameskip"), which -> settings.setFrameskip(which == 1));
        toggleRow(Tr.t("30 FPS Lock"), Tr.t("Draws every other frame, evenly paced: a steady 30 FPS while the game "
                        + "and sound keep full speed. For phones that cannot hold 60. Replaces Frameskip "
                        + "while on."),
                settings.fpsLock30(), on -> settings.setFpsLock30(on));
        heldToggle("vsync", Tr.t("V-Sync"), Tr.t("Frames go out in step with the display: smoother motion."), settings.vsync(),
                on -> settings.setVSync(on));
        heldToggle("dynamic_resolution", Tr.t("Dynamic Resolution"), Tr.t("Lowers the render resolution while the GPU holds the game back "
                        + "(never below 480 lines) and raises it again when there is room. Render Resolution stays the "
                        + "highest it goes."),
                settings.dynamicResolution(), on -> settings.setDynamicResolution(on));
        restart(null);
        toggleRow(Tr.t("GX Thread"), Tr.t("Runs the graphics work on its own CPU core, so busy battles run faster on "
                        + "slower phones."),
                settings.gxThread(), true, on -> {
                    settings.setGxThread(on);
                    confirmRestart(Tr.t("GX Thread"), on ? "on" : "off", on ? "off" : "on",
                            () -> settings.setGxThread(!on));
                }, false);
        final boolean thermal = AppSettings.thermalSupported(activity);
        toggleRow(Tr.t("Thermal Control"), thermal
                        ? Tr.t("When the phone warms up, draws at 30 fps (the game itself keeps full speed) "
                        + "for at least a minute, and lowers the resolution if the GPU is the one working "
                        + "hard. Back to normal once it cools down.")
                        : Tr.t("Not available: needs Android 12 or newer with a thermal sensor report."),
                settings.thermalControl(), thermal, on -> settings.setThermalControl(on));
        final boolean sustained = AppSettings.sustainedSupported(activity);
        toggleRow(Tr.t("Sustained Performance"), sustained
                        ? Tr.t("Keeps the CPU and GPU at speeds the phone can hold for hours: a little slower at the "
                        + "start, but no sudden drop once it gets hot in a long session. The game no longer asks for "
                        + "the fastest cores.")
                        : Tr.t("Not available: this phone does not offer it."),
                settings.sustainedPerformance(), sustained, on -> settings.setSustainedPerformance(on));
        restart(null);
        choiceRow(Tr.t("Upload Memory"), Tr.t("Vulkan renderer: the memory the game writes its graphics data into. "
                        + "Auto measures at start which one is faster on this phone. Standard suits most phones, "
                        + "Cached helps some slow ones."),
                new String[] {Tr.t("Auto"), Tr.t("Standard"), Tr.t("Cached")}, settings.uploadMemory(), true,
                which -> settings.setUploadMemory(which));
        restart(null);
        toggleRow(Tr.t("Skip Boot"), Tr.t("Skips the logos at start, straight to the save check."), settings.skipBoot(),
                on -> settings.setSkipBoot(on));
    }

    /**
     * The Low-End preset on any phone (DeviceProfile.lowEndValues); a low or mid-low phone has it on
     * from its first start (DeviceProfile.applyOnce). Off puts back the settings from before it.
     */
    private void lowEndRow() {
        final String gpu = settings.gpuName();
        final String render = AppSettings.resolutionLabel((Float) DeviceProfile.lowEndValues(gpu, 1).get("resolution_scale"));
        final String ultraRender = AppSettings.resolutionLabel((Float) DeviceProfile.lowEndValues(gpu, 2).get("resolution_scale"));
        final String[] options = {Tr.t("Off"), Tr.t("Low-End"), Tr.t("Ultra Low")};
        choiceRow(Tr.t("Low-End Preset"), Tr.t("The lightest settings in one tap, for a phone that cannot hold "
                        + "60 fps: steadier, blurrier.") + "\n• " + Tr.t("Display Resolution") + " 480p\n• "
                        + Tr.t("Render Resolution") + " " + render + "\n• " + Tr.t("Texture Detail") + " "
                        + Tr.t("Original") + "\n• " + Tr.t("Upscaler") + " " + Tr.t("Off") + "\n• "
                        + Tr.t("Frameskip") + " " + Tr.t("Auto") + "\n• " + Tr.t("Dynamic Resolution") + " "
                        + Tr.t("On") + "\n• " + Tr.t("HD Textures") + " " + Tr.t("Off") + "\n"
                        + Tr.t("Ultra Low: the same, with Display Resolution 360p, Render Resolution ") + ultraRender
                        + Tr.t(" and the 30 FPS Lock with GX Thread, for a phone that drops in the heaviest effects.") + "\n"
                        + Tr.t("Off puts back the settings from "
                        + "before it. Applies when the game starts again."),
                options, settings.lowEndLevel(), true, which -> {
                    if (which == settings.lowEndLevel()) return;
                    settings.undoLowEnd();  // a switch between levels keeps the backup from before either
                    if (which > 0) settings.applyLowEnd(DeviceProfile.lowEndValues(gpu, which), which);
                    changed();
                });
    }

    private void showSaveData() {
        final boolean has = SaveData.hasSave(activity);
        valueRow(Tr.t("Back Up Save"), has
                        ? android.text.format.DateFormat.getMediumDateFormat(activity)
                                        .format(new java.util.Date(SaveData.lastSaved(activity)))
                                + " " + android.text.format.DateFormat.getTimeFormat(activity)
                                        .format(new java.util.Date(SaveData.lastSaved(activity)))
                        : Tr.t("No save yet"),
                Tr.t("Back Up"), has, this::backUp);
        actionRow(Tr.t("Import Save"), Tr.t("Replaces this phone's save with a .zip made by Back Up. A running "
                        + "game is closed without saving."),
                Tr.t("Import"), this::importSave);
        final java.io.File last = SaveData.latestBackup(activity);
        valueRow(Tr.t("Undo Last Import"), last == null ? Tr.t("Nothing to undo")
                        : android.text.format.DateFormat.getMediumDateFormat(activity)
                                        .format(new java.util.Date(last.lastModified()))
                                + " " + android.text.format.DateFormat.getTimeFormat(activity)
                                        .format(new java.util.Date(last.lastModified())),
                Tr.t("Undo"), last != null, this::confirmUndoImport);
    }

    /**
     * A save import from the game: the game's process must end before its save is replaced, so the
     * start screen does it (LauncherActivity ACTION_IMPORT_SAVE) and the game opens again afterwards.
     */
    private void importFromGame(java.io.File file) {
        ((GameActivity) activity).toLauncher(LauncherActivity.ACTION_IMPORT_SAVE, file == null ? null : file.getPath());
    }

    private void backUp() {
        if (!AppFiles.hasFileAccess(activity)) {
            askFileAccess(() -> SaveData.startExport(activity));
            return;
        }
        final java.io.File saves = SaveData.savesDir();
        FileBrowser.showFolder(activity, saves != null ? saves : FileBrowser.downloads(),
                Tr.t("Where to save the backup"),
                Tr.t("Tap Save Here to keep it in ") + AppFiles.HOME + Tr.t("/Saves, or open another folder first."),
                folder -> SaveData.exportToFolder(activity, folder, this::changed));
    }

    private void importSave() {
        if (!AppFiles.hasFileAccess(activity)) {
            askFileAccess(() -> SaveData.startImport(activity));
            return;
        }
        FileBrowser.showFiles(activity, SaveData.importStart(), Tr.t("Choose your save backup"),
                Tr.t("The basara2heroes-save-….zip made by Back Up, or a data.bin copied from a Wii or Dolphin. "
                        + "The save on this phone is replaced (and kept for Undo Last Import)."),
                SaveData::isSaveFile, Tr.t("No folders, .zip or .bin files here."),
                this::importFromGame);
    }

    private void confirmUndoImport() {
        new Sheet(activity, Tr.t("Undo the last import?"), Tr.t("The save from before that import comes back. A "
                + "running game is closed without saving."))
                .action(Tr.t("Undo"), null, true, false, () -> importFromGame(null))
                .action(Tr.t("Cancel"), null, false, false, null)
                .show();
    }

    /** Without "All files access" the folders cannot be listed: allow it, or use the phone's own picker. */
    private void askFileAccess(Runnable systemPicker) {
        new Sheet(activity, Tr.t("Allow file access"), Tr.t("To list the folders on this phone."))
                .step("1", Tr.t("Open Settings"), AppFiles.fileAccessStep())
                .step("2", Tr.t("Come back"), Tr.t("Then tap the button again."))
                .action(Tr.t("Open Settings"), null, true, false, () -> {
                    if (android.os.Build.VERSION.SDK_INT < 30) {
                        AppFiles.requestLegacyFileAccess(activity);
                        return;
                    }
                    try {
                        activity.startActivity(new android.content.Intent(
                                android.provider.Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                android.net.Uri.parse("package:" + activity.getPackageName())));
                    } catch (android.content.ActivityNotFoundException e) {
                        systemPicker.run();
                    }
                })
                .action(Tr.t("Use System Picker"), Tr.t("The phone's own file picker, without the permission"), false,
                        false, systemPicker)
                .action(Tr.t("Cancel"), null, false, false, null)
                .show();
    }

    /** Only opened in a build with cheats (BuildConfig.CHEATS); applied at once in the game. */
    private void showCheats() {
        final TextView warning = Ui.text(activity, Tr.t("Cheats change your save: back it up first (Save Data). Off in LAN play."),
                12, Sheet.WARNING_TEXT);
        warning.setPadding(0, dp(2), 0, dp(4));
        rows.addView(warning, Ui.matchWidth(0));

        final int money = settings.cheatMoney();
        final String amount = String.format(java.util.Locale.ROOT, "%,d", settings.cheatMoneyAmount());
        choiceRow(Tr.t("Money"), Tr.t("Infinite keeps the money at the amount below, whatever you buy. Once sets it "
                        + "to that amount one time (Set Now), then it goes down as you spend it."),
                new String[] {Tr.t("Off"), Tr.t("Infinite"), Tr.t("Once")}, money, true,
                which -> settings.setCheatMoney(which));
        valueRow(Tr.t("Money Amount"), amount,
                money != AppSettings.MONEY_OFF ? Tr.t("Change") : null, this::askMoneyAmount);
        if (money == AppSettings.MONEY_ONCE) {
            actionRow(Tr.t("Set Money Now"), inGame
                            ? Tr.t("With your save loaded, tap Set Now: the money becomes ") + amount + "."
                            : Tr.t("Load your save in the game first, then tap Set Now and go back "
                                    + "to the game: the money becomes ") + amount + ".",
                    Tr.t("Set Now"), () -> {
                        settings.bumpCheatMoneySeq();
                        android.widget.Toast.makeText(activity, inGame ? Tr.t("Money set.")
                                        : Tr.t("Money is set when you go back to the game."),
                                android.widget.Toast.LENGTH_SHORT).show();
                    });
        }
        toggleRow(Tr.t("Infinite HP"), Tr.t("Your characters lose no HP. Enemies and allied officers still do."),
                settings.cheatInfiniteHp(), on -> settings.setCheatInfiniteHp(on));
        toggleRow(Tr.t("Infinite Basara"), Tr.t("Your characters' Basara gauge stays full, refilled as soon as a "
                        + "Basara attack ends."),
                settings.cheatInfiniteBasara(), on -> settings.setCheatInfiniteBasara(on));
        toggleRow(Tr.t("Unlock All"), Tr.t("Opens every character, stage, skill, weapon and item. What the game "
                        + "saves stays open after you turn this off."),
                settings.cheatUnlockAll(), on -> settings.setCheatUnlockAll(on));
    }

    private void askMoneyAmount() {
        final android.widget.EditText input = new android.widget.EditText(activity);
        input.setInputType(android.text.InputType.TYPE_CLASS_NUMBER);
        input.setText(String.valueOf(settings.cheatMoneyAmount()));
        input.setSelectAllOnFocus(true);
        input.setTextColor(Color.WHITE);
        input.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        input.setFilters(new android.text.InputFilter[] {new android.text.InputFilter.LengthFilter(7)});
        final Runnable save = () -> {
            try {
                settings.setCheatMoneyAmount(Integer.parseInt(input.getText().toString().trim()));
            } catch (NumberFormatException ignored) {
                return;  // empty: keep the old amount
            }
            changed();
        };
        final Sheet sheet = new Sheet(activity, Tr.t("Money Amount"), Tr.t("0 to 9,999,999."))
                .view(input)
                .action(Tr.t("Save"), null, true, false, save)
                .action(Tr.t("Cancel"), null, false, false, null);
        // The keyboard can cover the Save button on a landscape phone: its Done key saves too.
        input.setImeOptions(android.view.inputmethod.EditorInfo.IME_ACTION_DONE);
        input.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId != android.view.inputmethod.EditorInfo.IME_ACTION_DONE) return false;
            save.run();
            sheet.dismiss();
            return true;
        });
        sheet.show();
        input.requestFocus();
    }

    private void showControls() {
        actionRow(Tr.t("Gamepad Buttons"), GamepadButtons.summary(settings), Tr.t("Change"),
                () -> new GamepadButtons(activity, settings, this::changed).show());
        toggleRow(Tr.t("Touch Controls"), Tr.t("The on-screen Classic Controller."), settings.touchControls(),
                on -> settings.setTouchControls(on));
        final String[] opacities = {"20%", "40%", "60%", "80%", "100%"};
        final int opacity = Math.max(0, Math.min(4, Math.round(settings.touchOpacity() * 5f) - 1));
        sliderRow(Tr.t("Touch Control Opacity"), opacities, opacity,
                settings.touchControls(), which -> settings.setTouchOpacity((which + 1) / 5f));
        actionRow(Tr.t("Touch Control Layout"), settings.touchLayout().isEmpty()
                        ? Tr.t("Move and resize the controls while playing: Back > Edit Controls Layout.")
                        : Tr.t("Arranged by you. Change it while playing: Back > Edit Controls Layout."),
                settings.touchLayout().isEmpty() ? null : Tr.t("Reset"), () -> settings.setTouchLayout(""));
        toggleRow(Tr.t("Fixed Left Stick"), Tr.t("On: the stick moves only when touched where it is drawn. "
                        + "Off: a thumb anywhere on the empty left side grabs it."),
                settings.fixedStick(), on -> settings.setFixedStick(on));
        toggleRow(Tr.t("Vibration"), Tr.t("A short buzz when a touch button is pressed."), settings.haptics(),
                on -> settings.setHaptics(on));
        toggleRow(Tr.t("Touch camera"), Tr.t("Drag on the empty right side of the screen to turn the camera. The right stick is hidden while this is on."),
                settings.touchCamera(), on -> settings.setTouchCamera(on));
        toggleRow(Tr.t("Invert Camera"), Tr.t("On: the scene follows the finger (drag right, the view turns left), and the "
                        + "right touch stick is reversed both ways. Off: the game's own directions."),
                settings.invertCamera(), on -> settings.setInvertCamera(on));
        toggleRow(Tr.t("Tilt"), Tr.t("Turn the phone like a steering wheel."), settings.motionTilt(),
                on -> settings.setMotionTilt(on));
        toggleRow(Tr.t("Shake"), Tr.t("Jolt the phone to shake the remote."), settings.motionShake(),
                on -> settings.setMotionShake(on));
    }

    /** RAM under 4.5 GB, or a render scale below 1x: HD Textures costs a lot and shows little. */
    private boolean weakPhone() {
        final android.app.ActivityManager am = activity.getSystemService(android.app.ActivityManager.class);
        final android.app.ActivityManager.MemoryInfo info = new android.app.ActivityManager.MemoryInfo();
        if (am != null) am.getMemoryInfo(info);
        final float scale = settings.resolutionScale();
        return (info.totalMem > 0 && info.totalMem < (4500L << 20)) || (scale > 0f && scale < 1f);
    }

    private static String folderName(String sub) {
        return AppFiles.HOME + "/" + sub;
    }

    private void confirmClearHdCache() {
        new Sheet(activity, Tr.t("Clear HD Texture Cache?"), Tr.t("The sharper textures are made again while you play."))
                .action(Tr.t("Clear"), null, true, true, () -> {
                    final java.io.File dir = AppFiles.hdCacheDir();
                    if (dir != null) hdCacheWork(dir, true);
                    changed();
                })
                .action(Tr.t("Cancel"), null, false, false, null)
                .show();
    }

    /** "WxH" the game renders at for the chosen scale (0: the screen's own size). */
    static String renderSize(Activity activity, AppSettings settings) {
        final float scale = settings.resolutionScale();
        final DisplayMetrics m = screenMetrics(activity);
        final int screenLong = Math.max(m.widthPixels, m.heightPixels), screenShort = Math.min(m.widthPixels, m.heightPixels);
        final int h = outputLines(activity, settings);
        final int w = screenShort > 0 ? Math.round(screenLong * (float) h / screenShort) : screenLong;
        if (scale <= 0f) return w + "×" + h;
        final float aspect;
        switch (settings.aspectMode()) {
            case AppSettings.ASPECT_4_3: aspect = 4f / 3f; break;
            case AppSettings.ASPECT_16_9: aspect = 16f / 9f; break;
            default: aspect = h > 0 ? Math.max((float) w / h, 16f / 9f) : 16f / 9f; break;
        }
        final int lines = Math.round(480 * scale);
        return Math.round(lines * aspect) + "×" + lines;
    }

    // --- rows ------------------------------------------------------------------------------------
    // Dear ImGui rows, as hikki's DefJam menu: the name on the left in capitals, the control on the
    // right (a stepper, a checkbox, a slider or a button). The lines about each setting are not shown
    // in the game: the menu stays short and calm.

    interface Toggle { void set(boolean on); }
    interface Choice { void pick(int which); }

    /** The next row is only read at start: its name gets a "*" (the footer says what it means). */
    private boolean pendingRestart;

    /** Marks the next row as applying at the next start (`text` is not shown any more). */
    private void restart(String text) {
        pendingRestart = true;
    }

    /** A dim heading and a line between groups in one section (ImGui's SeparatorText). */
    private void group(String title) {
        final TextView caption = Ui.caption(activity, title);
        caption.setPadding(0, dp(10), 0, 0);
        rows.addView(caption);
        rows.addView(Ui.line(activity));
    }

    /** A row: the name, and `control` on the right (or under it, full width, when `underneath`). */
    LinearLayout card(String title, String about, View control, boolean underneath, boolean enabled) {
        final LinearLayout row = new LinearLayout(activity);
        row.setOrientation(underneath ? LinearLayout.VERTICAL : LinearLayout.HORIZONTAL);
        row.setGravity(underneath ? Gravity.START : Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(3), 0, dp(3));
        final TextView name = Ui.text(activity, title.toUpperCase(java.util.Locale.ROOT)
                + (pendingRestart && inGame ? " *" : ""), 13, Ui.TEXT);
        pendingRestart = false;
        if (underneath) {
            row.addView(name, Ui.matchWidth(0));
            if (control != null) row.addView(control, Ui.matchWidth(dp(4)));
        } else {
            row.addView(name, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
            if (control != null) {
                final android.view.ViewGroup.LayoutParams own = control.getLayoutParams();
                final boolean fixed = own != null && own.width > 0;  // the checkbox keeps its square
                final LinearLayout.LayoutParams params = fixed
                        ? new LinearLayout.LayoutParams(own.width, own.height)
                        : new LinearLayout.LayoutParams(0, dp(28), 1.1f);
                params.leftMargin = dp(10);
                row.addView(control, params);
            }
        }
        if (!enabled) row.setAlpha(0.4f);
        rows.addView(row, Ui.matchWidth(0));
        return row;
    }

    /** The whole row toggles; the checkbox on the right shows it. */
    void toggleRow(String title, String about, boolean on, Toggle toggle) {
        toggleRow(title, about, on, true, toggle);
    }

    /** Disabled: dimmed, unticked, and does nothing when tapped (the phone cannot do it). */
    void toggleRow(String title, String about, boolean on, boolean enabled, Toggle toggle) {
        toggleRow(title, about, on, enabled, toggle, true);
    }

    private void toggleRow(String title, String about, boolean on, boolean enabled, Toggle toggle, boolean redraw) {
        on = on && enabled;
        final LinearLayout row = card(title, about, Ui.toggle(activity, on), false, enabled);
        if (!enabled) return;
        final boolean was = on;
        row.setClickable(true);
        row.setOnClickListener(v -> {
            toggle.set(!was);
            if (redraw) changed();
        });
    }

    /** ImGui's combo as DefJam draws it: the value in a frame, then < and > to step through the options. */
    void choiceRow(String title, String about, String[] options, int selected, boolean enabled, Choice choice) {
        choiceRow(title, about, options, selected, enabled, choice, true);
    }

    /** `redraw` false: `choice` saves and redraws by itself (it may ask first). */
    private void choiceRow(String title, String about, String[] options, int selected, boolean enabled,
                           Choice choice, boolean redraw) {
        final LinearLayout control = new LinearLayout(activity);
        final TextView value = Ui.text(activity, options[Math.max(0, Math.min(selected, options.length - 1))]
                .toUpperCase(java.util.Locale.ROOT), 13, Ui.TEXT);
        value.setGravity(Gravity.CENTER);
        value.setSingleLine(true);
        value.setEllipsize(android.text.TextUtils.TruncateAt.END);
        value.setPadding(dp(4), 0, dp(4), 0);
        value.setBackgroundColor(Ui.FRAME);
        control.addView(value, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f));
        for (int dir = -1; dir <= 1; dir += 2) {
            final int step = dir;
            final TextView arrow = Ui.chip(activity, dir < 0 ? "<" : ">", false, enabled, v -> {
                final int next = ((selected + step) % options.length + options.length) % options.length;
                if (next == selected) return;
                choice.pick(next);
                if (redraw) changed();
            });
            arrow.setPadding(0, 0, 0, 0);
            final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(dp(28),
                    LinearLayout.LayoutParams.MATCH_PARENT);
            params.leftMargin = dp(2);
            control.addView(arrow, params);
        }
        card(title, about, control, false, enabled);
    }

    /** ImGui's slider over the options: drag anywhere on it, saved when the finger lets go. */
    private void sliderRow(String title, String[] options, int selected, boolean enabled, Choice choice) {
        final Ui.Slider slider = new Ui.Slider(activity, options.length - 1, selected);
        slider.setLabel(options[selected]);
        slider.setEnabled(enabled);
        slider.setListener(new Ui.Slider.Listener() {
            @Override
            public void changed(int step) {
                slider.setLabel(options[step]);
            }

            @Override
            public void released(int step) {
                if (step == selected) return;
                choice.pick(step);
                SettingsMenu.this.changed();
            }
        });
        card(title, null, slider, false, enabled);
    }

    /** Buttons under the name that each turn on or off by themselves; the lit ones are on. */
    private void multiRow(String title, String about, String[] options, boolean[] lit, boolean enabled,
                          Choice flip) {
        final Ui.FlowLayout chips = new Ui.FlowLayout(activity, dp(2));
        for (int i = 0; i < options.length; ++i) {
            final int which = i;
            chips.addView(Ui.chip(activity, (lit[i] ? "✓ " : "") + options[i], lit[i], enabled, v -> {
                flip.pick(which);
                changed();
            }));
        }
        card(title, about, chips, true, enabled);
    }

    /** A button filling the control's place (dimmed when `label` is null: nothing to do now). */
    void actionRow(String title, String about, String label, Runnable action) {
        valueRow(title, null, label, action);
    }

    /** `value` in a frame (the amount, the version, the cache size), then the button beside it. */
    void valueRow(String title, String value, String label, Runnable action) {
        valueRow(title, value, label, label != null, action);
    }

    /** `enabled` false: the button is shown dimmed with its name, and does nothing. */
    void valueRow(String title, String value, String label, boolean enabled, Runnable action) {
        final LinearLayout control = new LinearLayout(activity);
        if (value != null) {
            final TextView frame = Ui.text(activity, value, 13, Ui.TEXT);
            frame.setGravity(Gravity.CENTER);
            frame.setSingleLine(true);
            frame.setEllipsize(android.text.TextUtils.TruncateAt.END);
            frame.setPadding(dp(4), 0, dp(4), 0);
            frame.setBackgroundColor(Ui.FRAME);
            control.addView(frame, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f));
        }
        final TextView button = Ui.chip(activity, label == null ? "–" : label.toUpperCase(java.util.Locale.ROOT),
                false, enabled && label != null, v -> {
                    action.run();
                    changed();
                });
        final LinearLayout.LayoutParams params = value == null
                ? new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f)
                : new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT,
                        LinearLayout.LayoutParams.MATCH_PARENT);
        if (value != null) params.leftMargin = dp(2);
        control.addView(button, params);
        card(title, null, control, false, true);
    }

    private int dp(int value) {
        return Ui.dp(activity, value);
    }
}
