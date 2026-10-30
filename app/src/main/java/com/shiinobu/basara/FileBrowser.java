package com.shiinobu.basara;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.os.Environment;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/**
 * A plain file explorer for choosing the game file, like a phone's own file manager: folders first,
 * then the ISO and RVZ files with their sizes. It reads the storage directly, so it needs "All files
 * access" (Environment.isExternalStorageManager); LauncherActivity asks for it first.
 */
final class FileBrowser {
    interface Listener { void onChosen(File file); }

    private static final int MUTED = Color.rgb(170, 178, 186);
    private static final int RAISED = Color.rgb(38, 48, 58);
    private static final int FOLDER = Color.rgb(255, 183, 77);
    private static final int GAME = Color.rgb(102, 187, 106);

    private final Activity activity;
    private final Listener listener;
    private final List<File> roots = new ArrayList<>();
    private final LinearLayout list;
    private final TextView path;
    private final ScrollView scroll;
    private final AlertDialog dialog;
    private File current;  // null: the list of storage volumes

    private FileBrowser(Activity activity, File start, Listener listener) {
        this.activity = activity;
        this.listener = listener;
        final StorageManager storage = (StorageManager) activity.getSystemService(Context.STORAGE_SERVICE);
        for (StorageVolume volume : storage.getStorageVolumes()) {
            final File dir = volume.getDirectory();
            if (dir != null && dir.canRead()) roots.add(dir);
        }
        if (roots.isEmpty()) roots.add(Environment.getExternalStorageDirectory());

        path = new TextView(activity);
        path.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        path.setTextColor(MUTED);
        path.setSingleLine(true);
        path.setEllipsize(TextUtils.TruncateAt.START);
        path.setPadding(dp(24), dp(4), dp(24), dp(8));
        list = new LinearLayout(activity);
        list.setOrientation(LinearLayout.VERTICAL);
        scroll = new ScrollView(activity);
        scroll.addView(list);
        final LinearLayout column = new LinearLayout(activity);
        column.setOrientation(LinearLayout.VERTICAL);
        column.addView(path);
        column.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        dialog = new AlertDialog.Builder(activity).setTitle("Choose your ISO or RVZ").setView(column)
                .setNegativeButton("Cancel", null).create();
        open(start != null && start.isDirectory() ? start : null);
        dialog.show();
    }

    /** Opens on `start` (a folder), or the storage list when it is null. */
    static void show(Activity activity, File start, Listener listener) {
        new FileBrowser(activity, start, listener);
    }

    static File downloads() {
        return Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
    }

    private void open(File dir) {
        current = dir;
        list.removeAllViews();
        if (dir == null) {
            path.setText("Storage");
            for (File root : roots) {
                row(volumeName(root), null, FOLDER, () -> open(root));
            }
        } else {
            path.setText(dir.getPath());
            row("←  Up", null, MUTED, () -> open(isRoot(dir) ? null : dir.getParentFile()));
            final File[] children = dir.listFiles();
            final List<File> folders = new ArrayList<>(), games = new ArrayList<>();
            if (children != null) {
                Arrays.sort(children, (a, b) -> a.getName().compareToIgnoreCase(b.getName()));
                for (File child : children) {
                    if (child.getName().startsWith(".")) continue;
                    if (child.isDirectory()) folders.add(child);
                    else if (isGameFile(child)) games.add(child);
                }
            }
            for (File folder : folders) {
                row(folder.getName(), null, FOLDER, () -> open(folder));
            }
            for (File game : games) {
                row(game.getName(), size(game.length()), GAME, () -> {
                    dialog.dismiss();
                    listener.onChosen(game);
                });
            }
            if (folders.isEmpty() && games.isEmpty()) {
                final TextView empty = text("No folders or ISO / RVZ files here.", 14, MUTED);
                empty.setPadding(dp(24), dp(16), dp(24), dp(16));
                list.addView(empty);
            }
        }
        scroll.scrollTo(0, 0);
    }

    private boolean isRoot(File dir) {
        for (File root : roots) {
            if (root.equals(dir)) return true;
        }
        return dir.getParentFile() == null;
    }

    private String volumeName(File root) {
        return root.equals(Environment.getExternalStorageDirectory()) ? "Phone storage" : "SD card (" + root.getName() + ")";
    }

    static boolean isGameFile(File file) {
        final String name = file.getName().toLowerCase(Locale.ROOT);
        return name.endsWith(".iso") || name.endsWith(".rvz");
    }

    private void row(String title, String detail, int color, Runnable action) {
        final LinearLayout row = new LinearLayout(activity);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(24), dp(12), dp(24), dp(12));
        row.setMinimumHeight(dp(52));
        final TypedValue ripple = new TypedValue();
        activity.getTheme().resolveAttribute(android.R.attr.selectableItemBackground, ripple, true);
        row.setBackgroundResource(ripple.resourceId);
        row.setOnClickListener(v -> action.run());
        final TextView name = text(title, 16, color == MUTED ? MUTED : Color.WHITE);
        name.setSingleLine(true);
        name.setEllipsize(TextUtils.TruncateAt.MIDDLE);
        if (color == GAME) name.setTypeface(Typeface.DEFAULT_BOLD);
        row.addView(name, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        if (detail != null) {
            final TextView size = text(detail, 14, MUTED);
            size.setPadding(dp(16), 0, 0, 0);
            row.addView(size);
        }
        if (color == FOLDER || color == GAME) {
            // A coloured mark at the start: amber for folders, green for a game file.
            final TextView mark = new TextView(activity);
            mark.setBackground(new ColorDrawable(color));
            row.addView(mark, 0, new LinearLayout.LayoutParams(dp(4), dp(28)));
            ((LinearLayout.LayoutParams) mark.getLayoutParams()).setMarginEnd(dp(14));
        }
        list.addView(row);
        final TextView line = new TextView(activity);
        line.setBackground(new ColorDrawable(RAISED));
        list.addView(line, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 1));
    }

    private TextView text(String value, int sp, int color) {
        final TextView view = new TextView(activity);
        view.setText(value);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        view.setTextColor(color);
        return view;
    }

    private static String size(long bytes) {
        if (bytes >= 1L << 30) return String.format(Locale.ROOT, "%.2f GB", bytes / (double) (1L << 30));
        return String.format(Locale.ROOT, "%.0f MB", bytes / (double) (1L << 20));
    }

    private int dp(int value) {
        return Math.round(value * activity.getResources().getDisplayMetrics().density);
    }
}
