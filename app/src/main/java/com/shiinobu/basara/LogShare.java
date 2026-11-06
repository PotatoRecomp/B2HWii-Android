package com.shiinobu.basara;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.graphics.Typeface;
import android.net.Uri;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/**
 * Developer > Share Log: the game runs under Logs/ in a list (start time, length, crashed or not), the
 * ones picked go into one zip with the device info and Config.toml, sent with Android's share sheet
 * (Discord, Telegram, WhatsApp, Drive, e-mail...). The run that crashed is picked by default, not simply
 * the newest: a tester often starts the game again after a crash, which would push the crashed run down.
 */
final class LogShare {
    private LogShare() {}

    /** Files bigger than this (memory dumps) are left out of the zip and listed in skipped.txt. */
    private static final long MAX_FILE = 8L << 20;
    /** Discord's upload limit without Nitro: a bigger zip gets a warning. */
    private static final long DISCORD_LIMIT = 10L << 20;

    static final class Run {
        final File dir;
        final long start, end;
        final boolean crashed;
        /** The exit reason from crash/last_crash.txt ("native crash", "signaled", "low memory"), or null. */
        final String reason;

        Run(File dir, long start, long end, String reason) {
            this.dir = dir;
            this.start = start;
            this.end = end;
            this.reason = reason;
            this.crashed = reason != null && (reason.contains("crash") || reason.contains("anr"));
        }

        boolean lowMemory() { return "low memory".equals(reason); }
    }

    /** The runs under Logs/, newest first. */
    static List<Run> runs() {
        final List<Run> list = new ArrayList<>();
        final File root = AppFiles.logsRoot();
        final File[] dirs = root == null ? null : root.listFiles(AppFiles::isRunDir);
        if (dirs == null) return list;
        java.util.Arrays.sort(dirs, (a, b) -> b.getName().compareTo(a.getName()));
        final java.text.SimpleDateFormat format = new java.text.SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", Locale.ROOT);
        for (File dir : dirs) {
            long start = dir.lastModified();
            try {
                start = format.parse(dir.getName().substring(0, 19)).getTime();
            } catch (Exception ignored) {
            }
            list.add(new Run(dir, start, Math.max(start, newest(dir)), exitReason(new File(dir, "crash"))));
        }
        return list;
    }

    /**
     * The "reason:" line of the run's crash report; "crash" for a report without one; null: no report.
     * Only the .txt reports are read, and only their start: the memory dumps next to them (mem1.bin and
     * 128 MB of mem2, mostly zeros, so one endless "line") ran the launcher out of memory.
     */
    private static String exitReason(File crash) {
        final File[] files = crash.listFiles(File::isFile);
        if (files == null || files.length == 0) return null;
        for (File f : files) {
            if (!f.getName().endsWith(".txt")) continue;
            final byte[] head = new byte[4096];
            int n = 0;
            try (InputStream in = new FileInputStream(f)) {
                for (int got; n < head.length && (got = in.read(head, n, head.length - n)) > 0; ) n += got;
            } catch (IOException ignored) {
                continue;
            }
            for (String line : new String(head, 0, n, StandardCharsets.UTF_8).split("\n")) {
                line = line.trim();
                // The runtime's own report (crash_sigsegv.txt...): its reason is a crash of some kind.
                if (line.startsWith("[runtime] reason: ")) return "crash: " + line.substring(18);
                if (line.startsWith("reason: ")) return line.substring(8).replaceAll(" \\(\\d+\\)$", "");
            }
        }
        return "crash";
    }

    private static long newest(File dir) {
        final File[] files = dir.listFiles();
        long best = 0;
        if (files == null) return best;
        for (File f : files) best = Math.max(best, f.isDirectory() ? newest(f) : f.lastModified());
        return best;
    }

    /** "Today, 10:12", "Yesterday, 10:12" or "2 Nov, 10:12", in the phone's language. */
    static String when(Run run) {
        final java.util.Calendar day = java.util.Calendar.getInstance();
        final java.util.Calendar then = java.util.Calendar.getInstance();
        then.setTimeInMillis(run.start);
        final String time = new java.text.SimpleDateFormat("HH:mm", Locale.getDefault()).format(then.getTime());
        if (sameDay(day, then)) return Tr.t("Today") + ", " + time;
        day.add(java.util.Calendar.DAY_OF_YEAR, -1);
        if (sameDay(day, then)) return Tr.t("Yesterday") + ", " + time;
        return new java.text.SimpleDateFormat("d MMM", Locale.getDefault()).format(then.getTime()) + ", " + time;
    }

