package com.shiinobu.basara;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Typeface;
import android.text.InputType;
import android.view.Gravity;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * Developer tools: the testing options kept in env.txt (read by the game when it starts, see
 * runtime main.cpp), its editor, and resetting the settings or clearing caches in one sheet.
 */
final class DevTools {
    private DevTools() {}

    // --- env.txt ----------------------------------------------------------------------------------

    /** <homeDir>/env.txt, or null without "All files access". */
    static File envFile() {
        final File home = AppFiles.homeDir();
        return home == null ? null : new File(home, "env.txt");
    }

    static List<String> envLines() {
        final List<String> lines = new ArrayList<>();
        final File file = envFile();
        if (file == null || !file.isFile()) return lines;
        try {
            for (String line : new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8).split("\n")) {
                lines.add(line.replace("\r", ""));
            }
        } catch (IOException ignored) {
        }
        while (!lines.isEmpty() && lines.get(lines.size() - 1).trim().isEmpty()) lines.remove(lines.size() - 1);
        return lines;
    }

    /** The value of NAME in env.txt, or null. */
    static String env(String name) {
        for (String line : envLines()) {
            if (line.startsWith(name + "=")) return line.substring(name.length() + 1).trim();
        }
        return null;
    }

    /** Sets NAME=value in env.txt, or removes it (value null); other lines stay as they are. */
    static void setEnv(Context context, String name, String value) {
        final List<String> lines = envLines();
        boolean done = false;
        for (int i = lines.size() - 1; i >= 0; --i) {
            if (!lines.get(i).startsWith(name + "=")) continue;
            if (value == null || done) {
                lines.remove(i);
            } else {
                lines.set(i, name + "=" + value);
                done = true;
            }
        }
        if (value != null && !done) lines.add(name + "=" + value);
        writeEnv(context, String.join("\n", lines));
    }

    /** The NAME=value lines that take effect (not comments). */
    static int activeEnvCount() {
        int count = 0;
        for (String line : envLines()) {
            final String t = line.trim();
            if (!t.isEmpty() && !t.startsWith("#") && t.indexOf('=') > 0) ++count;
        }
        return count;
    }

    private static boolean writeEnv(Context context, String text) {
        final File file = envFile();
        if (file == null) return false;
        try {
            Files.write(file.toPath(), (text.trim().isEmpty() ? "" : text.trim() + "\n").getBytes(StandardCharsets.UTF_8));
            arm(context);
            return true;
        } catch (IOException e) {
            return false;
        }
    }

    /** A plain text editor for env.txt: one NAME=value a line, # for a comment. */
    static void editEnv(Activity activity, Runnable after) {
        final EditText edit = new EditText(activity);
        edit.setText(String.join("\n", envLines()));
        edit.setHint("BASARA_FRAMESKIP=0");
        edit.setTypeface(Typeface.MONOSPACE);
        edit.setTextSize(13);
        edit.setTextColor(Ui.TEXT);
        edit.setHintTextColor(Ui.MUTED);
        edit.setGravity(Gravity.TOP | Gravity.START);
        edit.setMinLines(6);
        edit.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE
                | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        edit.setHorizontallyScrolling(false);
        final int pad = Ui.dp(activity, 12);
        edit.setPadding(pad, pad, pad, pad);
        edit.setBackground(Ui.box(activity, Ui.BG, Ui.BORDER_STRONG, 10));
        new Sheet(activity, Tr.t("Edit env.txt"), Tr.t("One NAME=value a line, # starts a comment. Read when the "
                + "game starts. For testing only: a wrong value can make the game slow or close."))
                .view(edit)
                .action(Tr.t("Save"), null, true, false, () -> {
                    if (!writeEnv(activity, edit.getText().toString().replace("\r", ""))) {
                        Toast.makeText(activity, Tr.t("Could not save env.txt."), Toast.LENGTH_LONG).show();
                    }
                    if (after != null) after.run();
                })
                .action(Tr.t("Cancel"), null, false, false, null)
                .pinActions()
                .show();
    }

    // --- testing options, for players ---------------------------------------------------------------

    /** Testing options turn themselves off after this many game starts (set from the app). */
    static final int RUNS = 3;
    private static final String RUNS_LEFT = "env_runs_left", EXPIRED = "env_expired";

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences("settings", Context.MODE_PRIVATE | Context.MODE_MULTI_PROCESS);
    }

    /** After the app changed env.txt: count RUNS starts again (or stop counting when it is empty). */
    private static void arm(Context context) {
        if (activeEnvCount() > 0) prefs(context).edit().putInt(RUNS_LEFT, RUNS).commit();
        else prefs(context).edit().remove(RUNS_LEFT).commit();
    }

    /** Game starts left before the options turn off; -1: not counting (env.txt pushed by adb). */
    static int runsLeft(Context context) {
        return activeEnvCount() > 0 ? prefs(context).getInt(RUNS_LEFT, -1) : -1;
    }

    /**
     * In the game process before the runtime reads env.txt: one start used. After RUNS starts the
     * file is emptied, and the launcher says so once.
     */
    static void onGameStart(Context context) {
        final int left = prefs(context).getInt(RUNS_LEFT, -1);
        if (left < 0 || activeEnvCount() == 0) return;
        if (left == 0) {
            final File file = envFile();
            try {
                if (file != null) Files.write(file.toPath(), new byte[0]);
            } catch (IOException ignored) {
            }
            prefs(context).edit().remove(RUNS_LEFT).putBoolean(EXPIRED, true).commit();
        } else {
            prefs(context).edit().putInt(RUNS_LEFT, left - 1).commit();
        }
    }

    /** In the launcher: tells once that the testing options were turned off. */
    static void checkExpired(Context context) {
        if (!prefs(context).getBoolean(EXPIRED, false)) return;
        prefs(context).edit().remove(EXPIRED).commit();
        Toast.makeText(context, String.format(java.util.Locale.ROOT, Tr.t("Testing options turned off by "
                + "themselves after %d game starts."), RUNS), Toast.LENGTH_LONG).show();
    }

    /**
     * The options players may get from us, so a pasted line reads as words. {name, value or null for
     * any, title, about}.
     */
    private static final String[][] KNOWN = {
        {"AURORA_VK_GPU_TIMING", "0", "GPU timing off", "The log no longer says how long the GPU works on a frame."},
        {"AURORA_VK_DRAW_TIMING", "1", "GPU timing per draw", "Times every draw in the log: slower, for finding a heavy effect."},
        {"BASARA_FRAMESKIP", "0", "Frameskip forced off", "Every frame is drawn, whatever Performance says."},
        {"BASARA_FRAMESKIP", "1", "Frameskip forced to Auto", "Frames are skipped when the phone falls behind."},
        {"BASARA_FRAMESKIP", "2", "Locked to 30 FPS", "Every other frame is drawn."},
        {"BASARA_DYNRES", "2", "Dynamic resolution test", "The resolution steps down and back up every two seconds."},
        {"BASARA_LOG_FPS", null, "Frame timing log", "Writes the frame times to the log."},
        {"BASARA_LOG_TEXSTAT", null, "Texture statistics", "Writes how many textures load and how long it takes to the log."},
        {"BASARA_LOG_VOICES", null, "Sound log", "Writes the voices and sounds that play to the log."},
        {"BASARA_GX_THREAD", null, "GX Thread forced", "Turns GX Thread on (1) or off (0), whatever Performance says."},
        {"BASARA_RENDERER", null, "Renderer forced", "Uses the named renderer, whatever Graphics says."},
        {"BASARA_PROFILE", null, "CPU profile", "Records where the game spends its time, into the log folder."},
        {"BASARA_THERMAL", null, "Thermal control forced", "Thermal Control on (1) or off (0), whatever Performance says."},
        {"BASARA_NO_DVD_PRELOAD", null, "Disc preload off", "Reads the game file only when needed."},
        {"TU_DEBUG", null, "Turnip driver debug", "A debug switch for the Turnip GPU driver."},
    };

    /** {title, about} for an env.txt line. */
    static String[] describe(String line) {
        final int eq = line.indexOf('=');
        final String name = eq > 0 ? line.substring(0, eq).trim() : line.trim();
        final String value = eq > 0 ? line.substring(eq + 1).trim() : "";
        for (String[] k : KNOWN) {
            if (k[0].equals(name) && (k[1] == null || k[1].equals(value))) return new String[] {Tr.t(k[2]), Tr.t(k[3])};
        }
        return new String[] {Tr.t("Developer option"), Tr.t("Not in the app's list: from the developer.")};
    }

    /** NAME=value lines in pasted text (code blocks and "export" taken off). */
    static List<String> parse(String text) {
        final List<String> out = new ArrayList<>();
        if (text == null) return out;
        for (String raw : text.replace("\r", "").split("\n")) {
            String line = raw.trim().replace("`", "");
            if (line.startsWith("export ")) line = line.substring(7).trim();
            if (line.matches("[A-Za-z_][A-Za-z0-9_]*=.*")) out.add(line);
        }
        return out;
    }

    /** Testing Options: the ones on, as cards, with Paste from Discord and the manual editor. */
    static void showOptions(Activity activity, Runnable after) {
        final LinearLayout list = new LinearLayout(activity);
        list.setOrientation(LinearLayout.VERTICAL);
        final Runnable[] draw = new Runnable[1];
        draw[0] = () -> {
            list.removeAllViews();
            final List<String> on = new ArrayList<>();
            for (String line : envLines()) {
                final String t = line.trim();
                if (!t.isEmpty() && !t.startsWith("#") && t.indexOf('=') > 0) on.add(t);
            }
            if (on.isEmpty()) {
                list.addView(Ui.text(activity, Tr.t("None on. Copy the code the developer sends you on Discord, "
                        + "then tap Paste from Discord."), 14, Ui.SECONDARY));
                return;
            }
            final int left = runsLeft(activity);
            if (left >= 0) {
                list.addView(Sheet.warningBox(activity, Tr.t("Testing options are on"), String.format(
                        java.util.Locale.ROOT, Tr.t("They turn off by themselves after %d more game starts."),
                        left)), Ui.matchWidth(0));
            }
            for (String line : on) {
                final String[] about = describe(line);
                final LinearLayout card = new LinearLayout(activity);
                card.setGravity(Gravity.CENTER_VERTICAL);
                card.setPadding(Ui.dp(activity, 12), Ui.dp(activity, 10), Ui.dp(activity, 12), Ui.dp(activity, 10));
                card.setBackground(Ui.box(activity, Ui.BG, Ui.BORDER, 10));
                final LinearLayout words = new LinearLayout(activity);
                words.setOrientation(LinearLayout.VERTICAL);
                final TextView title = Ui.text(activity, about[0], 15, Ui.TEXT);
                title.setTypeface(Typeface.DEFAULT_BOLD);
                words.addView(title);
                words.addView(Ui.text(activity, about[1], 13, Ui.SECONDARY));
                final TextView code = Ui.text(activity, line, 12, Ui.MUTED);
                code.setTypeface(Typeface.MONOSPACE);
                words.addView(code);
                final LinearLayout.LayoutParams wordsParams =
                        new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
                wordsParams.rightMargin = Ui.dp(activity, 10);
                card.addView(words, wordsParams);
                final String name = line.substring(0, line.indexOf('=')).trim();
                card.addView(Ui.chip(activity, Tr.t("Remove"), false, true, v -> {
                    setEnv(activity, name, null);
                    draw[0].run();
                }));
                list.addView(card, Ui.matchWidth(Ui.dp(activity, 8)));
            }
        };
        draw[0].run();
        new Sheet(activity, Tr.t("Testing Options"), Tr.t("Settings the developer asks you to try. They apply the "
                + "next time the game starts."))
                .view(list)
                .action(Tr.t("Paste from Discord"), Tr.t("Copy the code first, then tap here."), true, false,
                        () -> paste(activity, after))
                .action(Tr.t("Edit manually"), null, false, false, () -> editEnv(activity, () -> showOptions(activity, after)))
                .action(Tr.t("Close"), null, false, false, after)
                .onCancel(after != null ? after : () -> {})
                .pinActions()
                .show();
    }

    private static void paste(Activity activity, Runnable after) {
        final android.content.ClipboardManager clip =
                (android.content.ClipboardManager) activity.getSystemService(Context.CLIPBOARD_SERVICE);
        String text = null;
        if (clip != null && clip.hasPrimaryClip() && clip.getPrimaryClip().getItemCount() > 0) {
            final CharSequence item = clip.getPrimaryClip().getItemAt(0).coerceToText(activity);
            text = item == null ? null : item.toString();
        }
        final List<String> lines = parse(text);
        if (lines.isEmpty()) {
            new Sheet(activity, Tr.t("Nothing to paste"), Tr.t("The copied text has no NAME=value lines. Copy the "
                    + "whole code from Discord (for example BASARA_FRAMESKIP=0), then try again."))
                    .action(Tr.t("OK"), null, true, false, () -> showOptions(activity, after)).show();
            return;
        }
        final StringBuilder summary = new StringBuilder();
        for (String line : lines) summary.append("• ").append(describe(line)[0]).append("\n   ").append(line).append("\n");
        new Sheet(activity, String.format(java.util.Locale.ROOT, Tr.t("Add %d testing options?"), lines.size()),
                String.format(java.util.Locale.ROOT, Tr.t("They apply the next %d times the game starts, then turn "
                        + "off by themselves."), RUNS))
                .paragraph(summary.toString().trim())
                .action(Tr.t("Add"), null, true, false, () -> {
                    for (String line : lines) {
                        final int eq = line.indexOf('=');
                        setEnv(activity, line.substring(0, eq).trim(), line.substring(eq + 1).trim());
                    }
                    showOptions(activity, after);
                })
                .action(Tr.t("Cancel"), null, false, false, () -> showOptions(activity, after))
                .pinActions()
                .show();
    }

    // --- reset and clear --------------------------------------------------------------------------

    /** Kept through Reset All Settings: the language and the ended-Beta notice. */
    private static final String[] KEEP = {"language", "beta_ended", "beta_ended_code"};

    /** One sheet: tick what to reset or clear, then do it all at once. */
    static void showReset(Activity activity, boolean gameRunning, Runnable after) {
        final String[] names = {Tr.t("Reset all settings"), Tr.t("Clear HD texture cache"),
            Tr.t("Clear shader cache"), Tr.t("Clear testing options (env.txt)"), Tr.t("Delete logs")};
        final String[] abouts = {
            Tr.t("Every setting back to its default for this phone, touch layout too. The language, game "
                    + "file, saves and installed GPU drivers stay."),
            Tr.t("The sharper HD textures are made again while you play."),
            Tr.t("The first battles stutter a little while it is built again. Try it when graphics look broken "
                    + "after an update."),
            Tr.t("Removes every line from env.txt."),
            Tr.t("Removes the Logs folder (game runs and crash reports)."),
        };
        final boolean[] picked = new boolean[names.length];
        final LinearLayout list = new LinearLayout(activity);
        list.setOrientation(LinearLayout.VERTICAL);
        final Runnable[] draw = new Runnable[1];
        draw[0] = () -> {
            list.removeAllViews();
            for (int i = 0; i < names.length; ++i) {
                final int index = i;
                final LinearLayout row = new LinearLayout(activity);
                row.setGravity(Gravity.CENTER_VERTICAL);
                row.setPadding(Ui.dp(activity, 12), Ui.dp(activity, 10), Ui.dp(activity, 12), Ui.dp(activity, 10));
                row.setBackground(Ui.touchBox(activity, picked[i] ? Ui.ACCENT_SUBTLE : Ui.BG,
                        picked[i] ? Ui.ACCENT : Ui.BORDER, 10));
                final LinearLayout words = new LinearLayout(activity);
                words.setOrientation(LinearLayout.VERTICAL);
                final TextView title = Ui.text(activity, names[i], 15, Ui.TEXT);
                title.setTypeface(Typeface.DEFAULT_BOLD);
                words.addView(title);
                words.addView(Ui.text(activity, abouts[i], 13, Ui.SECONDARY));
                final LinearLayout.LayoutParams wordsParams =
                        new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
                wordsParams.rightMargin = Ui.dp(activity, 10);
                row.addView(words, wordsParams);
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
        if (gameRunning) {
            new Sheet(activity, Tr.t("Reset and Clear"), Tr.t("Stop the game first: these files are in use while "
                    + "it runs.")).action(Tr.t("OK"), null, true, false, null).show();
            return;
        }
        new Sheet(activity, Tr.t("Reset and Clear"), Tr.t("Pick what to reset or clear. This cannot be undone."))
                .view(list)
                .action(Tr.t("Reset and Clear"), null, true, false, () -> {
                    boolean any = false;
                    for (boolean p : picked) any |= p;
                    if (!any) {
                        Toast.makeText(activity, Tr.t("Pick at least one."), Toast.LENGTH_SHORT).show();
                        return;
                    }
                    run(activity, picked.clone(), after);
                })
                .action(Tr.t("Cancel"), null, false, false, null)
                .pinActions()
                .show();
    }

    private static void run(Activity activity, boolean[] picked, Runnable after) {
        final Context app = activity.getApplicationContext();
        if (picked[0]) resetSettings(app);
        Toast.makeText(activity, Tr.t("Working..."), Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            if (picked[1]) {
                final File hd = AppFiles.hdCacheDir();
                if (hd != null) AppFiles.deleteRecursively(hd);
            }
            if (picked[2]) clearShaderCache(app);
            if (picked[3]) writeEnv(app, "");
            if (picked[4]) {
                final File logs = AppFiles.logsRoot();
                if (logs != null) AppFiles.deleteRecursively(logs);
            }
            AppFiles.deleteRecursively(LogShareProvider.dir(app));
            activity.runOnUiThread(() -> {
                Toast.makeText(activity, Tr.t("Done."), Toast.LENGTH_SHORT).show();
                if (picked[0]) {
                    Tr.init(activity);
                    activity.recreate();
                } else if (after != null) {
                    after.run();
                }
            });
        }, "dev-reset").start();
    }

    private static void resetSettings(Context context) {
        final SharedPreferences prefs = context.getSharedPreferences("settings",
                Context.MODE_PRIVATE | Context.MODE_MULTI_PROCESS);
        final Map<String, ?> all = prefs.getAll();
        final SharedPreferences.Editor edit = prefs.edit();
        outer:
        for (String key : all.keySet()) {
            for (String keep : KEEP) if (keep.equals(key)) continue outer;
            edit.remove(key);
        }
        edit.commit();
        final AppSettings settings = new AppSettings(context);
        DeviceProfile.applyOnce(context, settings);
        try {
            AppFiles.writeConfig(context, new AppSettings(context));
        } catch (IOException ignored) {
        }
    }

    /** The Vulkan and Dawn pipeline caches, wherever the app keeps them (not inside the game data). */
    private static void clearShaderCache(Context context) {
        final File game = AppFiles.gameDir(context);
        for (File root : new File[] {context.getFilesDir(), context.getCacheDir(), AppFiles.dataDir(context),
                context.getCodeCacheDir()}) {
            deleteCaches(root, game, 0);
        }
    }

    private static void deleteCaches(File dir, File skip, int depth) {
        final File[] files = dir == null || depth > 4 ? null : dir.listFiles();
        if (files == null) return;
        for (File f : files) {
            if (f.equals(skip)) continue;
            if (f.isDirectory()) {
                deleteCaches(f, skip, depth + 1);
            } else {
                final String name = f.getName();
                if (name.startsWith("vk_pipeline_cache") || name.startsWith("pipeline_cache.db")) {
                    //noinspection ResultOfMethodCallIgnored
                    f.delete();
                }
            }
        }
    }
}
