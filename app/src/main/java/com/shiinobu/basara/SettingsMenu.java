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
            EXPERIMENTAL = 6, SYSTEM = 7;
    private static final String[] NAMES = {"Display", "Graphics", "Performance", "Controls", "Save Data",
        "Cheats", "Experimental", "System"};
    private static final int[] ICONS = {R.drawable.ic_display, R.drawable.ic_graphics, R.drawable.ic_performance,
        R.drawable.ic_controls, R.drawable.ic_save, R.drawable.ic_cheats, R.drawable.ic_experimental,
        R.drawable.ic_system};

    static String name(int section) { return Tr.t(NAMES[section]); }
    static int icon(int section) { return ICONS[section]; }
    /** Sections a player should know are risky (warning colour on their tab). */
    static boolean risky(int section) { return section == CHEATS || section == EXPERIMENTAL; }

    /** The launcher: everything, with Cheats only in a build that has them, and System last. */
    static int[] launcherSections() {
        return BuildConfig.CHEATS
                ? new int[] {DISPLAY, GRAPHICS, PERFORMANCE, CONTROLS, SAVE_DATA, CHEATS, EXPERIMENTAL, SYSTEM}
                : new int[] {DISPLAY, GRAPHICS, PERFORMANCE, CONTROLS, SAVE_DATA, EXPERIMENTAL, SYSTEM};
    }

    /** The game: no save import (it would close the game) and no launcher things. */
    static int[] gameSections() {
        return BuildConfig.CHEATS
                ? new int[] {DISPLAY, GRAPHICS, PERFORMANCE, CONTROLS, CHEATS, EXPERIMENTAL}
                : new int[] {DISPLAY, GRAPHICS, PERFORMANCE, CONTROLS, EXPERIMENTAL};
    }

    /** The launcher's own rows for System (game file, updates). */
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
        section = index;
        settings = new AppSettings(activity);  // the other process may have written it
        rows.removeAllViews();
        switch (index) {
            case DISPLAY: showDisplay(); break;
            case GRAPHICS: showGraphics(); break;
            case PERFORMANCE: showPerformance(); break;
            case CONTROLS: showControls(); break;
            case SAVE_DATA: showSaveData(); break;
            case CHEATS: showCheats(); break;
            case EXPERIMENTAL: showExperimental(); break;
            default:
                if (systemRows != null) systemRows.fill(this);
                break;
        }
    }

    /**
     * A setting read once when the game starts. In the launcher: restart a running game now, or put it
     * back. In the game the card already says so in red: it is saved for the next start.
     */
    private void confirmRestart(String name, String now, String before, Runnable revert) {
        if (inGame || !(activity instanceof LauncherActivity) || !((LauncherActivity) activity).gameRunning()) {
            changed();
            return;
        }
        final Runnable back = () -> {
            revert.run();
            changed();
        };
        new Sheet(activity, Tr.t("Restart Required"), name + Tr.t(" takes effect when the game starts again."))
                .action(Tr.t("Restart"), Tr.t("Restart the game now with ") + name + " " + now, true, false, () -> {
                    changed();
                    ((LauncherActivity) activity).restartGame();
                })
                .action(Tr.t("Cancel"), Tr.t("Keep ") + name + " " + before, false, false, back)
                .onCancel(back)
                .show();
    }

    // --- sections --------------------------------------------------------------------------------

    private void showDisplay() {
        if (!inGame) {
            choiceRow(Tr.t("Language"), Tr.t("The launcher's language. Auto follows the phone's region: Indonesian "
                            + "in Indonesia, English elsewhere."),
                    new String[] {Tr.t("Auto"), "English", "Bahasa Indonesia"}, settings.language(), true, which -> {
                        settings.setLanguage(which);
                        Tr.init(activity);
                        activity.recreate();  // every screen is drawn again in the new language
                    });
        }
        restart(Tr.t("Switching between Original 4:3 and 16:9 or Fill applies when the game starts again."));
        choiceRow(Tr.t("Aspect Ratio"), Tr.t("The shape of the picture."),
                new String[] {Tr.t("Original 4:3"), "16:9", Tr.t("Fill Screen")}, settings.aspectMode(), true,
                which -> settings.setAspectMode(which));
        resolutionRow();
        toggleRow(Tr.t("Performance Overlay"), Tr.t("A see-through bar at the top of the game with FPS and phone stats."),
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
        final DisplayMetrics screen = activity.getResources().getDisplayMetrics();
        final boolean aboveScreen = settings.resolutionScale() > 0f
                && 480 * settings.resolutionScale() > Math.min(screen.widthPixels, screen.heightPixels) * 1.05f;
        choiceRow(Tr.t("Render Resolution"), Tr.t("Rendered at ") + renderSize(activity, settings)
                + (aboveScreen
                        ? Tr.t(", more than this screen shows: several times the GPU work for little sharpness, so fps can drop. Match screen is sharp enough.")
                        : Tr.t(". Lower is lighter on a slow phone; FSR1 keeps it sharp.")), labels, selected, true,
                which -> settings.setResolutionScale(scales[which]));
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

    private void showGraphics() {
        upscalerRow();
        choiceRow(Tr.t("Texture Detail"), Tr.t("Keeps the ground, walls and characters sharp at low Render "
                        + "Resolution and when seen at an angle. Sharpest costs a little more on a small phone GPU."),
                new String[] {Tr.t("Original"), Tr.t("Sharp"), Tr.t("Sharpest")}, settings.textureFilter(), true,
                which -> settings.setTextureFilter(which));
        restart(null);
        choiceRow(Tr.t("Graphics Compatibility"), Tr.t("Fixes for phone GPUs whose drivers break the 3D models "
                        + "(scrambled textures, stretched characters). Auto uses them where needed."),
                new String[] {Tr.t("Auto"), Tr.t("On"), Tr.t("Off")}, settings.gpuCompat(), true,
                which -> settings.setGpuCompat(which));
        showGpuDrivers();
    }

    /** Off / FSR Light / FSR Full / SGSR, shared with the launcher's quick settings. */
    void upscalerRow() {
        final String[] labels = new String[UPSCALERS.length];
        for (int i = 0; i < labels.length; ++i) labels[i] = Tr.t(UPSCALERS[i]);
        choiceRow(Tr.t("Upscaler"), Tr.t("Keeps a picture rendered below the screen's resolution sharp when it is "
                        + "stretched. FSR Light sharpens it and costs almost nothing. FSR Full adds FSR1's edge-aware "
                        + "upscale: sharpest, but several ms on a small phone GPU. SGSR: Qualcomm's edge-aware "
                        + "upscale in one pass, lighter than FSR Full."),
                labels, settings.fsr(), true, which -> settings.setFsr(which));
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
        if (inGame) return;  // installing and removing belong to the launcher
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
        if (!android.os.Environment.isExternalStorageManager()) {
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
        choiceRow(Tr.t("Frameskip"), Tr.t("The game moves one step per drawn frame, so a slow phone plays in slow "
                        + "motion. Auto skips drawing a frame when it falls behind: closer to full speed, "
                        + "choppier picture."),
                new String[] {Tr.t("Off"), Tr.t("Auto")}, settings.frameskip() ? 1 : 0, !settings.fpsLock30(),
                which -> settings.setFrameskip(which == 1));
        toggleRow(Tr.t("30 FPS Lock"), Tr.t("Draws every other frame, evenly paced: a steady 30 FPS while the game "
                        + "and sound keep full speed. For phones that cannot hold 60. Replaces Frameskip "
                        + "while on."),
                settings.fpsLock30(), on -> settings.setFpsLock30(on));
        restart(null);
        toggleRow(Tr.t("V-Sync"), Tr.t("Frames go out in step with the display: smoother motion."), settings.vsync(),
                on -> settings.setVSync(on));
        restart(null);
        toggleRow(Tr.t("Skip Boot"), Tr.t("Skips the logos at start, straight to the save check."), settings.skipBoot(),
                on -> settings.setSkipBoot(on));
    }

    private void showSaveData() {
        final String saves = AppFiles.HOME + "/Saves";
        card(Tr.t("How it works"), Tr.t("Back up: tap Back Up, then Save Here. The file basara2heroes-save-DATE.zip "
                        + "goes to ") + saves + Tr.t(". Copy it to a PC or Drive to keep it safe.")
                        + "\n\n" + Tr.t("Import (move a save to this phone): 1. Copy the .zip made by Back Up into ")
                        + saves + Tr.t(" (or Download) on this phone. 2. Tap Import and tap that .zip. 3. Start the "
                        + "game: it plays from the imported save.")
                        + "\n\n" + Tr.t("Before every import the save on this phone is kept in ") + saves
                        + Tr.t("/Auto: Undo Last Import puts it back. Only .zip files from Back Up (or a basara2w.dat) "
                        + "work; Dolphin's data.bin does not."),
                null, false, true);

        final boolean has = SaveData.hasSave(activity);
        actionRow(Tr.t("Back Up Save"), has
                        ? Tr.t("Last saved ")
                                + android.text.format.DateFormat.getMediumDateFormat(activity)
                                        .format(new java.util.Date(SaveData.lastSaved(activity)))
                                + " " + android.text.format.DateFormat.getTimeFormat(activity)
                                        .format(new java.util.Date(SaveData.lastSaved(activity))) + "."
                        : Tr.t("There is no save yet: save in the game first (各種設定 > セーブ)."),
                has ? Tr.t("Back Up") : null, this::backUp);
        actionRow(Tr.t("Import Save"), Tr.t("Replaces this phone's save with a .zip made by Back Up. A running "
                        + "game is closed without saving."),
                Tr.t("Import"), this::importSave);
        final java.io.File last = SaveData.latestBackup(activity);
        actionRow(Tr.t("Undo Last Import"), last == null
                        ? Tr.t("Nothing to undo: no save has been imported on this phone.")
                        : Tr.t("Puts back the save the last import replaced (from ")
                                + android.text.format.DateFormat.getMediumDateFormat(activity)
                                        .format(new java.util.Date(last.lastModified()))
                                + " " + android.text.format.DateFormat.getTimeFormat(activity)
                                        .format(new java.util.Date(last.lastModified())) + ").",
                last == null ? null : Tr.t("Undo"), this::confirmUndoImport);
    }

    /** The launcher's endGame, so a running game cannot write over an imported save. */
    private Runnable endGame() {
        return activity instanceof LauncherActivity ? ((LauncherActivity) activity)::endGame : null;
    }

    private void backUp() {
        if (!android.os.Environment.isExternalStorageManager()) {
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
        if (!android.os.Environment.isExternalStorageManager()) {
            askFileAccess(() -> SaveData.startImport(activity));
            return;
        }
        FileBrowser.showFiles(activity, SaveData.importStart(), Tr.t("Choose your save backup"),
                Tr.t("The basara2heroes-save-….zip made by Back Up. The save on this phone is replaced (and kept "
                        + "for Undo Last Import)."),
                SaveData::isSaveFile, Tr.t("No folders or .zip files here."),
                file -> SaveData.importFile(activity, file, endGame(), this::changed));
    }

    private void confirmUndoImport() {
        new Sheet(activity, Tr.t("Undo the last import?"), Tr.t("The save from before that import comes back. A "
                + "running game is closed without saving."))
                .action(Tr.t("Undo"), null, true, false, () -> {
                    SaveData.undoLastImport(activity, endGame(), this::changed);
                })
                .action(Tr.t("Cancel"), null, false, false, null)
                .show();
    }

    /** Without "All files access" the folders cannot be listed: allow it, or use the phone's own picker. */
    private void askFileAccess(Runnable systemPicker) {
        new Sheet(activity, Tr.t("Allow file access"), Tr.t("To list the folders on this phone."))
                .step("1", Tr.t("Open Settings"), Tr.t("Turn on \"Allow access to manage all files\" for this app."))
                .step("2", Tr.t("Come back"), Tr.t("Then tap the button again."))
                .action(Tr.t("Open Settings"), null, true, false, () -> {
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

    /** Only in a build with cheats (BuildConfig.CHEATS); applied at once in the game. */
    private void showCheats() {
        rows.addView(Sheet.warningBox(activity, Tr.t("Cheats"),
                Tr.t("They change your save: money and anything bought with it stay after the game saves. Back up "
                        + "your save first (Save Data). Cheats can break the game's balance or progress, and they "
                        + "are off in LAN play.")),
                Ui.matchWidth(0));

        final int money = settings.cheatMoney();
        final String amount = String.format(java.util.Locale.ROOT, "%,d", settings.cheatMoneyAmount());
        choiceRow(Tr.t("Money"), Tr.t("Infinite keeps the money at the amount below, whatever you buy. Once sets it "
                        + "to that amount one time (Set Now), then it goes down as you spend it."),
                new String[] {Tr.t("Off"), Tr.t("Infinite"), Tr.t("Once")}, money, true,
                which -> settings.setCheatMoney(which));
        actionRow(Tr.t("Money Amount"), amount + Tr.t(" (up to 9,999,999, the game's limit)."),
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
        toggleRow(Tr.t("Touch Controls"), Tr.t("The on-screen Classic Controller."), settings.touchControls(),
                on -> settings.setTouchControls(on));
        final String[] opacities = {"20%", "40%", "60%", "80%", "100%"};
        final int opacity = Math.max(0, Math.min(4, Math.round(settings.touchOpacity() * 5f) - 1));
        choiceRow(Tr.t("Touch Control Opacity"), Tr.t("How solid the controls look over the game."), opacities, opacity,
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
        toggleRow(Tr.t("Touch camera"), Tr.t("Drag on the empty right side of the screen to turn the camera."),
                settings.touchCamera(), on -> settings.setTouchCamera(on));
        toggleRow(Tr.t("Tilt"), Tr.t("Turn the phone like a steering wheel."), settings.motionTilt(),
                on -> settings.setMotionTilt(on));
        toggleRow(Tr.t("Shake"), Tr.t("Jolt the phone to shake the remote."), settings.motionShake(),
                on -> settings.setMotionShake(on));
    }

    /**
     * Everything unfinished, each with a warning: testers can find them, players know what they risk.
     * Each is read when the game starts, unless it says otherwise.
     */
    private void showExperimental() {
        rows.addView(Sheet.warningBox(activity, Tr.t("Experimental features"),
                Tr.t("Unfinished and only partly tested. They can crash the game, glitch the picture, use much more "
                        + "RAM, storage and battery, or make the phone hot. Leave them off to play normally. If "
                        + "you test one, say which in your report and send the log (") + folderName("console.log")
                        + ")."),
                Ui.matchWidth(0));

        restart(null);
        choiceRow(Tr.t("Renderer"), Tr.t("Auto uses Dawn, and the Vulkan renderer only for a GPU driver Dawn cannot "
                        + "use (like malisx2 on Mali). Vulkan always tries the Vulkan renderer first: lighter, "
                        + "made for older phones, still being tuned."),
                new String[] {Tr.t("Auto"), Tr.t("Vulkan")}, settings.renderer(), true, which -> {
                    final int before = settings.renderer();
                    settings.setRenderer(which);
                    confirmRestart(Tr.t("Renderer"), which == 1 ? "Vulkan" : "Auto", before == 1 ? "Vulkan" : "Auto",
                            () -> settings.setRenderer(before));
                }, false);

        warn(weakPhone()
                ? Tr.t("This phone has little RAM or renders below 1x: likely to crash, and the gain is small.")
                : Tr.t("Uses more RAM (up to 4x per texture) and storage, and works on the CPU while you play."));
        restart(null);
        choiceRow(Tr.t("HD Textures"), Tr.t("Makes the game's textures twice as sharp: each one is upscaled on a "
                        + "background thread the first time it appears, and kept for next time. It looks blurry, "
                        + "then sharp, while that happens. Prepare from disc (all at once, before playing) is "
                        + "coming later."),
                new String[] {Tr.t("Off"), Tr.t("Live")}, settings.hdTextures(), true, which -> {
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
        final long used = AppFiles.folderSize(hdCache);
        actionRow(Tr.t("HD Texture Cache"), hdCache == null ? Tr.t("Needs \"All files access\" for this app.")
                        : String.format(java.util.Locale.ROOT, Tr.t("%.0f MB in %s."), used / 1048576.0,
                                folderName("hd-cache")),
                used > 0 && !inGame ? Tr.t("Clear") : null, this::confirmClearHdCache);

        warn(Tr.t("Writes to the log while you play."));
        restart(null);
        toggleRow(Tr.t("Report Texture Keys"), Tr.t("Lists the name of every texture the game uses in the log (only "
                        + "the names, no pictures), so testers can check that texture packs match the game."),
                settings.textureKeyReport(), on -> settings.setTextureKeyReport(on));

        final String folder = folderName("textures");
        final boolean files = android.os.Environment.isExternalStorageManager();
        warn(Tr.t("A wrong or very large pack can crash the game or use up RAM."));
        restart(null);
        toggleRow(Tr.t("Custom Textures"), files
                        ? Tr.t("Uses your own textures (tex1_*.png, Dolphin-style names) from ") + folder
                                + Tr.t(" in place of the game's.")
                        : Tr.t("Needs \"All files access\" for this app, to read ") + folder + ".",
                settings.customTextures(), files, on -> settings.setCustomTextures(on));
        warn(Tr.t("Slows loading and fills storage quickly."));
        restart(null);
        toggleRow(Tr.t("Dump Textures"), settings.customTextures()
                        ? Tr.t("Saves each texture the game uses to ") + folder + Tr.t("/dump, named the way its "
                                + "replacement must be: edit one and put it in ") + folder + Tr.t(". Turn it off when "
                                + "done.")
                        : Tr.t("Turn Custom Textures on to choose."),
                settings.dumpTextures(), settings.customTextures(), on -> settings.setDumpTextures(on));

        final boolean thermal = AppSettings.thermalSupported(activity);
        warn(Tr.t("Still unreliable: can drop to 30 fps or lower the resolution when it is not needed."));
        toggleRow(Tr.t("Thermal Control"), thermal
                        ? Tr.t("When the phone warms up, draws at 30 fps (the game itself keeps full speed) "
                        + "for at least a minute, and lowers the resolution if the GPU is the one working "
                        + "hard. Back to normal once it cools down.")
                        : Tr.t("Not available: needs Android 12 or newer with a thermal sensor report."),
                settings.thermalControl(), thermal, on -> settings.setThermalControl(on));

        warn(Tr.t("Can make the picture switch between sharp and soft during battles."));
        toggleRow(Tr.t("Dynamic Resolution"), Tr.t("Lowers the render resolution while the GPU holds the game back "
                        + "(down to 0.5x) and raises it again when there is room. Render Resolution stays the "
                        + "highest it goes."),
                settings.dynamicResolution(), on -> settings.setDynamicResolution(on));
        warn(Tr.t("Known issue: textures and the sky can flicker white."));
        restart(null);
        toggleRow(Tr.t("GX Thread"), Tr.t("Runs the graphics work on its own CPU core, so busy battles run faster on "
                        + "slower phones."),
                settings.gxThread(), true, on -> {
                    settings.setGxThread(on);
                    confirmRestart(Tr.t("GX Thread"), on ? "on" : "off", on ? "off" : "on",
                            () -> settings.setGxThread(!on));
                }, false);
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
                    if (dir != null) AppFiles.deleteRecursively(dir);
                    changed();
                })
                .action(Tr.t("Cancel"), null, false, false, null)
                .show();
    }

    /** "WxH" the game renders at for the chosen scale (0: the screen's own size). */
    static String renderSize(Activity activity, AppSettings settings) {
        final float scale = settings.resolutionScale();
        final DisplayMetrics m = activity.getResources().getDisplayMetrics();
        final int w = Math.max(m.widthPixels, m.heightPixels), h = Math.min(m.widthPixels, m.heightPixels);
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

    interface Toggle { void set(boolean on); }
    interface Choice { void pick(int which); }

    /** The next card gets a red warning box under its text (experimental features). */
    private String pendingWarning;
    /** In the game: the next card is only read at start, and says so in red. */
    private boolean pendingRestart;
    private String pendingRestartText;

    private void warn(String warning) {
        pendingWarning = warning;
    }

    /** Marks the next card as applying at the next start (`text`: a more exact note, or null). */
    private void restart(String text) {
        pendingRestart = true;
        pendingRestartText = text;
    }

    /** A card: the setting's name and a line about it, with `control` on the right or underneath. */
    LinearLayout card(String title, String about, View control, boolean underneath, boolean enabled) {
        final LinearLayout card = new LinearLayout(activity);
        card.setOrientation(underneath ? LinearLayout.VERTICAL : LinearLayout.HORIZONTAL);
        card.setGravity(underneath ? Gravity.START : Gravity.CENTER_VERTICAL);
        // In the game the screen is short: tighter cards, smaller text.
        if (inGame) card.setPadding(dp(12), dp(9), dp(10), dp(9));
        else card.setPadding(dp(14), dp(12), dp(12), dp(12));
        card.setBackground(Ui.touchBox(activity, Ui.SURFACE, Ui.BORDER, inGame ? 10 : 12));
        final LinearLayout words = new LinearLayout(activity);
        words.setOrientation(LinearLayout.VERTICAL);
        final LinearLayout nameLine = new LinearLayout(activity);
        nameLine.setGravity(Gravity.CENTER_VERTICAL);
        final TextView name = Ui.text(activity, title, inGame ? 13.5f : 14, Ui.TEXT);
        name.setTypeface(Ui.MEDIUM);
        nameLine.addView(name);
        if (pendingRestart && inGame) {
            // A small red tag beside the name, not a box: several restart cards stay readable.
            final TextView tag = Ui.text(activity, "⚠ " + Tr.t("Restart needed"), 10, Ui.DANGER_TEXT);
            tag.setTypeface(Typeface.DEFAULT_BOLD);
            tag.setPadding(dp(6), dp(1), dp(6), dp(1));
            tag.setBackground(Ui.box(activity, Ui.DANGER_FILL, Ui.DANGER_BORDER, 4));
            final LinearLayout.LayoutParams tagParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
            tagParams.leftMargin = dp(8);
            nameLine.addView(tag, tagParams);
        }
        words.addView(nameLine);
        if (about != null && !about.isEmpty()) {
            final TextView line = Ui.text(activity, about, inGame ? 11 : 12, Ui.MUTED);
            line.setPadding(0, dp(2), 0, 0);
            words.addView(line);
        }
        if (pendingRestart && inGame && pendingRestartText != null) {
            final TextView exact = Ui.text(activity, pendingRestartText, 11, Ui.DANGER_TEXT);
            exact.setPadding(0, dp(3), 0, 0);
            words.addView(exact);
        }
        if (pendingWarning != null) {
            words.addView(Sheet.warningBox(activity, Tr.t("Experimental"), pendingWarning), Ui.matchWidth(dp(8)));
            pendingWarning = null;
        }
        pendingRestart = false;
        pendingRestartText = null;
        if (underneath) {
            card.addView(words, Ui.matchWidth(0));
            if (control != null) card.addView(control, Ui.matchWidth(dp(10)));
        } else {
            card.addView(words, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
            if (control != null) {
                // Keep a size the control set for itself (the switch is 40x23 dp, not its knob's size).
                final android.view.ViewGroup.LayoutParams own = control.getLayoutParams();
                final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                        own != null ? own.width : LinearLayout.LayoutParams.WRAP_CONTENT,
                        own != null ? own.height : LinearLayout.LayoutParams.WRAP_CONTENT);
                params.leftMargin = dp(12);
                card.addView(control, params);
            }
        }
        if (!enabled) {
            for (int i = 0; i < card.getChildCount(); ++i) card.getChildAt(i).setAlpha(0.45f);
        }
        rows.addView(card, Ui.matchWidth(rows.getChildCount() == 0 ? 0 : dp(inGame ? 6 : 8)));
        return card;
    }

    /** The whole card toggles; the switch on the right shows On or Off. */
    void toggleRow(String title, String about, boolean on, Toggle toggle) {
        toggleRow(title, about, on, true, toggle);
    }

    /** Disabled: dimmed, shows Off and does nothing when tapped (the phone cannot do it). */
    void toggleRow(String title, String about, boolean on, boolean enabled, Toggle toggle) {
        toggleRow(title, about, on, enabled, toggle, true);
    }

    private void toggleRow(String title, String about, boolean on, boolean enabled, Toggle toggle, boolean redraw) {
        on = on && enabled;
        final LinearLayout card = card(title, about, Ui.toggle(activity, on), false, enabled);
        if (!enabled) return;
        final boolean was = on;
        card.setClickable(true);
        card.setOnClickListener(v -> {
            toggle.set(!was);
            if (redraw) changed();
        });
    }

    /** The choices as chips under the line about it, wrapping onto more lines; the chosen one is lit. */
    void choiceRow(String title, String about, String[] options, int selected, boolean enabled, Choice choice) {
        choiceRow(title, about, options, selected, enabled, choice, true);
    }

    /** `redraw` false: `choice` saves and redraws by itself (it may ask first). */
    private void choiceRow(String title, String about, String[] options, int selected, boolean enabled,
                           Choice choice, boolean redraw) {
        final Ui.FlowLayout chips = new Ui.FlowLayout(activity, dp(6));
        for (int i = 0; i < options.length; ++i) {
            final int which = i;
            chips.addView(Ui.chip(activity, options[i], i == selected, enabled, v -> {
                if (which == selected) return;
                choice.pick(which);
                if (redraw) changed();
            }));
        }
        card(title, about, chips, true, enabled);
    }

    /** Chips that each turn on or off by themselves; the lit ones are on. */
    private void multiRow(String title, String about, String[] options, boolean[] lit, boolean enabled,
                          Choice flip) {
        final Ui.FlowLayout chips = new Ui.FlowLayout(activity, dp(6));
        for (int i = 0; i < options.length; ++i) {
            final int which = i;
            chips.addView(Ui.chip(activity, (lit[i] ? "✓ " : "") + options[i], lit[i], enabled, v -> {
                flip.pick(which);
                changed();
            }));
        }
        card(title, about, chips, true, enabled);
    }

    /** A card with a button on the right (none when `label` is null). */
    void actionRow(String title, String about, String label, Runnable action) {
        TextView button = null;
        if (label != null) {
            button = Ui.text(activity, label, 13, Ui.TEXT);
            button.setTypeface(Typeface.DEFAULT_BOLD);
            button.setPadding(dp(14), dp(7), dp(14), dp(7));
            button.setBackground(Ui.touchBox(activity, Ui.ACTIVE, Ui.BORDER_STRONG, 8));
            button.setOnClickListener(v -> {
                action.run();
                changed();
            });
        }
        card(title, about, button, false, true);
    }

    private int dp(int value) {
        return Ui.dp(activity, value);
    }
}
