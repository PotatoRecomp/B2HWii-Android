package com.shiinobu.basara;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.Arrays;
import java.util.Date;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/**
 * Settings > Save Data: the game's save (NAND title 00010000/5242534a, "RBSJ") backed up to a .zip
 * the player picks a place for, and imported back from one. Before an import the current save is
 * zipped to SaveBackups (the last few are kept), and a running game is ended so it cannot write over it.
 */
final class SaveData {
    private static final String TAG = "Basara";
    static final int REQUEST_EXPORT = 21;
    static final int REQUEST_IMPORT = 22;

    private static final String TITLE = "NAND/title/00010000/5242534a/data";
    private static final String SAVE = "basara2w.dat";
    private static final String BANNER = "banner.bin";
    private static final long MAX_FILE = 8L << 20;
    private static final int KEEP_BACKUPS = 5;

    private SaveData() {}

    /**
     * The save folder in use. The runtime gives a different game folder its own Saves/<key>/NAND
     * (runtime/include/nand_path.h), so the one whose save was written last wins; NAND/ when none has one.
     */
    static File saveDir(Context context) {
        final File data = AppFiles.dataDir(context);
        File best = new File(data, TITLE);
        long newest = new File(best, SAVE).lastModified();
        final File[] keyed = new File(data, "Saves").listFiles(File::isDirectory);
        if (keyed != null) {
            for (File key : keyed) {
                final File dir = new File(key, TITLE);
                final long time = new File(dir, SAVE).lastModified();
                if (time > newest) {
                    newest = time;
                    best = dir;
                }
            }
        }
        return best;
    }

    static boolean hasSave(Context context) {
        return new File(saveDir(context), SAVE).isFile();
    }

    static long lastSaved(Context context) {
        return new File(saveDir(context), SAVE).lastModified();
    }

    /** Automatic backups made before an import: Basara2Heroes/Saves/Auto, else (no file access) app storage. */
    static File backupDir(Context context) {
        final File saves = AppFiles.homeSub("Saves");
        return saves != null ? new File(saves, "Auto") : new File(AppFiles.dataDir(context), "SaveBackups");
    }

    /** Where Back Up opens and Import looks first: Basara2Heroes/Saves (null without file access). */
    static File savesDir() {
        return AppFiles.homeSub("Saves");
    }

    /** Import's first folder: Saves when a backup is there, else Download (where a copied .zip lands). */
    static File importStart() {
        final File saves = savesDir();
        final File[] zips = saves == null ? null : saves.listFiles(f -> f.isFile() && isSaveFile(f));
        return zips != null && zips.length > 0 ? saves : FileBrowser.downloads();
    }

    /** What Import lists: a .zip, a bare basara2w.dat, or a Wii SD export data.bin (any .bin). */
    static boolean isSaveFile(File file) {
        final String name = file.getName().toLowerCase(Locale.ROOT);
        return name.endsWith(".zip") || name.endsWith(".bin") || name.equals(SAVE);
    }

    static void startExport(Activity activity) {
        final Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("application/zip");
        intent.putExtra(Intent.EXTRA_TITLE, "basara2heroes-save-" + stamp() + ".zip");
        activity.startActivityForResult(intent, REQUEST_EXPORT);
    }

    static void startImport(Activity activity) {
        final Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra("android.content.extra.SHOW_ADVANCED", true);
        activity.startActivityForResult(intent, REQUEST_IMPORT);
    }

    /**
     * The launcher's onActivityResult. `endGame` runs first on an import (the game must not be
     * running while its save is replaced). True when the request was one of these.
     */
    static boolean onResult(Activity activity, int requestCode, int resultCode, Intent data, Runnable endGame) {
        if (requestCode != REQUEST_EXPORT && requestCode != REQUEST_IMPORT) return false;
        if (resultCode != Activity.RESULT_OK || data == null || data.getData() == null) return true;
        run(activity, requestCode == REQUEST_EXPORT, data.getData(), null, endGame, null);
        return true;
    }

    /** Back Up into a folder chosen in the app's FileBrowser: a new dated .zip there. */
    static void exportToFolder(Activity activity, File folder, Runnable done) {
        final File zip = new File(folder, "basara2heroes-save-" + stamp() + ".zip");
        run(activity, true, Uri.fromFile(zip), zip.getPath(), null, done);
    }

