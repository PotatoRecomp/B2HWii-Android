package com.shiinobu.basara;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.provider.Settings;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.HorizontalScrollView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.IOException;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Start screen, one portrait page: the game and its state at the top, the settings in use, then
 * every setting by section, and Play at the bottom within the thumb's reach. The game
 * comes from the player's own ISO or RVZ, which is unpacked once when chosen.
 */
public final class LauncherActivity extends Activity {
    private static final int REQUEST_DISC = 1;
    /** Set by GameActivity.restartForSession: open the game again once its old process is gone. */
    static final String EXTRA_RESTART_GAME = "restartGame";
    /** Set by the game's "Start Menu": no automatic update check on the way back. */
    static final String EXTRA_SHOW_START_MENU = "showStartMenu";

    static final String PROJECT_URL = "https://github.com/shiinobu/basara2heroes-android";

    private AppSettings settings;
    private boolean updating;
    /** An update waiting for the player to allow installs from this app. */
    private Updater.Release waitingUpdate;
    private static final String STATE_WAITING_UPDATE = "waiting_update";
    /** Gone to allow "All files access" for the file browser: open it on the way back. */
    private boolean waitingFileAccess;
    private static final String STATE_WAITING_FILE_ACCESS = "waiting_file_access";
    /** Gone to allow "All files access" before playing (the log goes to /sdcard): play on the way back. */
    private boolean waitingPlayAccess;
    /** The required "All files access" sheet is on screen. */
    private Sheet accessSheet;
    private static final String STATE_WAITING_PLAY_ACCESS = "waiting_play_access";
    private static final String STATE_SECTION = "section";

    private int endedGame;  // the pid endGame just ended
    private TextView status;
    private View statusDot;
    private LinearLayout configStrip;
    private LinearLayout tabs;
    private LinearLayout lowEndBox;
    private boolean lowEndDevice;
    private LinearLayout panel;
    private ScrollView scroll;
    private SettingsMenu menu;
    private int[] sections;
    private TextView sessionLine;
    private LinearLayout play;
    private TextView playLabel;
    private LinearLayout stop;
    private final Handler main = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        settings = new AppSettings(this);
        Tr.init(this);
        if (DeviceProfile.applyOnce(this, settings)) {
            Toast.makeText(this, Tr.t("This phone now uses the Low-End preset: steadier, a bit blurrier. "
                    + "Turn it off with the switch above All settings."), Toast.LENGTH_LONG).show();
        }
        // Updates are checked on a fresh start, not when the game sent the player here or is
        // restarting itself into a LAN session.
        final Intent launch = getIntent();
        final boolean asked = launch != null && (launch.getBooleanExtra(EXTRA_SHOW_START_MENU, false)
                || launch.getBooleanExtra(EXTRA_RESTART_GAME, false));
        final boolean opened = savedInstanceState == null && !asked;

