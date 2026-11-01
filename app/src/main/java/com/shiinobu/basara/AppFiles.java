package com.shiinobu.basara;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/**
 * Where the game's files live on the phone.
 *
 * <ul>
 * <li>Runtime files (DSP ROM, pipeline cache, Wii bootstrap NAND, CA bundle) travel in the APK's
 * assets/runtime and are unpacked to internal storage once per install or update; the native side
 * finds them through WIICOMPILED_RUNTIME_DIR.</li>
 * <li>User data (Config.toml, NAND with the saves, caches, logs) and the player's game data
 * (game/files, game/sys) live in the app's external files directory,
 * Android/data/com.shiinobu.basara/files, which a PC can reach over USB; WIICOMPILED_DATA_DIR.</li>
 * </ul>
 */
final class AppFiles {
    private AppFiles() {}

    static File dataDir(Context context) {
        final File external = context.getExternalFilesDir(null);
        final File dir = external != null ? external : new File(context.getFilesDir(), "data");
        //noinspection ResultOfMethodCallIgnored
        dir.mkdirs();
        return dir;
    }

    /**
     * Where console.log goes: /sdcard/basara2heroes (Beta: basara2heroes-beta), which every file
     * manager can open, unlike Android/data on many phones. Needs "All files access"; without it,
     * null (the log stays in dataDir/Logs).
     */
    static File logDir() {
        if (!android.os.Environment.isExternalStorageManager()) return null;
        final File dir = new File(android.os.Environment.getExternalStorageDirectory(),
                BuildConfig.BETA ? "basara2heroes-beta" : "basara2heroes");
        //noinspection ResultOfMethodCallIgnored
        dir.mkdirs();
        return dir.isDirectory() && dir.canWrite() ? dir : null;
    }

    /** Custom textures: <logDir>/textures (dumps in its dump/ folder), or null without "All files access". */
    static File textureDir() {
        final File logs = logDir();
        if (logs == null) return null;
        final File dir = new File(logs, "textures");
        //noinspection ResultOfMethodCallIgnored
        dir.mkdirs();
        return dir.isDirectory() ? dir : null;
    }

    /** HD Textures' upscaled textures: <logDir>/hd-cache, or null without "All files access". */
    static File hdCacheDir() {
        final File logs = logDir();
        if (logs == null) return null;
        final File dir = new File(logs, "hd-cache");
        //noinspection ResultOfMethodCallIgnored
        dir.mkdirs();
        return dir.isDirectory() ? dir : null;
    }

    /** Bytes in a folder and everything under it. */
    static long folderSize(File dir) {
        final File[] children = dir == null ? null : dir.listFiles();
        if (children == null) return 0;
        long total = 0;
        for (File child : children) total += child.isDirectory() ? folderSize(child) : child.length();
        return total;
    }

    static File gameDir(Context context) { return new File(dataDir(context), "game"); }

    static File runtimeDir(Context context) { return new File(context.getFilesDir(), "runtime"); }

    /** An extracted disc: sys/main.dol and the files/ tree. */
    static boolean isGameRoot(File dir) {
        return new File(dir, "sys/main.dol").isFile() && new File(dir, "files").isDirectory();
    }

    static boolean hasGameData(Context context) { return isGameRoot(gameDir(context)); }

    /** Imported, and the one game and revision this port runs. */
    static boolean gameReady(Context context) {
        return hasGameData(context) && gameProblem(gameDir(context)) == null;
    }

    /** Why the extracted game at `root` can't be played, or null when it can. */
    static String gameProblem(File root) {
        if (!isGameRoot(root)) {
            return "Its files are incomplete. Choose your ISO or RVZ again.";
        }
        final byte[] header = new byte[8];
        try (InputStream in = new FileInputStream(new File(root, "sys/boot.bin"))) {
            if (readFully(in, header) < header.length) {
                return "Its sys/boot.bin is too short, so the game can't be identified.";
            }
        } catch (IOException e) {
            return "It has no sys/boot.bin, so the game can't be identified.";
        }
        return headerProblem(header);
    }

    /**
     * The first 8 bytes of sys/boot.bin name the game (6 characters) and its revision (byte 7).
     * This port runs only the USA disc, revision 1: other revisions put the game's code elsewhere.
     */
    static String headerProblem(byte[] header) {
        final String id = new String(header, 0, 6, StandardCharsets.US_ASCII);
        if (!id.equals("RBSJ08")) {
            return "That is " + id.trim() + ", not Sengoku Basara 2 Heroes (RBSJ08). "
                    + "This port runs only the Japanese disc.";
        }
        return null;
    }

    static int readFully(InputStream in, byte[] buffer) throws IOException {
        int total = 0;
        while (total < buffer.length) {
            final int read = in.read(buffer, total, buffer.length - total);
            if (read <= 0) {
                break;
            }
            total += read;
        }
        return total;
    }

    /** Unpacks assets/runtime when this APK has not done it yet. */
    static void prepareRuntime(Context context) throws IOException {
        final File dir = runtimeDir(context);
        final File stampFile = new File(dir, ".apk-stamp");
        final String stamp = apkStamp(context);
        if (stamp.equals(readText(stampFile))) {
            return;
        }
        deleteRecursively(dir);
        if (!dir.mkdirs() && !dir.isDirectory()) {
            throw new IOException("cannot create " + dir);
        }
        copyAssetTree(context.getAssets(), "runtime", dir);
        writeText(stampFile, stamp);
    }

