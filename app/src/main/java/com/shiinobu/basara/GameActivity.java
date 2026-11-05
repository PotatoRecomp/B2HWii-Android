package com.shiinobu.basara;

import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.os.Process;
import android.system.Os;
import android.util.Log;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

/**
 * The game: SDL's activity running libmain.so (the translated game, its runtime, SDL and Dawn,
 * linked into one library), with the on-screen remote and the phone's motion on top.
 */
public final class GameActivity extends SDLActivity
        implements TouchControlsView.Listener, MotionInput.Listener {
    private static final String TAG = "Basara";

    /** Player 1's on-screen remote and motion (runtime/src/android/android_bridge.cpp). */
    static native void nativeSetTouchState(int buttons, boolean shake, float tilt);
    static native void nativeSetTouchSticks(float leftX, float leftY, float rightX, float rightY);

    /** The Display menu: aspect, render scale (480 lines per 1x), FPS counter (DisplaySettings). */
    static native void nativeApplyDisplaySettings(int aspect, float renderScale, boolean showFps,
            boolean dynamicResolution, int overlayItems, boolean frameGraph, boolean thermalControl,
            int textureFilter);

    /** Settings > Cheats (runtime/src/cheats.cpp); a new onceSeq sets the money once at the next frame. */
    static native void nativeApplyCheats(int money, int amount, int onceSeq, boolean infiniteHp,
            boolean infiniteBasara);

    /** The in-game settings: upscaler (0 off, 1 FSR Light, 2 FSR Full, 3 SGSR), frameskip (0 off, 1 auto, 2 30 FPS lock). */
    static native void nativeApplyLiveSettings(int upscaler, int frameskip);

    /** "GPU name\n1|0":the GPU the game draws with and whether its compatibility fixes are on. */
    static native String nativeGraphicsInfo();

    /** What this phone is called in LAN room lists (netplay). */
    static native void nativeSetDeviceName(String name);

    /** Whether this run is a LAN session, and leaving one (the host keeps the session's save). */
    static native boolean nativeSessionActive();
    static native void nativePrepareToLeave();
    /** A tap on the game's menus, as fractions of the screen. */
    static native void nativeTap(float x, float y);
    static native void nativePointer(float x, float y, boolean held);
    /** Whether a menu that taps drive is up (the remote is hidden then). */
    static native boolean nativeTapScreen();
    /** {render scale, show FPS, aspect} after the game's own settings window changed them, else null. */
    static native float[] nativeTakeSettings();
    /**
     * The app left the screen (true) or came back (false). Leaving, the game writes a backup Quick Save
     * of the progress so far (deleted again when the app comes back) and pauses; nativeGamePaused says
     * when it has (runtime/src/host_suspend.h).
     */
    static native void nativeSetBackground(boolean background);
    static native boolean nativeGamePaused();
    /** The overlay's battery: many phones deny the power_supply files to apps, so it comes from here. */
    static native void nativeSetBattery(int percent, float tempC);
    /** The Back menu: pause the game while it is open (true), carry on (false). */
    static native void nativeSetMenuPause(boolean paused);

    /** How long leaving the app waits for the backup save and the pause, before SDL's own pause. */
    private static final long SUSPEND_WAIT_MS = 1500;

    private WifiManager.MulticastLock multicastLock;

    private AppSettings settings;
    private boolean bootWidescreen;  // the game reads 4:3 versus widescreen once, when it starts
    private TouchControlsView touch;
    private MotionInput motion;
    private boolean nativeReady;
    private int touchButtons;
    private boolean motionShake;
    private float motionTilt;

    /**
     * Display Resolution: the game draws into a surface of that many lines and the compositor stretches
     * it to the screen for free (ARMSX2's hardware scaler), so the swapchain and every full-screen pass
     * (presentation, snapshot, overlay) cost less GPU time. Nothing when it is at or above the screen.
     */
    private void applyDisplayResolution() {
        final int lines = settings.displayLines();
        if (lines <= 0 || mSurface == null) return;
        final android.util.DisplayMetrics m = new android.util.DisplayMetrics();
        getWindowManager().getDefaultDisplay().getRealMetrics(m);
        final int longSide = Math.max(m.widthPixels, m.heightPixels), shortSide = Math.min(m.widthPixels, m.heightPixels);
        if (shortSide <= 0 || lines >= shortSide * 0.95f) return;
        final int width = Math.round(longSide * (float) lines / shortSide) & ~1;
        mSurface.getHolder().setFixedSize(width, lines);  // the game is landscape (sensorLandscape)
        Log.i(TAG, "display resolution: " + width + "x" + lines + " on a " + longSide + "x" + shortSide + " screen");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        CrashReport.installJavaHandler(this);
        CrashReport.testCrashIfAsked();
        settings = new AppSettings(this);
        Tr.init(this);  // the in-game menu speaks the launcher's language
        bootWidescreen = settings.aspectMode() != AppSettings.ASPECT_4_3;
        try {
            AppFiles.prepareRuntime(this);
            final String driverOff = GpuDrivers.checkLastStart(this, settings);
            if (driverOff != null) Toast.makeText(this, driverOff, Toast.LENGTH_LONG).show();
            GpuDrivers.applyToGame(this, settings);
            AppFiles.writeConfig(this, settings);
            DevTools.onGameStart(this);  // before the runtime reads env.txt
            // Read by runtime/src/platform/host_platform.cpp before anything else starts.
            Os.setenv("WIICOMPILED_RUNTIME_DIR", AppFiles.runtimeDir(this).getAbsolutePath(), true);
            Os.setenv("WIICOMPILED_DATA_DIR", AppFiles.dataDir(this).getAbsolutePath(), true);
            final java.io.File logs = AppFiles.newRunLogDir();
            if (logs != null) Os.setenv("BASARA_LOG_DIR", logs.getAbsolutePath(), true);
            // HD Textures keeps its own cache folder; without Custom Textures it is the only one indexed.
            final java.io.File hdCache = settings.hdTextures() != 0 ? AppFiles.hdCacheDir() : null;
            final java.io.File textures = settings.customTextures() ? AppFiles.textureDir() : hdCache;
            if (textures != null) Os.setenv("BASARA_TEXTURE_DIR", textures.getAbsolutePath(), true);
            if (hdCache != null) Os.setenv("BASARA_HD_CACHE_DIR", hdCache.getAbsolutePath(), true);
            // Sustained Performance: the system holds steady clocks, so the game thread is not pinned to
            // the fastest cores and no ADPF boost is asked for (runtime/src/cpu_boost.cpp). env.txt wins.
            if (sustainedMode()) Os.setenv("BASARA_CPU_BOOST", "0", false);
            try {
                // For the log's device line: the screen's refresh rate.
                Os.setenv("BASARA_DISPLAY_HZ", String.valueOf(Math.round(getDisplay().getRefreshRate())), true);
            } catch (Exception ignored) {
            }
        } catch (Exception e) {
            Log.e(TAG, "could not prepare the game's files", e);
            Toast.makeText(this, "Could not prepare the game's files: " + e.getMessage(), Toast.LENGTH_LONG).show();
        }
        super.onCreate(savedInstanceState);
        applyDisplayResolution();
        nativeReady = !SDLActivity.mBrokenLibraries;
        if (nativeReady) {
            nativeSetDeviceName(deviceName());
        }
        // LAN rooms are found with broadcasts, which many Wi-Fi drivers drop unless an app holds this.
        final WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        if (wifi != null) {
            multicastLock = wifi.createMulticastLock("basara-lan");
            multicastLock.setReferenceCounted(false);
            multicastLock.acquire();
        }

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        touch = new TouchControlsView(this);
        touch.setListener(this);
        applyControlSettings();
        addContentView(touch, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        motion = new MotionInput(this, this);
    }

    /** SDL is linked into libmain.so, so that one library is all there is to load. */
    @Override
    protected String[] getLibraries() {
        return new String[] {"main"};
    }

    // The game's menus take taps and the remote hides while they are up; it comes back a moment
    // after they go, so it doesn't blink while one menu screen hands over to the next.
    private final android.os.Handler menuPoll = new android.os.Handler(android.os.Looper.getMainLooper());
    private long lastTapScreen;
    private long lastBattery = -100000;
    private boolean menuOpen;

    private void setMenuPause(boolean paused) {
        if (!nativeReady) return;
        try {
            nativeSetMenuPause(paused);
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "menu pause bridge missing from libmain.so", e);
        }
    }

    private void pushBattery() {
        final Intent battery = registerReceiver(null,
                new android.content.IntentFilter(Intent.ACTION_BATTERY_CHANGED));
        if (battery == null) return;
        final int level = battery.getIntExtra(android.os.BatteryManager.EXTRA_LEVEL, -1);
        final int scale = battery.getIntExtra(android.os.BatteryManager.EXTRA_SCALE, 100);
        final int temp = battery.getIntExtra(android.os.BatteryManager.EXTRA_TEMPERATURE, -10);
        try {
            nativeSetBattery(level >= 0 && scale > 0 ? level * 100 / scale : -1, temp / 10f);
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "battery bridge missing from libmain.so", e);
        }
    }

    private final Runnable pollMenu = new Runnable() {
        @Override
        public void run() {
            final long now = android.os.SystemClock.uptimeMillis();
            if (nativeReady) {
                // The game's settings window: keep its choices, the config is rewritten at each start.
                final float[] changed = nativeTakeSettings();
                if (changed != null) {
                    settings.setResolutionScale(changed[0]);
                    settings.setShowFps(changed[1] > 0.5f);
                    if (changed.length > 2) settings.setAspectMode(Math.round(changed[2]));
                    if (changed.length > 3) settings.setSpinningCoins(changed[3] > 0.5f);
                    if (changed.length > 4) settings.setFrameGraph(changed[4] > 0.5f);
                    if (changed.length > 5) settings.setVSync(changed[5] > 0.5f);
                }
            }
            if (nativeReady && now - lastBattery > 10000) {
                lastBattery = now;
                pushBattery();
            }
            if (nativeReady && nativeTapScreen()) {
                lastTapScreen = now;
                touch.setMenuMode(true);
            } else if (now - lastTapScreen > 600) {
                touch.setMenuMode(false);
            }
            menuPoll.postDelayed(this, 100);
        }
    };

    @Override
    public void onTap(float x, float y) {
        if (nativeReady) nativeTap(x, y);
    }

    @Override
    public void onPointer(float x, float y, boolean held) {
        if (nativeReady) nativePointer(x, y, held);
    }

    @Override
    public void onLayoutDone(String layout, boolean fixedStick) {
        settings.setTouchLayout(layout);
        settings.setFixedStick(fixedStick);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (nativeReady) {
            nativeSetBackground(false);  // the game carries on where it stopped
            if (menuOpen) setMenuPause(true);  // ...unless its menu is still open
            // Back from the Start Menu (Continue): what was changed there applies now. The launcher
            // wrote it from its own process, so read the settings file again first.
            settings = new AppSettings(this);
            applyNativeSettings();
        }
        applyControlSettings();
        getWindow().setSustainedPerformanceMode(sustainedMode());
        menuPoll.removeCallbacks(pollMenu);
        menuPoll.post(pollMenu);
        hideSystemBars();
        if (motion != null) {
            motion.start(settings.motionTilt(), settings.motionShake());
        }
    }

    @Override
    protected void onPause() {
        if (nativeReady) {
            // Before SDL's pause stops the game thread: let the game write its backup Quick Save (in
            // case Android ends the app while it is away) and pause itself. Bounded: a save that takes
            // longer is simply not published (the NAND only commits closed files).
            nativeSetBackground(true);
            final long until = android.os.SystemClock.uptimeMillis() + SUSPEND_WAIT_MS;
            while (!nativeGamePaused() && android.os.SystemClock.uptimeMillis() < until) {
                try {
                    Thread.sleep(10);
                } catch (InterruptedException e) {
                    break;
                }
            }
        }
        menuPoll.removeCallbacks(pollMenu);
        if (motion != null) motion.stop();
        if (touch != null) touch.release();
        super.onPause();
    }

    private boolean sustainedMode() {
        return settings.sustainedPerformance() && AppSettings.sustainedSupported(this);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemBars();
        } else if (touch != null) {
            touch.release();  // a dialog or the shade took the screen: let go of every button
        }
    }

    @Override
    public void onBackPressed() {
        onMenu();
    }

    /** SDL takes the Back key as keyboard input before onBackPressed: the Back menu gets it first. */
    @Override
    public boolean dispatchKeyEvent(android.view.KeyEvent event) {
        if (event.getKeyCode() == android.view.KeyEvent.KEYCODE_BACK) {
            if (event.getAction() == android.view.KeyEvent.ACTION_UP && !event.isCanceled()) {
                onMenu();
            }
            return true;
        }
        return super.dispatchKeyEvent(event);
    }

    /**
     * "Start Menu": back to the launcher. The game stays paused in its process, and the launcher's
     * Continue brings it back; a LAN session ends its process, as before.
     */
    private void openStartMenu(boolean inSession) {
        if (!inSession) {
            startActivity(new Intent(this, LauncherActivity.class)
                    .putExtra(LauncherActivity.EXTRA_SHOW_START_MENU, true)
                    .addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT));
            return;
        }
        nativePrepareToLeave();  // the host keeps the session's progress
        startActivity(new Intent(this, LauncherActivity.class)
                .putExtra(LauncherActivity.EXTRA_SHOW_START_MENU, true)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP));
        finish();
    }

    /**
     * "Quit Game": closes the whole app, not just the game. The task (launcher included) leaves the
     * recent apps, the launcher's process ends, and onDestroy ends this one.
     */
    private void quitApp() {
        final android.app.ActivityManager am = getSystemService(android.app.ActivityManager.class);
        // The launcher can sit in a task of its own: every task of the app goes, or it would come back.
        if (am != null) {
            for (android.app.ActivityManager.AppTask task : am.getAppTasks()) {
                task.finishAndRemoveTask();
            }
        }
        if (am != null && am.getRunningAppProcesses() != null) {
            for (android.app.ActivityManager.RunningAppProcessInfo info : am.getRunningAppProcesses()) {
                if (info.pid != Process.myPid() && info.uid == Process.myUid()
                        && info.processName.equals(getPackageName())) {
                    Process.killProcess(info.pid);
                }
            }
        }
        finishAndRemoveTask();
    }

    /** "Leave LAN Game": keep the host's progress, then start the game again on its own. */
    private void leaveSession() {
        nativePrepareToLeave();
        restartForSession();  // with the session file gone, the next start is an ordinary one
    }

    /** The name the user gave the phone, else its model. */
    private String deviceName() {
        String name = Settings.Global.getString(getContentResolver(), Settings.Global.DEVICE_NAME);
        if (name == null || name.trim().isEmpty()) {
            name = Build.MODEL;
        }
        return name == null ? "Phone" : name.trim();
    }

    /**
     * Network play: the runtime has written the session (netplay_start.cpp); start the game again in a
     * fresh process. The launcher waits for this one to end, then opens the game, which finds the
     * session and boots into it. Called from the game thread.
     */
    void restartForSession() {
        runOnUiThread(() -> {
            final Intent intent = new Intent(this, LauncherActivity.class)
                    .putExtra(LauncherActivity.EXTRA_RESTART_GAME, true)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
            startActivity(intent);
            finish();
        });
    }

    @Override
    protected void onDestroy() {
        if (multicastLock != null && multicastLock.isHeld()) {
            multicastLock.release();
        }
        super.onDestroy();
        if (isFinishing()) {
            GpuDrivers.endedOnPurpose(this);
            // The runtime starts once per process; the next start must get a fresh one.
            Process.killProcess(Process.myPid());
        }
    }

    // --- input ---------------------------------------------------------------------------------

    @Override
    public void onTouchInput(int buttons) {
        touchButtons = buttons;
        pushInput();
    }

    @Override
    public void onTouchSticks(float leftX, float leftY, float rightX, float rightY) {
        if (!nativeReady) return;
        try {
            nativeSetTouchSticks(leftX, leftY, rightX, rightY);
        } catch (UnsatisfiedLinkError e) {
            nativeReady = false;
            Log.e(TAG, "touch input bridge missing from libmain.so", e);
        }
    }

    @Override
    public void onMotion(float tilt, boolean shake) {
        motionTilt = tilt;
        motionShake = shake;
        pushInput();
    }

    private void pushInput() {
        if (!nativeReady) return;
        try {
            nativeSetTouchState(touchButtons, motionShake, motionTilt);
        } catch (UnsatisfiedLinkError e) {
            nativeReady = false;
            Log.e(TAG, "touch input bridge missing from libmain.so", e);
        }
    }

    private void applyControlSettings() {
        touch.setControlsVisible(settings.touchControls());
        touch.setOpacity(settings.touchOpacity());
        touch.setHaptics(settings.haptics());
        touch.setTouchCamera(settings.touchCamera());
        touch.setInvertCamera(settings.invertCamera());
        touch.setFixedStick(settings.fixedStick());
        touch.setLayout(settings.touchLayout());
    }

    /** What the game can change while it runs: picture, upscaler, frame pacing, overlay, cheats. */
    private void applyNativeSettings() {
        if (!nativeReady) return;
        nativeApplyDisplaySettings(settings.aspectMode(), settings.resolutionScale(), settings.showFps(),
                settings.dynamicResolution(), settings.overlayItems(), settings.frameGraph(),
                settings.thermalControl(), settings.textureFilter());
        nativeApplyCheats(settings.cheatMoney(), settings.cheatMoneyAmount(), settings.cheatMoneySeq(),
                settings.cheatInfiniteHp(), settings.cheatInfiniteBasara());
        try {
            nativeApplyLiveSettings(settings.fsr(), settings.fpsLock30() ? 2 : settings.frameskip() ? 1 : 0);
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "live settings bridge missing from libmain.so", e);
        }
    }

    /** A change in the in-game settings: everything that applies while playing, at once. */
    private void applyLiveSettings() {
        settings = new AppSettings(this);
        applyNativeSettings();
        applyControlSettings();
        if (motion != null) {
            motion.stop();
            motion.start(settings.motionTilt(), settings.motionShake());
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

    // --- menu ----------------------------------------------------------------------------------

    /**
     * The in-game menu, from the phone's Back gesture or the gear on the touch controls: the game
     * pauses while it is open.
     */
    @Override
    public void onMenu() {
        touch.release();
        final boolean inSession = nativeReady && nativeSessionActive();
        // The game stands still while the menu is open (no backup save: it isn't leaving the screen).
        menuOpen = true;
        setMenuPause(true);
        new PauseMenu(inSession).show();
    }

    /**
     * The in-game menu: a landscape card, actions on the left (Resume, Edit Controls, Start Menu,
     * Leave LAN, Quit) and the settings on the right. What the game can change while playing applies
     * at once; settings read only at start carry a red restart warning.
     */
    private final class PauseMenu {
        private final android.app.Dialog dialog;
        private final android.widget.TextView telemetry;
        private final android.widget.TextView heading;
        private final android.widget.LinearLayout sectionList;
        private final android.widget.ScrollView rowsScroll;
        private final SettingsMenu menu;
        private final int[] sections = SettingsMenu.gameSections();

        PauseMenu(boolean inSession) {
            final GameActivity a = GameActivity.this;
            final android.widget.LinearLayout card = new android.widget.LinearLayout(a);
            card.setOrientation(android.widget.LinearLayout.HORIZONTAL);
            card.setBackground(Ui.box(a, Ui.SURFACE, Ui.BORDER_STRONG, 18));
            card.setClipToOutline(true);

            // Left: PAUSED, Resume, the sections, then the actions in one row at the foot.
            final android.widget.LinearLayout nav = new android.widget.LinearLayout(a);
            nav.setOrientation(android.widget.LinearLayout.VERTICAL);
            nav.setBackgroundColor(Ui.ELEVATED);
            nav.setPadding(dp(10), dp(12), dp(10), dp(10));
            final android.widget.LinearLayout title = new android.widget.LinearLayout(a);
            title.setGravity(android.view.Gravity.CENTER_VERTICAL);
            final android.widget.TextView badge = Ui.text(a, Tr.t("PAUSED"), 9.5f, android.graphics.Color.WHITE);
            badge.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
            badge.setLetterSpacing(0.1f);
            badge.setPadding(dp(7), dp(2), dp(7), dp(2));
            badge.setBackground(Ui.box(a, Ui.ACCENT, 0, 4));
            title.addView(badge);
            final android.widget.TextView game = Ui.text(a, "BASARA 2 HEROES", 10f, Ui.SECONDARY);
            game.setTypeface(Ui.MONO);
            game.setSingleLine(true);
            game.setEllipsize(android.text.TextUtils.TruncateAt.END);
            final android.widget.LinearLayout.LayoutParams gameParams = new android.widget.LinearLayout.LayoutParams(
                    0, android.widget.LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            gameParams.leftMargin = dp(8);
            title.addView(game, gameParams);
            nav.addView(title, Ui.matchWidth(0));
            nav.addView(Ui.button(a, R.drawable.ic_play, Tr.t("Resume Game"), Ui.PRIMARY, v -> close()),
                    Ui.matchWidth(dp(10)));
            sectionList = new android.widget.LinearLayout(a);
            sectionList.setOrientation(android.widget.LinearLayout.VERTICAL);
            nav.addView(sectionList, Ui.matchWidth(dp(10)));
            nav.addView(new android.view.View(a), new android.widget.LinearLayout.LayoutParams(1, 0, 1f));
            final android.widget.LinearLayout actions = new android.widget.LinearLayout(a);
            actions.addView(action(R.drawable.ic_move, Tr.t("Layout"), false, v -> {
                close();
                touch.setEditMode(true);
            }), actionParams(false));
            actions.addView(action(R.drawable.ic_home, Tr.t("Menu"), false, v -> {
                close();
                openStartMenu(inSession);
            }), actionParams(true));
            if (inSession) {
                actions.addView(action(R.drawable.ic_logout, Tr.t("Leave LAN"), false, v -> {
                    close();
                    leaveSession();
                }), actionParams(true));
            }
            actions.addView(action(R.drawable.ic_power, Tr.t("Quit"), true, v -> quitApp()), actionParams(true));
            nav.addView(actions, Ui.matchWidth(dp(10)));
            final android.widget.ScrollView navScroll = new android.widget.ScrollView(a);
            navScroll.setFillViewport(true);
            navScroll.setVerticalScrollBarEnabled(false);
            navScroll.addView(nav);
            card.addView(navScroll, new android.widget.LinearLayout.LayoutParams(dp(196),
                    android.widget.LinearLayout.LayoutParams.MATCH_PARENT));
            final android.view.View split = new android.view.View(a);
            split.setBackgroundColor(Ui.BORDER);
            card.addView(split, new android.widget.LinearLayout.LayoutParams(1,
                    android.widget.LinearLayout.LayoutParams.MATCH_PARENT));

            // Right: the section's name, what shapes the picture now, then its settings.
            final android.widget.LinearLayout right = new android.widget.LinearLayout(a);
            right.setOrientation(android.widget.LinearLayout.VERTICAL);
            right.setPadding(dp(16), dp(12), dp(14), 0);
            final android.widget.LinearLayout head = new android.widget.LinearLayout(a);
            head.setGravity(android.view.Gravity.CENTER_VERTICAL);
            heading = Ui.text(a, "", 16, Ui.TEXT);
            heading.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
            head.addView(heading, new android.widget.LinearLayout.LayoutParams(0,
                    android.widget.LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
            telemetry = Ui.text(a, "", 10.5f, Ui.SECONDARY);
            telemetry.setTypeface(Ui.MONO);
            telemetry.setSingleLine(true);
            telemetry.setPadding(dp(9), dp(3), dp(9), dp(3));
            telemetry.setBackground(Ui.box(a, Ui.BG, Ui.BORDER, 6));
            head.addView(telemetry);
            right.addView(head, Ui.matchWidth(0));
            final android.widget.TextView note = Ui.text(a,
                    Tr.t("Changes apply at once, except those marked Restart needed."), 11, Ui.MUTED);
            note.setPadding(0, dp(2), 0, 0);
            right.addView(note, Ui.matchWidth(0));
            final android.widget.LinearLayout rows = new android.widget.LinearLayout(a);
            rows.setOrientation(android.widget.LinearLayout.VERTICAL);
            rows.setPadding(0, dp(10), 0, dp(14));
            rowsScroll = new android.widget.ScrollView(a);
            rowsScroll.setVerticalScrollBarEnabled(false);
            rowsScroll.addView(rows);
            right.addView(rowsScroll, new android.widget.LinearLayout.LayoutParams(
                    android.widget.LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
            card.addView(right, new android.widget.LinearLayout.LayoutParams(0,
                    android.widget.LinearLayout.LayoutParams.MATCH_PARENT, 1f));

            menu = new SettingsMenu(a, rows, true, () -> {
                applyLiveSettings();
                showTelemetry();
            });

            // The backdrop dims the game; the card nearly fills it (a phone is short in landscape).
            final android.widget.FrameLayout backdrop = new android.widget.FrameLayout(a);
            backdrop.setBackgroundColor(android.graphics.Color.argb(190, 10, 11, 13));
            final android.util.DisplayMetrics screen = getResources().getDisplayMetrics();
            final int width = Math.min(dp(900), Math.round(screen.widthPixels * 0.92f));
            final int height = Math.min(dp(520), Math.round(screen.heightPixels * 0.92f));
            backdrop.addView(card, new android.widget.FrameLayout.LayoutParams(width, height,
                    android.view.Gravity.CENTER));

            dialog = new android.app.Dialog(a, android.R.style.Theme_DeviceDefault_NoActionBar_Fullscreen);
            dialog.setContentView(backdrop);
            final android.view.Window window = dialog.getWindow();
            if (window != null) {
                window.setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(
                        android.graphics.Color.TRANSPARENT));
                window.setLayout(android.view.WindowManager.LayoutParams.MATCH_PARENT,
                        android.view.WindowManager.LayoutParams.MATCH_PARENT);
            }
            dialog.setOnShowListener(d -> {
                if (window == null) return;
                window.setDecorFitsSystemWindows(false);
                final WindowInsetsController controller = window.getInsetsController();
                if (controller != null) {
                    controller.hide(WindowInsets.Type.systemBars());
                    controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                }
            });
            dialog.setOnCancelListener(d -> resumeFromMenu());
            dialog.setOnDismissListener(d -> hideSystemBars());
            select(SettingsMenu.DISPLAY);
            showTelemetry();
        }

        void show() { dialog.show(); }

        private void close() {
            dialog.dismiss();
            resumeFromMenu();
        }

        /** An action at the foot of the left column: its icon over a short name. */
        private android.widget.LinearLayout action(int icon, String label, boolean danger,
                                                   android.view.View.OnClickListener onClick) {
            final GameActivity a = GameActivity.this;
            final android.widget.LinearLayout box = new android.widget.LinearLayout(a);
            box.setOrientation(android.widget.LinearLayout.VERTICAL);
            box.setGravity(android.view.Gravity.CENTER);
            box.setPadding(dp(2), dp(7), dp(2), dp(6));
            box.setBackground(Ui.touchBox(a, danger ? Ui.DANGER_FILL : Ui.SURFACE,
                    danger ? Ui.DANGER_BORDER : Ui.BORDER, 8));
            box.addView(Ui.icon(a, icon, danger ? android.graphics.Color.rgb(248, 113, 113) : Ui.SECONDARY, 16));
            final android.widget.TextView name = Ui.text(a, label, 9.5f, danger ? Ui.DANGER_TEXT : Ui.SECONDARY);
            name.setSingleLine(true);
            name.setEllipsize(android.text.TextUtils.TruncateAt.END);
            name.setGravity(android.view.Gravity.CENTER);
            name.setPadding(0, dp(3), 0, 0);
            box.addView(name, Ui.matchWidth(0));
            box.setClickable(true);
            box.setOnClickListener(onClick);
            box.setContentDescription(label);
            return box;
        }

        private android.widget.LinearLayout.LayoutParams actionParams(boolean gap) {
            final android.widget.LinearLayout.LayoutParams params = new android.widget.LinearLayout.LayoutParams(
                    0, android.widget.LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            if (gap) params.leftMargin = dp(6);
            return params;
        }

        private void select(int section) {
            menu.show(section);
            rowsScroll.scrollTo(0, 0);
            heading.setText(SettingsMenu.name(section));
            sectionList.removeAllViews();
            for (int id : sections) {
                final boolean open = id == section;
                final int color = open ? Ui.TEXT : SettingsMenu.risky(id) ? Sheet.WARNING_TEXT : Ui.SECONDARY;
                final android.widget.LinearLayout item = new android.widget.LinearLayout(GameActivity.this);
                item.setGravity(android.view.Gravity.CENTER_VERTICAL);
                item.setPadding(dp(10), dp(7), dp(10), dp(7));
                item.setBackground(open ? Ui.touchBox(GameActivity.this, Ui.ACTIVE, Ui.BORDER_STRONG, 8)
                        : Ui.touchBox(GameActivity.this, android.graphics.Color.TRANSPARENT,
                                android.graphics.Color.TRANSPARENT, 8));
                item.addView(Ui.icon(GameActivity.this, SettingsMenu.icon(id),
                        open ? Ui.ACCENT : SettingsMenu.risky(id) ? Sheet.WARNING_TEXT : Ui.MUTED, 16));
                final android.widget.TextView name = Ui.text(GameActivity.this, SettingsMenu.name(id), 13, color);
                name.setTypeface(open ? android.graphics.Typeface.DEFAULT_BOLD : Ui.MEDIUM);
                final android.widget.LinearLayout.LayoutParams nameParams = new android.widget.LinearLayout.LayoutParams(
                        android.widget.LinearLayout.LayoutParams.WRAP_CONTENT,
                        android.widget.LinearLayout.LayoutParams.WRAP_CONTENT);
                nameParams.leftMargin = dp(10);
                item.addView(name, nameParams);
                item.setClickable(true);
                item.setOnClickListener(v -> select(id));
                sectionList.addView(item, Ui.matchWidth(sectionList.getChildCount() == 0 ? 0 : dp(2)));
            }
        }

        /** "0.75x · SGSR · Frameskip Auto": the settings shaping the picture right now. */
        private void showTelemetry() {
            final String[] upscalers = {Tr.t("Upscaler Off"), "FSR Light", "FSR Full", "SGSR"};
            telemetry.setText(AppSettings.resolutionLabel(settings.resolutionScale()) + " · "
                    + upscalers[settings.fsr()] + " · "
                    + (settings.fpsLock30() ? Tr.t("30 FPS Lock")
                            : Tr.t("Frameskip ") + (settings.frameskip() ? Tr.t("Auto") : Tr.t("Off"))));
        }
    }

    private void resumeFromMenu() {
        menuOpen = false;
        setMenuPause(false);
    }

    private android.view.View line() {
        final android.view.View view = new android.view.View(this);
        view.setBackgroundColor(Ui.BORDER);
        return view;
    }

    private int dp(int value) {
        return Ui.dp(this, value);
    }
}
