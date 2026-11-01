package com.shiinobu.basara;

import android.app.Activity;
import android.app.Dialog;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * What changed in each build, laid out like the Settings screen: every version down the left, the
 * chosen version's changes on the right. Opens once after an install or update (on the newest
 * version) and from the launcher's What's New tile. The newest entry is first and describes the
 * build installed; add one for every release, listing only what that build changed.
 */
final class ReleaseNotes {
    private static final class Entry {
        final String version, title, date;
        final String[] added, fixed, known;
        Entry(String version, String title, String date, String[] added, String[] fixed, String[] known) {
            this.version = version;
            this.title = title;
            this.date = date;
            this.added = added;
            this.fixed = fixed;
            this.known = known;
        }
    }

    private static final Entry[] ENTRIES = {
        new Entry("Beta 4", "Clean voices, cheats and save backups", "2026-11-02",
                new String[] {
                    "Settings > Save Data: back up your save to a .zip and import it again.",
                    "Settings > Cheats: money (infinite, or set once to any amount), infinite HP and infinite Basara gauge.",
                },
                new String[] {
                    "Dialogue and cutscene voices no longer stutter or repeat when the game runs below 60 fps (Low resolution, 30 FPS Lock, busy battles).",
                    "Thermal Control is now off by default and moved to Experimental: it switched to 30 fps when it was not needed.",
                    "A character's voice right after picking them no longer plays slow and broken, and the character select no longer pauses for a moment on each pick.",
                    "Settings changed in the Start Menu now apply when you Continue the game.",
                },
                new String[] {}),
        new Entry("Beta 3", "Older phones", "2026-11-02",
                new String[] {},
                new String[] {
                    "The game closed right after Play on phones with older CPUs (Snapdragon 662, 665, 460, Helio G80, G85 and similar).",
                },
                new String[] {}),
        new Entry("Beta 2", "Crash reports and Beta switch", "2026-11-01",
                new String[] {
                    "Crash reports: when the game closes unexpectedly, the next start saves last_crash.txt in the log folder. Send it with your report.",
                    "The developer can now switch the Beta off or ask for a newer one from afar. Offline, it keeps playing.",
                },
                new String[] {},
                new String[] {}),
        new Entry("Beta 1", "Experimental features and What's New", "2026-11-01",
                new String[] {
                    "New Beta numbering: 0.0.1 Beta 1. New Betas are sent by hand for now.",
                    "Coming from the old Beta (0.9.0)? Uninstall it once first; later Betas install over each other.",
                    "What's New: this screen, after every update, and the full history from the What's New tile.",
                    "Settings > Experimental: every unfinished feature in one place, each with its warning.",
                    "HD Textures (Experimental): textures are made sharper while you play and kept for next time.",
                    "Report Texture Keys (Experimental): lists the game's textures in the log for testers' reports.",
                    "The left panel shows the settings in use; the file browser and update screens have the new look.",
                    "Language: English or Indonesian, picked from the phone's region (Settings > Display).",
                },
                new String[] {
                    "Custom textures now match the 3D models' palette textures (characters, HUD).",
                },
                new String[] {
                    "HD Textures: textures change from blurry to sharp the first time they appear; uses more RAM and storage.",
                    "GX Thread: the sky and some textures can flicker white.",
                }),
        new Entry("0.9.0 Beta", "Texture Detail and custom textures", "2026-11-01",
                new String[] {
                    "Texture Detail (Settings > Graphics): sharper ground and walls at low render resolution.",
                    "Custom Textures and Dump Textures, Dolphin-style names.",
                    "Dynamic Resolution steps back up when the GPU has room.",
                },
                new String[] {},
                new String[] {}),
        new Entry("0.9.0 Beta", "30 FPS Lock and Thermal Control", "2026-11-01",
                new String[] {
                    "30 FPS Lock: a steady 30 FPS while the game and sound keep full speed.",
                    "Thermal Control: draws at 30 FPS and lowers resolution while the phone is hot.",
                    "GX Thread (Experimental): graphics work on its own CPU core.",
                    "Faster skinned characters in battles.",
                },
                new String[] {},
                new String[] {"GX Thread: textures and the sky can flicker white, so it is off by default."}),
        new Entry("0.9.0 Beta", "Device profiles and the new launcher", "2026-11-01",
                new String[] {
                    "The first start picks a render resolution and Frameskip for your phone.",
                    "Dynamic Resolution, a new Settings screen and the performance overlay bar.",
                    "Touch camera: drag on the empty right side.",
                },
                new String[] {
                    "Noise during spoken dialogue.",
                    "The picture zoomed in or was cut off after changing resolution in game.",
                },
                new String[] {}),
        new Entry("0.9.0 Beta", "Frameskip, FSR1 and Continue", "2026-10-31",
                new String[] {
                    "Frameskip Auto, FSR1 upscaling, Skip Boot and a faster start.",
                    "Continue: the game keeps running while you are in the launcher.",
                    "Smaller touch controls and a layout editor.",
                },
                new String[] {},
                new String[] {}),
        new Entry("0.9.0 Beta", "First Android Beta", "2026-10-31",
                new String[] {
                    "Load your own ISO or RVZ on the phone.",
                    "Smoother sound on slow phones (audio stretching).",
                },
                new String[] {"Crashes on phones with little RAM."},
                new String[] {}),
    };

