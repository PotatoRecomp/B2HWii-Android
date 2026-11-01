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

    /** "GPU name\n1|0": the GPU the game draws with and whether its compatibility fixes are on. */
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

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        settings = new AppSettings(this);
        bootWidescreen = settings.aspectMode() != AppSettings.ASPECT_4_3;
        try {
            AppFiles.prepareRuntime(this);
            AppFiles.writeConfig(this, settings);
            // Read by runtime/src/platform/host_platform.cpp before anything else starts.
            Os.setenv("WIICOMPILED_RUNTIME_DIR", AppFiles.runtimeDir(this).getAbsolutePath(), true);
            Os.setenv("WIICOMPILED_DATA_DIR", AppFiles.dataDir(this).getAbsolutePath(), true);
            final java.io.File logs = AppFiles.logDir();
            if (logs != null) Os.setenv("BASARA_LOG_DIR", logs.getAbsolutePath(), true);
            final java.io.File textures = settings.customTextures() ? AppFiles.textureDir() : null;
            if (textures != null) Os.setenv("BASARA_TEXTURE_DIR", textures.getAbsolutePath(), true);
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
            // Back from the Start Menu (Continue): what was changed there applies now.
            nativeApplyDisplaySettings(settings.aspectMode(), settings.resolutionScale(), settings.showFps(),
                    settings.dynamicResolution(), settings.overlayItems(), settings.frameGraph(),
                    settings.thermalControl(), settings.textureFilter());
        }
        applyControlSettings();
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
        touch.setFixedStick(settings.fixedStick());
        touch.setLayout(settings.touchLayout());
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
     * The phone's Back gesture: back to the start menu (where the settings are), leave a LAN game,
     * or quit. There is no on-screen menu button; the game's own settings window (the gear on the
     * save file screen and the pause menus) covers resolution and the FPS counter while playing.
     */
    @Override
    public void onMenu() {
        touch.release();
        final boolean inSession = nativeReady && nativeSessionActive();
        // The game stands still while the menu is open (no backup save: it isn't leaving the screen).
        menuOpen = true;
        setMenuPause(true);
        final Runnable resume = () -> {
            menuOpen = false;
            setMenuPause(false);
        };
        final Sheet sheet = new Sheet(this, "Paused", "Basara 2 Heroes").horizontal()
                .action("Resume", "Back to the game", true, false, resume)
                .action("Start Menu", "Settings and the launcher; Continue brings you back", false, false, () -> {
                    resume.run();
                    openStartMenu(inSession);
                })
                .action("Edit Controls", "Move and resize the touch buttons", false, false, () -> {
                    resume.run();
                    touch.setEditMode(true);
                });
        if (inSession) {
            sheet.action("Leave LAN Game", null, false, false, () -> {
                resume.run();
                leaveSession();
            });
        }
        sheet.action("Quit Game", "Close the app completely", false, true, this::quitApp)
                .onCancel(resume)
                .onDismiss(this::hideSystemBars)
                .show();
    }
}
