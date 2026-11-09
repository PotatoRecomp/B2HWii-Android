package com.shiinobu.basara;

import android.content.Context;
import android.os.Build;
import android.system.Os;
import android.util.Log;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Player-installed Vulkan drivers: adrenotools driver packs (a .zip with meta.json and the driver's .so,
 * such as Turnip), unpacked into the app's private files (the loader refuses shared storage) and handed
 * to the game through BASARA_GPU_DRIVER (aurora/lib/webgpu/custom_driver.cpp).
 *
 * A start with a driver leaves a marker file that the game removes after 20 s of running. A marker
 * still there at the next start (the game died with that driver), or a reason the game wrote to
 * marker.failed, sends the setting back to the system driver.
 */
final class GpuDrivers {
    private static final String TAG = "GpuDrivers";
    private static final long MAX_ZIP_ENTRY = 256L << 20;

    static final class Driver {
        final String id;
        final String name;
        final String description;
        final String author;
        final String version;
        final String library;
        final File dir;

        Driver(String id, String name, String description, String author, String version, String library, File dir) {
            this.id = id;
            this.name = name;
            this.description = description;
            this.author = author;
            this.version = version;
            this.library = library;
            this.dir = dir;
        }

        File lib() { return new File(dir, library); }

        String label() { return version.isEmpty() ? name : name + " " + version; }
    }

    private GpuDrivers() {}

    static File root(Context context) { return new File(context.getFilesDir(), "gpu_drivers"); }

    private static File marker(Context context) { return new File(root(context), "trying"); }

    private static File failedFile(Context context) { return new File(root(context), "trying.failed"); }

    /** Left by a start with a Renderer other than Auto, removed after 20 s of running (rendererStarted). */
    private static File rendererMarker(Context context) { return new File(context.getFilesDir(), "renderer_trying"); }

    /** Left by the Vulkan renderer when the GPU stopped responding (VK_ERROR_DEVICE_LOST). */
    static File gpuLostFile(Context context) { return new File(context.getFilesDir(), "gpu_lost"); }

    /**
     * The last game closed because the GPU stopped responding (a hang under too much work a frame):
     * Render Resolution one step lower, so the next start does not hang the same way. Null if not.
     */
    static String checkGpuLost(Context context, AppSettings settings) {
        final File lost = gpuLostFile(context);
        if (!lost.isFile()) return null;
        lost.delete();
        final float[] scales = AppSettings.RESOLUTION_SCALES;
        final float scale = settings.resolutionScale();
        float lower = scale;
        for (float s : scales) {
            if (s >= 0.5f && s < scale - 0.01f) lower = s;  // the largest step below, never Auto (0)
        }
        if (lower >= scale - 0.01f) {
            return Tr.t("The GPU stopped responding and the game closed. A lower Render Resolution or Renderer may help.");
        }
        settings.setResolutionScale(lower);
        return Tr.t("The GPU stopped responding and the game closed. Render Resolution lowered to ")
                + (lower == (int) lower ? String.valueOf((int) lower) : String.valueOf(lower)) + "x.";
    }

    /** Phones the loader works on: arm64 with Android 9+ (this app needs 11 anyway). */
    static boolean supported() {
        for (String abi : Build.SUPPORTED_64_BIT_ABIS) {
            if ("arm64-v8a".equals(abi)) return true;
        }
        return false;
    }

    static List<Driver> installed(Context context) {
        final List<Driver> list = new ArrayList<>();
        final File[] dirs = root(context).listFiles(File::isDirectory);
        if (dirs == null) return list;
        java.util.Arrays.sort(dirs);
        for (File dir : dirs) {
            final Driver driver = read(dir);
            if (driver != null) list.add(driver);
        }
        return list;
    }

    static Driver find(Context context, String id) {
        if (id == null || id.isEmpty()) return null;
        final Driver driver = read(new File(root(context), id));
        return driver != null && driver.id.equals(id) ? driver : null;
    }

    private static Driver read(File dir) {
        try {
            final JSONObject meta = new JSONObject(new String(readAll(new FileInputStream(new File(dir, "meta.json"))),
                    StandardCharsets.UTF_8));
            final String library = meta.optString("libraryName", "");
            if (library.isEmpty() || !new File(dir, library).isFile()) return null;
            return new Driver(dir.getName(), meta.optString("name", dir.getName()), meta.optString("description", ""),
                    meta.optString("author", ""), meta.optString("driverVersion", ""), library, dir);
        } catch (Exception e) {
            return null;
        }
    }

