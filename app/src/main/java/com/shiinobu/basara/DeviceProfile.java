package com.shiinobu.basara;

import android.app.ActivityManager;
import android.content.Context;
import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLSurface;
import android.opengl.GLES20;
import android.util.Log;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * First-run settings picked from the phone, so a slow phone does not start at a render resolution
 * its GPU cannot hold and a fast one is not left at 1x. Only settings the player never changed are
 * set, and only once (AppSettings.applyDeviceProfile).
 *
 * <p>Tiers come from tester logs: Mali-G52 MC2 / G57 MP1 hold 60 fps only at 0.5-0.75x, G57 MC2 /
 * G615 MC2 hold 1-1.5x (then the CPU limits), Mali-G720 MC7 at 4x was GPU-bound but has room for 2x.
 * Frameskip goes on unless the fastest core reaches 3 GHz: every phone below that was CPU-bound in
 * crowded battles.
 */
final class DeviceProfile {
    private static final String TAG = "Basara";

    private DeviceProfile() {}

    /** Runs the profiles this install has not had yet; true when the Low-End preset was just applied. */
    static boolean applyOnce(Context context, AppSettings settings) {
        final boolean first = settings.needsDeviceProfile();
        if (!first && !settings.needsLowEndProfile()) {
            settings.resetGxThreadOnce();
            settings.resetThermalOnce();
            return false;
        }
        final String gpu = gpuName();
        settings.setGpuName(gpu);
        final int maxMhz = maxCpuMhz();
        final long ramMb = ramMb(context);
        float scale = scaleFor(gpu);
        if (ramMb > 0 && ramMb < 4608) scale = Math.min(scale, 0.75f);
        final boolean frameskip = maxMhz <= 0 || maxMhz < 3000;
        if (first) {
            // The same phones are CPU-bound: the GX thread takes the GX work off the game thread.
            // GX Thread stays off: it flickers in battles, and syncing it enough to stop that is slower
            // than running without it (G81, 2026-11-01).
            final boolean gxThread = false;
            settings.applyDeviceProfile(scale, frameskip, gxThread);
            Log.i(TAG, "device profile: gpu '" + gpu + "', cpu max " + maxMhz + " MHz, ram " + ramMb
                    + " MB -> resolution " + scale + "x, frameskip " + frameskip + ", gx thread " + gxThread);
        } else {
            settings.resetGxThreadOnce();
            settings.resetThermalOnce();
        }
        settings.markLowEndProfile();
        if (!lowEnd(context, gpu, ramMb)) return false;
        // Only what the player never changed: a key never written, or still holding the first
        // profile's own pick (which wrote resolution_scale and frameskip without the player).
        final java.util.Map<String, Object> values = lowEndValues(gpu);
        final java.util.Iterator<java.util.Map.Entry<String, Object>> it = values.entrySet().iterator();
        final AppSettings now = new AppSettings(context);
        while (it.hasNext()) {
            final String key = it.next().getKey();
            if (!now.has(key)) continue;
            if (key.equals("resolution_scale") && Math.abs(now.resolutionScale() - scale) < 0.01f) continue;
            if (key.equals("frameskip") && now.frameskip() == frameskip) continue;
            it.remove();
        }
        if (values.isEmpty()) return false;
        now.applyLowEnd(values);
        Log.i(TAG, "device profile 2: low-end phone, preset " + values.keySet());
        return true;
    }

    /**
     * A phone the Low-End preset is for: a budget GPU (the first profile's 0.75x or lower: Adreno 505..620,
     * Mali G52 / G57 MP1, PowerVR), Android's low-RAM flag, fewer than 6 cores, or under 3.6 GB of RAM
     * (ARMSX2's DeviceTier uses the last three).
     */
    static boolean lowEnd(Context context, AppSettings settings) {
        return lowEnd(context, settings.gpuName(), ramMb(context));
    }

    static boolean lowEnd(Context context, String gpu, long ramMb) {
        final ActivityManager am = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
        return scaleFor(gpu) <= 0.75f || (am != null && am.isLowRamDevice())
                || Runtime.getRuntime().availableProcessors() < 6 || (ramMb > 0 && ramMb < 3686);
    }

    /**
     * What the Low-End preset sets: a 480-line surface the screen stretches, render at the same 480
     * lines (0.5x on the weakest GPUs), no extra texture or upscaling work, frameskip and dynamic
     * resolution to hold speed, and V-Sync off so a late frame does not wait for the next refresh.
     */
    static java.util.Map<String, Object> lowEndValues(String gpu) {
        final java.util.Map<String, Object> v = new java.util.LinkedHashMap<>();
        v.put("display_lines", 480);
        v.put("resolution_scale", scaleFor(gpu) <= 0.5f ? 0.5f : 1f);
        v.put("texture_filter", 0);
        v.put("fsr_on", false);
        v.put("frameskip", true);
        v.put("dynamic_resolution", true);
        v.put("hd_textures", 0);
        v.put("show_fps", false);
        v.put("vsync", false);
        return v;
    }