    private static boolean sameDay(java.util.Calendar a, java.util.Calendar b) {
        return a.get(java.util.Calendar.YEAR) == b.get(java.util.Calendar.YEAR)
                && a.get(java.util.Calendar.DAY_OF_YEAR) == b.get(java.util.Calendar.DAY_OF_YEAR);
    }

    /** "29 min played · Crashed" */
    static String describe(Run run) {
        final long minutes = Math.max(0, (run.end - run.start) / 60000);
        final String length = minutes < 1 ? Tr.t("Under 1 min played") : minutes + " " + Tr.t("min played");
        final String end = run.crashed ? Tr.t("Crashed")
                : run.lowMemory() ? Tr.t("Closed: phone ran out of memory")
                : Tr.t("Closed normally");
        return length + " · " + end;
    }

    // --- device info ------------------------------------------------------------------------------

    private static String gpu;

    /** Phone, chip, Android, GPU, RAM and the app's version, one per line. */
    static String deviceInfo(Context context) {
        if (gpu == null) gpu = DeviceProfile.gpuName();
        final android.content.pm.PackageManager pm = context.getPackageManager();
        String vulkan = "?";
        for (android.content.pm.FeatureInfo f : pm.getSystemAvailableFeatures()) {
            if (android.content.pm.PackageManager.FEATURE_VULKAN_HARDWARE_VERSION.equals(f.name)) {
                vulkan = (f.version >> 22) + "." + ((f.version >> 12) & 0x3ff);
            }
        }
        final AppSettings settings = new AppSettings(context);
        return CrashReport.header(context)
                + "gpu: " + (gpu.isEmpty() ? "?" : gpu) + ", vulkan " + vulkan + "\n"
                + "gpu driver: " + (settings.gpuDriver().isEmpty() ? "system" : settings.gpuDriver())
                + ", renderer " + AppSettings.rendererName(settings.renderer()).toLowerCase(java.util.Locale.ROOT) + "\n";
    }

    static void copyDeviceInfo(Context context) {
        final ClipboardManager clip = (ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);
        if (clip == null) return;
        clip.setPrimaryClip(ClipData.newPlainText("Device info", deviceInfo(context)));
        Toast.makeText(context, Tr.t("Copied."), Toast.LENGTH_SHORT).show();
    }

    // --- the picker -------------------------------------------------------------------------------

