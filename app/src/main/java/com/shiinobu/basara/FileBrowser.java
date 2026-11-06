package com.shiinobu.basara;

import android.app.Activity;
import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Environment;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/**
 * A plain file explorer for choosing the game file, in the launcher's card style: folders first, then
 * the ISO and RVZ files with their sizes. It reads the storage directly, so it needs "All files access"
 * (Environment.isExternalStorageManager); LauncherActivity asks for it first.
 */
final class FileBrowser {
    interface Listener { void onChosen(File file); }

    private static final int FOLDER = LauncherActivity.AMBER;
    private static final int GAME = LauncherActivity.GREEN;

    private final Activity activity;
    private final Listener listener;
    /** Files listed and chosen (null in folder mode: only folders are listed). */
    private final java.util.function.Predicate<File> accept;
    private final boolean folderMode;
    private final String emptyText;
    private File current;
    private final List<File> roots = new ArrayList<>();
    private final LinearLayout list;
    private final TextView path;
    private final Sheet sheet;

    private FileBrowser(Activity activity, File start, String title, String about,
                        java.util.function.Predicate<File> accept, String emptyText, Listener listener) {
        this.activity = activity;
        this.listener = listener;
        this.accept = accept;
        this.folderMode = accept == null;
        this.emptyText = emptyText;
        final StorageManager storage = (StorageManager) activity.getSystemService(Context.STORAGE_SERVICE);
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            for (StorageVolume volume : storage.getStorageVolumes()) {
                final File dir = volume.getDirectory();
                if (dir != null && dir.canRead()) roots.add(dir);
            }
        } else {
            // Android 10 has no StorageVolume.getDirectory: each volume's root is above its
            // Android/data/<package>/files folder.
            for (File files : activity.getExternalFilesDirs(null)) {
                if (files == null) continue;
                final String full = files.getAbsolutePath();
                final int cut = full.indexOf("/Android/data/");
                final File dir = cut > 0 ? new File(full.substring(0, cut)) : null;
                if (dir != null && dir.canRead()) roots.add(dir);
            }
        }
        if (roots.isEmpty()) roots.add(Environment.getExternalStorageDirectory());

        final LinearLayout column = new LinearLayout(activity);
        column.setOrientation(LinearLayout.VERTICAL);
        path = text("", 13, LauncherActivity.MUTED);
        path.setSingleLine(true);
        path.setEllipsize(TextUtils.TruncateAt.START);
        path.setPadding(dp(14), dp(8), dp(14), dp(8));
        path.setBackground(rounded(LauncherActivity.BACKGROUND, dp(14)));
        column.addView(path);
        list = new LinearLayout(activity);
        list.setOrientation(LinearLayout.VERTICAL);
        final LinearLayout.LayoutParams listParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        listParams.topMargin = dp(4);
        column.addView(list, listParams);