    /** The build installed: its notes are shown once. */
    private static String build(Activity activity) {
        try {
            return activity.getPackageManager().getPackageInfo(activity.getPackageName(), 0).getLongVersionCode()
                    + ":" + ENTRIES[0].title;
        } catch (Exception e) {
            return ENTRIES[0].title;
        }
    }

    /** After an install or update: the screen, on the newest version, once. */
    static void showIfNew(Activity activity, AppSettings settings) {
        final String build = build(activity);
        if (build.equals(settings.notesSeen())) return;
        settings.setNotesSeen(build);
        showAll(activity);
    }

    /** The full screen, on the newest version. */
    static void showAll(Activity activity) {
        new ReleaseNotes(activity).dialog.show();
    }

    // --- the screen (same layout and look as SettingsMenu) ---------------------------------------

    private final Activity activity;
    private final Dialog dialog;
    private final LinearLayout nav;
    private final LinearLayout rows;
    private final ScrollView rowsScroll;

    private ReleaseNotes(Activity activity) {
        this.activity = activity;

        // Left column: the title, the versions (scrolling, there are more than fit), Done always in view.
        final LinearLayout column = new LinearLayout(activity);
        column.setOrientation(LinearLayout.VERTICAL);
        final TextView title = text(Tr.t("What's New"), 24, Color.WHITE);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setPadding(dp(8), 0, 0, dp(2));
        column.addView(title);
        final TextView installed = text(Tr.t("Installed: Basara ") + Updater.displayVersion(activity), 12,
                LauncherActivity.MUTED);
        installed.setPadding(dp(8), 0, 0, dp(8));
        column.addView(installed);
        nav = new LinearLayout(activity);
        nav.setOrientation(LinearLayout.VERTICAL);
        for (int i = 0; i < ENTRIES.length; ++i) {
            final int index = i;
            final LinearLayout item = new LinearLayout(activity);
            item.setOrientation(LinearLayout.VERTICAL);
            item.setPadding(dp(16), dp(10), dp(16), dp(10));
            item.setClickable(true);
            item.setOnClickListener(v -> showEntry(index));
            final TextView name = text(ENTRIES[i].version + (i == 0 ? Tr.t(" · this version") : ""), 16, Color.WHITE);
            name.setTypeface(LauncherActivity.MEDIUM);
            item.addView(name);
            item.addView(text(Tr.t(ENTRIES[i].title) + " · " + ENTRIES[i].date, 12, LauncherActivity.MUTED));
            nav.addView(item, matchWidth(i == 0 ? dp(4) : dp(6)));
        }
        final ScrollView navScroll = new ScrollView(activity);
        navScroll.setVerticalScrollBarEnabled(false);
        navScroll.addView(nav);
        column.addView(navScroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
        final TextView done = text(Tr.t("Done"), 17, Color.WHITE);
        done.setTypeface(Typeface.DEFAULT_BOLD);
        done.setGravity(Gravity.CENTER);
        done.setPadding(0, dp(12), 0, dp(12));
        done.setBackground(rounded(LauncherActivity.ACCENT, dp(24)));
        column.addView(done, matchWidth(dp(10)));

        rows = new LinearLayout(activity);
        rows.setOrientation(LinearLayout.VERTICAL);
        rowsScroll = new ScrollView(activity);
        rowsScroll.setVerticalScrollBarEnabled(false);
        rowsScroll.addView(rows);

        final LinearLayout page = new LinearLayout(activity);
        page.setOrientation(LinearLayout.HORIZONTAL);
        page.setPadding(dp(16), dp(16), dp(16), dp(16));
        page.setBackgroundColor(LauncherActivity.BACKGROUND);
        page.addView(column, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 0.3f));
        final LinearLayout.LayoutParams rowsParams =
                new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 0.7f);
        rowsParams.leftMargin = dp(16);
        page.addView(rowsScroll, rowsParams);

