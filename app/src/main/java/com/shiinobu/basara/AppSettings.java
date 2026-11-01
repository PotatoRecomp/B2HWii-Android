package com.shiinobu.basara;

import android.content.Context;
import android.content.SharedPreferences;

/** Player settings, kept in one SharedPreferences file shared by the launcher and the game. */
final class AppSettings {
    static final int ASPECT_4_3 = 0;
    static final int ASPECT_16_9 = 1;
    static final int ASPECT_FILL = 2;

    /** GPU driver workarounds: picked from the phone's GPU, or forced (AuroraGpuCompat). */
    static final int GPU_COMPAT_AUTO = 0;
    static final int GPU_COMPAT_ON = 1;
    static final int GPU_COMPAT_OFF = 2;

    /** Render scales offered in the menu; 1x renders 480 lines, whatever the aspect; 0 the screen's own. */
    static final float[] RESOLUTION_SCALES = {0f, 0.5f, 0.75f, 1f, 1.5f, 2f, 3f, 4f};

    private static final String FILE = "settings";
    private final SharedPreferences prefs;

    AppSettings(Context context) {
        // Both processes (launcher and :game) read it. Writes use commit(): the game process is
        // ended right after its menu closes, which would drop a pending apply().
        prefs = context.getSharedPreferences(FILE, Context.MODE_PRIVATE);
        // Once: the FPS / performance overlay costs frame time on slow phones, so installs that had
        // it on start with it off again (it can still be turned back on in Display settings).
        if (!prefs.getBoolean("show_fps_reset_1", false)) {
            prefs.edit().putBoolean("show_fps", false).putBoolean("show_fps_reset_1", true).commit();
        }
    }

    /** DeviceProfile has not run yet on this install. */
    boolean needsDeviceProfile() { return !prefs.getBoolean("device_profile_1", false); }

    /** The device profile's picks, for the settings the player never set; runs once. */
    /** GX Thread was on by default for slower phones and flickers: turn it off once on every install. */
    void resetGxThreadOnce() {
        if (prefs.getBoolean("gx_thread_off_1", false)) return;
        prefs.edit().putBoolean("gx_thread_off_1", true).putBoolean("gx_thread", false).commit();
    }

    void applyDeviceProfile(float scale, boolean frameskip, boolean gxThread) {
        final SharedPreferences.Editor e = prefs.edit().putBoolean("device_profile_1", true)
                .putBoolean("gx_thread_off_1", true);
        if (!prefs.contains("resolution_scale")) e.putFloat("resolution_scale", scale);
        if (!prefs.contains("frameskip")) e.putBoolean("frameskip", frameskip);
        if (!prefs.contains("gx_thread")) e.putBoolean("gx_thread", gxThread);
        e.commit();
    }

    int aspectMode() { return prefs.getInt("aspect_mode", ASPECT_FILL); }
    void setAspectMode(int mode) { prefs.edit().putInt("aspect_mode", mode).commit(); }

    float resolutionScale() { return prefs.getFloat("resolution_scale", 1f); }
    void setResolutionScale(float scale) { prefs.edit().putFloat("resolution_scale", scale).commit(); }

    int gpuCompat() { return prefs.getInt("gpu_compat", GPU_COMPAT_AUTO); }
    void setGpuCompat(int mode) { prefs.edit().putInt("gpu_compat", mode).commit(); }

    /** Frameskip Auto: skip drawing a frame while the game runs behind (off by default). */
    boolean frameskip() { return prefs.getBoolean("frameskip", false); }
    void setFrameskip(boolean on) { prefs.edit().putBoolean("frameskip", on).commit(); }
    /** 30 FPS Lock: draw every other frame, game logic and audio still at full speed (off by default). */
    boolean fpsLock30() { return prefs.getBoolean("fps_lock_30", false); }
    void setFpsLock30(boolean on) { prefs.edit().putBoolean("fps_lock_30", on).commit(); }
    /**
     * FSR1 upscaling when the game renders below the screen's resolution (on by default), in light
     * quality (sharpen the stretched picture) or full (FSR1's edge-aware upscale, heavier on the GPU).
     */
    boolean fsrOn() { return prefs.getBoolean("fsr_on", true); }
    void setFsrOn(boolean on) { prefs.edit().putBoolean("fsr_on", on).commit(); }
    boolean fsrFull() { return prefs.getBoolean("fsr_full", false); }
    void setFsrFull(boolean full) { prefs.edit().putBoolean("fsr_full", full).commit(); }
    /** The config value: 0 off, 1 light, 2 full. */
    int fsr() { return fsrOn() ? (fsrFull() ? 2 : 1) : 0; }

    /** Auto Update: opening the app installs the latest GitHub release (off by default). */
    boolean autoUpdate() { return prefs.getBoolean("auto_update", false); }
    void setAutoUpdate(boolean on) { prefs.edit().putBoolean("auto_update", on).commit(); }