    /** Unpacks a driver pack; the message of the IOException says what is wrong with the file. */
    static Driver install(Context context, File zip) throws IOException {
        final Map<String, byte[]> files = new HashMap<>();
        try (ZipInputStream in = new ZipInputStream(new FileInputStream(zip))) {
            ZipEntry entry;
            while ((entry = in.getNextEntry()) != null) {
                if (entry.isDirectory()) continue;
                final String name = new File(entry.getName()).getName();
                if (name.isEmpty() || name.startsWith(".")) continue;
                files.put(name, readLimited(in));
            }
        } catch (IOException e) {
            throw new IOException(Tr.t("It is not a .zip file that can be read."));
        }
        final byte[] metaBytes = files.get("meta.json");
        if (metaBytes == null) {
            throw new IOException(Tr.t("It has no meta.json, so it is not a driver pack (like the Turnip .zip "
                    + "files made for Dolphin or Yuzu)."));
        }
        final JSONObject meta;
        try {
            meta = new JSONObject(new String(metaBytes, StandardCharsets.UTF_8));
        } catch (Exception e) {
            throw new IOException(Tr.t("Its meta.json cannot be read."));
        }
        final String library = meta.optString("libraryName", "");
        if (library.isEmpty() || !files.containsKey(library)) {
            throw new IOException(Tr.t("The driver library its meta.json names is not in the .zip."));
        }
        final int minApi = meta.optInt("minApi", 0);
        if (minApi > Build.VERSION.SDK_INT) {
            throw new IOException(Tr.t("It needs a newer Android (API ") + minApi + ").");
        }
        final String name = meta.optString("name", library);
        final String version = meta.optString("driverVersion", "");
        String id = (name + "-" + version).toLowerCase(Locale.ROOT).replaceAll("[^a-z0-9._-]+", "_");
        if (id.length() > 64) id = id.substring(0, 64);
        if (id.isEmpty() || id.startsWith(".") || id.equals("trying")) id = "driver";

        final File root = root(context);
        final File dir = new File(root, id);
        final File temp = new File(root, ".installing");
        AppFiles.deleteRecursively(temp);
        if (!temp.mkdirs()) throw new IOException(Tr.t("Could not make its folder."));
        for (Map.Entry<String, byte[]> file : files.entrySet()) {
            try (OutputStream out = new FileOutputStream(new File(temp, file.getKey()))) {
                out.write(file.getValue());
            }
        }
        AppFiles.deleteRecursively(dir);
        if (!temp.renameTo(dir)) throw new IOException(Tr.t("Could not make its folder."));
        final Driver driver = read(dir);
        if (driver == null) throw new IOException(Tr.t("Its files could not be read back."));
        return driver;
    }

    static void remove(Context context, Driver driver) {
        AppFiles.deleteRecursively(driver.dir);
    }

    /**
     * Before a start (or when the launcher shows): a driver that failed or crashed the last start goes
     * back to the system driver. Returns why, for a message, or null.
     */
    static String checkLastStart(Context context, AppSettings settings) {
        // A Renderer choice that closed the game while it started goes back to Auto, so a renderer that
        // cannot run on this phone never locks the player out of the menu that changes it.
        String rendererOff = null;
        if (rendererMarker(context).isFile()) {
            rendererMarker(context).delete();
            if (settings.renderer() != 0) {
                rendererOff = Tr.t("Renderer ") + AppSettings.rendererName(settings.renderer())
                        + Tr.t(" closed the game while it was starting, so it is back to Auto.");
                settings.setRenderer(0);
            }
        }
        final String driverOff = checkDriverLastStart(context, settings);
        if (rendererOff == null) return driverOff;
        return driverOff == null ? rendererOff : rendererOff + '\n' + driverOff;
    }