    /** Import from a file chosen in the app's FileBrowser. */
    static void importFile(Activity activity, File file, Runnable endGame, Runnable done) {
        run(activity, false, Uri.fromFile(file), null, endGame, done);
    }

    /** The newest automatic backup made before an import, or null. */
    static File latestBackup(Context context) {
        final File[] all = backupDir(context).listFiles((d, name) -> name.startsWith("before-import-"));
        if (all == null || all.length == 0) return null;
        Arrays.sort(all, (a, b) -> Long.compare(b.lastModified(), a.lastModified()));
        return all[0];
    }

    /** Puts back the save the last import replaced (itself backed up first, like any import). */
    static void undoLastImport(Activity activity, Runnable endGame, Runnable done) {
        final File backup = latestBackup(activity);
        if (backup == null) return;
        // Copied out first: the import's own backup must not count as the newest one being read.
        final File copy = new File(activity.getCacheDir(), "undo-import.zip");
        try (InputStream in = new FileInputStream(backup); OutputStream out = new FileOutputStream(copy)) {
            copy(in, out);
        } catch (IOException e) {
            Log.e(TAG, "undo import failed", e);
            return;
        }
        //noinspection ResultOfMethodCallIgnored
        backup.delete();
        run(activity, false, Uri.fromFile(copy), null, endGame, done);
    }

    /** Export or import on a worker thread, then a sheet with the outcome. `shownPath`: where an export went. */
    private static void run(Activity activity, boolean export, Uri uri, String shownPath, Runnable endGame,
                            Runnable done) {
        if (!export && endGame != null) endGame.run();
        new Thread(() -> {
            String error;
            try {
                if (export) {
                    export(activity, uri);
                } else {
                    Thread.sleep(600);  // let the ended game's process go before its files are replaced
                    importFrom(activity, uri);
                }
                error = null;
            } catch (Exception e) {
                Log.e(TAG, "save " + (export ? "backup" : "import") + " failed", e);
                error = e.getMessage() != null ? e.getMessage() : e.toString();
            }
            final String failure = error;
            // Undo Last Import goes through the same import, from its cache copy.
            final boolean restore = !export && "undo-import.zip".equals(uri.getLastPathSegment());
            activity.runOnUiThread(() -> {
                final String title = failure == null
                        ? (export ? Tr.t("Save backed up") : restore ? Tr.t("Save restored") : Tr.t("Save imported"))
                        : (export ? Tr.t("Backup failed") : Tr.t("Import failed"));
                final String about = failure != null ? failure
                        : export ? (shownPath != null ? Tr.t("Saved as ") + shownPath + ". " : "")
                                + Tr.t("Keep the .zip somewhere safe. Import it here to bring the save back.")
                        : restore ? Tr.t("The save from before the last import is back. Start the game to play from it.")
                        : Tr.t("Start the game to play from the imported save. The save it replaced can be put "
                                + "back with Undo Last Import.");
                if (done != null) done.run();  // e.g. Settings > Save Data shows the new dates
                new Sheet(activity, title, about).action(Tr.t("OK"), null, true, false, null).show();
            });
        }, "save-data").start();
    }

    private static void export(Context context, Uri uri) throws IOException {
        final File dir = saveDir(context);
        if (!new File(dir, SAVE).isFile()) throw new IOException(Tr.t("There is no save yet."));
        try (OutputStream out = context.getContentResolver().openOutputStream(uri)) {
            if (out == null) throw new IOException("cannot write " + uri);
            zipSave(dir, out);
        }
    }

    /** basara2w.dat (and banner.bin when there) as a .zip at their own names. */
    private static void zipSave(File dir, OutputStream out) throws IOException {
        try (ZipOutputStream zip = new ZipOutputStream(out)) {
            for (String name : new String[] {SAVE, BANNER}) {
                final File file = new File(dir, name);
                if (!file.isFile()) continue;
                zip.putNextEntry(new ZipEntry(name));
                try (InputStream in = new FileInputStream(file)) {
                    copy(in, zip);
                }
                zip.closeEntry();
            }
        }
    }

