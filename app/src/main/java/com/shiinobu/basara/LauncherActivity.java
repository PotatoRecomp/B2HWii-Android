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
 * Start screen, one landscape page: the game and Play on the left, everything else as tiles beside
 * them. The game comes from the player's own ISO or RVZ, which is unpacked once when chosen.
 */
public final class LauncherActivity extends Activity {
    private static final int REQUEST_DISC = 1;
    /** Set by GameActivity.restartForSession: open the game again once its old process is gone. */
    static final String EXTRA_RESTART_GAME = "restartGame";
    /** Set by the game's "Start Menu": no automatic update check on the way back. */
    static final String EXTRA_SHOW_START_MENU = "showStartMenu";

    static final String PROJECT_URL = "https://github.com/shiinobu/basara2heroes-android";

    private AppSettings settings;
    private Tile skip;
    private Tile updateCheck;
    private Tile autoUpdate;
    private Tile gameFile;
    private boolean updating;
    /** An update waiting for the player to allow installs from this app. */
    private Updater.Release waitingUpdate;
    private static final String STATE_WAITING_UPDATE = "waiting_update";
    /** Gone to allow "All files access" for the file browser: open it on the way back. */
    private boolean waitingFileAccess;
    private static final String STATE_WAITING_FILE_ACCESS = "waiting_file_access";
    /** Gone to allow "All files access" before playing (the log goes to /sdcard): play on the way back. */
    private boolean waitingPlayAccess;
    private static final String STATE_WAITING_PLAY_ACCESS = "waiting_play_access";

    private int endedGame;  // the pid endGame just ended
    private LinearLayout activeSettings;
    private TextView status;
    private Button play;
    private final Handler main = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        settings = new AppSettings(this);
        Tr.init(this);
        DeviceProfile.applyOnce(this, settings);
        // Updates are checked on a fresh start, not when the game sent the player here or is
        // restarting itself into a LAN session.
        final Intent launch = getIntent();
        final boolean asked = launch != null && (launch.getBooleanExtra(EXTRA_SHOW_START_MENU, false)
                || launch.getBooleanExtra(EXTRA_RESTART_GAME, false));
        final boolean opened = savedInstanceState == null && !asked;

        gameFile = tile(Tr.t("Game File"), "", v -> pickDisc());
        final Tile settingsTile = tile(Tr.t("Settings"), Tr.t("Display, controls, experimental"),
                v -> SettingsMenu.show(this, this::refresh));
        // Online play isn't ready yet: shown, but off.
        final Tile devices = tile(Tr.t("Tailscale & LAN Devices"), Tr.t("Coming soon"), null);
        devices.view.setClickable(false);
        devices.view.setAlpha(0.4f);
        autoUpdate = tile(Tr.t("Auto Update"), Tr.t("Update on launch"), v -> {
            settings.setAutoUpdate(!settings.autoUpdate());
            updateAutoUpdateButton();
        });
        updateAutoUpdateButton();
        skip = tile(Tr.t("Skip Boot"), Tr.t("Skip the logos, straight to the save check"), v -> {
            settings.setSkipBoot(!settings.skipBoot());
            updateSkipButton();
        });
        updateSkipButton();
        updateCheck = tile(Tr.t("Check for Updates"), Tr.t("Installed: ") + Updater.displayVersion(this),
                v -> checkForUpdates(false));
        final Tile help = tile(Tr.t("Help"), Tr.t("Getting started"), v -> showHelp());
        final Tile notes = tile(Tr.t("What's New"), Tr.t("Features and fixes in each version"),
                v -> ReleaseNotes.showAll(this));

        if (!BuildConfig.ONLINE_UPDATES) {
            // Updates are handed out by hand for now: Auto Update is shown, but off.
            autoUpdate.view.setClickable(false);
            autoUpdate.view.setAlpha(0.4f);
        }