    /**
     * Writes Config.toml from the settings before every start. The runtime reads it once at boot;
     * changes made while playing go through JNI as well and are saved here the next time.
     */
    static void writeConfig(Context context, AppSettings settings) throws IOException {
        final File config = new File(dataDir(context), "Config.toml");
        final String game = gameDir(context).getAbsolutePath().replace('\\', '/');
        final String text = "# Written by the app before every start; change settings in the game menu.\n"
                + "\n[video]\n"
                + "aspect_mode = " + settings.aspectMode() + "\n"
                + "gpu_compat = " + settings.gpuCompat() + "\n"
                + "frameskip = " + (settings.fpsLock30() ? 2 : settings.frameskip() ? 1 : 0) + "\n"
                + "fsr = " + settings.fsr() + "\n"
                + "texture_filter = " + settings.textureFilter() + "\n"
                + "resolution_multiplier = " + String.format(Locale.ROOT, "%.2f", settings.resolutionScale()) + "\n"
                + "frame_interpolation_fps = 0\n"
                + "graphics_api = \"auto\"\n"
                + "skip_unready_pipelines = true\n"
                + "disable_copy_filter = true\n"
                + "show_fps = " + settings.showFps() + "\n"
                + "spinning_coins = " + settings.spinningCoins() + "\n"
                + "skip_boot = " + settings.skipBoot() + "\n"
                + "gx_thread = " + settings.gxThread() + "\n"
                + "dynamic_resolution = " + settings.dynamicResolution() + "\n"
                + "thermal_control = " + settings.thermalControl() + "\n"
                + "frame_graph = " + settings.frameGraph() + "\n"
                + "overlay_items = " + settings.overlayItems() + "\n"
                + "vsync = " + settings.vsync() + "\n"
                + "texture_replacements = " + settings.customTextures() + "\n"
                + "texture_dumps = " + (settings.customTextures() && settings.dumpTextures()) + "\n"
                + "hd_textures = " + settings.hdTextures() + "\n"
                + "hd_cache_mb = " + settings.hdCacheMb() + "\n"
                + "texture_key_report = " + settings.textureKeyReport() + "\n"
                + (hasTable(config, "audio") ? "" : "\n[audio]\n"
                + "volume = 1.0\n"
                + "muted = false\n"
                + "attenuate_music_when_media_plays = false\n"
                + "mix_worker = true\n")
                + "\n[network]\nenabled = false\n"
                + "\n[discord]\nenabled = false\n"
                + "\n[paths]\n"
                + "dvd_root = '" + game + "'\n";
        writeText(config, text + keptSections(config));
    }

    /** Tables of the old Config.toml the app doesn't write (the game's own: keyboard keybinds...). */
    private static final java.util.Set<String> APP_TABLES =
            new java.util.HashSet<>(java.util.Arrays.asList("video", "network", "discord", "paths"));

    /** Whether the old Config.toml has [name]: the game's Audio settings live in [audio], so an
     *  existing one is kept (keptSections) rather than reset to the defaults. */
    private static boolean hasTable(File config, String name) {
        if (!config.isFile()) return false;
        try {
            for (String line : java.nio.file.Files.readAllLines(config.toPath(), java.nio.charset.StandardCharsets.UTF_8)) {
                if (line.trim().equals("[" + name + "]")) return true;
            }
        } catch (IOException e) {
            android.util.Log.w("Basara", "could not read the old Config.toml", e);
        }
        return false;
    }

    private static String keptSections(File config) {
        if (!config.isFile()) return "";
        final StringBuilder kept = new StringBuilder();
        try (java.io.BufferedReader in = new java.io.BufferedReader(new java.io.InputStreamReader(
                new java.io.FileInputStream(config), java.nio.charset.StandardCharsets.UTF_8))) {
            boolean keep = false;
            String line;
            while ((line = in.readLine()) != null) {
                final String trimmed = line.trim();
                if (trimmed.startsWith("[") && trimmed.endsWith("]")) {
                    final String name = trimmed.substring(1, trimmed.length() - 1).trim();
                    keep = !APP_TABLES.contains(name);
                    if (keep) kept.append('\n');
                }
                if (keep) kept.append(line).append('\n');
            }
        } catch (IOException e) {
            android.util.Log.w("Basara", "could not read the old Config.toml", e);
        }
        return kept.toString();
    }

    private static String apkStamp(Context context) {
        try {
            final PackageInfo info = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
            return info.getLongVersionCode() + ":" + info.lastUpdateTime;
        } catch (PackageManager.NameNotFoundException e) {
            return "unknown";
        }
    }

    /** Copies an asset directory recursively. AssetManager.list() is empty for a file. */
    private static void copyAssetTree(AssetManager assets, String path, File target) throws IOException {
        final String[] children = assets.list(path);
        if (children == null || children.length == 0) {
            try (InputStream in = assets.open(path); OutputStream out = new FileOutputStream(target)) {
                copy(in, out);
            }
            return;
        }
        if (!target.mkdirs() && !target.isDirectory()) {
            throw new IOException("cannot create " + target);
        }
        for (String child : children) {
            copyAssetTree(assets, path + "/" + child, new File(target, child));
        }
    }

    static void copy(InputStream in, OutputStream out) throws IOException {
        final byte[] buffer = new byte[1 << 16];
        int read;
        while ((read = in.read(buffer)) > 0) {
            out.write(buffer, 0, read);
        }
    }

    static void deleteRecursively(File file) {
        final File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteRecursively(child);
            }
        }
        //noinspection ResultOfMethodCallIgnored
        file.delete();
    }

    private static String readText(File file) {
        if (!file.isFile()) {
            return "";
        }
        try (InputStream in = new FileInputStream(file)) {
            final byte[] bytes = new byte[(int) Math.min(file.length(), 4096)];
            final int read = in.read(bytes);
            return read > 0 ? new String(bytes, 0, read, StandardCharsets.UTF_8) : "";
        } catch (IOException e) {
            return "";
        }
    }

    private static void writeText(File file, String text) throws IOException {
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }
}