    boolean showFps() { return prefs.getBoolean("show_fps", false); }
    void setShowFps(boolean show) { prefs.edit().putBoolean("show_fps", show).commit(); }
    boolean spinningCoins() { return prefs.getBoolean("spinning_coins", true); }
    void setSpinningCoins(boolean on) { prefs.edit().putBoolean("spinning_coins", on).commit(); }

    /** What the performance overlay shows: OVERLAY_* bits (the runtime's kOverlay* in settings_overlay.h). */
    static final int OVERLAY_FPS = 1, OVERLAY_CPU = 2, OVERLAY_GPU = 4, OVERLAY_BATTERY = 8, OVERLAY_TEMPS = 16,
            OVERLAY_DETAILS = 32, OVERLAY_DEFAULT = 31;
    int overlayItems() { return prefs.getInt("overlay_items", OVERLAY_DEFAULT); }
    void setOverlayItems(int items) { prefs.edit().putInt("overlay_items", items).commit(); }

    boolean frameGraph() { return prefs.getBoolean("frame_graph", false); }
    void setFrameGraph(boolean on) { prefs.edit().putBoolean("frame_graph", on).commit(); }

    /** V-Sync: frames go out in step with the display and the game's clock follows its vsync. */
    boolean vsync() { return prefs.getBoolean("vsync", true); }
    void setVSync(boolean on) { prefs.edit().putBoolean("vsync", on).commit(); }

    boolean touchControls() { return prefs.getBoolean("touch_controls", true); }
    void setTouchControls(boolean on) { prefs.edit().putBoolean("touch_controls", on).commit(); }

    float touchOpacity() { return prefs.getFloat("touch_opacity", 0.6f); }
    void setTouchOpacity(float opacity) { prefs.edit().putFloat("touch_opacity", opacity).commit(); }

    boolean motionTilt() { return prefs.getBoolean("motion_tilt", true); }
    void setMotionTilt(boolean on) { prefs.edit().putBoolean("motion_tilt", on).commit(); }

    boolean motionShake() { return prefs.getBoolean("motion_shake", true); }
    void setMotionShake(boolean on) { prefs.edit().putBoolean("motion_shake", on).commit(); }

    boolean haptics() { return prefs.getBoolean("haptics", true); }
    void setHaptics(boolean on) { prefs.edit().putBoolean("haptics", on).commit(); }
    boolean touchCamera() { return prefs.getBoolean("touch_camera", true); }
    void setTouchCamera(boolean on) { prefs.edit().putBoolean("touch_camera", on).commit(); }

    /** The touch controls as the player arranged them in the layout editor ("" is the default). */
    boolean fixedStick() { return prefs.getBoolean("touch_fixed_stick", false); }
    void setFixedStick(boolean on) { prefs.edit().putBoolean("touch_fixed_stick", on).commit(); }

    String touchLayout() { return prefs.getString("touch_layout", ""); }
    void setTouchLayout(String layout) { prefs.edit().putString("touch_layout", layout).commit(); }

    /** Skip Boot:the logos run at full speed, undrawn, up to the game's save check. */
    /** Lowers the render resolution while the GPU holds the game back (the chosen one is the ceiling). */
    boolean gxThread() { return prefs.getBoolean("gx_thread", false); }
    void setGxThread(boolean on) { prefs.edit().putBoolean("gx_thread", on).commit(); }
    boolean thermalControl() { return prefs.getBoolean("thermal_control", true); }
    void setThermalControl(boolean on) { prefs.edit().putBoolean("thermal_control", on).commit(); }

    /**
     * Whether the system reports a thermal headroom (Android 12+, and a thermal HAL that has the
     * skin sensors it is computed from). Asked once a process: Android answers NaN to calls less
     * than a second apart.
     */
    static boolean thermalSupported(android.content.Context context) {
        if (thermalSupported == null) {
            boolean ok = false;
            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.S) {
                final android.os.PowerManager pm = context.getSystemService(android.os.PowerManager.class);
                ok = pm != null && !Float.isNaN(pm.getThermalHeadroom(10));
            }
            thermalSupported = ok;
        }
        return thermalSupported;
    }
    private static Boolean thermalSupported;

    boolean dynamicResolution() { return prefs.getBoolean("dynamic_resolution", false); }
    void setDynamicResolution(boolean on) { prefs.edit().putBoolean("dynamic_resolution", on).commit(); }
    boolean skipBoot() { return prefs.getBoolean("skip_boot", true); }
    void setSkipBoot(boolean on) { prefs.edit().putBoolean("skip_boot", on).commit(); }

    /** Beta only: why the Beta ended, once BetaGate has seen it end (kept, so going offline won't reopen it). */
    String betaEnded() { return prefs.getString("beta_ended", null); }
    void setBetaEnded(String reason) { prefs.edit().putString("beta_ended", reason).commit(); }

    /** "2× (960p)": the scale and the number of lines it renders, as the menu shows it. */
    static String resolutionLabel(float scale) {
        if (scale <= 0f) return "Match screen";
        final String factor = scale == Math.rint(scale)
                ? String.valueOf((int) scale) : String.valueOf(scale);
        return factor + "× (" + Math.round(480 * scale) + "p)";
    }
}