        final LinearLayout tiles = new LinearLayout(this);
        tiles.setOrientation(LinearLayout.VERTICAL);
        tiles.addView(tileRow(gameFile, help), tileRowParams(0));
        tiles.addView(tileRow(settingsTile, devices), tileRowParams(GAP));
        tiles.addView(tileRow(autoUpdate, skip), tileRowParams(GAP));
        tiles.addView(tileRow(updateCheck, notes), tileRowParams(GAP));
        // Fills the height where there's room, and scrolls on a short screen or with a large font.
        final ScrollView tilesScroll = new ScrollView(this);
        tilesScroll.setFillViewport(true);
        tilesScroll.setVerticalScrollBarEnabled(false);
        tilesScroll.addView(tiles);

        final LinearLayout page = new LinearLayout(this);
        page.setOrientation(LinearLayout.HORIZONTAL);
        page.setPadding(dp(GAP), dp(GAP), dp(GAP), dp(GAP));
        page.addView(hero(), new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 0.42f));
        final LinearLayout.LayoutParams tilesParams =
                new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 0.58f);
        tilesParams.leftMargin = dp(GAP);
        page.addView(tilesScroll, tilesParams);
        setContentView(page);
        hideSystemBars();

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

    /** The left side: which game this is, whether it's ready, and the one big button. */
    private View hero() {
        final LinearLayout hero = new LinearLayout(this);
        hero.setOrientation(LinearLayout.VERTICAL);
        hero.setPadding(dp(20), dp(20), dp(20), dp(16));
        final GradientDrawable background = new GradientDrawable(GradientDrawable.Orientation.TL_BR,
                new int[] {HERO_GLOW, SURFACE});
        background.setCornerRadius(dp(24));
        hero.setBackground(background);

        final LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        final ImageView icon = new ImageView(this);
        icon.setImageResource(R.mipmap.ic_launcher);
        header.addView(icon, new LinearLayout.LayoutParams(dp(56), dp(56)));
        final LinearLayout names = new LinearLayout(this);
        names.setOrientation(LinearLayout.VERTICAL);
        final TextView title = text(Tr.t("Basara 2 Heroes"), 24);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setGravity(Gravity.START);
        names.addView(title);
        final TextView subtitle = text(Tr.t("Wii, recompiled to run natively"), 13);
        subtitle.setGravity(Gravity.START);
        subtitle.setTextColor(MUTED);
        names.addView(subtitle);
        final LinearLayout.LayoutParams namesParams =
                new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        namesParams.leftMargin = dp(12);
        header.addView(names, namesParams);
        hero.addView(header, matchWidth(0));

        final TextView inUse = text(Tr.t("IN USE"), 11);
        inUse.setGravity(Gravity.START);
        inUse.setTextColor(FAINT);
        inUse.setTypeface(Typeface.DEFAULT_BOLD);
        inUse.setLetterSpacing(0.08f);
        hero.addView(inUse, matchWidth(dp(16)));
        activeSettings = new LinearLayout(this);
        activeSettings.setOrientation(LinearLayout.VERTICAL);
        hero.addView(activeSettings, matchWidth(dp(4)));

        final View space = new View(this);
        hero.addView(space, new LinearLayout.LayoutParams(0, 0, 1f));

        status = text("", 14);
        status.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
        status.setCompoundDrawablePadding(dp(10));
        hero.addView(status, matchWidth(0));

        play = button(Tr.t("Play"));
        play.setOnClickListener(v -> {
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
        });
        hero.addView(play, matchWidth(dp(12)));

        final TextView version = text(Tr.t("Basara ") + versionText(), 12);
        version.setTextColor(FAINT);
        hero.addView(version, matchWidth(dp(10)));
        return hero;
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
                if (name.equals(info.processName) && info.pid != endedGame) return info.pid;
            }
        }
        return 0;
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
    }

    @Override
    protected void onResume() {
        super.onResume();
        refresh();
        showBetaEnded();
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
        if (waitingPlayAccess) {
            waitingPlayAccess = false;
            if (Environment.isExternalStorageManager() && AppFiles.gameReady(this)) {
                startActivity(new Intent(this, GameActivity.class)
                        .addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT));
            }
        }
    }

    /**
     * The log always goes to /sdcard/Basara2Heroes[-Beta] (AppFiles.logDir), which needs "All files
     * access": without it the game does not start, and this asks for it first.
     */
    private boolean requirePlayAccess() {
        if (Environment.isExternalStorageManager()) return true;
        new Sheet(this, Tr.t("Allow file access first"), Tr.t("The game writes its log to the ")
                + AppFiles.HOME
                + Tr.t(" folder on your phone's storage, so problems can be reported."))
                .step("1", Tr.t("Open Settings"), Tr.t("Turn on \"Allow access to manage all files\" for this app."))
                .step("2", Tr.t("Come back"), Tr.t("The game starts by itself."))
                .action(Tr.t("Open Settings"), null, true, false, () -> {
                    waitingPlayAccess = true;
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
                })
                .action(Tr.t("Cancel"), null, false, false, null)
                .show();
        return false;
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

    private void hideSystemBars() {
        getWindow().setDecorFitsSystemWindows(false);
        final WindowInsetsController controller = getWindow().getInsetsController();
        if (controller != null) {
            controller.hide(WindowInsets.Type.systemBars());
            controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        }
    }

    // --- updates -------------------------------------------------------------------------------

    private void updateAutoUpdateButton() {
        showToggle(autoUpdate.pill, settings.autoUpdate());
    }

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
        updateCheck.view.setEnabled(false);
        updateCheck.name.setText(Tr.t("Checking…"));
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
                updateCheck.view.setEnabled(true);
                updateCheck.name.setText(Tr.t("Check for Updates"));
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
        updateCheck.view.setEnabled(false);
        updateCheck.name.setText(Tr.t("Checking…"));
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
                updateCheck.view.setEnabled(true);
                updateCheck.name.setText(Tr.t("Check for Updates"));
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

    private void updateSkipButton() {
        showToggle(skip.pill, settings.skipBoot());
    }

    private void refresh() {
        final boolean present = AppFiles.hasGameData(this);
        final String problem = present ? AppFiles.gameProblem(AppFiles.gameDir(this)) : null;
        final boolean ready = present && problem == null;
        if (!present) {
            showStatus(Tr.t("No game loaded yet"), AMBER);
        } else if (problem != null) {
            showStatus(Tr.t("The game can't be played. ") + problem, ACCENT);
        } else {
            showStatus(Tr.t("Ready to play"), GREEN);
        }
        play.setText(ready ? (gameProcess() != 0 ? Tr.t("Continue") : Tr.t("Play")) : Tr.t("Choose Game File"));
        gameFile.about.setText(ready ? Tr.t("Loaded (RBSJ08)")
                : Tr.t("Your ISO or RVZ (RBSJ08)"));
        showAction(gameFile.pill, ready ? Tr.t("Change") : Tr.t("Choose"));
        showActiveSettings();
    }

    /** The left panel's summary of the settings in use, with the experimental ones on their own line. */
    private void showActiveSettings() {
        if (activeSettings == null) return;
        settings = new AppSettings(this);  // the settings screen and the game write the same file
        activeSettings.removeAllViews();
        final String[] aspects = {"4:3", "16:9", Tr.t("Fill")};
        final String[] fsr = {Tr.t("FSR Off"), Tr.t("FSR Light"), Tr.t("FSR Full")};
        final String[] detail = {Tr.t("Original"), Tr.t("Sharp"), Tr.t("Sharpest")};
        settingLine(AppSettings.resolutionLabel(settings.resolutionScale()) + "  ·  "
                + aspects[Math.max(0, Math.min(2, settings.aspectMode()))] + "  ·  " + fsr[settings.fsr()], Color.WHITE);
        settingLine((settings.fpsLock30() ? Tr.t("30 FPS Lock") : Tr.t("Frameskip ") + (settings.frameskip() ? Tr.t("Auto") : Tr.t("Off")))
                + Tr.t("  ·  V-Sync ") + (settings.vsync() ? Tr.t("On") : Tr.t("Off")) + Tr.t("  ·  Textures ")
                + detail[settings.textureFilter()], MUTED);
        settingLine(Tr.t("Touch ") + (settings.touchControls() ? Tr.t("On") : Tr.t("Off")) + Tr.t("  ·  Overlay ")
                + (settings.showFps() ? Tr.t("On") : Tr.t("Off")),
                MUTED);
        if (settings.anyCheat()) {
            final java.util.List<String> cheats = new java.util.ArrayList<>();
            if (settings.cheatMoney() == AppSettings.MONEY_INFINITE) cheats.add(Tr.t("Infinite Money"));
            if (settings.cheatInfiniteHp()) cheats.add(Tr.t("Infinite HP"));
            if (settings.cheatInfiniteBasara()) cheats.add(Tr.t("Infinite Basara"));
            settingLine(Tr.t("⚠  Cheats: ") + String.join(", ", cheats), Sheet.WARNING_TEXT);
        }
        final java.util.List<String> experimental = new java.util.ArrayList<>();
        if (settings.thermalControl()) experimental.add(Tr.t("Thermal Control"));
        if (settings.hdTextures() != 0) experimental.add(Tr.t("HD Textures"));
        if (settings.gxThread()) experimental.add(Tr.t("GX Thread"));
        if (settings.dynamicResolution()) experimental.add(Tr.t("Dynamic Resolution"));
        if (settings.customTextures()) experimental.add(settings.dumpTextures() ? Tr.t("Custom + Dump Textures") : Tr.t("Custom Textures"));
        if (settings.textureKeyReport()) experimental.add(Tr.t("Texture Keys"));
        if (!experimental.isEmpty()) {
            settingLine(Tr.t("⚠  Experimental: ") + String.join(", ", experimental), Sheet.WARNING_TEXT);
        }
    }

    private void settingLine(String value, int color) {
        final TextView line = text(value, 12);
        line.setGravity(Gravity.START);
        line.setTextColor(color);
        line.setPadding(0, dp(2), 0, 0);
        activeSettings.addView(line, matchWidth(0));
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

    private TextView link(String label, View.OnClickListener onClick) {
        final TextView view = text(label, 16);
        view.setTextColor(Color.rgb(255, 138, 128));
        view.setTypeface(Typeface.DEFAULT_BOLD);
        view.setPadding(dp(12), dp(10), dp(12), dp(10));
        view.setOnClickListener(onClick);
        view.setBackgroundResource(android.R.drawable.list_selector_background);
        return view;
    }

    // The start menu's colors, on the theme's #101418 background (the settings screen shares them).
    static final int ACCENT = Color.rgb(229, 57, 53);
    static final int HERO_GLOW = Color.rgb(84, 24, 26);
    static final int SURFACE = Color.rgb(24, 31, 38);
    static final int BACKGROUND = Color.rgb(16, 20, 24);
    static final int RAISED = Color.rgb(38, 48, 58);
    static final int MUTED = Color.rgb(170, 178, 186);
    static final int FAINT = Color.rgb(120, 128, 136);
    static final int GREEN = Color.rgb(102, 187, 106);
    static final int AMBER = Color.rgb(255, 183, 77);
    static final Typeface MEDIUM = Typeface.create("sans-serif-medium", Typeface.NORMAL);
    /** Between the screen's edge, its two sides and the tiles, in dp. */
    private static final int GAP = 10;

    /** A rounded shape that ripples when touched; `disabled` is its color while disabled. */
    private RippleDrawable rounded(int color, int disabled, int radius) {
        final GradientDrawable shape = new GradientDrawable();
        shape.setCornerRadius(radius);
        shape.setColor(new ColorStateList(new int[][] {{-android.R.attr.state_enabled}, {}},
                new int[] {disabled, color}));
        final GradientDrawable mask = new GradientDrawable();
        mask.setCornerRadius(radius);
        mask.setColor(Color.WHITE);
        return new RippleDrawable(ColorStateList.valueOf(Color.argb(56, 255, 255, 255)), shape, mask);
    }

    /** The big red pill. */
    private Button button(String label) {
        final Button view = new Button(this);
        view.setText(label);
        view.setAllCaps(false);
        view.setStateListAnimator(null);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, 20);
        view.setTypeface(Typeface.DEFAULT_BOLD);
        view.setTextColor(Color.WHITE);
        final int height = dp(56);
        view.setMinHeight(height);
        view.setMinimumHeight(height);
        view.setPadding(dp(20), 0, dp(20), 0);
        view.setBackground(rounded(ACCENT, RAISED, height / 2));
        return view;
    }

    /** A tappable tile: a name, a line about it, and on the right a pill for its state or action. */
    private static final class Tile {
        final View view;
        final TextView name, about, pill;
        Tile(View view, TextView name, TextView about, TextView pill) {
            this.view = view;
            this.name = name;
            this.about = about;
            this.pill = pill;
        }
    }

    private Tile tile(String label, String help, View.OnClickListener onClick) {
        final LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(16), dp(6), dp(12), dp(6));
        row.setBackground(rounded(SURFACE, SURFACE, dp(18)));
        if (onClick != null) {
            row.setClickable(true);
            row.setOnClickListener(onClick);
        }
        final LinearLayout words = new LinearLayout(this);
        words.setOrientation(LinearLayout.VERTICAL);
        final TextView name = text(label, 15);
        name.setTypeface(MEDIUM);
        name.setGravity(Gravity.START);
        words.addView(name);
        final TextView about = text(help, 12);
        about.setGravity(Gravity.START);
        about.setTextColor(MUTED);
        words.addView(about);
        row.addView(words, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        final TextView pill = text("", 13);
        pill.setTypeface(Typeface.DEFAULT_BOLD);
        pill.setGravity(Gravity.CENTER);
        pill.setPadding(dp(14), dp(6), dp(14), dp(6));
        pill.setVisibility(View.GONE);
        final LinearLayout.LayoutParams pillParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        pillParams.leftMargin = dp(8);
        row.addView(pill, pillParams);
        return new Tile(row, name, about, pill);
    }

    /** Tiles side by side, sharing the width. */
    private LinearLayout tileRow(Tile... tiles) {
        final LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        for (int i = 0; i < tiles.length; ++i) {
            final LinearLayout.LayoutParams params =
                    new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f);
            params.leftMargin = i == 0 ? 0 : dp(GAP);
            row.addView(tiles[i].view, params);
        }
        return row;
    }

    /** A row takes its share of the side's height, and never less than a comfortable tap. */
    private LinearLayout.LayoutParams tileRowParams(int topMarginDp) {
        final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(56), 1f);
        params.topMargin = dp(topMarginDp);
        return params;
    }

    private void showToggle(TextView pill, boolean on) {
        pill.setVisibility(View.VISIBLE);
        pill.setText(on ? Tr.t("On") : Tr.t("Off"));
        pill.setTextColor(on ? Color.WHITE : MUTED);
        showPill(pill, on ? ACCENT : RAISED);
    }

    private void showAction(TextView pill, String label) {
        pill.setVisibility(View.VISIBLE);
        pill.setText(label);
        pill.setTextColor(Color.WHITE);
        showPill(pill, RAISED);
    }

    private void showPill(TextView pill, int color) {
        final GradientDrawable shape = new GradientDrawable();
        shape.setCornerRadius(dp(16));
        shape.setColor(color);
        pill.setBackground(shape);
    }

    private void showStatus(String text, int color) {
        status.setText(text);
        final GradientDrawable dot = new GradientDrawable();
        dot.setShape(GradientDrawable.OVAL);
        dot.setColor(color);
        dot.setSize(dp(10), dp(10));
        status.setCompoundDrawablesRelativeWithIntrinsicBounds(dot, null, null, null);
    }

    private LinearLayout.LayoutParams matchWidth(int topMargin) {
        final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.topMargin = topMargin;
        return params;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
