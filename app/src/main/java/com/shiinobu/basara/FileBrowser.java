package com.shiinobu.basara;

import android.app.Activity;
import android.app.Dialog;
import android.content.Context;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.os.Environment;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.text.TextUtils;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/**
 * The app's own file explorer, as an ImGui file dialog (the look of the in-game menu): the folder's
 * path, then a table of its folders ([DIR]) and the files it lists with their sizes, and CANCEL and
 * OPEN (SAVE HERE when choosing a folder). A tap opens a folder or selects a file; a second tap on the
 * selected file opens it. It reads the storage directly, so it needs "All files access"
 * (Environment.isExternalStorageManager); its callers ask for it first.
 */
final class FileBrowser {
    interface Listener { void onChosen(File file); }

    private final Activity activity;
    private final Listener listener;
    /** Files listed and chosen (null in folder mode: only folders are listed). */
    private final java.util.function.Predicate<File> accept;
    private final boolean folderMode;
    private final String emptyText;
    private File current;
    private File selected;
    private final List<File> roots = new ArrayList<>();
    private final LinearLayout list;
    private final ScrollView scroll;
    private final TextView path;
    private final TextView picked;
    private final TextView go;
    private Dialog dialog;  // not final: the Cancel button's lambda is made before it

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

        final LinearLayout window = new LinearLayout(activity);
        window.setOrientation(LinearLayout.VERTICAL);
        window.setBackground(Ui.box(activity, Ui.BG, Ui.BORDER, 0));
        window.setPadding(1, 1, 1, 1);
        window.addView(Ui.titleBar(activity, title, null), Ui.matchWidth(0));
        final LinearLayout root = new LinearLayout(activity);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(dp(10), dp(6), dp(10), dp(8));
        window.addView(root, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        path = Ui.text(activity, "", 13, Ui.TEXT);
        path.setSingleLine(true);
        path.setEllipsize(TextUtils.TruncateAt.START);
        path.setPadding(dp(8), dp(4), dp(8), dp(4));
        path.setBackgroundColor(Ui.FRAME);
        root.addView(path, Ui.matchWidth(0));
        root.addView(columns(Tr.t("Type"), Tr.t("Name").toUpperCase(Locale.ROOT), Tr.t("Size"), Ui.MUTED, 11), Ui.matchWidth(dp(4)));
        root.addView(Ui.line(activity));
        list = new LinearLayout(activity);
        list.setOrientation(LinearLayout.VERTICAL);
        scroll = new ScrollView(activity);
        scroll.setScrollbarFadingEnabled(false);
        scroll.addView(list);
        root.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
        root.addView(Ui.line(activity));

        final LinearLayout footer = new LinearLayout(activity);
        footer.setGravity(Gravity.CENTER_VERTICAL);
        picked = Ui.text(activity, "", 11, Ui.MUTED);
        picked.setSingleLine(true);
        picked.setEllipsize(TextUtils.TruncateAt.MIDDLE);
        footer.addView(picked, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        footer.addView(button(Tr.t("Cancel"), false, v -> this.dialog.dismiss()), buttonParams());
        go = button(folderMode ? Tr.t("Save Here") : Tr.t("Open"), true, v -> choose());
        footer.addView(go, buttonParams());
        root.addView(footer, Ui.matchWidth(dp(2)));

        dialog = new Dialog(activity);
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
        dialog.setContentView(window);
        final Window w = dialog.getWindow();
        if (w != null) {
            w.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            w.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
            w.setDimAmount(0.65f);
            final android.util.DisplayMetrics m = activity.getResources().getDisplayMetrics();
            final boolean landscape = m.widthPixels > m.heightPixels;
            w.setLayout(landscape ? Math.min(dp(680), Math.round(m.widthPixels * 0.74f)) : Math.round(m.widthPixels * 0.94f),
                    Math.round(m.heightPixels * (landscape ? 0.9f : 0.8f)));
        }
        dialog.setOnShowListener(d -> SystemBars.hide(w));
        open(start != null && start.isDirectory() ? start : null);
        dialog.show();
    }

    /** Opens on `start` (a folder), or the storage list when it is null. */
    static void show(Activity activity, File start, Listener listener) {
        new FileBrowser(activity, start, Tr.t("Choose your game file"), null,
                FileBrowser::isGameFile, Tr.t("No folders or ISO / RVZ files here."), listener);
    }

    /** Choose a file that `accept` lists. */
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

    private void choose() {
        final File chosen = folderMode ? current : selected;
        if (chosen == null) return;
        dialog.dismiss();
        listener.onChosen(chosen);
    }

    private void open(File dir) {
        list.removeAllViews();
        current = dir;
        select(null);
        scroll.scrollTo(0, 0);
        if (dir == null) {
            path.setText(Tr.t("Storage"));
            for (File root : roots) row("[DIR]", volumeName(root), "", false, v -> open(root));
            return;
        }
        path.setText(dir.getPath());
        row("[DIR]", "..", "", false, v -> open(isRoot(dir) ? null : dir.getParentFile()));
        final File[] children = dir.listFiles();
        final List<File> folders = new ArrayList<>(), files = new ArrayList<>();
        if (children != null) {
            Arrays.sort(children, (a, b) -> a.getName().compareToIgnoreCase(b.getName()));
            for (File child : children) {
                if (child.getName().startsWith(".")) continue;
                if (child.isDirectory()) folders.add(child);
                else if (!folderMode && accept.test(child)) files.add(child);
            }
        }
        for (File folder : folders) row("[DIR]", folder.getName(), "", false, v -> open(folder));
        for (File file : files) {
            row("", file.getName(), size(file.length()), true, v -> {
                if (file.equals(selected)) {
                    choose();  // a second tap opens it
                    return;
                }
                select(file);
                for (int i = 0; i < list.getChildCount(); ++i) {
                    final View r = list.getChildAt(i);
                    r.setBackground(file.equals(r.getTag()) ? Ui.box(activity, Ui.SELECTED, 0, 0)
                            : Ui.touchBox(activity, Color.TRANSPARENT, 0, 0));
                }
            }).setTag(file);
        }
        if (folders.isEmpty() && files.isEmpty()) {
            final TextView empty = Ui.text(activity, emptyText, 13, Ui.MUTED);
            empty.setPadding(dp(4), dp(8), dp(4), dp(8));
            list.addView(empty);
        }
    }

    /** The file OPEN takes (null: none yet, OPEN dimmed); in folder mode SAVE HERE needs a folder. */
    private void select(File file) {
        selected = file;
        picked.setText(file == null ? "" : file.getName());
        final boolean ready = folderMode ? current != null : file != null;
        go.setEnabled(ready);
        go.setAlpha(ready ? 1f : 0.4f);
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

    /** A table row: [DIR] (amber) or nothing, the name, the size on the right. */
    private View row(String type, String name, String size, boolean file, View.OnClickListener onClick) {
        final LinearLayout row = columns(type, name, size, Ui.TEXT, 13);
        ((TextView) row.getChildAt(0)).setTextColor(Ui.WARNING);
        ((TextView) row.getChildAt(2)).setTextColor(Ui.MUTED);
        row.setPadding(dp(4), dp(7), dp(4), dp(7));
        row.setBackground(Ui.touchBox(activity, Color.TRANSPARENT, 0, 0));
        row.setClickable(true);
        row.setOnClickListener(onClick);
        list.addView(row, Ui.matchWidth(0));
        return row;
    }

    /** Type, name and size in the table's three columns. */
    private LinearLayout columns(String type, String name, String size, int color, float sp) {
        final LinearLayout row = new LinearLayout(activity);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(4), 0, dp(4), 0);
        final TextView t = Ui.text(activity, type.toUpperCase(Locale.ROOT), sp, color);
        row.addView(t, new LinearLayout.LayoutParams(dp(48), LinearLayout.LayoutParams.WRAP_CONTENT));
        final TextView n = Ui.text(activity, name, sp, color);
        n.setSingleLine(true);
        n.setEllipsize(TextUtils.TruncateAt.MIDDLE);
        row.addView(n, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        final TextView s = Ui.text(activity, size.toUpperCase(Locale.ROOT), sp, color);
        s.setGravity(Gravity.END);
        row.addView(s, new LinearLayout.LayoutParams(dp(80), LinearLayout.LayoutParams.WRAP_CONTENT));
        return row;
    }

    private TextView button(String label, boolean primary, View.OnClickListener onClick) {
        final TextView button = Ui.chip(activity, label.toUpperCase(Locale.ROOT), primary, true, onClick);
        button.setPadding(dp(8), 0, dp(8), 0);
        return button;
    }

    private LinearLayout.LayoutParams buttonParams() {
        final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(dp(90), dp(30));
        params.leftMargin = dp(4);
        return params;
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