    static void show(Activity activity) {
        final List<Run> runs = runs();
        if (runs.isEmpty()) {
            new Sheet(activity, Tr.t("Share Log"), Tr.t("No game runs in Logs yet. Play once, then come back."))
                    .action(Tr.t("OK"), null, true, false, null).show();
            return;
        }
        final boolean[] picked = new boolean[runs.size()];
        int first = -1;
        for (int i = 0; i < runs.size() && first < 0; ++i) if (runs.get(i).crashed) first = i;
        for (int i = 0; i < runs.size() && first < 0; ++i) if (runs.get(i).lowMemory()) first = i;
        if (first < 0) first = 0;
        picked[first] = true;
        final LinearLayout list = new LinearLayout(activity);
        list.setOrientation(LinearLayout.VERTICAL);
        final Runnable[] draw = new Runnable[1];
        draw[0] = () -> {
            list.removeAllViews();
            for (int i = 0; i < runs.size(); ++i) {
                final int index = i;
                final Run run = runs.get(i);
                final LinearLayout row = new LinearLayout(activity);
                row.setGravity(Gravity.CENTER_VERTICAL);
                row.setPadding(Ui.dp(activity, 12), Ui.dp(activity, 10), Ui.dp(activity, 12), Ui.dp(activity, 10));
                row.setBackground(Ui.touchBox(activity, picked[i] ? Ui.ACCENT_SUBTLE : Ui.BG,
                        picked[i] ? Ui.ACCENT : Ui.BORDER, 10));
                final LinearLayout words = new LinearLayout(activity);
                words.setOrientation(LinearLayout.VERTICAL);
                final TextView title = Ui.text(activity, when(run), 15, Ui.TEXT);
                title.setTypeface(Typeface.DEFAULT_BOLD);
                words.addView(title);
                words.addView(Ui.text(activity, describe(run), 13,
                        run.crashed ? Sheet.WARNING_TEXT : run.lowMemory() ? Ui.WARNING : Ui.SECONDARY));
                row.addView(words, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
                row.addView(Ui.toggle(activity, picked[i]));
                row.setClickable(true);
                row.setOnClickListener(v -> {
                    picked[index] = !picked[index];
                    draw[0].run();
                });
                list.addView(row, Ui.matchWidth(i == 0 ? 0 : Ui.dp(activity, 6)));
            }
        };
        draw[0].run();
        new Sheet(activity, Tr.t("Share Log"), Tr.t("Each one is a time you played. Pick the one with the "
                + "problem (a crash is picked already), then send it to Discord or any app."))
                .view(list)
                .action(Tr.t("Share"), null, true, false, () -> {
                    final List<Run> chosen = new ArrayList<>();
                    for (int i = 0; i < runs.size(); ++i) if (picked[i]) chosen.add(runs.get(i));
                    if (chosen.isEmpty()) {
                        Toast.makeText(activity, Tr.t("Pick at least one run."), Toast.LENGTH_SHORT).show();
                        return;
                    }
                    share(activity, chosen);
                })
                .action(Tr.t("Cancel"), null, false, false, null)
                .pinActions()
                .show();
    }

    private static void share(Activity activity, List<Run> runs) {
        Toast.makeText(activity, Tr.t("Preparing the log..."), Toast.LENGTH_SHORT).show();
        final Context app = activity.getApplicationContext();
        new Thread(() -> {
            try {
                final File zip = makeZip(app, runs);
                activity.runOnUiThread(() -> {
                    if (zip.length() > DISCORD_LIMIT) {
                        Toast.makeText(activity, String.format(Locale.ROOT, Tr.t("The log is %.1f MB, over "
                                + "Discord's 10 MB: pick fewer runs for Discord."), zip.length() / 1048576.0),
                                Toast.LENGTH_LONG).show();
                    }
                    final Uri uri = Uri.parse("content://" + LogShareProvider.authority() + "/" + zip.getName());
                    final Intent send = new Intent(Intent.ACTION_SEND)
                            // A zip: only apps that take files (Discord, Telegram, WhatsApp, Drive...).
                            .setType("application/zip")
                            .putExtra(Intent.EXTRA_STREAM, uri)
                            .putExtra(Intent.EXTRA_SUBJECT, zip.getName())
                            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    send.setClipData(ClipData.newRawUri(zip.getName(), uri));
                    activity.startActivity(Intent.createChooser(send, Tr.t("Share Log")));
                });
            } catch (IOException e) {
                activity.runOnUiThread(() -> Toast.makeText(activity, Tr.t("Could not make the log: ")
                        + e.getMessage(), Toast.LENGTH_LONG).show());
            }
        }, "log-share").start();
    }

    /** cache/share/basara-log-<time>.zip: each run under its folder name, plus device.txt and Config.toml. */
    private static File makeZip(Context context, List<Run> runs) throws IOException {
        final File dir = LogShareProvider.dir(context);
        final File[] old = dir.listFiles();
        if (old != null) {
            for (File f : old) {
                //noinspection ResultOfMethodCallIgnored
                f.delete();
            }
        }
        final String stamp = new java.text.SimpleDateFormat("yyyy-MM-dd_HH-mm", Locale.ROOT).format(new java.util.Date());
        final File zip = new File(dir, "basara-log-" + stamp + ".zip");
        final StringBuilder skipped = new StringBuilder();
        try (ZipOutputStream out = new ZipOutputStream(new FileOutputStream(zip))) {
            put(out, "device.txt", deviceInfo(context).getBytes(StandardCharsets.UTF_8));
            final File config = new File(AppFiles.dataDir(context), "Config.toml");
            if (config.isFile()) addFile(out, config, "Config.toml", skipped);
            for (Run run : runs) addTree(out, run.dir, run.dir.getName(), skipped);
            if (skipped.length() > 0) put(out, "skipped.txt", skipped.toString().getBytes(StandardCharsets.UTF_8));
        }
        return zip;
    }

    private static void addTree(ZipOutputStream out, File dir, String path, StringBuilder skipped) throws IOException {
        final File[] files = dir.listFiles();
        if (files == null) return;
        java.util.Arrays.sort(files);
        for (File f : files) {
            final String name = path + "/" + f.getName();
            if (f.isDirectory()) addTree(out, f, name, skipped);
            else addFile(out, f, name, skipped);
        }
    }

    private static void addFile(ZipOutputStream out, File file, String name, StringBuilder skipped) throws IOException {
        if (file.length() > MAX_FILE) {
            skipped.append(name).append(" (").append(file.length() >> 20).append(" MB)\n");
            return;
        }
        out.putNextEntry(new ZipEntry(name));
        try (InputStream in = new FileInputStream(file)) {
            final byte[] buffer = new byte[64 << 10];
            for (int n; (n = in.read(buffer)) > 0; ) out.write(buffer, 0, n);
        }
        out.closeEntry();
    }

    private static void put(ZipOutputStream out, String name, byte[] data) throws IOException {
        out.putNextEntry(new ZipEntry(name));
        out.write(data);
        out.closeEntry();
    }
}
