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
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * What changed in each build: the versions in a strip across the top, the
 * chosen version's changes below. Opens once after an install or update (on the newest
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

    private static final Entry[] ALL = {
        new Entry("1.2", "Straight into the game, settings while playing", "2026-11-07",
                new String[] {
                    "No launcher: the app opens straight into the game. Your ISO or RVZ is chosen once, the first time; after that the icon goes right to the battlefield.",
                    "Every setting is in the game's menu (Back, or the gear): Display, Graphics, Textures, Performance, Controls, Save Data, Cheats and System.",
                    "Restart beside Quit in the menu: the game starts again, so settings marked Restart needed apply.",
                    "Display Resolution, V-Sync and Sustained Performance now apply at once while playing, without a restart.",
                    "Settings > System in the game: change the game file, check for updates, Share Log, Device Info, language and Reset and Clear.",
                    "New Vulkan renderer: the game now talks to the phone's Vulkan driver directly on every Vulkan phone. Older drivers without the newest Vulkan features get a mode made for them.",
                    "Graphics > Renderer: Auto, Vulkan, WebGPU 1.2, WebGPU 1.0 and OpenGL ES (Experimental). WebGPU 1.0 is the renderer of version 1.0, for phones that ran 1.0 smoother.",
                    "Custom GPU drivers: Settings > Graphics > Install GPU Driver takes a Turnip driver zip for Adreno phones. On Adreno 6xx it usually takes much of the GPU's load off.",
                    "Low-End Preset now has two levels: Low-End (480p) and Ultra Low (Display Resolution 360p, Render Resolution 0.75x, the 30 FPS Lock and GX Thread) for phones that drop in the heaviest effects.",
                    "Display Resolution 360p: a new step below 480p for the slowest GPUs.",
                    "Dynamic Resolution handles big effects: when a Basara art or ultimate pushes the GPU far past its limit, the picture drops for a moment and climbs back once the effect is over.",
                    "Thermal Control on every phone: reads the phone's heat forecast (or the battery temperature where there is none) and eases the load before the phone throttles.",
                    "Busy battles no longer lose speed on mid and low-end phones: a frame that ran a little late used to wait for the next screen refresh as well. The game now keeps full speed there, and the GX thread does less bookkeeping.",
                    "Less work per frame: draws that share their settings reuse them instead of sending a copy, and the game thread checks its timers far less often.",
                    "Smaller download: the APK is about 44 MB instead of 109 MB.",
                    "One look for the touch buttons: clean round outlines with white icons. Button Style and Button Color are gone.",
                    "16:9 and Fill Screen stretch the picture again, as in 1.0. The wider view of 1.1 is now Settings > Display > Real Widescreen: it wakes more enemies at the sides, and on slower phones the sound broke up.",
                },
                new String[] {
                    "Battles could freeze, show wrong colours, then close on several phones. The game no longer closes there.",
                    "PowerVR Rogue phones (for example OPPO with a Helio P-series chip) showed a black screen on Vulkan. They now draw normally without any extra setup.",
                    "PowerVR phones closed in battle with \"invalid wrap mode 3\".",
                    "Adreno 610 phones with a custom Turnip driver showed black frames and missing layers: the driver now draws without tiling there.",
                    "Turnip drivers ran in the slower mode meant for old stock Qualcomm drivers; they now get the full renderer.",
                    "Changing Render Resolution with the slider while playing closed the app.",
                    "The Restart button in the menu could close the app.",
                },
                new String[] {
                    "In heavy battles a frame can briefly show wrong colours or missing textures. Please send Share Log if you see it.",
                    "Weak GPUs (Mali-G52 MC2, PowerVR) still slow down in Basara arts and ultimates: use Ultra Low.",
                }),
        new Entry("1.1", "Widescreen, Camera Distance, Android 10 and PowerVR phones", "2026-11-07",
                new String[] {
                    "Real widescreen: 16:9 and Fill Screen now show more of the battlefield to the sides instead of stretching the 4:3 picture, so characters keep their shape. Health bars and names above characters stay on them, and screen effects line up.",
                    "New Camera Distance (Settings > Display): pull the battle camera back to 1.5x or 2x to see more of the field. Cutscenes and stage intros keep the normal camera, and it is off in LAN play.",
                    "New cheat Unlock All (Settings > Cheats): opens every character, stage, skill, weapon and item. It only fills what is still locked; what the game saves stays open after you turn it off.",
                    "New look for the Modern touch buttons: lighter round buttons with the controller letter on the rim and a D-pad of four round keys, in the same colours. L is now labelled SWITCH and R GUARD.",
                    "Gamepad Buttons (Settings > Controls): choose which gamepad button presses each Classic Controller button. Tap one, press the gamepad button, done; Reset to Default puts back the usual layout.",
                    "Graphics > Renderer > Vulkan now also works on Android 10 phones with an older Vulkan driver (for example realme 5, Adreno 610).",
                    "On Android 10 phones with four fast CPU cores, the graphics work now runs on the fast cores beside the game.",
                    "Android 10 support: the game and the launcher now run on Android 10. File access there is the usual storage permission dialog.",
                    "The log now records more when the game stops on a black screen, so such reports can be traced faster.",
                    "The launcher checks the game file against the original disc. A changed one (patched or badly converted) gets a warning, Play Anyway still works, and the log says so.",
                    "Adreno 5xx phones on Android 10 (for example Redmi Note 7) run the game on OpenGL ES, since their Vulkan driver stops it on the first screens. The launcher says once that this GPU isn't supported.",
                    "Graphics > Renderer has a third choice, OpenGL ES, for phones whose Vulkan driver shows a black screen or closes the game.",
                },
                new String[] {
                    "16:9 and Fill Screen stretched the game's 4:3 picture sideways, so characters and the field looked squashed. They are now drawn wider instead.",
                    "Phones with a PowerVR GPU (for example OPPO phones with Helio G35 or P90) no longer crash on Play with \"Graphics adapter unavailable\".",
                    "Some Adreno 610 phones with an older Vulkan driver (for example Redmi Note 13 4G) closed on Play (\"No supported adapters\"). They now start the game the same way as the PowerVR phones above.",
                    "Adreno 5xx phones (for example Redmi Note 7) closed on the first screens or as the first battle loaded, inside their Vulkan driver. The game now feeds their driver in a way it handles.",
                    "Closing the Wii HOME Menu could crash the game on slow phones (seen on realme 5).",
                    "Save import refused a real save when an earlier import had put a wrong file in its place. Only a real save (173,568 bytes) is accepted now, and it always replaces whatever is there.",
                    "Save Data's help now says that a data.bin exported from a Wii or Dolphin can be imported.",
                    "Renderer Vulkan: if the phone isn't ready to show the picture when you come back to the game (from the home screen or after the screen turns off), the game now waits and tries again instead of closing.",
                    "Renderer Vulkan on Android 10: the game takes memory in smaller pieces, and when memory runs short while loading a texture it tries again before giving up, so phones with 2-4 GB of RAM close less often.",
                },
                new String[] {
                    "A modified or damaged disc image (for example a patched ISO) stops on a black screen after the first frame. Use your own unmodified RBSJ08 dump.",
                    "PowerVR GPUs are slow for this game: use the Low-End Preset.",
                    "In 16:9 and Fill Screen the HUD, text and menus are still stretched to the screen's width; only the 3D picture is widened.",
                }),
        new Entry("1.0", "The first release", "2026-11-06",
                new String[] {
                    "The whole game runs on Android, from boot through Story battles, with sound, saving and loading. The game's code is recompiled for the phone ahead of time: nothing is emulated.",
                    "Load your own disc dump (.iso or .rvz, Wii NTSC-J RBSJ08) straight from the phone.",
                    "Runs on most phones: the Low-End Preset sets the lightest settings in one tap, with Display Resolution, Dynamic Resolution, Frameskip, the 30 FPS Lock, GX Thread, Thermal Control and Sustained Performance for the rest.",
                    "A sharper picture on stronger phones: render resolution from 0.5x to 4x, FSR1 and SGSR upscaling, HD Textures and custom texture packs (.png or .dds).",
                    "Touch controls in two styles, Modern (a picture for each button) or Classic (the controller's letters), and a layout editor that pauses the game: move every button on its own and size it from 50% to 200%. Touch camera, gamepads, tilt and shake work too.",
                    "Clean sound without crackle on slow phones, and Recording-Friendly Audio for screen recordings.",
                    "Two renderers and custom GPU drivers (Turnip for Adreno, malisx2 for Mali).",
                    "Settings in the launcher and in game, cheats, save backup and import, Share Log for bug reports, English and Indonesian.",
                },
                new String[] {},
                new String[] {
                    "Adreno 618 (Redmi Note 10 Pro and similar): very heavy effects still drop to about 40 fps, the limit of the GPU.",
                    "Adreno 610 with the phone's own driver (Redmi Pad SE): use Turnip from Settings > Graphics > GPU Driver.",
                }),
        new Entry("Final Beta", "Experimental features passed testing, new Textures tab", "2026-11-04",
                new String[] {
                    "New touch controls in two styles (Settings > Controls > Button Style): Modern shows a picture for what each button does, Classic shows the controller's letters. Blue, with A green, B red, X blue and Y amber.",
                    "A new layout editor (Back > Layout): the game pauses and dims, every control moves on its own (A, B, X and Y too), and its size goes from 50% to 200% with the slider, the - and + buttons or a pinch. Opacity, the left stick's mode, Reset and Cancel are on the same panel, and controls snap to the centre lines and to each other.",
                    "The right stick hides while Touch camera is on, and comes back when it is off. Touch camera is now off by default.",
                    "Every Experimental feature passed testing and is now a regular setting, without warnings: HD Textures, Custom Textures, Dump Textures, Report Texture Keys, the Vulkan renderer, GPU drivers and Thermal Control. The Experimental tab is gone.",
                    "New Textures tab: Custom Textures (.png or .dds), Dump Textures, Report Texture Keys and HD Textures in one place. Renderer and GPU Driver moved to Graphics, Thermal Control to Performance.",
                    "New Display Resolution (Display): the game sends a smaller picture (960p, 720p or 480p) that the screen stretches for free. Every frame, menus too, takes less GPU work: steadier fps on a slow phone, blurrier picture.",
                    "New Low-End Preset (Performance): the lightest settings in one tap (480p, Texture Detail Original, no upscaler, Frameskip, Dynamic Resolution), with Undo. A slow phone gets it by itself once, only for the settings you never changed.",
                    "Share Log in System: sends the log of the times you played (the crashed one first) with the device info as one zip, to Discord, Telegram, WhatsApp or any app. Device Info with Copy, and Reset and Clear: reset all settings and clear the caches or logs in one place. The new Developer tab is empty for now.",
                    "Low-End Device: a slow phone shows a warning above All settings with the Low-End Preset switch. While it is on, the settings it set stay as they are; Render Resolution can still be changed.",
                    "New Sustained Performance (Performance, off by default): speeds the phone can hold for hours, for long sessions without a drop once it gets hot. Only on phones that offer it.",
                    "The Vulkan renderer measures at start which memory the game writes its graphics data into fastest on this phone, and uses that one.",
                    "Texture Detail now starts at Original, like most phone emulators: less GPU work on every phone. Sharp is one tap away in Graphics.",
                    "New Recording-Friendly Audio (System): on phones whose screen recordings come out silent, keeps the game's sound where the recording hears it.",
                    "Phones with less than 4.5 GB of RAM keep a smaller cache of HD and custom textures, so the system is less likely to close the game.",
                    "The settings tabs are one row you can swipe sideways, and only that row stays at the top while you scroll, so the settings get most of the screen. The Low-End Preset is a smaller card, and the empty Developer tab is gone.",
                    "Custom Textures also reads .dds packs (BC1 to BC7). On a GPU without BC textures (most Mali) they are unpacked while loading, so the same pack works on every phone.",
                    "Dynamic Resolution with the Vulkan renderer now follows the GPU's own time, so it also lowers the resolution when heavy effects slow the GPU down.",
                    "New Upload Memory (Performance, Vulkan renderer): Auto measures at start which memory is faster on this phone; Standard or Cached sets it by hand.",
                    "The Low-End Preset also turns on the Performance Overlay with only the FPS counter, so you see what the preset gives. It can be turned off or changed while the preset is on.",
                    "The Low-End Preset is now offered on mid-low phones too (Helio G99, Mali-G57 MC2/MC3, Snapdragon 845, and 4 GB phones): Basara arts and busy battles hold their speed better.",
                },
                new String[] {
                    "Dynamic Resolution with the 30 FPS Lock lowered the picture down to half for no reason (it measured the GPU against 60 fps). The picture now stays sharp while the game holds 30 fps.",
                    "Sound goes straight to Android's low-latency audio output: less crackle and fewer dialog stutters on slow phones. It falls back to the regular output on phones that refuse it, comes back by itself after headphones or Bluetooth change, and pauses while the game is in the background.",
                    "Leaving the game from its menu no longer shows \"The game closed unexpectedly\" in the launcher on some phones (Android 16).",
                    "HD Textures Live no longer slows the game down while the sharper textures are being made.",
                    "Dump Textures no longer slows the game down, and skips movie frames.",
                    "GX Thread: busy battles drop fewer frames (the game kept waiting for the graphics thread).",
                    "The launcher showed Continue and Stop after the game had closed normally.",
                    "Thermal Control stayed at 30 fps on a phone that had only been warm for a moment.",
                    "Vulkan renderer: .dds textures on GPUs without BC textures no longer fail to load.",
                    "Phones whose GPU the default renderer cannot use (Adreno 610, such as the Redmi Pad SE) closed as soon as the game started. The game now switches to the Vulkan renderer by itself.",
                    "The picture could freeze while the game and its sound went on (seen on Adreno 616 with Turnip, when the resolution changed). The screen now recovers by itself within about two seconds.",
                },
                new String[] {
                    "Adreno 618 (Redmi Note 10 Pro and similar): very heavy effects still drop to about 40 fps, the limit of the GPU.",
                    "malisx2 on Mali-G52 works, but is not faster than the phone's own driver.",
                    "Still to be tested once the phones and testers' reports come in: Mali phones (BC textures, malisx2), Adreno 616 (silver enemies) and Adreno 610 (horse riding).",
                }),
        new Entry("Beta 5", "New launcher, settings in game and GPU drivers", "2026-11-03",
                new String[] {
                    "A new upright launcher: Play and the settings in use on the first page, every setting in the tabs below it.",
                    "Settings in game: tap the gear at the top left (or press Back). Resolution, upscaler, frameskip, 30 FPS Lock, overlay, texture detail, cheats and touch controls change at once. Settings that need the game to start again are marked in red.",
                    "Stop button in the launcher: closes the game waiting in the background.",
                    "SGSR upscaling option (Graphics > Upscaler).",
                    "Graphics > GPU Driver: install a custom Vulkan driver .zip (Turnip for Adreno, malisx2 for Mali). If the game does not start with it, it switches back to System.",
                    "Graphics > Renderer: the Vulkan renderer, lighter than the default on some phones.",
                    "Performance > GX Thread: the graphics work runs on its own CPU core, so busy battles run faster on slower phones.",
                    "Dynamic Resolution moved from Experimental to Performance.",
                    "Logs: every game start has its own folder, Logs/<date and time>, with log/ (console.log) and crash/ (crash reports). The 20 newest are kept.",
                    "The launcher asks for file access as soon as it opens, until it is given.",
                    "Settings > Controls > Invert Camera: choose which way a drag turns the camera.",
                },
                new String[] {
                    "The game closed in Story while riding a horse (\"Missing translated function\").",
                    "The game closed on Adreno 610 phones (Poco M3, Redmi 9T) when Dynamic Resolution changed the resolution. It now changes at most every 5 seconds.",
                    "malisx2 on Mali-G52 showed a black screen.",
                    "The game closed in battle against some bosses (\"Missing translated function\").",
                    "Dialogue repeated in short steps when the game ran around 30 fps.",
                    "The touch camera turned slowly or not at all on gentle drags. Any drag now turns it at full speed.",
                    "Loud battle sound crackled when many effects played at once.",
                    "GX Thread: black flashes in the battle preparation screen and white flashes in battle.",
                    "Invert Camera now also reverses the right touch stick (both ways), not only camera drags.",
                    "Installing this Beta over an ended one still showed \"The Beta has ended\".",
                    "Import Save refused a data.bin copied from a Wii or Dolphin (\"not a save of this game\"). It is now read as it is.",
                },
                new String[] {
                    "malisx2 on Mali-G52 works, but is not faster than the phone's own driver.",
                }),
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

    /** The release shows its own notes from 1.0 on; the Beta keeps the Beta history too. */
    private static final Entry[] ENTRIES = BuildConfig.BETA ? betaEntries() : releaseEntries();

    /** The Beta ends at the Final Beta: the release's own notes are not its. */
    private static Entry[] betaEntries() {
        for (int i = 0; i < ALL.length; ++i) {
            if (ALL[i].version.equals("Final Beta")) return java.util.Arrays.copyOfRange(ALL, i, ALL.length);
        }
        return ALL;
    }

    private static Entry[] releaseEntries() {
        for (int i = 0; i < ALL.length; ++i) {
            if (ALL[i].version.equals(BetaGate.RELEASE_VERSION)) return java.util.Arrays.copyOf(ALL, i + 1);
        }
        return ALL;
    }

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

    /** As above, with `after` run once it is closed. False (and `after` not run) when nothing is new. */
    static boolean showIfNew(Activity activity, AppSettings settings, Runnable after) {
        final String build = build(activity);
        if (build.equals(settings.notesSeen())) return false;
        settings.setNotesSeen(build);
        final ReleaseNotes notes = new ReleaseNotes(activity);
        notes.dialog.setOnDismissListener(d -> after.run());
        notes.dialog.show();
        return true;
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

        // Upright page: the title, a sideways strip of versions, the chosen version's notes, Done in view.
        final TextView title = text(Tr.t("What's New"), 24, Color.WHITE);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        final TextView installed = text(Tr.t("Installed: Basara ") + Updater.displayVersion(activity), 12,
                LauncherActivity.MUTED);
        installed.setPadding(0, dp(2), 0, dp(10));
        nav = new LinearLayout(activity);
        nav.setOrientation(LinearLayout.HORIZONTAL);
        for (int i = 0; i < ENTRIES.length; ++i) {
            final int index = i;
            final LinearLayout item = new LinearLayout(activity);
            item.setOrientation(LinearLayout.VERTICAL);
            item.setPadding(dp(14), dp(8), dp(14), dp(8));
            item.setClickable(true);
            item.setOnClickListener(v -> showEntry(index));
            final TextView name = text(ENTRIES[i].version, 15, Color.WHITE);
            name.setTypeface(LauncherActivity.MEDIUM);
            item.addView(name);
            item.addView(text(i == 0 ? Tr.t("this version") : ENTRIES[i].date, 11, LauncherActivity.MUTED));
            final LinearLayout.LayoutParams itemParams = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
            if (i > 0) itemParams.leftMargin = dp(6);
            nav.addView(item, itemParams);
        }
        final HorizontalScrollView navScroll = new HorizontalScrollView(activity);
        navScroll.setHorizontalScrollBarEnabled(false);
        navScroll.addView(nav);

        rows = new LinearLayout(activity);
        rows.setOrientation(LinearLayout.VERTICAL);
        rows.setPadding(0, dp(14), 0, 0);
        rowsScroll = new ScrollView(activity);
        rowsScroll.setVerticalScrollBarEnabled(false);
        rowsScroll.addView(rows);

        final TextView done = text(Tr.t("Done"), 17, Color.WHITE);
        done.setTypeface(Typeface.DEFAULT_BOLD);
        done.setGravity(Gravity.CENTER);
        done.setPadding(0, dp(12), 0, dp(12));
        done.setBackground(rounded(LauncherActivity.ACCENT, dp(24)));

        final LinearLayout page = new LinearLayout(activity);
        page.setOrientation(LinearLayout.VERTICAL);
        page.setBackgroundColor(LauncherActivity.BACKGROUND);
        page.addView(title);
        page.addView(installed);
        page.addView(navScroll, matchWidth(0));
        page.addView(rowsScroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
        page.addView(done, matchWidth(dp(10)));

        dialog = new Dialog(activity, android.R.style.Theme_DeviceDefault_NoActionBar_Fullscreen);
        dialog.setContentView(page);
        // Bars stay visible (like the launcher); the page keeps clear of them.
        SystemBars.edgeToEdge(dialog.getWindow());
        page.setOnApplyWindowInsetsListener((v, insets) -> {
            final android.graphics.Insets bars = SystemBars.bars(insets, false);
            v.setPadding(dp(16) + bars.left, dp(16) + bars.top, dp(16) + bars.right, dp(16) + bars.bottom);
            return SystemBars.consumed(insets);
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