    /** A .zip from Back Up (or any .zip with basara2w.dat in it), or a bare basara2w.dat. */
    private static void importFrom(Context context, Uri uri) throws IOException {
        byte[] save = null, banner = null;
        try (InputStream raw = context.getContentResolver().openInputStream(uri)) {
            if (raw == null) throw new IOException("cannot read " + uri);
            final byte[] head = new byte[4];
            final java.io.BufferedInputStream in = new java.io.BufferedInputStream(raw);
            in.mark(8);
            final int got = in.read(head);
            in.reset();
            if (got == 4 && head[0] == 'P' && head[1] == 'K' && head[2] == 3 && head[3] == 4) {
                try (ZipInputStream zip = new ZipInputStream(in)) {
                    for (ZipEntry entry; (entry = zip.getNextEntry()) != null; ) {
                        final String name = new File(entry.getName()).getName();
                        if (entry.isDirectory()) continue;
                        if (name.equals(SAVE)) save = readAll(zip);
                        else if (name.equals(BANNER)) banner = readAll(zip);
                        else if (name.equalsIgnoreCase("data.bin") && save == null) save = readAll(zip);
                    }
                }
            } else {
                save = readAll(in);
            }
        }
        if (save != null && isWiiDataBin(save)) {
            final byte[][] parts = fromWiiDataBin(save);
            save = parts[0];
            if (parts[1] != null) banner = parts[1];
        }
        if (save == null) throw new IOException(Tr.t("No basara2w.dat in this file: pick a backup made here."));
        if (save.length < 1024) throw new IOException(Tr.t("This is not a Sengoku Basara 2 Heroes save."));
        final File current = new File(saveDir(context), SAVE);
        if (current.isFile() && current.length() != save.length) {
            throw new IOException(String.format(Locale.ROOT,
                    Tr.t("This save is %d bytes, the game's is %d: not a save of this game."),
                    save.length, current.length()));
        }

        final File dir = saveDir(context);
        if (new File(dir, SAVE).isFile()) backUpBeforeImport(context, dir);
        if (!dir.isDirectory() && !dir.mkdirs()) throw new IOException("cannot create " + dir);
        write(new File(dir, SAVE), save);
        if (banner != null) write(new File(dir, BANNER), banner);
    }

    // Wii SD export (Data Management > copy to SD, or Dolphin's Export Wii Save): private/wii/title/RBSJ/data.bin.
    // Layout as Dolphin's Core/WiiSave.cpp: 0xF0C0 encrypted header (title id, banner), 0x80 "Bk" header,
    // then per file a 0x80 header (size, name, IV) and its data encrypted and padded to 0x40, then certificates.
    private static final int DATABIN_HEADER = 0xF0C0;
    private static final byte[] SD_KEY = {(byte) 0xab, 0x01, (byte) 0xb9, (byte) 0xd8, (byte) 0xe1, 0x62, 0x2b, 0x08,
            (byte) 0xaf, (byte) 0xba, (byte) 0xd8, 0x4d, (byte) 0xbf, (byte) 0xc2, (byte) 0xa5, 0x5d};
    private static final byte[] SD_IV = {0x21, 0x67, 0x12, (byte) 0xe6, (byte) 0xaa, 0x1f, 0x68, (byte) 0x9f,
            (byte) 0x95, (byte) 0xc5, (byte) 0xa2, 0x23, 0x24, (byte) 0xdc, 0x6a, (byte) 0x98};
    private static final long TITLE_ID = 0x000100005242534aL;

    private static boolean isWiiDataBin(byte[] bin) {
        return bin.length > DATABIN_HEADER + 0x100 && be32(bin, DATABIN_HEADER + 4) == 0x426b0001;
    }

