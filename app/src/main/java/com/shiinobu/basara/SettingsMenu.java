package com.shiinobu.basara;

import android.app.Activity;
import android.app.Dialog;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.util.DisplayMetrics;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * The app's settings on the start menu: one full screen, the sections down the left and the open
 * section's settings on the right, each changed with one tap (a switch, or a row of choices). They are
 * saved to the app's preferences and written to Config.toml before every start; the game's own settings
 * window changes resolution, aspect and the FPS counter while playing, and the app keeps those choices.
 */
final class SettingsMenu {
    private static final String TAG = "Basara";
    private static final int BACKGROUND = Color.rgb(16, 20, 24);

    private static final int DISPLAY = 0, GRAPHICS = 1, PERFORMANCE = 2, CONTROLS = 3, SAVE_DATA = 4, CHEATS = 5,
            EXPERIMENTAL = 6;
    private static final String[] SECTION_NAMES = {"Display", "Graphics", "Performance", "Controls", "Save Data",
        "Cheats", "Experimental"};
    private static final String[] SECTION_ABOUT = {
        "Picture shape and size",
        "Sharpness and GPU fixes",
        "Smoothness and speed",
        "Touch controls and motion",
        "Back up and import",
        "Money, HP, Basara gauge",
        "Unfinished, for testers",
    };
    /** The sections shown, in order: Cheats only in a build that has them (-PbasaraCheats). */
    private static final int[] SECTIONS = BuildConfig.CHEATS
            ? new int[] {DISPLAY, GRAPHICS, PERFORMANCE, CONTROLS, SAVE_DATA, CHEATS, EXPERIMENTAL}
            : new int[] {DISPLAY, GRAPHICS, PERFORMANCE, CONTROLS, SAVE_DATA, EXPERIMENTAL};

    private final Activity activity;
    private final AppSettings settings;
    private final Dialog dialog;
    private final LinearLayout nav;
    private final LinearLayout rows;
    private final ScrollView rowsScroll;
    private int section;

    static void show(Activity activity) {
        show(activity, null);
    }

    /** `closed`: runs when the screen closes (the launcher shows the settings in use). */
    static void show(Activity activity, Runnable closed) {
        final SettingsMenu menu = new SettingsMenu(activity);
        if (closed != null) menu.dialog.setOnDismissListener(d -> closed.run());
        menu.dialog.show();
    }

    private SettingsMenu(Activity activity) {
        this.activity = activity;
        this.settings = new AppSettings(activity);

        nav = new LinearLayout(activity);
        nav.setOrientation(LinearLayout.VERTICAL);
        final TextView title = text(Tr.t("Settings"), 24, Color.WHITE);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setPadding(dp(8), 0, 0, dp(14));
        nav.addView(title);
        for (int i = 0; i < SECTIONS.length; ++i) {
            final int index = i;
            final LinearLayout item = new LinearLayout(activity);
            item.setOrientation(LinearLayout.VERTICAL);
            item.setPadding(dp(16), dp(10), dp(16), dp(10));
            item.setClickable(true);
            item.setOnClickListener(v -> showSection(index));
            final int id = SECTIONS[i];
            final TextView name = text(Tr.t(SECTION_NAMES[id]), 16,
                    id == EXPERIMENTAL || id == CHEATS ? Sheet.WARNING_TEXT : Color.WHITE);
            name.setTypeface(LauncherActivity.MEDIUM);
            item.addView(name);
            item.addView(text(Tr.t(SECTION_ABOUT[id]), 12, LauncherActivity.MUTED));
            nav.addView(item, matchWidth(dp(6)));
        }
        final View spacer = new View(activity);
        nav.addView(spacer, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
        final TextView done = text(Tr.t("Done"), 17, Color.WHITE);
        done.setTypeface(Typeface.DEFAULT_BOLD);
        done.setGravity(Gravity.CENTER);
        done.setPadding(0, dp(12), 0, dp(12));
        done.setBackground(rounded(LauncherActivity.ACCENT, dp(24)));
        done.setOnClickListener(v -> close());
        nav.addView(done, matchWidth(dp(10)));

        rows = new LinearLayout(activity);
        rows.setOrientation(LinearLayout.VERTICAL);
        rowsScroll = new ScrollView(activity);
        rowsScroll.setVerticalScrollBarEnabled(false);
        rowsScroll.addView(rows);

        final LinearLayout page = new LinearLayout(activity);
        page.setOrientation(LinearLayout.HORIZONTAL);
        page.setPadding(dp(16), dp(16), dp(16), dp(16));
        page.setBackgroundColor(BACKGROUND);
        final ScrollView navScroll = new ScrollView(activity);
        navScroll.setFillViewport(true);
        navScroll.setVerticalScrollBarEnabled(false);
        navScroll.addView(nav);
        page.addView(navScroll, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 0.3f));
        final LinearLayout.LayoutParams rowsParams =
                new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 0.7f);
        rowsParams.leftMargin = dp(16);
        page.addView(rowsScroll, rowsParams);