        final LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setBackgroundColor(Ui.BG);
        page.addView(header(), Ui.matchWidth(0));
        page.addView(line(), new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 1));
        page.addView(configStripView(), Ui.matchWidth(0));
        page.addView(line(), new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 1));

        // The section tabs stay above the scrolling settings, so another section is one tap away anywhere.
        final LinearLayout tabBox = new LinearLayout(this);
        tabBox.setOrientation(LinearLayout.VERTICAL);
        tabBox.setPadding(dp(14), dp(12), dp(14), dp(10));
        sections = SettingsMenu.launcherSections();
        lowEndDevice = DeviceProfile.lowEnd(this, settings);
        lowEndBox = new LinearLayout(this);
        lowEndBox.setOrientation(LinearLayout.VERTICAL);
        tabBox.addView(lowEndBox, Ui.matchWidth(0));
        final LinearLayout divider = new LinearLayout(this);
        divider.setGravity(Gravity.CENTER_VERTICAL);
        divider.addView(Ui.caption(this, Tr.t("All settings")),
                new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        final TextView count = Ui.text(this, sections.length + Tr.t(" sections"), 10.5f, Ui.MUTED);
        count.setTypeface(Ui.MONO);
        divider.addView(count);
        tabBox.addView(divider, Ui.matchWidth(0));
        tabs = new LinearLayout(this);
        tabs.setOrientation(LinearLayout.VERTICAL);
        tabBox.addView(tabs, Ui.matchWidth(dp(8)));
        page.addView(tabBox, Ui.matchWidth(0));
        page.addView(line(), new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 1));

        final LinearLayout body = new LinearLayout(this);
        body.setOrientation(LinearLayout.VERTICAL);
        body.setPadding(dp(14), dp(2), dp(14), dp(20));
        panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        body.addView(panel, Ui.matchWidth(dp(10)));

        scroll = new ScrollView(this);
        scroll.setVerticalScrollBarEnabled(false);
        scroll.addView(body);
        page.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
        page.addView(line(), new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 1));
        page.addView(actionBar(), Ui.matchWidth(0));

        // Room for the status and navigation bars: the page is drawn edge to edge.
        page.setOnApplyWindowInsetsListener((v, insets) -> {
            final android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars()
                    | WindowInsets.Type.displayCutout());
            v.setPadding(bars.left, bars.top, bars.right, bars.bottom);
            return WindowInsets.CONSUMED;
        });
        setContentView(page);

        menu = new SettingsMenu(this, panel, false, this::showSummary);
        menu.systemRows = this::systemRows;
        final int section = savedInstanceState != null
                ? savedInstanceState.getInt(STATE_SECTION, SettingsMenu.DISPLAY) : SettingsMenu.DISPLAY;
        selectSection(section, false);

        restartGameIfAsked(getIntent());
        if (savedInstanceState != null) {
            // Android 11 ends this process when "Allow from this source" changes.
            waitingUpdate = Updater.Release.from(savedInstanceState.getBundle(STATE_WAITING_UPDATE));
            waitingFileAccess = savedInstanceState.getBoolean(STATE_WAITING_FILE_ACCESS, false);
            waitingPlayAccess = savedInstanceState.getBoolean(STATE_WAITING_PLAY_ACCESS, false);
        }
        if (opened && (settings.autoUpdate() || BuildConfig.BETA)) {
            checkForUpdates(true);
        }
        // After an install or update: what this version brings, once.
        if (savedInstanceState == null) main.post(() -> ReleaseNotes.showIfNew(this, settings));
    }

    /** Title, version, the game's state, and What's New / Help / language on the right. */
    private View header() {
        final LinearLayout header = new LinearLayout(this);
        header.setGravity(Gravity.CENTER_VERTICAL);
        header.setPadding(dp(16), dp(14), dp(12), dp(12));

        final LinearLayout brand = new LinearLayout(this);
        brand.setOrientation(LinearLayout.VERTICAL);
        final LinearLayout titleRow = new LinearLayout(this);
        titleRow.setGravity(Gravity.CENTER_VERTICAL);
        final TextView title = Ui.text(this, Tr.t("Basara 2 Heroes"), 18, Ui.TEXT);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setLetterSpacing(-0.02f);
        titleRow.addView(title);
        final TextView badge = Ui.text(this, versionText(), 10, Ui.SECONDARY);
        badge.setTypeface(Ui.MONO);
        badge.setPadding(dp(6), dp(2), dp(6), dp(2));
        badge.setBackground(Ui.box(this, Ui.SURFACE, Ui.BORDER, 5));
        badge.setSingleLine(true);
        final LinearLayout.LayoutParams badgeParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        badgeParams.leftMargin = dp(8);
        titleRow.addView(badge, badgeParams);
        brand.addView(titleRow);

        final LinearLayout statusRow = new LinearLayout(this);
        statusRow.setGravity(Gravity.CENTER_VERTICAL);
        statusDot = new View(this);
        statusRow.addView(statusDot, new LinearLayout.LayoutParams(dp(7), dp(7)));
        status = Ui.text(this, "", 11.5f, Ui.SECONDARY);
        status.setTypeface(Ui.MONO);
        final LinearLayout.LayoutParams statusParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        statusParams.leftMargin = dp(7);
        statusRow.addView(status, statusParams);
        brand.addView(statusRow, Ui.matchWidth(dp(5)));
        final TextView credit = Ui.text(this, "Port made by Xu", 10.5f, Ui.SECONDARY);
        credit.setTypeface(Ui.MONO);
        brand.addView(credit, Ui.matchWidth(dp(3)));
        header.addView(brand, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));

        header.addView(iconButton(R.drawable.ic_whatsnew, null, v -> ReleaseNotes.showAll(this)));
        header.addView(iconButton(R.drawable.ic_help, null, v -> showHelp()));
        final boolean indonesian = Tr.resolve(settings.language()) == Tr.INDONESIAN;
        header.addView(iconButton(0, indonesian ? "ID" : "EN", v -> {
            settings.setLanguage(indonesian ? Tr.ENGLISH : Tr.INDONESIAN);
            Tr.init(this);
            recreate();
        }));
        return header;
    }

    private View iconButton(int icon, String label, View.OnClickListener onClick) {
        final LinearLayout button = new LinearLayout(this);
        button.setGravity(Gravity.CENTER);
        button.setMinimumWidth(dp(36));
        button.setPadding(dp(8), 0, dp(8), 0);
        button.setBackground(Ui.touchBox(this, Ui.SURFACE, Ui.BORDER, 8));
        if (icon != 0) button.addView(Ui.icon(this, icon, Ui.SECONDARY, 17));
        if (label != null) {
            final TextView text = Ui.text(this, label, 12, Ui.SECONDARY);
            text.setTypeface(Typeface.create(Typeface.MONOSPACE, Typeface.BOLD));
            button.addView(text);
        }
        button.setClickable(true);
        button.setOnClickListener(onClick);
        final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, dp(36));
        params.leftMargin = dp(6);
        button.setLayoutParams(params);
        return button;
    }

    /** "Active configuration": one chip per setting in use; a tap opens its section. */
    private View configStripView() {
        final LinearLayout wrap = new LinearLayout(this);
        wrap.setOrientation(LinearLayout.VERTICAL);
        wrap.setBackgroundColor(Ui.ELEVATED);
        wrap.setPadding(dp(16), dp(8), dp(16), dp(9));
        final LinearLayout labels = new LinearLayout(this);
        labels.addView(Ui.caption(this, Tr.t("In use")),
                new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        final TextView hint = Ui.text(this, Tr.t("TAP TO OPEN"), 10, Ui.MUTED);
        hint.setTypeface(Ui.MONO);
        labels.addView(hint);
        wrap.addView(labels, Ui.matchWidth(0));
        configStrip = new LinearLayout(this);
        final HorizontalScrollView strip = new HorizontalScrollView(this);
        strip.setHorizontalScrollBarEnabled(false);
        strip.addView(configStrip);
        wrap.addView(strip, Ui.matchWidth(dp(6)));
        return wrap;
    }

    /** Play (or Continue), and Stop while a game waits in the background. */
    private View actionBar() {
        final LinearLayout bar = new LinearLayout(this);
        bar.setOrientation(LinearLayout.VERTICAL);
        bar.setBackgroundColor(Ui.SURFACE);
        bar.setPadding(dp(16), dp(9), dp(16), dp(14));
        sessionLine = Ui.text(this, "", 11, Ui.MUTED);
        bar.addView(sessionLine, Ui.matchWidth(0));

        final LinearLayout buttons = new LinearLayout(this);
        play = new LinearLayout(this);
        play.setGravity(Gravity.CENTER);
        play.setBackground(Ui.touchBox(this, Ui.ACCENT, Ui.ACCENT_PRESSED, 12));
        play.addView(Ui.icon(this, R.drawable.ic_play, Color.WHITE, 17));
        playLabel = Ui.text(this, Tr.t("Play"), 15, Color.WHITE);
        playLabel.setTypeface(Typeface.DEFAULT_BOLD);
        final LinearLayout.LayoutParams labelParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        labelParams.leftMargin = dp(8);
        play.addView(playLabel, labelParams);
        play.setClickable(true);
        play.setOnClickListener(v -> onPlay());
        buttons.addView(play, new LinearLayout.LayoutParams(0, dp(48), 1f));

        stop = new LinearLayout(this);
        stop.setGravity(Gravity.CENTER);
        stop.setPadding(dp(14), 0, dp(14), 0);
        stop.setBackground(Ui.touchBox(this, Ui.ACTIVE, Ui.BORDER_STRONG, 12));
        stop.addView(Ui.icon(this, R.drawable.ic_stop, Ui.TEXT, 15));
        final TextView stopLabel = Ui.text(this, Tr.t("Stop"), 13, Ui.TEXT);
        stopLabel.setTypeface(Ui.MEDIUM);
        final LinearLayout.LayoutParams stopLabelParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        stopLabelParams.leftMargin = dp(6);
        stop.addView(stopLabel, stopLabelParams);
        stop.setClickable(true);
        stop.setOnClickListener(v -> new Sheet(this, Tr.t("Stop the game?"),
                Tr.t("The game waiting in the background is closed without saving. Play starts it fresh."))
                .action(Tr.t("Stop"), null, true, true, this::endGame)
                .action(Tr.t("Cancel"), null, false, false, null)
                .show());
        final LinearLayout.LayoutParams stopParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, dp(48));
        stopParams.leftMargin = dp(8);
        buttons.addView(stop, stopParams);
        bar.addView(buttons, Ui.matchWidth(dp(8)));
        return bar;
    }

    private void onPlay() {
        if (settings.betaEnded() != null) {
            showBetaEnded();
        } else if (AppFiles.gameReady(this)) {
            if (!requirePlayAccess()) return;
            // A game left for the Start Menu is still running: Continue brings it back.
            startActivity(new Intent(this, GameActivity.class)
                    .addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT));
        } else {
            pickDisc();
        }
    }

    /**
     * The section tabs with the open one lit: four to a row with the icon above the name, or, for nine,
     * three to a row with the icon beside it (a 3x3 grid about as tall as two rows of four).
     */
    private void showTabs() {
        tabs.removeAllViews();
        final boolean wide = sections.length == 9;
        final int perRow = wide ? 3 : 4;
        LinearLayout row = null;
        for (int i = 0; i < sections.length; ++i) {
            if (i % perRow == 0) {
                row = new LinearLayout(this);
                tabs.addView(row, Ui.matchWidth(i == 0 ? 0 : dp(6)));
            }
            final int id = sections[i];
            final boolean open = id == menu.section();
            final int color = open ? Ui.TEXT : SettingsMenu.risky(id) ? Sheet.WARNING_TEXT : Ui.SECONDARY;
            final LinearLayout tab = new LinearLayout(this);
            tab.setOrientation(wide ? LinearLayout.HORIZONTAL : LinearLayout.VERTICAL);
            tab.setGravity(Gravity.CENTER);
            tab.setPadding(dp(wide ? 6 : 4), dp(9), dp(wide ? 6 : 4), dp(wide ? 9 : 8));
            tab.setBackground(Ui.touchBox(this, open ? Ui.ACTIVE : Ui.SURFACE, open ? Ui.SECONDARY : Ui.BORDER, 8));
            tab.addView(Ui.icon(this, SettingsMenu.icon(id), color, 17));
            final TextView name = Ui.text(this, SettingsMenu.name(id), wide ? 12 : 11, color);
            name.setTypeface(open ? Typeface.DEFAULT_BOLD : Ui.MEDIUM);
            name.setSingleLine(true);
            name.setEllipsize(android.text.TextUtils.TruncateAt.END);
            name.setGravity(Gravity.CENTER);
            if (wide) {
                final LinearLayout.LayoutParams beside = new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
                beside.leftMargin = dp(6);
                tab.addView(name, beside);
            } else {
                tab.addView(name, Ui.matchWidth(dp(4)));
            }
            tab.setClickable(true);
            tab.setOnClickListener(v -> selectSection(id, false));
            final LinearLayout.LayoutParams params =
                    new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            params.leftMargin = i % perRow == 0 ? 0 : dp(6);
            row.addView(tab, params);
        }
        // An unfinished last row keeps the tabs the same width.
        for (int i = sections.length % perRow; i != 0 && i < perRow; ++i) {
            final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(0, 1, 1f);
            params.leftMargin = dp(6);
            row.addView(new View(this), params);
        }
    }

    /** Opens a section under the tabs, from its top (`reveal`: from a chip at the top; the tabs never scroll away). */
    private void selectSection(int section, boolean reveal) {
        menu.show(section);
        showTabs();
        scroll.post(() -> scroll.scrollTo(0, 0));  // a new section starts at its top
    }

    /** The launcher's own System rows: the game file, updates, network play, the project. */
    private void systemRows(SettingsMenu rows) {
        final boolean ready = AppFiles.gameReady(this);
        rows.actionRow(Tr.t("Game File"), ready ? Tr.t("Loaded (RBSJ08)") : Tr.t("Your ISO or RVZ (RBSJ08)"),
                ready ? Tr.t("Change") : Tr.t("Choose"), this::pickDisc);
        rows.actionRow(updating ? Tr.t("Checking…") : Tr.t("Check for Updates"),
                Tr.t("Installed: ") + Updater.displayVersion(this), updating ? null : Tr.t("Check"),
                () -> checkForUpdates(false));
        rows.toggleRow(Tr.t("Auto Update"), BuildConfig.ONLINE_UPDATES ? Tr.t("Update on launch")
                        : Tr.t("Updates are sent by hand for now."),
                settings.autoUpdate(), BuildConfig.ONLINE_UPDATES, on -> settings.setAutoUpdate(on));
        rows.reportRows(gameRunning());
        final LinearLayout lan = rows.card(Tr.t("Tailscale & LAN Devices"), Tr.t("Coming soon"), null, false, false);
        lan.setClickable(false);
        rows.actionRow(Tr.t("Project page on GitHub"), PROJECT_URL.replace("https://", ""), Tr.t("Open"),
                () -> openWeb(PROJECT_URL));
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        restartGameIfAsked(intent);
    }

    /** The pid of the game's process (":game") while it runs, else 0. */
    private int gameProcess() {
        final ActivityManager am = getSystemService(ActivityManager.class);
        final java.util.List<ActivityManager.RunningAppProcessInfo> all =
                am == null ? null : am.getRunningAppProcesses();
        if (all != null) {
            final String name = getPackageName() + ":game";
            for (ActivityManager.RunningAppProcessInfo info : all) {
                // One just ended still lists for a moment: it is no longer a game to Continue.
                if (!name.equals(info.processName) || info.pid == endedGame) continue;
                if (gameTaskAlive(am)) return info.pid;
                // Swiped from the recent apps: the task is gone, but some systems (MIUI) leave the
                // process behind with no game in it. Its runtime has already started once, so it must
                // not be reused; end it and offer a fresh Play.
                android.os.Process.killProcess(info.pid);
                endedGame = info.pid;
                return 0;
            }
        }
        return 0;
    }

    /** A task of this app still holds the game (paused behind the launcher, or in a task of its own). */
    private boolean gameTaskAlive(ActivityManager am) {
        final String game = GameActivity.class.getName();
        for (ActivityManager.AppTask task : am.getAppTasks()) {
            try {
                final ActivityManager.RecentTaskInfo info = task.getTaskInfo();
                if (info == null) continue;
                if (info.numActivities > 1
                        || (info.baseActivity != null && game.equals(info.baseActivity.getClassName()))
                        || (info.topActivity != null && game.equals(info.topActivity.getClassName()))) {
                    return true;
                }
            } catch (IllegalArgumentException gone) {
                // The task ended while we looked.
            }
        }
        return false;
    }

    /** Ends a game still running in the background, so the next start is a fresh one. */
    void endGame() {
        final int pid = gameProcess();
        if (pid != 0) {
            GpuDrivers.endedOnPurpose(this);
            android.os.Process.killProcess(pid);
            endedGame = pid;
            refresh();
        }
    }

    /** Ends a running game (if any) and restarts the app into the launcher, so settings read once at start apply. */
    boolean restartGame() {
        endGame();
        status.setText(Tr.t("Restarting..."));
        final Intent launch = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (launch == null) return false;
        launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        main.postDelayed(() -> {
            startActivity(launch);
            android.os.Process.killProcess(android.os.Process.myPid());
        }, 500);
        return true;
    }

    /** Network play: the game asked to be started again into a LAN session. */
    private void restartGameIfAsked(Intent intent) {
        if (intent == null || !intent.getBooleanExtra(EXTRA_RESTART_GAME, false)) {
            return;
        }
        intent.removeExtra(EXTRA_RESTART_GAME);
        status.setText(Tr.t("Starting the LAN game..."));
        main.postDelayed(() -> startActivity(new Intent(this, GameActivity.class)), 700);
    }

    @Override
    protected void onSaveInstanceState(Bundle outState) {
        super.onSaveInstanceState(outState);
        if (waitingUpdate != null) {
            outState.putBundle(STATE_WAITING_UPDATE, waitingUpdate.toBundle());
        }
        outState.putBoolean(STATE_WAITING_FILE_ACCESS, waitingFileAccess);
        outState.putBoolean(STATE_WAITING_PLAY_ACCESS, waitingPlayAccess);
        if (menu != null) outState.putInt(STATE_SECTION, menu.section());
    }

    @Override
    protected void onResume() {
        super.onResume();
        refresh();
        showBetaEnded();
        DevTools.checkExpired(this);
        // The game died (maybe before its own log opened): say why and where the report is.
        final String crash = CrashReport.checkLastGameExit(this);
        if (crash != null) {
            Toast.makeText(this, "The game closed unexpectedly: " + crash, Toast.LENGTH_LONG).show();
        }
        // A custom GPU driver that failed or crashed the last start is switched back to the system one.
        if (gameProcess() == 0) {
            final String driver = GpuDrivers.checkLastStart(this, new AppSettings(this));
            if (driver != null) Toast.makeText(this, driver, Toast.LENGTH_LONG).show();
        }
        // Back from "Allow from this source": carry on with the update that was waiting for it.
        if (waitingUpdate != null && Updater.canInstall(this)) {
            final Updater.Release release = waitingUpdate;
            waitingUpdate = null;
            startUpdate(release);
        }
        // Back from "All files access".
        if (waitingFileAccess) {
            waitingFileAccess = false;
            if (Environment.isExternalStorageManager()) {
                browseDisc();
            }
        }
        // Right after install (and until it is given): "All files access" is required, the logs,
        // saves and textures live in /sdcard/Basara2Heroes[-Beta].
        if (!Environment.isExternalStorageManager() && !waitingFileAccess && !waitingPlayAccess && accessSheet == null) {
            requireAccess(false);
        }
        if (waitingPlayAccess) {
            waitingPlayAccess = false;
            if (Environment.isExternalStorageManager() && AppFiles.gameReady(this)) {
                startActivity(new Intent(this, GameActivity.class)
                        .addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT));
            }
        }
    }

    /**
     * The log always goes to /sdcard/Basara2Heroes[-Beta] (AppFiles.newRunLogDir), which needs "All files
     * access": without it the game does not start, and this asks for it first.
     */
    private boolean requirePlayAccess() {
        if (Environment.isExternalStorageManager()) return true;
        requireAccess(true);
        return false;
    }

    /**
     * Asks for "All files access" and cannot be dismissed. forPlay: Play asked, and the game starts
     * once it is given; otherwise the launcher asks at start (after install).
     */
    private void requireAccess(boolean forPlay) {
        if (accessSheet != null) accessSheet.dismiss();
        final Sheet sheet = new Sheet(this, Tr.t("Allow file access first"), Tr.t("The game writes its log to the ")
                + AppFiles.HOME
                + Tr.t(" folder on your phone's storage, so problems can be reported."))
                .step("1", Tr.t("Open Settings"), Tr.t("Turn on \"Allow access to manage all files\" for this app."))
                .step("2", Tr.t("Come back"), forPlay ? Tr.t("The game starts by itself.") : Tr.t("Then return to this app."))
                .cancelable(false);
        accessSheet = sheet;
        sheet.action(Tr.t("Open Settings"), null, true, false, () -> {
                    accessSheet = null;
                    waitingPlayAccess = forPlay;
                    try {
                        startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                Uri.parse("package:" + getPackageName())));
                    } catch (ActivityNotFoundException e) {
                        waitingPlayAccess = false;
                        try {
                            startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                        } catch (ActivityNotFoundException ignored) {
                            Toast.makeText(this, Tr.t("Open Settings > Apps > special access > All files access"),
                                    Toast.LENGTH_LONG).show();
                        }
                    }
                });
        sheet.show();
    }

    /** A short message in the Sheet look, with one button. */
    private void message(String title, String body) {
        new Sheet(this, title, body).action(Tr.t("OK"), null, true, false, null).show();
    }

    /** A determinate progress bar in the accent colour. */
    private ProgressBar progressBar() {
        final ProgressBar bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setProgressTintList(ColorStateList.valueOf(ACCENT));
        bar.setProgressBackgroundTintList(ColorStateList.valueOf(RAISED));
        bar.setIndeterminateTintList(ColorStateList.valueOf(ACCENT));
        bar.setMax(1000);
        return bar;
    }

    /** A progress bar and a line under it, for a Sheet. */
    private LinearLayout progressBox(ProgressBar bar, TextView label) {
        final LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.addView(bar, matchWidth(dp(4)));
        label.setGravity(Gravity.START);
        label.setTextColor(MUTED);
        box.addView(label, matchWidth(dp(6)));
        return box;
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        }
    }

    /** The page is drawn edge to edge under visible system bars (a dialog may have hidden them). */
    private void hideSystemBars() {
        getWindow().setDecorFitsSystemWindows(false);
        final WindowInsetsController controller = getWindow().getInsetsController();
        if (controller != null) controller.show(WindowInsets.Type.systemBars());
    }

    /** While an update check runs, System says so. */
    private void showChecking() {
        if (menu != null && menu.section() == SettingsMenu.SYSTEM) menu.show(SettingsMenu.SYSTEM);
    }

    // --- updates -------------------------------------------------------------------------------

    /** The latest GitHub release: installs it when newer. `automatic`: Auto Update, at app start. */
    private void checkForUpdates(boolean automatic) {
        if (BuildConfig.BETA) {
            checkBeta(automatic);  // the lock is checked even while updates are sent by hand
            return;
        }
        if (!BuildConfig.ONLINE_UPDATES) {
            if (!automatic) {
                message(Tr.t("Updates are sent by hand for now"),
                        Tr.t("New versions come straight from the developer. Install the new APK over this one; "
                                + "your settings and saves stay."));
            }
            return;
        }
        if (BuildConfig.BETA) {
            checkBeta(automatic);  // the Beta isn't updated: it ends, and testers move to the release
            return;
        }
        if (updating) {
            return;
        }
        updating = true;
        showChecking();
        new Thread(() -> {
            Updater.Release found = null;
            String failure = null;
            try {
                found = Updater.latest();
            } catch (Exception e) {
                failure = e.getMessage();
            }
            final Updater.Release release = found;
            final String error = failure;
            main.post(() -> {
                updating = false;
                showChecking();
                final String installed = Updater.installedVersion(this);
                if (release != null && release.apkUrl != null && Updater.isNewer(release.version, installed)) {
                    startUpdate(release);
                } else if (!automatic) {
                    if (error != null) {
                        message(Tr.t("Couldn't check for updates"), error);
                    } else if (release == null || release.apkUrl == null) {
                        message(Tr.t("No Android download"), Tr.t("The latest release has no Android download."));
                    } else {
                        new Sheet(this, Tr.t("You're up to date"), Tr.t("Basara ") + installed + Tr.t(" is the latest version."))
                                .action(Tr.t("OK"), null, true, false, null)
                                .action(Tr.t("What's New"), null, false, false, () -> ReleaseNotes.showAll(this))
                                .show();
                    }
                }
            });
        }, "update-check").start();
    }

    private void startUpdate(Updater.Release release) {
        if (!Updater.canInstall(this)) {
            new Sheet(this, Tr.t("Basara ") + release.version + Tr.t(" is available"),
                    Tr.t("To install updates, let Basara install apps."))
                    .step("1", Tr.t("Open Settings"), Tr.t("Turn on Allow from this source."))
                    .step("2", Tr.t("Come back"), Tr.t("The update carries on by itself."))
                    .action(Tr.t("Open Settings"), null, true, false, () -> {
                        waitingUpdate = release;
                        startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                                Uri.parse("package:" + getPackageName())));
                    })
                    .action(Tr.t("Not Now"), null, false, false, null)
                    .show();
            return;
        }
        final ProgressBar bar = progressBar();
        bar.setIndeterminate(true);
        final TextView label = text(Tr.t("Downloading…"), 14);
        final Sheet sheet = new Sheet(this, Tr.t("Updating to Basara ") + release.version,
                Tr.t("Your saves and settings stay."))
                .view(progressBox(bar, label))
                .cancelable(false);
        sheet.show();
        new Thread(() -> {
            String failure = null;
            try {
                Updater.downloadAndInstall(getApplicationContext(), release, (done, total) -> main.post(() -> {
                    bar.setIndeterminate(total <= 0);
                    if (total > 0) {
                        bar.setProgress((int) (done * 1000 / total));
                        label.setText(String.format(Locale.US, Tr.t("Downloading… %d of %d MB"),
                                done >> 20, total >> 20));
                    }
                }));
            } catch (Exception e) {
                failure = e.getMessage();
            }
            final String error = failure;
            main.post(() -> {
                sheet.dismiss();
                if (error != null) {
                    message(Tr.t("The update failed"), error);
                } else {
                    status.setText(Tr.t("Installing Basara ") + release.version + "…");
                }
            });
        }, "update-download").start();
    }

        // --- Beta ----------------------------------------------------------------------------------

    /** Beta: may it still run (BetaGate)? `automatic`: at app start, quiet unless it has ended. */
    private void checkBeta(boolean automatic) {
        if (updating) {
            return;
        }
        updating = true;
        showChecking();
        new Thread(() -> {
            String ended = null;
            String failure = null;
            Updater.Release found = null;
            try {
                ended = BetaGate.check();
                if (ended == null && BuildConfig.ONLINE_UPDATES) found = Updater.latestBeta();
            } catch (Exception e) {
                failure = e.getMessage();
            }
            final String reason = ended;
            final String error = failure;
            final Updater.Release beta = found;
            main.post(() -> {
                updating = false;
                showChecking();
                if (reason != null) {
                    settings.setBetaEnded(reason);
                    showBetaEnded();
                } else if (beta != null && beta.beta > Updater.installedBeta(this)) {
                    // Every Beta updates itself, at start and from Check for Updates.
                    startUpdate(beta);
                } else if (!automatic) {
                    if (error != null) {
                        message(Tr.t("Couldn't check for updates"), error);
                    } else if (!BuildConfig.ONLINE_UPDATES) {
                        message(Tr.t("Updates are sent by hand for now"),
                                Tr.t("New versions come straight from the developer. Install the new APK over this one; "
                                        + "your settings and saves stay."));
                    } else {
                        new Sheet(this, Tr.t("You're up to date"), "Basara " + Updater.displayVersion(this)
                                + Tr.t(" is the latest Beta. Thanks for testing!"))
                                .action(Tr.t("OK"), null, true, false, null)
                                .action(Tr.t("What's New"), null, false, false, () -> ReleaseNotes.showAll(this))
                                .show();
                    }
                }
            });
        }, "beta-check").start();
    }

    private boolean betaEndedShowing;

    /** Beta, once it has ended: the only way on is the release. Shown again each time the app returns. */
    private void showBetaEnded() {
        final String reason = settings.betaEnded();
        if (reason == null || betaEndedShowing) {
            return;
        }
        betaEndedShowing = true;
        new Sheet(this, Tr.t("The Beta has ended"), reason)
                .paragraph(Tr.t("Thanks for testing! Download Basara ") + BetaGate.RELEASE_VERSION
                        + Tr.t(" or later from the project's Releases page. It installs as its own app: load your "
                        + "disc there again, then uninstall this Beta."))
                .cancelable(false)
                .action(Tr.t("Download"), null, true, false, () -> openWeb(PROJECT_URL + "/releases/latest"))
                .action(Tr.t("Uninstall Beta"), null, false, true, () -> startActivity(new Intent(
                        Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:" + getPackageName()))))
                .action(Tr.t("Close"), null, false, false, this::finish)
                .onDismiss(() -> betaEndedShowing = false)
                .show();
    }

    /** A game waits in the background (left for the Start Menu). */
    boolean gameRunning() { return gameProcess() != 0; }

    private void refresh() {
        final boolean present = AppFiles.hasGameData(this);
        final String problem = present ? AppFiles.gameProblem(AppFiles.gameDir(this)) : null;
        final boolean ready = present && problem == null;
        final boolean running = ready && gameRunning();
        if (!present) {
            showStatus(Tr.t("No game loaded yet"), AMBER);
        } else if (problem != null) {
            showStatus(Tr.t("The game can't be played. ") + problem, ACCENT);
        } else {
            showStatus(running ? "RBSJ08 • " + Tr.t("Paused in background") : "RBSJ08 • " + Tr.t("Ready to play"),
                    running ? AMBER : GREEN);
        }
        playLabel.setText(ready ? (running ? Tr.t("Continue") : Tr.t("Play")) : Tr.t("Choose Game File"));
        stop.setVisibility(running ? View.VISIBLE : View.GONE);
        sessionLine.setText(running ? Tr.t("The game is paused in the background: Continue goes back to it.")
                : ready ? Tr.t("Settings are saved as you change them.")
                : Tr.t("Choose your own ISO or RVZ of the game first."));
        showSummary();
        if (menu != null) {
            menu.show(menu.section());
            showTabs();
        }
    }

    /** The "In use" chips, drawn again after any change. */
    private void showSummary() {
        settings = new AppSettings(this);  // the settings panel and the game write the same file
        showConfigStrip();
        showLowEndCard();
    }

    /**
     * On a low-end phone: a warning above "All settings" with the Low-End preset's switch. While it is on,
     * the settings it set are held (SettingsMenu dims them); Render Resolution stays free.
     */
    private void showLowEndCard() {
        if (lowEndBox == null) return;
        lowEndBox.removeAllViews();
        if (!lowEndDevice) return;
        final boolean on = settings.lowEndApplied();
        final LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(12), dp(10), dp(12), dp(10));
        card.setBackground(Ui.touchBox(this, Sheet.WARNING_FILL, Ui.DANGER_BORDER, 8));
        final TextView title = Ui.text(this, "⚠ " + Tr.t("Low-End Device"), 13, Sheet.WARNING_TEXT);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        card.addView(title, Ui.matchWidth(0));
        card.addView(Ui.text(this, Tr.t("This phone's GPU or RAM is below what the game needs for a steady 60 fps. "
                + "The Low-End Preset trades sharpness for speed."), 11.5f, Ui.SECONDARY), Ui.matchWidth(dp(3)));

        final LinearLayout row = new LinearLayout(this);
        row.setGravity(Gravity.CENTER_VERTICAL);
        final LinearLayout words = new LinearLayout(this);
        words.setOrientation(LinearLayout.VERTICAL);
        final TextView name = Ui.text(this, Tr.t("Low-End Preset"), 13, Ui.TEXT);
        name.setTypeface(Typeface.DEFAULT_BOLD);
        words.addView(name);
        words.addView(Ui.text(this, on
                ? Tr.t("On: 480p, Texture Detail Original, no upscaler, Frameskip Auto, Dynamic Resolution, HD "
                        + "Textures off. These stay as set until it is off; Render Resolution can still be changed.")
                : Tr.t("Off: every setting is yours to change."), 11, Ui.MUTED), Ui.matchWidth(dp(2)));
        row.addView(words, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        final View toggle = Ui.toggle(this, on);
        final android.view.ViewGroup.LayoutParams own = toggle.getLayoutParams();
        final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                own != null ? own.width : LinearLayout.LayoutParams.WRAP_CONTENT,
                own != null ? own.height : LinearLayout.LayoutParams.WRAP_CONTENT);
        params.leftMargin = dp(12);
        row.addView(toggle, params);
        row.setClickable(true);
        row.setOnClickListener(v -> setLowEnd(!on));
        card.addView(row, Ui.matchWidth(dp(10)));
        lowEndBox.addView(card, Ui.matchWidth(0));
        final View gap = new View(this);
        lowEndBox.addView(gap, new LinearLayout.LayoutParams(1, dp(12)));
    }

    private void setLowEnd(boolean on) {
        final AppSettings now = new AppSettings(this);
        if (on) now.applyLowEnd(DeviceProfile.lowEndValues(now.gpuName()));
        else now.undoLowEnd();
        try {
            AppFiles.writeConfig(this, now);
        } catch (Exception e) {
            android.util.Log.e("Basara", "could not save the settings", e);
        }
        if (gameRunning()) {
            Toast.makeText(this, Tr.t("Applies when the game starts again."), Toast.LENGTH_SHORT).show();
        }
        showSummary();
        if (menu != null) menu.show(menu.section());
    }

    private void showConfigStrip() {
        configStrip.removeAllViews();
        final String[] aspects = {"4:3", "16:9", Tr.t("Fill")};
        final String[] detail = {Tr.t("Textures Original"), Tr.t("Textures Sharp"), Tr.t("Textures Sharpest")};
        stripChip(AppSettings.resolutionLabel(settings.resolutionScale()), SettingsMenu.DISPLAY, false);
        stripChip(aspects[Math.max(0, Math.min(2, settings.aspectMode()))], SettingsMenu.DISPLAY, false);
        stripChip(Tr.t(SettingsMenu.UPSCALERS[settings.fsr()]).equals(Tr.t("Off")) ? Tr.t("Upscaler Off")
                : Tr.t(SettingsMenu.UPSCALERS[settings.fsr()]), SettingsMenu.GRAPHICS, false);
        stripChip(detail[settings.textureFilter()], SettingsMenu.GRAPHICS, false);
        if (!settings.gpuDriver().isEmpty()) stripChip(Tr.t("Custom GPU driver"), SettingsMenu.GRAPHICS, false);
        stripChip(settings.fpsLock30() ? Tr.t("30 FPS Lock")
                : Tr.t("Frameskip ") + (settings.frameskip() ? Tr.t("Auto") : Tr.t("Off")), SettingsMenu.PERFORMANCE, false);
        if (settings.showFps()) stripChip(Tr.t("Overlay"), SettingsMenu.DISPLAY, false);
        stripChip(Tr.t("Touch ") + (settings.touchControls() ? Tr.t("On") : Tr.t("Off")), SettingsMenu.CONTROLS, false);
        if (settings.anyCheat()) stripChip(Tr.t("Cheats on"), SettingsMenu.CHEATS, true);
        // Each chip opens the section its setting is in.
        if (settings.renderer() == 1) stripChip(Tr.t("Vulkan"), SettingsMenu.GRAPHICS, false);
        if (settings.hdTextures() != 0) stripChip(Tr.t("HD Textures"), SettingsMenu.TEXTURES, false);
        if (settings.customTextures()) stripChip(settings.dumpTextures() ? Tr.t("Custom + Dump Textures")
                : Tr.t("Custom Textures"), SettingsMenu.TEXTURES, false);
        if (settings.gxThread()) stripChip(Tr.t("GX Thread"), SettingsMenu.PERFORMANCE, false);
        if (settings.dynamicResolution()) stripChip(Tr.t("Dynamic Resolution"), SettingsMenu.PERFORMANCE, false);
        if (settings.thermalControl()) stripChip(Tr.t("Thermal Control"), SettingsMenu.PERFORMANCE, false);
        if (settings.lowEndApplied()) stripChip(Tr.t("Low-End Preset"), SettingsMenu.PERFORMANCE, false);
        if (settings.displayLines() > 0) stripChip(settings.displayLines() + "p", SettingsMenu.DISPLAY, false);
        if (settings.textureKeyReport()) stripChip(Tr.t("Texture Keys"), SettingsMenu.TEXTURES, false);
        final int testing = DevTools.activeEnvCount();
        if (testing > 0) {
            stripChip(String.format(java.util.Locale.ROOT, Tr.t("Testing options (%d)"), testing), SettingsMenu.SYSTEM, true);
        }
    }

    private void stripChip(String label, int section, boolean warning) {
        final LinearLayout chip = new LinearLayout(this);
        chip.setGravity(Gravity.CENTER_VERTICAL);
        chip.setPadding(dp(8), dp(4), dp(9), dp(4));
        chip.setBackground(Ui.touchBox(this, Ui.SURFACE, warning ? Sheet.WARNING_TEXT : Ui.BORDER, 6));
        final View dot = new View(this);
        final GradientDrawable round = new GradientDrawable();
        round.setShape(GradientDrawable.OVAL);
        round.setColor(warning ? Ui.WARNING : Ui.ACCENT);
        dot.setBackground(round);
        chip.addView(dot, new LinearLayout.LayoutParams(dp(5), dp(5)));
        final TextView text = Ui.text(this, (warning ? "⚠ " : "") + label, 11.5f, warning ? Sheet.WARNING_TEXT : Ui.TEXT);
        text.setSingleLine(true);
        final LinearLayout.LayoutParams textParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        textParams.leftMargin = dp(5);
        chip.addView(text, textParams);
        chip.setClickable(true);
        chip.setOnClickListener(v -> selectSection(section, true));
        final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.rightMargin = dp(6);
        configStrip.addView(chip, params);
    }

    /** Getting Started. */
    private void showHelp() {
        new Sheet(this, Tr.t("Getting Started"), Tr.t("Three steps from your disc to the battlefield."))
                .step("1", Tr.t("Copy your game to the phone"),
                        Tr.t("An ISO or RVZ of your own Sengoku Basara 2 Heroes disc (Japan, RBSJ08), e.g. in Download."))
                .step("2", Tr.t("Tap Game File and choose it"),
                        Tr.t("It is unpacked once (about 4 GB free needed). Afterwards you can delete the ISO or RVZ."))
                .step("3", Tr.t("Tap Play"),
                        Tr.t("Swipe Back while playing for the menu: Start Menu, Edit Controls Layout or Quit Game."))
                .heading(Tr.t("Tips"))
                .step("?", Tr.t("Slow or choppy?"),
                        Tr.t("Settings > Display: a lower Render Resolution. Settings > Performance: Frameskip Auto."))
                .step("?", Tr.t("Turning the camera"),
                        Tr.t("Drag on the empty right side of the screen, or use the right stick."))
                .step("?", Tr.t("Saving"),
                        Tr.t("Save from the game's own menu (各種設定 > セーブ). Your saves stay when the app updates."))
                .note(Tr.t("Basara ") + versionText())
                .action(Tr.t("Got it"), null, true, false, null)
                .action(Tr.t("Project page on GitHub"), null, false, false, () -> openWeb(PROJECT_URL))
                .show();
    }

    private void section(LinearLayout column, String title, String body) {
        final TextView heading = text(title, 20);
        heading.setGravity(Gravity.START);
        heading.setTypeface(Typeface.DEFAULT_BOLD);
        column.addView(heading);
        final TextView text = text(body, 15);
        text.setGravity(Gravity.START);
        text.setTextColor(Color.rgb(200, 206, 212));
        text.setPadding(0, dp(6), 0, dp(18));
        column.addView(text);
    }

    private void openWeb(String url) {
        try {
            startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
        } catch (ActivityNotFoundException e) {
            message(Tr.t("Open this page in a browser"), url);
        }
    }

    private String versionText() {
        try {
            final PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
            return BuildConfig.BETA ? info.versionName + " · Beta " + info.getLongVersionCode() : info.versionName;
        } catch (PackageManager.NameNotFoundException e) {
            return "";
        }
    }

    // --- the game file -------------------------------------------------------------------------

    /**
     * The app's own file browser (FileBrowser), opening on Download. It needs "All files access";
     * without it the player can allow it, or use the system's picker instead.
     */
    private void pickDisc() {
        endGame();  // a game still running from the old file must not carry on with the new one
        if (Environment.isExternalStorageManager()) {
            browseDisc();
            return;
        }
        new Sheet(this, Tr.t("Allow file access"), Tr.t("To list the folders on this phone and find your ISO or RVZ."))
                .step("1", Tr.t("Open Settings"), Tr.t("Turn on \"Allow access to manage all files\" for this app."))
                .step("2", Tr.t("Come back"), Tr.t("The file browser opens by itself."))
                .action(Tr.t("Open Settings"), null, true, false, () -> {
                    waitingFileAccess = true;
                    try {
                        startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                Uri.parse("package:" + getPackageName())));
                    } catch (ActivityNotFoundException e) {
                        waitingFileAccess = false;
                        pickWithSystem();
                    }
                })
                .action(Tr.t("Use System Picker"), Tr.t("The phone's own file picker, without the permission"), false, false,
                        this::pickWithSystem)
                .action(Tr.t("Cancel"), null, false, false, null)
                .show();
    }

    private void browseDisc() {
        // Basara2Heroes/ISO when the game file is kept there, else Download (where a downloaded one lands).
        final java.io.File iso = AppFiles.homeSub("ISO");
        final java.io.File[] games = iso == null ? null : iso.listFiles(f -> f.isFile() && FileBrowser.isGameFile(f));
        FileBrowser.show(this, games != null && games.length > 0 ? iso : FileBrowser.downloads(),
                file -> loadGame(Uri.fromFile(file)));
    }

    /**
     * Any file: the system has no type for .rvz, so the file itself is what gets checked. Opens on
     * the phone's Download folder rather than the picker's Recent view, where dumps usually are.
     */
    private void pickWithSystem() {
        final Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(android.provider.DocumentsContract.EXTRA_INITIAL_URI, android.provider.DocumentsContract
                .buildDocumentUri("com.android.externalstorage.documents", "primary:Download"));
        intent.putExtra("android.content.extra.SHOW_ADVANCED", true);  // list the phone's own storage
        startActivityForResult(intent, REQUEST_DISC);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (SaveData.onResult(this, requestCode, resultCode, data, this::endGame)) return;
        if (requestCode == REQUEST_DISC && resultCode == RESULT_OK && data != null && data.getData() != null) {
            loadGame(data.getData());
        }
    }

    private void loadGame(Uri disc) {
        final ProgressBar bar = progressBar();
        bar.setIndeterminate(true);
        final TextView label = text(Tr.t("Looking at the game file…"), 14);
        final AtomicBoolean cancel = new AtomicBoolean();
        final Sheet dialog = new Sheet(this, Tr.t("Loading your game"),
                Tr.t("Unpacked once, about 4 GB. Keep the app open until it is done."))
                .view(progressBox(bar, label))
                .cancelable(false);
        dialog.actionStay(Tr.t("Cancel"), true, name -> {
            cancel.set(true);
            name.setText(Tr.t("Stopping…"));
            label.setText(Tr.t("Stopping…"));
        });
        dialog.show();
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        new Thread(() -> {
            String result;
            try {
                result = unpack(disc, (done, total) -> {
                    main.post(() -> {
                        if (cancel.get()) {
                            return;
                        }
                        bar.setIndeterminate(false);
                        bar.setProgress((int) (done * 1000 / Math.max(total, 1)));
                        label.setText(String.format(Locale.ROOT, Tr.t("%.1f of %.1f GB"),
                                done / (double) (1 << 30), total / (double) (1 << 30)));
                    });
                    return !cancel.get();
                });
            } catch (Exception e) {
                AppFiles.deleteRecursively(stagingDir());
                result = e.getMessage();
            }
            final String failure = result;
            main.post(() -> {
                dialog.dismiss();
                getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                refresh();
                if (failure != null && !failure.equals(DiscImport.CANCELLED)) {
                    message(Tr.t("Couldn't load the game"), failure + Tr.t("\n\nNothing was changed."));
                }
            });
        }, "game-import").start();
    }

    private File stagingDir() { return new File(AppFiles.dataDir(this), "game.importing"); }

    /** Into the staging folder, checked, then swapped in; saves stay. Null, or what went wrong. */
    private String unpack(Uri disc, DiscImport.Listener listener) throws IOException {
        try (ParcelFileDescriptor file = getContentResolver().openFileDescriptor(disc, "r")) {
            if (file == null) throw new IOException(Tr.t("Couldn't open that file."));
            final String problem = AppFiles.headerProblem(DiscImport.readHeader(file.getFd()));
            if (problem != null) return problem;
            final File staging = stagingDir();
            AppFiles.deleteRecursively(staging);
            if (!staging.mkdirs()) throw new IOException(Tr.t("Couldn't create ") + staging);
            final String failure = DiscImport.extract(file.getFd(), staging.getPath(),
                    AppFiles.dataDir(this).getUsableSpace(), listener);
            final String result = failure != null ? failure : install(staging);
            AppFiles.deleteRecursively(staging);
            return result;
        }
    }

    /** The checked game at `root` becomes the game. Null, or why it can't. */
    private String install(File root) throws IOException {
        final String problem = AppFiles.gameProblem(root);
        if (problem != null) return problem;
        final File game = AppFiles.gameDir(this);
        AppFiles.deleteRecursively(game);
        if (!root.renameTo(game)) throw new IOException(Tr.t("Couldn't move the game into place."));
        return null;
    }

    // --- views ---------------------------------------------------------------------------------

    private TextView text(String value, int sp) {
        final TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        view.setTextColor(Color.WHITE);
        view.setGravity(Gravity.CENTER_HORIZONTAL);
        return view;
    }

    /** A 1 px border line between the page's bands. */
    private View line() {
        final View view = new View(this);
        view.setBackgroundColor(Ui.BORDER);
        return view;
    }

    // The app's colors (Ui), under the names the other screens use.
    static final int ACCENT = Ui.ACCENT;
    static final int SURFACE = Ui.SURFACE;
    static final int BACKGROUND = Ui.BG;
    static final int RAISED = Ui.ACTIVE;
    static final int MUTED = Ui.SECONDARY;
    static final int FAINT = Ui.MUTED;
    static final int GREEN = Ui.SUCCESS;
    static final int AMBER = Ui.WARNING;
    static final Typeface MEDIUM = Ui.MEDIUM;

    private void showStatus(String text, int color) {
        status.setText(text);
        final GradientDrawable dot = new GradientDrawable();
        dot.setShape(GradientDrawable.OVAL);
        dot.setColor(color);
        statusDot.setBackground(dot);
    }

    private LinearLayout.LayoutParams matchWidth(int topMargin) {
        return Ui.matchWidth(topMargin);
    }

    private int dp(int value) {
        return Ui.dp(this, value);
    }
}