    /** { basara2w.dat, banner.bin or null } out of a Wii data.bin. */
    private static byte[][] fromWiiDataBin(byte[] bin) throws IOException {
        final byte[] header = aes(bin, 0, DATABIN_HEADER, SD_IV);
        final long title = ((long) be32(header, 0) << 32) | (be32(header, 4) & 0xffffffffL);
        if (title != TITLE_ID) throw new IOException(Tr.t("This is not a Sengoku Basara 2 Heroes save."));
        final int bannerSize = be32(header, 8);
        final byte[] banner = bannerSize > 0 && bannerSize <= DATABIN_HEADER - 0x20
                ? Arrays.copyOfRange(header, 0x20, 0x20 + bannerSize) : null;
        final int files = be32(bin, DATABIN_HEADER + 0xc);
        int at = DATABIN_HEADER + 0x80;
        for (int i = 0; i < files && at + 0x80 <= bin.length; ++i) {
            if (be32(bin, at) != 0x03adf17e) break;
            final int size = be32(bin, at + 4);
            final int type = bin[at + 0xa];
            int end = at + 0xb;
            while (end < at + 0x50 && bin[end] != 0) ++end;
            final String name = new String(bin, at + 0xb, end - (at + 0xb), java.nio.charset.StandardCharsets.US_ASCII);
            final byte[] iv = Arrays.copyOfRange(bin, at + 0x50, at + 0x60);
            at += 0x80;
            if (type != 1) continue;  // a directory: no data follows
            final int padded = (size + 0x3f) & ~0x3f;
            if (size < 0 || at + padded > bin.length) break;
            if (name.equals(SAVE)) return new byte[][] {Arrays.copyOf(aes(bin, at, padded, iv), size), banner};
            at += padded;
        }
        throw new IOException(Tr.t("No basara2w.dat in this file: pick a backup made here."));
    }

    private static byte[] aes(byte[] src, int off, int len, byte[] iv) throws IOException {
        try {
            final javax.crypto.Cipher cipher = javax.crypto.Cipher.getInstance("AES/CBC/NoPadding");
            cipher.init(javax.crypto.Cipher.DECRYPT_MODE, new javax.crypto.spec.SecretKeySpec(SD_KEY, "AES"),
                    new javax.crypto.spec.IvParameterSpec(iv));
            return cipher.doFinal(src, off, len);
        } catch (java.security.GeneralSecurityException e) {
            throw new IOException("data.bin: " + e.getMessage());
        }
    }

    private static int be32(byte[] b, int off) {
        return ((b[off] & 0xff) << 24) | ((b[off + 1] & 0xff) << 16) | ((b[off + 2] & 0xff) << 8) | (b[off + 3] & 0xff);
    }

    private static void backUpBeforeImport(Context context, File dir) throws IOException {
        final File backups = backupDir(context);
        if (!backups.isDirectory() && !backups.mkdirs()) throw new IOException("cannot create " + backups);
        try (OutputStream out = new FileOutputStream(new File(backups, "before-import-" + stamp() + ".zip"))) {
            zipSave(dir, out);
        }
        final File[] old = backups.listFiles((d, name) -> name.startsWith("before-import-"));
        if (old != null && old.length > KEEP_BACKUPS) {
            Arrays.sort(old, (a, b) -> Long.compare(a.lastModified(), b.lastModified()));
            for (int i = 0; i < old.length - KEEP_BACKUPS; ++i) {
                //noinspection ResultOfMethodCallIgnored
                old[i].delete();
            }
        }
    }

    /** Through a temporary file and a rename, so a failure leaves the old file whole. */
    private static void write(File file, byte[] bytes) throws IOException {
        final File tmp = new File(file.getPath() + ".import.tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(bytes);
            out.getFD().sync();
        }
        if (!tmp.renameTo(file)) {
            //noinspection ResultOfMethodCallIgnored
            tmp.delete();
            throw new IOException("cannot replace " + file);
        }
    }

    private static byte[] readAll(InputStream in) throws IOException {
        final java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        final byte[] buffer = new byte[64 * 1024];
        long total = 0;
        for (int n; (n = in.read(buffer)) > 0; ) {
            total += n;
            if (total > MAX_FILE) throw new IOException(Tr.t("This file is too large to be a save."));
            out.write(buffer, 0, n);
        }
        return out.toByteArray();
    }

    private static void copy(InputStream in, OutputStream out) throws IOException {
        final byte[] buffer = new byte[64 * 1024];
        for (int n; (n = in.read(buffer)) > 0; ) out.write(buffer, 0, n);
    }

    private static String stamp() {
        return new SimpleDateFormat("yyyyMMdd-HHmm", Locale.ROOT).format(new Date());
    }
}
