package com.shiinobu.basara;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.content.SharedPreferences;
import android.os.Build;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * Crash reports that don't need the native log. A game that dies while loading libmain.so, or
 * before the runtime opens console.log, leaves nothing behind, so:
 * <ul>
 * <li>the :game process writes crash_java.txt for an uncaught Java exception, and</li>
 * <li>the launcher, the next time it opens, asks Android why :game ended
 * (ApplicationExitInfo) and writes last_crash.txt, plus the tombstone for a native crash
 * (last_crash_tombstone.pb, Android 12+).</li>
 * </ul>
 * Both go to the run's crash folder (/sdcard/Basara2Heroes[-Beta]/Logs/<start time>/crash).
 */
final class CrashReport {
    private static final String TAG = "Basara";
    private static final String PREFS = "crash_report";
    private static final String KEY_LAST = "last_exit_ms";

    private CrashReport() {}

    /**
     * The crash/ folder of the run: this process's run in :game, else (the launcher, after the game
     * ended) the newest run, which is the one that crashed. Android/data/.../files/Logs without
     * "All files access".
     */
    static File dir(Context context) {
        File run = AppFiles.currentRun;
        if (run == null) run = AppFiles.newestRunDir();
        final File dir = run != null ? new File(run, "crash") : new File(AppFiles.dataDir(context), "Logs");
        //noinspection ResultOfMethodCallIgnored
        dir.mkdirs();
        return dir;
    }

    /** In the :game process, before anything else: Java exceptions go to crash_java.txt. */
    static void installJavaHandler(Context context) {
        final Context app = context.getApplicationContext();
        final Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            try {
                final StringWriter trace = new StringWriter();
                error.printStackTrace(new PrintWriter(trace));
                write(new File(dir(app), "crash_java.txt"), header(app) + "thread: " + thread.getName()
                        + "\n\n" + trace);
            } catch (Throwable ignored) {
            }
            if (previous != null) previous.uncaughtException(thread, error);
        });
    }

    /** Testing over adb: setprop debug.basara.test_crash java|native crashes the game at start. */
    static void testCrashIfAsked() {
        final String mode = Updater.debugProperty("debug.basara.test_crash", "");
        if (mode.equals("java")) throw new RuntimeException("test crash (debug.basara.test_crash)");
        if (mode.equals("native")) android.os.Process.sendSignal(android.os.Process.myPid(), 11);
    }

    /**
     * In the launcher: when :game ended abnormally since the last check, writes last_crash.txt and
     * returns a one-line reason for the player; null otherwise.
     */
    static String checkLastGameExit(Context context) {
        try {
            final ActivityManager am = context.getSystemService(ActivityManager.class);
            if (am == null) return null;
            final List<ApplicationExitInfo> exits =
                    am.getHistoricalProcessExitReasons(context.getPackageName(), 0, 10);
            final SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
            final long seen = prefs.getLong(KEY_LAST, 0);
            ApplicationExitInfo newest = null;
            for (ApplicationExitInfo exit : exits) {
                final String process = exit.getProcessName();
                if (process == null || !process.endsWith(":game")) continue;
                if (newest == null || exit.getTimestamp() > newest.getTimestamp()) newest = exit;
            }
            if (newest == null || newest.getTimestamp() <= seen) return null;
            prefs.edit().putLong(KEY_LAST, newest.getTimestamp()).apply();
            if (!abnormal(newest.getReason())) return null;

            final File dir = dir(context);
            final StringBuilder text = new StringBuilder(header(context));
            text.append("game ended: ").append(stamp(newest.getTimestamp())).append('\n')
                    .append("reason: ").append(reasonName(newest.getReason()))
                    .append(" (").append(newest.getReason()).append(")\n")
                    .append("status: ").append(newest.getStatus()).append('\n')
                    .append("description: ").append(newest.getDescription()).append('\n')
                    .append("importance: ").append(newest.getImportance()).append('\n')
                    .append("pss: ").append(newest.getPss() / 1024).append(" MB, rss: ")
                    .append(newest.getRss() / 1024).append(" MB\n");
            try (InputStream trace = newest.getTraceInputStream()) {
                if (trace != null) {
                    if (newest.getReason() == ApplicationExitInfo.REASON_CRASH_NATIVE) {
                        final File pb = new File(dir, "last_crash_tombstone.pb");
                        try (OutputStream out = new FileOutputStream(pb)) {
                            AppFiles.copy(trace, out);
                        }
                        text.append("tombstone: ").append(pb.getName()).append('\n');
                    } else {
                        final java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream();
                        AppFiles.copy(trace, bytes);
                        text.append("\ntrace:\n").append(bytes.toString("UTF-8"));
                    }
                }
            }
            final File log = new File(dir, "last_crash.txt");
            write(log, text.toString());
            return reasonName(newest.getReason()) + " → " + log.getAbsolutePath();
        } catch (Throwable e) {
            Log.w(TAG, "could not read the game's last exit", e);
            return null;
        }
    }

    private static boolean abnormal(int reason) {
        switch (reason) {
            case ApplicationExitInfo.REASON_EXIT_SELF:
            case ApplicationExitInfo.REASON_USER_REQUESTED:
            case ApplicationExitInfo.REASON_USER_STOPPED:
            case ApplicationExitInfo.REASON_PACKAGE_UPDATED:
            case ApplicationExitInfo.REASON_PERMISSION_CHANGE:
                return false;
            default:
                return true;
        }
    }

    private static String reasonName(int reason) {
        switch (reason) {
            case ApplicationExitInfo.REASON_ANR: return "ANR";
            case ApplicationExitInfo.REASON_CRASH: return "Java crash";
            case ApplicationExitInfo.REASON_CRASH_NATIVE: return "native crash";
            case ApplicationExitInfo.REASON_DEPENDENCY_DIED: return "dependency died";
            case ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE: return "excessive resource usage";
            case ApplicationExitInfo.REASON_INITIALIZATION_FAILURE: return "initialization failure";
            case ApplicationExitInfo.REASON_LOW_MEMORY: return "low memory";
            case ApplicationExitInfo.REASON_SIGNALED: return "signaled";
            case ApplicationExitInfo.REASON_OTHER: return "other";
            default: return "unknown";
        }
    }

    private static String header(Context context) {
        String version = "?";
        try {
            final android.content.pm.PackageInfo info =
                    context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
            version = info.versionName + " (code " + info.getLongVersionCode() + ")";
        } catch (Exception ignored) {
        }
        final String soc = Build.VERSION.SDK_INT >= 31 ? Build.SOC_MANUFACTURER + " " + Build.SOC_MODEL : Build.HARDWARE;
        return "app: " + context.getPackageName() + " " + version + "\n"
                + "device: " + Build.MANUFACTURER + " " + Build.MODEL + " (" + Build.DEVICE + "), soc " + soc + "\n"
                + "android: " + Build.VERSION.RELEASE + " (sdk " + Build.VERSION.SDK_INT + "), abi "
                + String.join(",", Build.SUPPORTED_ABIS) + "\n"
                + "ram: " + DeviceProfile.ramMb(context) + " MB, max cpu " + DeviceProfile.maxCpuMhz() + " MHz\n"
                + "written: " + stamp(System.currentTimeMillis()) + "\n";
    }

    private static String stamp(long ms) {
        return new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.ROOT).format(new Date(ms));
    }

    private static void write(File file, String text) throws java.io.IOException {
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.UTF_8));
        }
    }
}