    private static String checkDriverLastStart(Context context, AppSettings settings) {
        String reason = null;
        final File failed = failedFile(context);
        if (failed.isFile()) {
            try {
                reason = new String(readAll(new FileInputStream(failed)), StandardCharsets.UTF_8).trim();
            } catch (IOException ignored) {
                reason = "";
            }
            failed.delete();
        } else if (marker(context).isFile()) {
            reason = Tr.t("the game closed while it was starting with this driver");
        }
        marker(context).delete();
        if (reason == null || settings.gpuDriver().isEmpty()) return null;
        final Driver driver = find(context, settings.gpuDriver());
        settings.setGpuDriver("");
        return (driver != null ? driver.label() : Tr.t("The GPU driver")) + Tr.t(" was turned off, back to the "
                + "system driver: ") + reason;
    }

    /** The game was ended on purpose (Quit, Restart, a new game file): not the driver's fault. */
    static void endedOnPurpose(Context context) {
        marker(context).delete();
        rendererMarker(context).delete();
    }

    /**
     * Renderer WebGPU 1.0 runs the 1.0 renderer (aurora10/, built into libmain10.so). 1.0 had no
     * support for PowerVR, Adreno 5xx on Android 10 or Android 10 drivers (1.1 added it), so those
     * phones keep libmain.so and Auto.
     */
    static boolean renderer10Usable(AppSettings settings) {
        return Build.VERSION.SDK_INT >= 30 && !DeviceProfile.powerVr(settings.gpuName())
                && !DeviceProfile.oldAdreno5(settings.gpuName());
    }

    /** The Renderer setting; testing over adb: setprop debug.basara.renderer 0-4 overrides it. */
    static int renderer(AppSettings settings) {
        try {
            return Integer.parseInt(Updater.debugProperty("debug.basara.renderer", ""));
        } catch (NumberFormatException e) {
            return settings.renderer();
        }
    }

    /**
     * What a driver offered on its last start: caps.txt beside it, written by the game
     * (aurora/lib/webgpu/custom_driver.cpp): the probe's guess (vulkan=, dawn=) and how each renderer
     * that was tried on it went (vulkan_run=, dawn_run=, and dawn10_run= for WebGPU 1.0: ok or fail).
     */
    static Map<String, String> caps(Driver driver) {
        final Map<String, String> caps = new HashMap<>();
        try {
            final String text = new String(readAll(new FileInputStream(new File(driver.dir, "caps.txt"))),
                    StandardCharsets.UTF_8);
            for (String line : text.split("\n")) {
                final int eq = line.indexOf('=');
                if (eq > 0) caps.put(line.substring(0, eq), line.substring(eq + 1).trim());
            }
        } catch (IOException ignored) {
            // Not started with it yet: nothing known.
        }
        return caps;
    }

    /** A renderer runs on the driver: a real start decides, the probe's guess otherwise. */
    private static boolean runs(Map<String, String> caps, String run, String probe, boolean unknown) {
        final String outcome = caps.get(run);
        if (outcome != null) return outcome.equals("ok");
        final String guess = caps.get(probe);
        return guess != null ? guess.equals("1") : unknown;
    }

    /**
     * Whether Renderer `which` (AppSettings.RENDERER_ORDER values) can run with the GPU driver chosen in
     * the settings. A player driver is Vulkan only, so OpenGL ES never runs on it, and each Vulkan
     * renderer is offered only when it runs on that driver: the game works on the active driver alone.
     * Before the first start with a driver, malisx2 (Mali) is known not to pass Dawn's adapter rules.
     */
    static boolean rendererRuns(Context context, AppSettings settings, int which) {
        if (which == 4 && !renderer10Usable(settings)) return false;
        final Driver driver = find(context, settings.gpuDriver());
        if (driver == null || which == 0) return true;
        if (which == 2) return false;
        final Map<String, String> caps = caps(driver);
        final boolean dawnGuess = !driver.library.toLowerCase(Locale.ROOT).contains("mali");
        if (which == 1) return runs(caps, "vulkan_run", "vulkan", true);
        if (which == 3) return runs(caps, "dawn_run", "dawn", dawnGuess);
        return runs(caps, "dawn10_run", "dawn", dawnGuess);
    }

    /** The renderer this start uses: the setting, or Auto when it cannot run on the chosen driver. */
    static int effectiveRenderer(Context context, AppSettings settings) {
        final int which = renderer(settings);
        return rendererRuns(context, settings, which) ? which : 0;
    }

    /** True when the game should load libmain10.so (Renderer WebGPU 1.0, and the APK has it). */
    static boolean useRenderer10(Context context, AppSettings settings) {
        return effectiveRenderer(context, settings) == 4
                && new File(context.getApplicationInfo().nativeLibraryDir, "libmain10.so").exists();
    }