        dialog = new Dialog(activity, android.R.style.Theme_DeviceDefault_NoActionBar_Fullscreen);
        dialog.setContentView(page);
        dialog.setOnShowListener(d -> {
            dialog.getWindow().setDecorFitsSystemWindows(false);
            final WindowInsetsController controller = dialog.getWindow().getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        });
        done.setOnClickListener(v -> dialog.dismiss());
        showEntry(0);
    }


    private void showEntry(int index) {
        for (int i = 0; i < ENTRIES.length; ++i) {
            nav.getChildAt(i).setBackground(i == index ? rounded(LauncherActivity.RAISED, dp(16)) : null);
        }
        rows.removeAllViews();
        final Entry e = ENTRIES[index];
        final TextView heading = text(e.version, 22, Color.WHITE);
        heading.setTypeface(Typeface.DEFAULT_BOLD);
        rows.addView(heading);
        final TextView about = text(Tr.t(e.title) + " · " + e.date, 13, LauncherActivity.MUTED);
        about.setPadding(0, dp(2), 0, 0);
        rows.addView(about);
        group(Tr.t("Added"), "+", LauncherActivity.ACCENT, e.added);
        group(Tr.t("Fixed"), "✓", LauncherActivity.GREEN, e.fixed);
        group(Tr.t("Known issues"), "!", Sheet.WARNING_TEXT, e.known);
        rowsScroll.scrollTo(0, 0);
    }

    /** A label, then one card per change, each with its mark on the left. */
    private void group(String label, String mark, int markColor, String[] lines) {
        if (lines.length == 0) return;
        final TextView title = text(label, 13, LauncherActivity.MUTED);
        title.setTypeface(LauncherActivity.MEDIUM);
        rows.addView(title, matchWidth(dp(18)));
        for (String line : lines) {
            final LinearLayout card = new LinearLayout(activity);
            card.setOrientation(LinearLayout.HORIZONTAL);
            card.setGravity(Gravity.CENTER_VERTICAL);
            card.setPadding(dp(18), dp(14), dp(14), dp(14));
            card.setBackground(rounded(LauncherActivity.SURFACE, dp(18)));
            final TextView markView = text(mark, 16, markColor);
            markView.setTypeface(Typeface.DEFAULT_BOLD);
            markView.setGravity(Gravity.CENTER);
            card.addView(markView, new LinearLayout.LayoutParams(dp(22), LinearLayout.LayoutParams.WRAP_CONTENT));
            final TextView words = text(Tr.t(line), 15, Color.WHITE);
            final LinearLayout.LayoutParams wordsParams =
                    new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            wordsParams.leftMargin = dp(10);
            card.addView(words, wordsParams);
            rows.addView(card, matchWidth(dp(8)));
        }
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

    private static LinearLayout.LayoutParams matchWidth(int topMargin) {
        final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.topMargin = topMargin;
        return params;
    }

    private int dp(int value) {
        return Math.round(value * activity.getResources().getDisplayMetrics().density);
    }
}