    /** Render scale for a GL_RENDERER string; 1x when the GPU is unknown. */
    static float scaleFor(String gpu) {
        final String g = gpu == null ? "" : gpu.toLowerCase(Locale.ROOT);
        Matcher m = Pattern.compile("adreno[^0-9]*(\\d{3})").matcher(g);
        if (m.find()) {
            final int model = Integer.parseInt(m.group(1));
            if (model >= 730) return 2f;          // 8 Gen 2 and newer
            if (model >= 650) return 1.5f;        // 865 / 888 / 7+ Gen 2 class
            if (model >= 630) return 1f;          // 845 / 855 / 778G class
            // Adreno 618/619/620 (720G, 732G, 750G): measured on a 732G, 1x keeps the render worker
            // at ~20 ms a battle frame (35 fps); 0.75x brings it to ~8 ms (57 fps).
            return 0.75f;                         // 505..616: budget Snapdragons
        }
        if (g.contains("immortalis") || g.contains("xclipse")) return 2f;
        m = Pattern.compile("mali-g(\\d+)").matcher(g);
        if (m.find()) {
            final int model = Integer.parseInt(m.group(1));
            final Matcher c = Pattern.compile("m[cp](\\d+)").matcher(g);
            final int cores = c.find() ? Integer.parseInt(c.group(1)) : 1;
            final boolean valhallaOrNewer = model == 57 || model == 68 || model == 77 || model == 78
                    || model >= 310;              // G57, G68, G77, G78, G310, G510, G610, G615, G710...
            if (!valhallaOrNewer) {                                 // G31/G51/G52/G71/G72/G76
                return cores >= 6 ? 1f : cores >= 3 ? 0.75f : 0.5f; // G52 MC2 (G81): 60 fps at 0.5x
            }
            if (model >= 700 && cores >= 6) return 2f;              // G710 MC10, G715, G720 MC7, G925
            if (cores >= 3 || model >= 600) return 1f;              // G57 MC3, G68 MC4, G610/G615
            return model == 57 && cores <= 1 ? 0.75f : 1f;          // G57 MP1 (itel) vs MC2 (G99)
        }
        if (g.contains("mali-t") || g.contains("powervr") || g.contains("ge8")) return 0.5f;
        return 1f;
    }

    /** GL_RENDERER from a throwaway 1x1 pbuffer context; "" when EGL is not available. */
    static String gpuName() {
        EGLDisplay display = EGL14.EGL_NO_DISPLAY;
        EGLContext ctx = EGL14.EGL_NO_CONTEXT;
        EGLSurface surface = EGL14.EGL_NO_SURFACE;
        try {
            display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
            final int[] version = new int[2];
            if (!EGL14.eglInitialize(display, version, 0, version, 1)) return "";
            final int[] attribs = {EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
                    EGL14.EGL_SURFACE_TYPE, EGL14.EGL_PBUFFER_BIT, EGL14.EGL_NONE};
            final EGLConfig[] configs = new EGLConfig[1];
            final int[] count = new int[1];
            if (!EGL14.eglChooseConfig(display, attribs, 0, configs, 0, 1, count, 0) || count[0] == 0) return "";
            ctx = EGL14.eglCreateContext(display, configs[0], EGL14.EGL_NO_CONTEXT,
                    new int[] {EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE}, 0);
            surface = EGL14.eglCreatePbufferSurface(display, configs[0],
                    new int[] {EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE}, 0);
            if (!EGL14.eglMakeCurrent(display, surface, surface, ctx)) return "";
            final String name = GLES20.glGetString(GLES20.GL_RENDERER);
            return name != null ? name : "";
        } catch (RuntimeException e) {
            Log.w(TAG, "GPU name query failed", e);
            return "";
        } finally {
            if (display != EGL14.EGL_NO_DISPLAY) {
                EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT);
                if (surface != EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(display, surface);
                if (ctx != EGL14.EGL_NO_CONTEXT) EGL14.eglDestroyContext(display, ctx);
                EGL14.eglTerminate(display);
            }
        }
    }

    /** The fastest core's top clock, from cpufreq; 0 when unreadable. */
    static int maxCpuMhz() {
        int best = 0;
        for (int i = 0; i < 16; ++i) {
            final File f = new File("/sys/devices/system/cpu/cpu" + i + "/cpufreq/cpuinfo_max_freq");
            if (!f.isFile()) continue;
            try {
                final String text = new String(Files.readAllBytes(f.toPath()), StandardCharsets.US_ASCII).trim();
                best = Math.max(best, Integer.parseInt(text) / 1000);
            } catch (Exception ignored) {
            }
        }
        return best;
    }

    static long ramMb(Context context) {
        final ActivityManager am = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
        if (am == null) return 0;
        final ActivityManager.MemoryInfo info = new ActivityManager.MemoryInfo();
        am.getMemoryInfo(info);
        return info.totalMem / (1024 * 1024);
    }
}