    /** In the game process, before SDL loads the runtime: the driver it should load, if any. */
    static void applyToGame(Context context, AppSettings settings) {
        final int renderer = effectiveRenderer(context, settings);
        if (renderer != renderer(settings)) {
            Log.i(TAG, "renderer " + AppSettings.rendererName(renderer(settings)) + " does not run on the GPU "
                    + "driver " + settings.gpuDriver() + ": using Auto");
        }
        final File rendererMarker = rendererMarker(context);
        if (renderer != 0) {
            try {
                AppFiles.writeText(rendererMarker, Integer.toString(renderer));
                new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(rendererMarker::delete,
                        20_000);
            } catch (Exception e) {
                Log.e(TAG, "could not mark the renderer start", e);
            }
        }
        if (renderer >= 1 && renderer <= 4) {
            try {
                // WebGPU 1.0 runs the 1.0 renderer, which starts on Dawn as 1.0 did.
                Os.setenv("BASARA_RENDERER", renderer == 1 ? "vulkan" : renderer == 2 ? "gles" : "dawn", true);
            } catch (Exception e) {
                Log.e(TAG, "could not choose the renderer", e);
            }
        } else if (settings.gpuDriver().isEmpty() && Os.getenv("BASARA_RENDERER") == null
                && DeviceProfile.oldAdreno5(settings.gpuName())) {
            // Android 10 Adreno 5xx only (DeviceProfile.oldAdreno5): OpenGL ES first, Vulkan after it.
            try {
                Os.setenv("BASARA_RENDERER", "gles", true);
            } catch (Exception e) {
                Log.e(TAG, "could not choose the renderer", e);
            }
        } else if (settings.gpuDriver().isEmpty() && Os.getenv("BASARA_RENDERER") == null
                && DeviceProfile.powerVr(settings.gpuName())) {
            // PowerVR (DeviceProfile.powerVr): aurora's Vulkan backend (tier 0) first, Dawn after it.
            try {
                Os.setenv("BASARA_RENDERER", "vulkan", true);
            } catch (Exception e) {
                Log.e(TAG, "could not choose the renderer", e);
            }
        }
        final Driver driver = find(context, settings.gpuDriver());
        if (driver == null) {
            if (!settings.gpuDriver().isEmpty()) settings.setGpuDriver("");
            return;
        }
        final File hooks = new File(AppFiles.runtimeDir(context), "gpu_hooks");
        if (!new File(hooks, "libmain_hook.so").isFile()) {
            Log.w(TAG, "the GPU driver hooks are missing from this APK");
            return;
        }
        try {
            final File marker = marker(context);
            AppFiles.writeText(marker, driver.id);
            Os.setenv("BASARA_GPU_DRIVER", driver.lib().getAbsolutePath(), true);
            Os.setenv("BASARA_GPU_HOOK_DIR", hooks.getAbsolutePath(), true);
            Os.setenv("BASARA_GPU_DRIVER_MARKER", marker.getAbsolutePath(), true);
            // Turnip on Adreno 610 (2026-11-07, Snapdragon 680 + MrPurple T30): in tiled (GMEM) rendering
            // whole layers drop out, frames go black or flat, shapes stretch; the same driver is clean on
            // Adreno 618. Its GMEM is small, so a frame is many tiles: direct rendering avoids both.
            if (DeviceProfile.adreno610(settings.gpuName()) && Os.getenv("TU_DEBUG") == null) {
                Os.setenv("TU_DEBUG", "sysmem", true);
            }
        } catch (Exception e) {
            Log.e(TAG, "could not pass the GPU driver to the game", e);
        }
    }

    private static byte[] readLimited(InputStream in) throws IOException {
        final ByteArrayOutputStream out = new ByteArrayOutputStream();
        final byte[] buffer = new byte[64 * 1024];
        long total = 0;
        int read;
        while ((read = in.read(buffer)) > 0) {
            total += read;
            if (total > MAX_ZIP_ENTRY) throw new IOException("entry too large");
            out.write(buffer, 0, read);
        }
        return out.toByteArray();
    }

    private static byte[] readAll(InputStream stream) throws IOException {
        try (InputStream in = stream) {
            return readLimited(in);
        }
    }
}
