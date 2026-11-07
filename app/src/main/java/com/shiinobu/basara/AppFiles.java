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

    /** The app's folder on the phone's storage, spelled exactly so (homeDir fixes another case). */
    static final String HOME = BuildConfig.BETA ? "Basara2Heroes-Beta" : "Basara2Heroes";

    /**
     * /sdcard/Basara2Heroes (Beta: Basara2Heroes-Beta): everything of the game a player handles, which
     * every file manager can open, unlike Android/data on many phones. Logs/<start time>/log and
     * /crash per game start, Saves/ for save backups, Textures/ and HD-Cache/, README.txt. Needs "All files
     * access"; without it, null (the log stays in dataDir/Logs).
     */
    static File homeDir() {
        // Android 10: the legacy storage permission; without it the folder below can't be written.
        if (android.os.Build.VERSION.SDK_INT >= 30 && !android.os.Environment.isExternalStorageManager()) return null;
        final File storage = android.os.Environment.getExternalStorageDirectory();
        final File dir = new File(storage, HOME);
        fixCase(storage, dir);
        //noinspection ResultOfMethodCallIgnored
        dir.mkdirs();
        // 1.1 and older kept an ISO/ folder for the game file; gone when empty (a dump in it stays).
        //noinspection ResultOfMethodCallIgnored
        new File(dir, "ISO").delete();
        if (!dir.isDirectory() || !dir.canWrite()) return null;
        if (!readmeWritten) {
            readmeWritten = true;
            writeIfChanged(new File(dir, "README.txt"), HOME_README);
        }
        return dir;
    }

    private static volatile boolean readmeWritten;

    /** Writes `text` (CRLF, so Windows Notepad shows the lines) unless the file already holds as much. */
    private static void writeIfChanged(File file, String text) {
        final byte[] bytes = text.replace("\n", "\r\n").getBytes(java.nio.charset.StandardCharsets.UTF_8);
        if (file.length() == bytes.length) return;
        try (java.io.FileOutputStream out = new java.io.FileOutputStream(file)) {
            out.write(bytes);
        } catch (java.io.IOException ignored) {
            // Only a help file.
        }
    }

    /** Keeps the phone's gallery out of a folder of textures. */
    private static void noMedia(File dir) {
        final File marker = new File(dir, ".nomedia");
        if (marker.exists()) return;
        try {
            //noinspection ResultOfMethodCallIgnored
            marker.createNewFile();
        } catch (java.io.IOException ignored) {
            // The gallery shows them; nothing breaks.
        }
    }

    private static final String HOME_README =
            "Basara2Heroes: what each folder is for\n"
            + "\n"
            + "Textures/  Texture mods, one folder per mod. How: Textures/README.txt.\n"
            + "HD-Cache/  Textures made sharper by HD Textures. Safe to delete: made again while playing.\n"
            + "Saves/     Save backups (game menu > Save Data).\n"
            + "Logs/      A log of each game run, for bug reports (game menu > System > Share Log).\n"
            + "\n"
            + "Your ISO or RVZ can stay anywhere (Download is fine). It is read once, the first time,\n"
            + "and can be deleted afterwards.\n"
            + "\n"
            + "--- Bahasa Indonesia ---\n"
            + "\n"
            + "Textures/  Mod tekstur, satu folder per mod. Caranya: Textures/README.txt.\n"
            + "HD-Cache/  Tekstur yang dipertajam HD Textures. Aman dihapus: dibuat lagi saat bermain.\n"
            + "Saves/     Cadangan save (menu game > Save Data).\n"
            + "Logs/      Log tiap kali game dijalankan, untuk lapor bug (menu game > System > Share Log).\n"
            + "\n"
            + "ISO atau RVZ milikmu boleh disimpan di mana saja (misalnya Download). File itu hanya dibaca\n"
            + "sekali, saat pertama kali, dan boleh dihapus sesudahnya.\n";

    private static final String TEXTURES_README =
            "Texture mods\n"
            + "\n"
            + "1. Put each mod in its own folder here, for example Textures/My Mod/ (folders inside it are fine).\n"
            + "2. Files are named the Dolphin way:\n"
            + "     tex1_<width>x<height>_<hash>_<format>.png  (or .dds)\n"
            + "     tex1_<width>x<height>_<hash>_<palette hash>_<format>.png  for palette textures;\n"
            + "     $ in place of the palette hash matches every palette.\n"
            + "   More mip levels: the same name with _mip1, _mip2 ... at the end.\n"
            + "   Dolphin packs made for RBSJ08 (Sengoku Basara 2 Heroes, Japan) work as they are.\n"
            + "3. Game menu > Graphics: turn on Custom Textures, tick the mods to use and order them with\n"
            + "   the arrows (the higher one wins when two mods change the same texture), then restart.\n"
            + "   Each mod shows how many of its files have a valid name: 0 means the names are wrong.\n"
            + "\n"
            + "Making a mod: turn on Dump Textures and play. Every texture shown is saved to Textures/Dump\n"
            + "under the name its replacement needs. Edit a copy, put it in your mod's folder, and turn\n"
            + "Dump Textures off when done. Files left directly in Textures/ show as \"Loose files\".\n"
            + "\n"
            + "--- Bahasa Indonesia ---\n"
            + "\n"
            + "1. Taruh tiap mod di foldernya sendiri di sini, misalnya Textures/Mod Saya/ (boleh ada subfolder).\n"
            + "2. Nama file mengikuti Dolphin:\n"
            + "     tex1_<lebar>x<tinggi>_<hash>_<format>.png  (atau .dds)\n"
            + "     tex1_<lebar>x<tinggi>_<hash>_<hash palette>_<format>.png  untuk tekstur palette;\n"
            + "     $ di posisi hash palette cocok untuk semua palette.\n"
            + "   Level mip tambahan: nama yang sama diakhiri _mip1, _mip2 ...\n"
            + "   Paket Dolphin untuk RBSJ08 (Sengoku Basara 2 Heroes, Jepang) bisa langsung dipakai.\n"
            + "3. Menu game > Graphics: nyalakan Custom Textures, centang mod yang dipakai dan urutkan dengan\n"
            + "   panah (yang di atas menang bila dua mod mengganti tekstur yang sama), lalu restart.\n"
            + "   Tiap mod menampilkan jumlah file yang namanya benar: 0 berarti namanya salah.\n"
            + "\n"
            + "Membuat mod: nyalakan Dump Textures lalu main. Tiap tekstur yang tampil disimpan ke Textures/Dump\n"
            + "dengan nama yang dibutuhkan penggantinya. Edit salinannya, taruh di folder mod-mu, lalu matikan\n"
            + "Dump Textures. File yang ditaruh langsung di Textures/ tampil sebagai \"Loose files\".\n";

    /**
     * Shared storage ignores case, so an older "basara2heroes" (or "hd-cache") would be used as is and
     * keep its spelling: rename it (through a temporary name, a case-only rename does nothing) to `dir`.
     */
    private static void fixCase(File storage, File dir) {
        final String[] names = storage.list();
        if (names == null) return;
        final String want = dir.getName();
        for (String name : names) {
            if (!name.equalsIgnoreCase(want) || name.equals(want)) continue;
            final File old = new File(storage, name);
            final File temp = new File(storage, want + ".renaming");
            if (old.renameTo(temp) && !temp.renameTo(dir)) {
                //noinspection ResultOfMethodCallIgnored
                temp.renameTo(old);
            }
            return;
        }
    }

    /** Logs/ under homeDir: one folder per game start, named by its start time. */
    static File logsRoot() {
        return homeSub("Logs");
    }

    private static final int KEEP_RUNS = 20;

    /** The run folder newRunLogDir made in this process, or null. */
    static volatile File currentRun;

    /**
     * In the :game process: makes Logs/<yyyy-MM-dd_HH-mm-ss>/ with log/ (console.log and other
     * files the game writes while running) and crash/ (crash reports), keeps the newest KEEP_RUNS
     * runs, and moves the files the old flat layout left at the top of homeDir into Logs/old.
     * Returns the run's log/ folder, or null without "All files access".
     */
    static File newRunLogDir() {
        final File root = logsRoot();
        if (root == null) return null;
        moveOldFlatLogs(root);
        final String stamp = new java.text.SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", Locale.ROOT)
                .format(new java.util.Date());
        File run = new File(root, stamp);
        for (int i = 2; run.exists(); i++) run = new File(root, stamp + "_" + i);
        final File log = new File(run, "log");
        //noinspection ResultOfMethodCallIgnored
        log.mkdirs();
        pruneRuns(root);
        if (!log.isDirectory()) return null;
        currentRun = run;
        return log;
    }

    /** The newest run folder under Logs/, or null. Run names sort by time. */
    static File newestRunDir() {
        final File root = logsRoot();
        final File[] runs = root == null ? null : root.listFiles(AppFiles::isRunDir);
        if (runs == null || runs.length == 0) return null;
        java.util.Arrays.sort(runs);
        return runs[runs.length - 1];
    }

    static boolean isRunDir(File f) {
        return f.isDirectory() && f.getName().matches("\\d{4}-\\d{2}-\\d{2}_\\d{2}-\\d{2}-\\d{2}(_\\d+)?");
    }

    private static void pruneRuns(File root) {
        final File[] runs = root.listFiles(AppFiles::isRunDir);
        if (runs == null || runs.length <= KEEP_RUNS) return;
        java.util.Arrays.sort(runs);
        for (int i = 0; i < runs.length - KEEP_RUNS; i++) deleteRecursively(runs[i]);
    }

    private static void moveOldFlatLogs(File root) {
        final File home = root.getParentFile();
        final File[] files = home == null ? null : home.listFiles(f -> f.isFile() && isOldLogName(f.getName()));
        if (files == null || files.length == 0) return;
        final File old = new File(root, "old");
        //noinspection ResultOfMethodCallIgnored
        old.mkdirs();
        for (File f : files) {
            //noinspection ResultOfMethodCallIgnored
            f.renameTo(new File(old, f.getName()));
        }
    }

    private static boolean isOldLogName(String name) {
        return name.equals("console.log") || name.startsWith("last_crash") || name.startsWith("crash_")
                || name.startsWith("mem1.bin") || name.startsWith("profile-stacks");
    }

    /** A folder under homeDir (made when missing), or null without "All files access". */
    static File homeSub(String name) {
        final File home = homeDir();
        if (home == null) return null;
        final File dir = new File(home, name);
        fixCase(home, dir);
        //noinspection ResultOfMethodCallIgnored
        dir.mkdirs();
        return dir.isDirectory() ? dir : null;
    }

    /** Texture mods: <homeDir>/Textures (1.1: textures; dumps in its Dump/ folder), or null without "All files access". */
    static File textureDir() {
        final File dir = homeSub("Textures");
        if (dir == null) return null;
        fixCase(dir, new File(dir, "Dump"));  // 1.1: textures/dump
        noMedia(dir);
        writeIfChanged(new File(dir, "README.txt"), TEXTURES_README);
        return dir;
    }

    /** HD Textures' upscaled textures: <homeDir>/HD-Cache (1.1: hd-cache, renamed), or null without "All files access". */
    static File hdCacheDir() {
        final File dir = homeSub("HD-Cache");
        if (dir != null) noMedia(dir);
        return dir;
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

    /**
     * The phone's folders are readable and writable: "All files access" on Android 11 and later, the
     * legacy storage permission (with requestLegacyExternalStorage) on Android 10.
     */
    static boolean hasFileAccess(Context context) {
        if (android.os.Build.VERSION.SDK_INT >= 30) return android.os.Environment.isExternalStorageManager();
        return context.checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE)
                == PackageManager.PERMISSION_GRANTED;
    }

    /** The step that tells the player how to give file access on this Android version. */
    static String fileAccessStep() {
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            return Tr.t("Turn on \"Allow access to manage all files\" for this app.");
        }
        return Tr.t("Allow access to files (storage) for this app.");
    }

    /**
     * Android 10 only: asks for the storage permission, or opens the app's settings once the player has
     * refused it for good (the dialog no longer shows). The activity resumes afterwards, so the callers'
     * "back from Settings" checks in onResume run the same way.
     */
    static void requestLegacyFileAccess(android.app.Activity activity) {
        final android.content.SharedPreferences prefs =
                activity.getSharedPreferences("file_access", Context.MODE_PRIVATE);
        final boolean askedBefore = prefs.getBoolean("asked", false);
        if (askedBefore && !activity.shouldShowRequestPermissionRationale(
                android.Manifest.permission.WRITE_EXTERNAL_STORAGE)) {
            try {
                activity.startActivity(new android.content.Intent(
                        android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                        android.net.Uri.parse("package:" + activity.getPackageName())));
                return;
            } catch (android.content.ActivityNotFoundException ignored) {
                // fall through to the dialog
            }
        }
        prefs.edit().putBoolean("asked", true).apply();
        activity.requestPermissions(new String[] {android.Manifest.permission.READ_EXTERNAL_STORAGE,
                android.Manifest.permission.WRITE_EXTERNAL_STORAGE}, 0x5A);
    }

    /** An extracted disc: sys/main.dol and the files/ tree. */
    static boolean isGameRoot(File dir) {
        return new File(dir, "sys/main.dol").isFile() && new File(dir, "files").isDirectory();
    }

    static boolean hasGameData(Context context) { return isGameRoot(gameDir(context)); }

    /** Imported, and the one game and revision this port runs. */
    static boolean gameReady(Context context) {
        return hasGameData(context) && gameProblem(gameDir(context)) == null;
    }

    /** The game's archives and their sizes on the original RBSJ08 disc. */
    private static final String[] DISC_ARCHIVES = {"linkData.afs", "linkDt01.afs", "linkDt02.afs"};
    private static final long[] DISC_ARCHIVE_SIZES = {944601088L, 1454065664L, 1879459840L};

    /**
     * How the archives at `root` differ from the original disc's, one line each, or null when they
     * match. A changed disc (a patch, a bad conversion) can still be played; this is only a warning.
     */
    static String discDifferences(File root) {
        final StringBuilder out = new StringBuilder();
        for (int i = 0; i < DISC_ARCHIVES.length; ++i) {
            final File file = new File(root, "files/" + DISC_ARCHIVES[i]);
            final long size = file.isFile() ? file.length() : -1;
            if (size == DISC_ARCHIVE_SIZES[i]) continue;
            if (out.length() > 0) out.append('\n');
            out.append(DISC_ARCHIVES[i]).append(": ")
                    .append(size < 0 ? Tr.t("missing") : size + Tr.t(" bytes"))
                    .append(Tr.t(" (original ")).append(DISC_ARCHIVE_SIZES[i]).append(')');
        }
        return out.length() > 0 ? out.toString() : null;
    }

    /** A key for the archive sizes at `root`, so a warning seen once is not shown again for them. */
    static String discSignature(File root) {
        final StringBuilder out = new StringBuilder();
        for (String name : DISC_ARCHIVES) out.append(new File(root, "files/" + name).length()).append('/');
        return out.toString();
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
                + "real_widescreen = " + settings.realWidescreen() + "\n"
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
                + "\n[cheats]\n"
                + "money = " + settings.cheatMoney() + "\n"
                + "money_amount = " + settings.cheatMoneyAmount() + "\n"
                + "money_once_seq = " + settings.cheatMoneySeq() + "\n"
                + "infinite_hp = " + settings.cheatInfiniteHp() + "\n"
                + "infinite_basara = " + settings.cheatInfiniteBasara() + "\n"
                + "unlock_all = " + settings.cheatUnlockAll() + "\n"
                + "camera_distance = " + String.format(java.util.Locale.ROOT, "%.2f", settings.cameraDistanceFactor()) + "\n"
                + "\n[input]\n"
                + "gamepad_map = \"" + settings.gamepadMap() + "\"\n"
                + "\n[network]\nenabled = false\n"
                + "\n[discord]\nenabled = false\n"
                + "\n[paths]\n"
                + "dvd_root = '" + game + "'\n";
        writeText(config, text + keptSections(config));
    }

    /** Tables of the old Config.toml the app doesn't write (the game's own: keyboard keybinds...). */
    private static final java.util.Set<String> APP_TABLES =
            new java.util.HashSet<>(java.util.Arrays.asList("video", "cheats", "input", "network", "discord", "paths"));

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

    static String readText(File file) {
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

    static void writeText(File file, String text) throws IOException {
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }
}