        sheet = new Sheet(activity, title, about).view(column);
        if (folderMode) {
            // The folder on screen; on the storage list, Download.
            sheet.action(Tr.t("Save Here"), null, true, false,
                    () -> listener.onChosen(current != null ? current : downloads()));
        }
        sheet.action(Tr.t("Cancel"), null, false, false, null);
        open(start != null && start.isDirectory() ? start : null);
        sheet.show();
    }

    /** Opens on `start` (a folder), or the storage list when it is null. */
    static void show(Activity activity, File start, Listener listener) {
        new FileBrowser(activity, start, Tr.t("Choose your game file"), Tr.t("An ISO or RVZ of your own disc (RBSJ08). "
                        + "Tip: keep it in ") + AppFiles.HOME + Tr.t("/ISO, this list opens there next time."),
                FileBrowser::isGameFile, Tr.t("No folders or ISO / RVZ files here."), listener);
    }

    /** Choose a file that `accept` lists (marked green, like game files). */
    static void showFiles(Activity activity, File start, String title, String about,
                          java.util.function.Predicate<File> accept, String emptyText, Listener listener) {
        new FileBrowser(activity, start, title, about, accept, emptyText, listener);
    }

    /** Choose a folder: browse into it, then Save Here. */
    static void showFolder(Activity activity, File start, String title, String about, Listener listener) {
        new FileBrowser(activity, start, title, about, null, Tr.t("No folders here."), listener);
    }

    static File downloads() {
        return Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
    }

    private void open(File dir) {
        list.removeAllViews();
        current = dir;
        if (sheet != null) sheet.scrollToTop();
        if (dir == null) {
            path.setText(Tr.t("Storage"));
            for (File root : roots) {
                row("▸", volumeName(root), null, FOLDER, () -> open(root));
            }
        } else {
            path.setText(dir.getPath());
            row("‹", Tr.t("Up"), null, LauncherActivity.MUTED, () -> open(isRoot(dir) ? null : dir.getParentFile()));
            final File[] children = dir.listFiles();
            final List<File> folders = new ArrayList<>(), games = new ArrayList<>();
            if (children != null) {
                Arrays.sort(children, (a, b) -> a.getName().compareToIgnoreCase(b.getName()));
                for (File child : children) {
                    if (child.getName().startsWith(".")) continue;
                    if (child.isDirectory()) folders.add(child);
                    else if (!folderMode && accept.test(child)) games.add(child);
                }
            }
            // Game files first: they are what the player came for.
            for (File game : games) {
                row("●", game.getName(), size(game.length()), GAME, () -> {
                    sheet.dismiss();
                    listener.onChosen(game);
                });
            }
            for (File folder : folders) {
                row("▸", folder.getName(), null, FOLDER, () -> open(folder));
            }
            if (folders.isEmpty() && games.isEmpty()) {
                final TextView empty = text(emptyText, 14, LauncherActivity.MUTED);
                empty.setPadding(dp(14), dp(16), dp(14), dp(8));
                list.addView(empty);
            }
        }
    }

    private boolean isRoot(File dir) {
        for (File root : roots) {
            if (root.equals(dir)) return true;
        }
        return dir.getParentFile() == null;
    }

    private String volumeName(File root) {
        return root.equals(Environment.getExternalStorageDirectory()) ? Tr.t("Phone storage") : Tr.t("SD card (") + root.getName() + ")";
    }

    static boolean isGameFile(File file) {
        final String name = file.getName().toLowerCase(Locale.ROOT);
        return name.endsWith(".iso") || name.endsWith(".rvz");
    }

    /** A rounded row: a coloured mark, the name, and on the right the size of a game file. */
    private void row(String mark, String title, String detail, int color, Runnable action) {
        final LinearLayout row = new LinearLayout(activity);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(14), dp(10), dp(14), dp(10));
        row.setMinimumHeight(dp(52));
        row.setBackground(rounded(LauncherActivity.RAISED, dp(16)));
        row.setClickable(true);
        row.setOnClickListener(v -> action.run());
        final TextView badge = text(mark, 15, color == GAME ? Color.WHITE : color);
        badge.setGravity(Gravity.CENTER);
        badge.setTypeface(Typeface.DEFAULT_BOLD);
        if (color == GAME) {
            final GradientDrawable circle = new GradientDrawable();
            circle.setShape(GradientDrawable.OVAL);
            circle.setColor(GAME);
            badge.setBackground(circle);
            badge.setText("");
        }
        row.addView(badge, new LinearLayout.LayoutParams(dp(color == GAME ? 12 : 20), dp(color == GAME ? 12 : 24)));
        final TextView name = text(title, 15, color == LauncherActivity.MUTED ? LauncherActivity.MUTED : Color.WHITE);
        name.setSingleLine(true);
        name.setEllipsize(TextUtils.TruncateAt.MIDDLE);
        name.setTypeface(color == GAME ? Typeface.DEFAULT_BOLD : LauncherActivity.MEDIUM);
        final LinearLayout.LayoutParams nameParams =
                new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        nameParams.leftMargin = dp(12);
        row.addView(name, nameParams);
        if (detail != null) {
            final TextView size = text(detail, 13, Color.WHITE);
            size.setTypeface(Typeface.DEFAULT_BOLD);
            size.setPadding(dp(12), dp(4), dp(12), dp(4));
            size.setBackground(rounded(Color.argb(60, 102, 187, 106), dp(12)));
            final LinearLayout.LayoutParams sizeParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
            sizeParams.leftMargin = dp(10);
            row.addView(size, sizeParams);
        }
        final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.topMargin = dp(8);
        list.addView(row, params);
    }

    private TextView text(String value, int sp, int color) {
        final TextView view = new TextView(activity);
        view.setText(value);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        view.setTextColor(color);
        return view;
    }

    private static GradientDrawable rounded(int color, int radius) {
        final GradientDrawable shape = new GradientDrawable();
        shape.setCornerRadius(radius);
        shape.setColor(color);
        return shape;
    }

    private static String size(long bytes) {
        if (bytes >= 1L << 30) return String.format(Locale.ROOT, Tr.t("%.2f GB"), bytes / (double) (1L << 30));
        if (bytes < 1L << 20) return String.format(Locale.ROOT, "%d KB", Math.max(1, (bytes + 1023) >> 10));
        return String.format(Locale.ROOT, Tr.t("%.0f MB"), bytes / (double) (1L << 20));
    }

    private int dp(int value) {
        return Math.round(value * activity.getResources().getDisplayMetrics().density);
    }
}