        dialog = new Dialog(activity, android.R.style.Theme_DeviceDefault_NoActionBar_Fullscreen);
        dialog.setContentView(page);
        dialog.setOnShowListener(d -> {
            dialog.getWindow().setDecorFitsSystemWindows(false);
            final WindowInsetsController controller = dialog.getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        });
        showSection(0);
    }

    private void close() {
        dialog.dismiss();
    }

    private void save() {
        try {
            AppFiles.writeConfig(activity, settings);
        } catch (Exception e) {
            Log.e(TAG, "could not save the settings", e);
        }
    }

    /** A setting read once when the game starts: either the change restarts it, or it goes back. */
    private void confirmRestart(String name, String now, String before, Runnable revert) {
        final Runnable back = () -> {
            revert.run();
            changed();
        };
        new Sheet(activity, Tr.t("Restart Required"), name + Tr.t(" takes effect when the game starts again."))
                .action(Tr.t("Restart"), Tr.t("Restart the game now with ") + name + " " + now, true, false, () -> {
                    if (activity instanceof LauncherActivity && ((LauncherActivity) activity).restartGame()) {
                        dialog.dismiss();
                    }
                })
                .action(Tr.t("Cancel"), Tr.t("Keep ") + name + " " + before, false, false, back)
                .onCancel(back)
                .show();
    }

    /** After a change: save, and draw the section again with the new values. */
    private void changed() {
        save();
        // Rebuilt in place: the views are swapped before the next frame, so the list stays where it was
        // (no jump to the top and back).
        fillSection(section);
    }

    private void showSection(int index) {
        fillSection(index);
        rowsScroll.scrollTo(0, 0);
    }

    private void fillSection(int index) {
        section = index;
        for (int i = 0; i < SECTIONS.length; ++i) {
            nav.getChildAt(i + 1).setBackground(i == index ? rounded(LauncherActivity.RAISED, dp(16)) : null);
        }
        rows.removeAllViews();
        switch (SECTIONS[index]) {
            case DISPLAY: showDisplay(); break;
            case GRAPHICS: showGraphics(); break;
            case PERFORMANCE: showPerformance(); break;
            case CONTROLS: showControls(); break;
            case SAVE_DATA: showSaveData(); break;
            case CHEATS: showCheats(); break;
            default: showExperimental(); break;
        }
    }

    // --- sections --------------------------------------------------------------------------------

    private void showDisplay() {
        choiceRow(Tr.t("Language"), Tr.t("The launcher's language. Auto follows the phone's region: Indonesian "
                        + "in Indonesia, English elsewhere."),
                new String[] {Tr.t("Auto"), "English", "Bahasa Indonesia"}, settings.language(), true, which -> {
                    settings.setLanguage(which);
                    Tr.init(activity);
                    // Every screen is drawn again in the new language.
                    dialog.dismiss();
                    activity.recreate();
                });
        choiceRow(Tr.t("Aspect Ratio"), Tr.t("The shape of the picture."),
                new String[] {Tr.t("Original 4:3"), "16:9", Tr.t("Fill Screen")}, settings.aspectMode(), true,
                which -> settings.setAspectMode(which));
        final float[] scales = AppSettings.RESOLUTION_SCALES;
        final String[] labels = new String[scales.length];
        int selected = 3;
        for (int i = 0; i < scales.length; ++i) {
            labels[i] = AppSettings.resolutionLabel(scales[i]);
            if (scales[i] <= 0f ? settings.resolutionScale() <= 0f
                    : Math.abs(scales[i] - settings.resolutionScale()) < 0.01f) {
                selected = i;
            }
        }
        final DisplayMetrics screen = activity.getResources().getDisplayMetrics();
        final boolean aboveScreen = settings.resolutionScale() > 0f
                && 480 * settings.resolutionScale() > Math.min(screen.widthPixels, screen.heightPixels) * 1.05f;
        choiceRow(Tr.t("Render Resolution"), Tr.t("Rendered at ") + renderSize(settings.resolutionScale())
                + (aboveScreen
                        ? Tr.t(", more than this screen shows: several times the GPU work for little sharpness, so fps can drop. Match screen is sharp enough.")
                        : Tr.t(". Lower is lighter on a slow phone; FSR1 keeps it sharp.")), labels, selected, true,
                which -> settings.setResolutionScale(scales[which]));
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

    private void showGraphics() {
        toggleRow(Tr.t("FSR1 Upscaling"), Tr.t("Keeps a picture rendered below the screen's resolution sharp when "
                + "it is stretched to fill it."), settings.fsrOn(), on -> settings.setFsrOn(on));
        choiceRow(Tr.t("FSR1 Quality"), settings.fsrOn()
                        ? Tr.t("Light sharpens the stretched picture and costs almost nothing. Full adds FSR1's "
                                + "edge-aware upscale: sharpest, but several ms on a small phone GPU.")
                        : Tr.t("Turn FSR1 Upscaling on to choose."),
                new String[] {Tr.t("Light"), Tr.t("Full")}, settings.fsrFull() ? 1 : 0, settings.fsrOn(),
                which -> settings.setFsrFull(which == 1));
        choiceRow(Tr.t("Texture Detail"), Tr.t("Keeps the ground, walls and characters sharp at low Render "
                        + "Resolution and when seen at an angle. Sharpest costs a little more on a small phone GPU."),
                new String[] {Tr.t("Original"), Tr.t("Sharp"), Tr.t("Sharpest")}, settings.textureFilter(), true,
                which -> settings.setTextureFilter(which));
        choiceRow(Tr.t("Graphics Compatibility"), Tr.t("Fixes for phone GPUs whose drivers break the 3D models "
                        + "(scrambled textures, stretched characters). Auto uses them where needed."),
                new String[] {Tr.t("Auto"), Tr.t("On"), Tr.t("Off")}, settings.gpuCompat(), true,
                which -> settings.setGpuCompat(which));
        showGpuDrivers();
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
        choiceRow(Tr.t("GPU Driver"), Tr.t("System is the phone's own driver. A driver you install (a Turnip .zip "
                        + "made for Dolphin or Yuzu, for Snapdragon / Adreno) can be faster or fix broken graphics. "
                        + "Mali drivers (malisx2) run on the Vulkan renderer, which starts by itself for them. "
                        + "If it fails or the game closes while starting, it goes back to System by itself."),
                options, selected, true, which -> {
                    settings.setGpuDriver(which == 0 ? "" : drivers.get(which - 1).id);
                    confirmRestart(Tr.t("GPU Driver"), options[which], beforeName,
                            () -> settings.setGpuDriver(before));
                });
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
        toggleRow(Tr.t("V-Sync"), Tr.t("Frames go out in step with the display: smoother motion."), settings.vsync(),
                on -> settings.setVSync(on));
        choiceRow(Tr.t("Frameskip"), Tr.t("The game moves one step per drawn frame, so a slow phone plays in slow "
                        + "motion. Auto skips drawing a frame when it falls behind: closer to full speed, "
                        + "choppier picture."),
                new String[] {Tr.t("Off"), Tr.t("Auto")}, settings.frameskip() ? 1 : 0, true,
                which -> settings.setFrameskip(which == 1));
        toggleRow(Tr.t("30 FPS Lock"), Tr.t("Draws every other frame, evenly paced: a steady 30 FPS while the game "
                        + "and sound keep full speed. For phones that cannot hold 60. Replaces Frameskip "
                        + "while on."),
                settings.fpsLock30(), on -> settings.setFpsLock30(on));
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

    /** Only in a build with cheats (BuildConfig.CHEATS); applied when the game starts or comes back. */
    private void showCheats() {
        rows.addView(Sheet.warningBox(activity, Tr.t("Cheats"),
                Tr.t("They change your save: money and anything bought with it stay after the game saves. Back up "
                        + "your save first (Save Data). Cheats can break the game's balance or progress, and they "
                        + "are off in LAN play. Changes apply when you go back to the game.")),
                matchWidth(0));

        final int money = settings.cheatMoney();
        final String amount = String.format(java.util.Locale.ROOT, "%,d", settings.cheatMoneyAmount());
        choiceRow(Tr.t("Money"), Tr.t("Infinite keeps the money at the amount below, whatever you buy. Once sets it "
                        + "to that amount one time (Set Now), then it goes down as you spend it."),
                new String[] {Tr.t("Off"), Tr.t("Infinite"), Tr.t("Once")}, money, true,
                which -> settings.setCheatMoney(which));
        actionRow(Tr.t("Money Amount"), amount + Tr.t(" (up to 9,999,999, the game's limit)."),
                money != AppSettings.MONEY_OFF ? Tr.t("Change") : null, this::askMoneyAmount);
        if (money == AppSettings.MONEY_ONCE) {
            actionRow(Tr.t("Set Money Now"), Tr.t("Load your save in the game first, then tap Set Now and go back "
                            + "to the game: the money becomes ") + amount + ".",
                    Tr.t("Set Now"), () -> {
                        settings.bumpCheatMoneySeq();
                        android.widget.Toast.makeText(activity, Tr.t("Money is set when you go back to the game."),
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
        // The keyboard covers the Save button on a landscape phone: its Done key saves too.
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
                matchWidth(0));

        choiceRow(Tr.t("Renderer"), Tr.t("Auto uses Dawn, and the Vulkan renderer only for a GPU driver Dawn cannot "
                        + "use (like malisx2 on Mali). Vulkan always tries the Vulkan renderer first: lighter, "
                        + "made for older phones, still being tuned. Restarts the game."),
                new String[] {Tr.t("Auto"), Tr.t("Vulkan")}, settings.renderer(), true, which -> {
                    final int before = settings.renderer();
                    settings.setRenderer(which);
                    confirmRestart(Tr.t("Renderer"), which == 1 ? "Vulkan" : "Auto", before == 1 ? "Vulkan" : "Auto",
                            () -> settings.setRenderer(before));
                });

        warn(weakPhone()
                ? Tr.t("This phone has little RAM or renders below 1x: likely to crash, and the gain is small.")
                : Tr.t("Uses more RAM (up to 4x per texture) and storage, and works on the CPU while you play."));
        choiceRow(Tr.t("HD Textures"), Tr.t("Makes the game's textures twice as sharp: each one is upscaled on a "
                        + "background thread the first time it appears, and kept for next time. It looks blurry, "
                        + "then sharp, while that happens. Prepare from disc (all at once, before playing) is "
                        + "coming later."),
                new String[] {Tr.t("Off"), Tr.t("Live")}, settings.hdTextures(), true, which -> {
                    final int before = settings.hdTextures();
                    settings.setHdTextures(which);
                    confirmRestart(Tr.t("HD Textures"), which == 1 ? Tr.t("Live") : "off", before == 1 ? Tr.t("Live") : "off",
                            () -> settings.setHdTextures(before));
                });
        final int[] sizes = AppSettings.HD_CACHE_SIZES;
        int size = 1;
        for (int i = 0; i < sizes.length; ++i) if (sizes[i] == settings.hdCacheMb()) size = i;
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
                used > 0 ? Tr.t("Clear") : null, this::confirmClearHdCache);

        warn(Tr.t("Writes to the log while you play."));
        toggleRow(Tr.t("Report Texture Keys"), Tr.t("Lists the name of every texture the game uses in the log (only "
                        + "the names, no pictures), so testers can check that texture packs match the game. "
                        + "Takes effect when the game starts again."),
                settings.textureKeyReport(), on -> settings.setTextureKeyReport(on));

        final String folder = folderName("textures");
        final boolean files = android.os.Environment.isExternalStorageManager();
        warn(Tr.t("A wrong or very large pack can crash the game or use up RAM."));
        toggleRow(Tr.t("Custom Textures"), files
                        ? Tr.t("Uses your own textures (tex1_*.png, Dolphin-style names) from ") + folder
                                + Tr.t(" in place of the game's. Takes effect when the game starts again.")
                        : Tr.t("Needs \"All files access\" for this app, to read ") + folder + ".",
                settings.customTextures(), files, on -> settings.setCustomTextures(on));
        warn(Tr.t("Slows loading and fills storage quickly."));
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
        toggleRow(Tr.t("GX Thread"), Tr.t("Runs the graphics work on its own CPU core, so busy battles run faster on "
                        + "slower phones. Turning it on or off restarts the game."),
                settings.gxThread(), on -> {
                    settings.setGxThread(on);
                    confirmRestart(Tr.t("GX Thread"), on ? "on" : "off", on ? "off" : "on",
                            () -> settings.setGxThread(!on));
                });
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

    /** "WxH" the game renders at for a scale (0: the screen's own size). */
    private String renderSize(float scale) {
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

    private interface Toggle { void set(boolean on); }
    private interface Choice { void pick(int which); }

    /** The next card gets a red warning box under its text (experimental features). */
    private String pendingWarning;

    private void warn(String warning) {
        pendingWarning = warning;
    }

    /** A card: the setting's name and a line about it, with `control` on the right or underneath. */
    private LinearLayout card(String title, String about, View control, boolean underneath, boolean enabled) {
        final LinearLayout card = new LinearLayout(activity);
        card.setOrientation(underneath ? LinearLayout.VERTICAL : LinearLayout.HORIZONTAL);
        card.setGravity(underneath ? Gravity.START : Gravity.CENTER_VERTICAL);
        card.setPadding(dp(18), dp(14), dp(14), dp(14));
        card.setBackground(rounded(LauncherActivity.SURFACE, dp(18)));
        card.setAlpha(enabled ? 1f : 0.45f);
        final LinearLayout words = new LinearLayout(activity);
        words.setOrientation(LinearLayout.VERTICAL);
        final TextView name = text(title, 16, Color.WHITE);
        name.setTypeface(LauncherActivity.MEDIUM);
        words.addView(name);
        final TextView line = text(about, 13, LauncherActivity.MUTED);
        line.setPadding(0, dp(2), 0, 0);
        words.addView(line);
        if (pendingWarning != null) {
            words.addView(Sheet.warningBox(activity, Tr.t("Experimental"), pendingWarning), matchWidth(dp(8)));
            pendingWarning = null;
        }
        if (underneath) {
            card.addView(words, matchWidth(0));
            if (control != null) card.addView(control, matchWidth(dp(12)));
        } else {
            card.addView(words, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
            if (control != null) {
                final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
                params.leftMargin = dp(12);
                card.addView(control, params);
            }
        }
        rows.addView(card, matchWidth(rows.getChildCount() == 0 ? 0 : dp(10)));
        return card;
    }

    /** The whole card toggles; the switch on the right shows On or Off. */
    private void toggleRow(String title, String about, boolean on, Toggle toggle) {
        toggleRow(title, about, on, true, toggle);
    }

    /** Disabled: dimmed, shows Off and does nothing when tapped (the phone cannot do it). */
    private void toggleRow(String title, String about, boolean on, boolean enabled, Toggle toggle) {
        on = on && enabled;
        final LinearLayout track = new LinearLayout(activity);
        track.setGravity(on ? Gravity.END | Gravity.CENTER_VERTICAL : Gravity.START | Gravity.CENTER_VERTICAL);
        track.setPadding(dp(4), dp(4), dp(4), dp(4));
        track.setBackground(rounded(on ? LauncherActivity.ACCENT : LauncherActivity.RAISED, dp(16)));
        final View knob = new View(activity);
        final GradientDrawable dot = new GradientDrawable();
        dot.setShape(GradientDrawable.OVAL);
        dot.setColor(on ? Color.WHITE : LauncherActivity.MUTED);
        knob.setBackground(dot);
        track.addView(knob, new LinearLayout.LayoutParams(dp(24), dp(24)));
        final LinearLayout.LayoutParams trackParams = new LinearLayout.LayoutParams(dp(56), dp(32));
        final LinearLayout box = new LinearLayout(activity);
        box.setGravity(Gravity.CENTER_VERTICAL);
        final TextView state = text(on ? Tr.t("On") : Tr.t("Off"), 13, on ? Color.WHITE : LauncherActivity.MUTED);
        state.setTypeface(Typeface.DEFAULT_BOLD);
        state.setPadding(0, 0, dp(10), 0);
        box.addView(state);
        box.addView(track, trackParams);
        final LinearLayout card = card(title, about, box, false, enabled);
        if (!enabled) return;
        final boolean was = on;
        card.setClickable(true);
        card.setOnClickListener(v -> {
            toggle.set(!was);
            changed();
        });
    }

    /** The choices as chips under the line about it, wrapping onto more lines; the chosen one is lit. */
    private void choiceRow(String title, String about, String[] options, int selected, boolean enabled,
                           Choice choice) {
        final FlowLayout chips = new FlowLayout(activity, dp(8));
        for (int i = 0; i < options.length; ++i) {
            final int which = i;
            final boolean on = i == selected;
            final TextView chip = text(options[i], 14, on ? Color.WHITE : LauncherActivity.MUTED);
            chip.setTypeface(on ? Typeface.DEFAULT_BOLD : LauncherActivity.MEDIUM);
            chip.setGravity(Gravity.CENTER);
            chip.setPadding(dp(16), dp(8), dp(16), dp(8));
            chip.setMinWidth(dp(64));
            chip.setBackground(rounded(on ? LauncherActivity.ACCENT : LauncherActivity.RAISED, dp(18)));
            if (enabled) {
                chip.setOnClickListener(v -> {
                    if (which == selected) return;
                    choice.pick(which);
                    changed();
                });
            }
            chips.addView(chip);
        }
        card(title, about, chips, true, enabled);
    }

    /** Chips that each turn on or off by themselves; the lit ones are on. */
    private void multiRow(String title, String about, String[] options, boolean[] lit, boolean enabled,
                          Choice flip) {
        final FlowLayout chips = new FlowLayout(activity, dp(8));
        for (int i = 0; i < options.length; ++i) {
            final int which = i;
            final boolean on = lit[i];
            final TextView chip = text((on ? "✓  " : "") + options[i], 14, on ? Color.WHITE : LauncherActivity.MUTED);
            chip.setTypeface(on ? Typeface.DEFAULT_BOLD : LauncherActivity.MEDIUM);
            chip.setGravity(Gravity.CENTER);
            chip.setPadding(dp(16), dp(8), dp(16), dp(8));
            chip.setBackground(rounded(on ? LauncherActivity.ACCENT : LauncherActivity.RAISED, dp(18)));
            if (enabled) {
                chip.setOnClickListener(v -> {
                    flip.pick(which);
                    changed();
                });
            }
            chips.addView(chip);
        }
        card(title, about, chips, true, enabled);
    }

    /** Children left to right, starting a new line when the next one does not fit. */
    private static final class FlowLayout extends ViewGroup {
        private final int gap;

        FlowLayout(Activity activity, int gap) {
            super(activity);
            this.gap = gap;
        }

        @Override
        protected void onMeasure(int widthSpec, int heightSpec) {
            final int max = MeasureSpec.getSize(widthSpec) - getPaddingLeft() - getPaddingRight();
            final boolean bounded = MeasureSpec.getMode(widthSpec) != MeasureSpec.UNSPECIFIED;
            int x = 0, y = 0, lineH = 0, widest = 0;
            for (int i = 0; i < getChildCount(); ++i) {
                final View c = getChildAt(i);
                c.measure(MeasureSpec.makeMeasureSpec(bounded ? max : 0,
                                bounded ? MeasureSpec.AT_MOST : MeasureSpec.UNSPECIFIED),
                        MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED));
                if (x > 0 && bounded && x + c.getMeasuredWidth() > max) {
                    x = 0;
                    y += lineH + gap;
                    lineH = 0;
                }
                x += c.getMeasuredWidth();
                widest = Math.max(widest, x);
                x += gap;
                lineH = Math.max(lineH, c.getMeasuredHeight());
            }
            final int w = bounded ? MeasureSpec.getSize(widthSpec) : widest + getPaddingLeft() + getPaddingRight();
            setMeasuredDimension(w, resolveSize(y + lineH + getPaddingTop() + getPaddingBottom(), heightSpec));
        }

        @Override
        protected void onLayout(boolean changed, int l, int t, int r, int b) {
            final int max = r - l - getPaddingRight();
            int x = getPaddingLeft(), y = getPaddingTop(), lineH = 0;
            for (int i = 0; i < getChildCount(); ++i) {
                final View c = getChildAt(i);
                final int cw = c.getMeasuredWidth(), ch = c.getMeasuredHeight();
                if (x > getPaddingLeft() && x + cw > max) {
                    x = getPaddingLeft();
                    y += lineH + gap;
                    lineH = 0;
                }
                c.layout(x, y, x + cw, y + ch);
                x += cw + gap;
                lineH = Math.max(lineH, ch);
            }
        }
    }

    /** A card with a button on the right (none when `label` is null). */
    private void actionRow(String title, String about, String label, Runnable action) {
        TextView button = null;
        if (label != null) {
            button = text(label, 14, Color.WHITE);
            button.setTypeface(Typeface.DEFAULT_BOLD);
            button.setPadding(dp(16), dp(8), dp(16), dp(8));
            button.setBackground(rounded(LauncherActivity.RAISED, dp(18)));
            button.setOnClickListener(v -> {
                action.run();
                changed();
            });
        }
        card(title, about, button, false, true);
    }

    // --- views -----------------------------------------------------------------------------------

    private TextView text(String value, int sp, int color) {
        final TextView view = new TextView(activity);
        view.setText(value);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        view.setTextColor(color);
        return view;
    }

    private static GradientDrawable rounded(int color, int radius) {
        final GradientDrawable shape = new GradientDrawable();
        shape.setCornerRadius(radius);
        shape.setColor(color);
        return shape;
    }

    private static LinearLayout.LayoutParams matchWidth(int topMargin) {
        final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.topMargin = topMargin;
        return params;
    }

    private int dp(int value) {
        return Math.round(value * activity.getResources().getDisplayMetrics().density);
    }
}
