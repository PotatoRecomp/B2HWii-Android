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
    }

    /** In the game process, before SDL loads the runtime: the driver it should load, if any. */
    static void applyToGame(Context context, AppSettings settings) {
        if (settings.renderer() == 1 || settings.renderer() == 2) {
            try {
                Os.setenv("BASARA_RENDERER", settings.renderer() == 1 ? "vulkan" : "gles", true);
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
