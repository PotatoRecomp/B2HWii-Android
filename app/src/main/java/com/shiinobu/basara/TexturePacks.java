package com.shiinobu.basara;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.regex.Pattern;

/**
 * Texture mods: each folder in Textures/ is one mod (Dump/ is not), and the files left directly in
 * Textures/ are one more, LOOSE. Settings > Graphics ticks them and puts them in order; the game gets
 * the ticked ones, first wins, in BASARA_TEXTURE_PACKS (aurora texture_replacement.cpp build_index).
 */
final class TexturePacks {
    private TexturePacks() {}

    /** The files directly in Textures/, as the game is told them. */
    static final String LOOSE = "*";

    static final class Pack {
        final String name;  // folder name, or LOOSE
        boolean on;

        Pack(String name, boolean on) {
            this.name = name;
            this.on = on;
        }
    }

    /** A replacement the game can match, as aurora's parse_replacement_filename reads it. */
    private static final Pattern NAME = Pattern.compile(
            "(?i)tex1_\\d+x\\d+_(m_)?([0-9a-f]{1,16}|\\$)(_([0-9a-f]{1,16}|\\$))?_\\d+\\.(png|dds)");

    static boolean isTextureName(String name) {
        return NAME.matcher(name).matches();
    }

    private static boolean isImage(String name) {
        final String lower = name.toLowerCase(java.util.Locale.ROOT);
        return lower.endsWith(".png") || lower.endsWith(".dds");
    }

    private static boolean isPackFolder(File f) {
        return f.isDirectory() && !f.getName().startsWith(".") && !f.getName().equalsIgnoreCase("Dump");
    }

    /**
     * The saved order and ticks ("1|name" or "0|name" per line) with what is in `root` now: a mod no
     * longer there is dropped, a new one is added at the bottom, ticked.
     */
    static List<Pack> load(AppSettings settings, File root) {
        final List<String> found = new ArrayList<>();
        final File[] children = root == null ? null : root.listFiles();
        boolean loose = false;
        if (children != null) {
            java.util.Arrays.sort(children);
            for (File f : children) {
                if (isPackFolder(f)) found.add(f.getName());
                else if (f.isFile() && isImage(f.getName())) loose = true;
            }
        }
        if (loose) found.add(LOOSE);
        final List<Pack> packs = new ArrayList<>();
        for (String line : settings.texturePacks().split("\n")) {
            if (line.length() < 3 || line.charAt(1) != '|') continue;
            final String name = line.substring(2);
            if (found.remove(name)) packs.add(new Pack(name, line.charAt(0) == '1'));
        }
        for (String name : found) packs.add(new Pack(name, true));
        return packs;
    }

    static void save(AppSettings settings, List<Pack> packs) {
        final StringBuilder out = new StringBuilder();
        for (Pack p : packs) out.append(p.on ? '1' : '0').append('|').append(p.name).append('\n');
        settings.setTexturePacks(out.toString());
    }

    /** What the game is told: the ticked mods in order, one per line (may be empty). */
    static String envValue(List<Pack> packs, File root) {
        final StringBuilder out = new StringBuilder();
        for (Pack p : packs) {
            if (!p.on) continue;
            out.append(p.name.equals(LOOSE) ? LOOSE : new File(root, p.name).getAbsolutePath()).append('\n');
        }
        return out.toString();
    }

    /** Files with a valid name per mod, counted off the UI thread (mods hold thousands of files). */
    private static final Map<String, Integer> counts = new ConcurrentHashMap<>();
    private static volatile boolean counting;

    /** -1 while not counted yet. */
    static int count(String name) {
        final Integer n = counts.get(name);
        return n == null ? -1 : n;
    }

    static void recount() {
        counts.clear();
    }

    /** Counts what is missing, then runs `done` on the UI thread (only when something was counted). */
    static void countMissing(android.app.Activity activity, File root, List<Pack> packs, Runnable done) {
        if (counting || root == null) return;
        final List<String> missing = new ArrayList<>();
        for (Pack p : packs) if (!counts.containsKey(p.name)) missing.add(p.name);
        if (missing.isEmpty()) return;
        counting = true;
        new Thread(() -> {
            for (String name : missing) {
                counts.put(name, name.equals(LOOSE) ? countIn(root, false) : countIn(new File(root, name), true));
            }
            activity.runOnUiThread(() -> {
                counting = false;
                if (!activity.isFinishing()) done.run();
            });
        }, "texture-pack-count").start();
    }

    private static int countIn(File dir, boolean recursive) {
        final File[] children = dir.listFiles();
        if (children == null) return 0;
        int n = 0;
        for (File f : children) {
            if (f.isDirectory()) {
                if (recursive) n += countIn(f, true);
            } else if (isTextureName(f.getName())) {
                ++n;
            }
        }
        return n;
    }
}
