package com.shiinobu.basara;

import android.app.Activity;

/**
 * What changed in each build: shown once after an install or update (the newest entry), and in full
 * from the launcher's What's New tile. The newest entry is first and describes the build installed;
 * add one for every release.
 */
final class ReleaseNotes {
    private static final class Entry {
        final String title, date;
        final String[] added, fixed, known;
        Entry(String title, String date, String[] added, String[] fixed, String[] known) {
            this.title = title;
            this.date = date;
            this.added = added;
            this.fixed = fixed;
            this.known = known;
        }
    }

    private static final Entry[] ENTRIES = {
        new Entry("Experimental features and What's New", "2026-11-01",
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
        new Entry("Texture Detail and custom textures", "2026-11-01",
                new String[] {
                    "Texture Detail (Settings > Graphics): sharper ground and walls at low render resolution.",
                    "Custom Textures and Dump Textures, Dolphin-style names.",
                    "Dynamic Resolution steps back up when the GPU has room.",
                },
                new String[] {},
                new String[] {}),
        new Entry("30 FPS Lock and Thermal Control", "2026-11-01",
                new String[] {
                    "30 FPS Lock: a steady 30 FPS while the game and sound keep full speed.",
                    "Thermal Control: draws at 30 FPS and lowers resolution while the phone is hot.",
                    "GX Thread (Experimental): graphics work on its own CPU core.",
                    "Faster skinned characters in battles.",
                },
                new String[] {},
                new String[] {"GX Thread: textures and the sky can flicker white, so it is off by default."}),
        new Entry("Device profiles and the new launcher", "2026-11-01",
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
        new Entry("Frameskip, FSR1 and Continue", "2026-10-31",
                new String[] {
                    "Frameskip Auto, FSR1 upscaling, Skip Boot and a faster start.",
                    "Continue: the game keeps running while you are in the launcher.",
                    "Smaller touch controls and a layout editor.",
                },
                new String[] {},
                new String[] {}),
        new Entry("First Android Beta", "2026-10-31",
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

    /** After an install or update: the newest notes, once. */
    static void showIfNew(Activity activity, AppSettings settings) {
        final String build = build(activity);
        if (build.equals(settings.notesSeen())) return;
        settings.setNotesSeen(build);
        final Entry e = ENTRIES[0];
        final Sheet sheet = new Sheet(activity, Tr.t("What's New"), "Basara " + Updater.displayVersion(activity) + " · " + e.date);
        add(sheet, e, true);
        sheet.action(Tr.t("Got it"), null, true, false, null)
                .action(Tr.t("All versions"), null, false, false, () -> showAll(activity))
                .show();
    }

    /** Every entry, newest first. */
    static void showAll(Activity activity) {
        final Sheet sheet = new Sheet(activity, Tr.t("What's New"), Tr.t("Installed: Basara ") + Updater.displayVersion(activity));
        for (int i = 0; i < ENTRIES.length; ++i) add(sheet, ENTRIES[i], i == 0);
        sheet.action(Tr.t("Close"), null, true, false, null).show();
    }

    private static void add(Sheet sheet, Entry e, boolean newest) {
        sheet.heading((newest ? Tr.t("This version · ") : "") + Tr.t(e.title) + " · " + e.date);
        for (String line : e.added) sheet.step("+", Tr.t(line), null);
        for (String line : e.fixed) sheet.step("✓", Tr.t(line), null);
        for (String line : e.known) sheet.step("!", Tr.t("Known issue"), Tr.t(line));
    }

    private static String version(Activity activity) {
        try {
            return activity.getPackageManager().getPackageInfo(activity.getPackageName(), 0).versionName;
        } catch (Exception e) {
            return "";
        }
    }

    private ReleaseNotes() {}
}
