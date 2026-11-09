package com.shiinobu.basara;

import java.io.File;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.regex.Pattern;

/**
 * Texture mods: each folder in Textures/ is one mod (Dump/ is not), and the files left directly in
 * Textures/ are one more, LOOSE. Settings > Textures ticks them and puts them in order; the game gets
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
     * longer there is dropped, a new one is added at the bottom, not ticked: the player turns it on.
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
        for (String name : found) packs.add(new Pack(name, false));
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

    /** What a scan found in one mod (or in the loose files). */
    static final class Info {
        int valid;           // names the game can match
        int bad;             // .png/.dds whose name it cannot match
        String badSample;    // one of them, to show
        int duplicates;      // the same texture twice in this mod (png + dds, or two folders)
        int dds;             // real .dds files (unpacked while loading on a GPU without BC textures)
        int renamed;         // .dds files that are PNG inside (Dolphin reads by content, so do we)
        int unreadable;      // named right, but neither PNG nor DDS inside (often a JPEG): skipped
        String unreadableSample;
        int seen = -1;       // of `valid`, how many the game has shown (names in Dump/); -1: no dump
        long bytes;          // size of the valid files
        final java.util.Set<String> keys = new java.util.HashSet<>();
    }

    /** Per mod, filled off the UI thread (mods hold thousands of files). */
    private static final Map<String, Info> infos = new ConcurrentHashMap<>();
    private static volatile boolean counting;
    private static volatile boolean rootScanned;
    /** Archives left in Textures/ (a mod not unpacked yet), and the names in Dump/. */
    private static volatile List<String> archives = new ArrayList<>();
    private static volatile java.util.Set<String> dumpKeys = new java.util.HashSet<>();

    /** null while not scanned yet. */
    static Info info(String name) {
        return infos.get(name);
    }

    /** -1 while not counted yet. */
    static int count(String name) {
        final Info i = infos.get(name);
        return i == null ? -1 : i.valid;
    }

    static List<String> archives() { return archives; }

    static void recount() {
        infos.clear();
        rootScanned = false;
    }

    /**
     * Of each ticked mod, how many of its textures a mod higher in the order already replaces (the
     * higher one wins). Mods not scanned yet count 0.
     */
    static Map<String, Integer> overridden(List<Pack> packs) {
        final Map<String, Integer> out = new java.util.HashMap<>();
        final java.util.Set<String> taken = new java.util.HashSet<>();
        for (Pack p : packs) {
            if (!p.on) continue;
            final Info i = infos.get(p.name);
            if (i == null) continue;
            int n = 0;
            for (String k : i.keys) if (!taken.add(k)) ++n;
            out.put(p.name, n);
        }
        return out;
    }

    /** Counts what is missing, then runs `done` on the UI thread (only when something was counted). */
    static void countMissing(android.app.Activity activity, File root, List<Pack> packs, Runnable done) {
        if (counting || root == null) return;
        final List<String> missing = new ArrayList<>();
        for (Pack p : packs) if (!infos.containsKey(p.name)) missing.add(p.name);
        if (missing.isEmpty() && rootScanned) return;
        counting = true;
        new Thread(() -> {
            if (!rootScanned) {
                final List<String> zips = new ArrayList<>();
                final File[] children = root.listFiles();
                if (children != null) {
                    for (File f : children) {
                        final String n = f.getName().toLowerCase(java.util.Locale.ROOT);
                        if (f.isFile() && (n.endsWith(".zip") || n.endsWith(".rar") || n.endsWith(".7z"))) {
                            zips.add(f.getName());
                        }
                    }
                }
                final java.util.Set<String> dump = new java.util.HashSet<>();
                collectKeys(new File(root, "Dump"), dump);
                archives = zips;
                dumpKeys = dump;
                rootScanned = true;
            }
            for (String name : missing) {
                final Info i = new Info();
                if (name.equals(LOOSE)) scan(root, false, i);
                else scan(new File(root, name), true, i);
                if (!dumpKeys.isEmpty()) {
                    i.seen = 0;
                    for (String k : i.keys) if (dumpKeys.contains(k)) ++i.seen;
                }
                infos.put(name, i);
            }
            activity.runOnUiThread(() -> {
                counting = false;
                if (!activity.isFinishing()) done.run();
            });
        }, "texture-pack-count").start();
    }

    /** The texture a file replaces: its name without the extension, lower case (png and dds alike). */
    private static String key(String fileName) {
        final int dot = fileName.lastIndexOf('.');
        return (dot < 0 ? fileName : fileName.substring(0, dot)).toLowerCase(java.util.Locale.ROOT);
    }

    private static void collectKeys(File dir, java.util.Set<String> out) {
        final File[] children = dir.listFiles();
        if (children == null) return;
        for (File f : children) {
            if (f.isDirectory()) collectKeys(f, out);
            else if (isTextureName(f.getName())) out.add(key(f.getName()));
        }
    }

    private static final int PNG = 0, DDS = 1, OTHER = 2;

    /** What a file really is, from its first bytes (aurora load_texture_file reads it the same way). */
    private static int kind(File f) {
        final byte[] magic = new byte[4];
        try (java.io.FileInputStream in = new java.io.FileInputStream(f)) {
            if (in.read(magic) != 4) return OTHER;
        } catch (java.io.IOException e) {
            return OTHER;
        }
        if ((magic[0] & 0xFF) == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G') return PNG;
        if (magic[0] == 'D' && magic[1] == 'D' && magic[2] == 'S' && magic[3] == ' ') return DDS;
        return OTHER;
    }

    /** What the game loaded, per mod, from Textures/Loaded.txt (aurora note_loaded). */
    static final class Used {
        int ok;
        int failed;
        String failedSample;
    }

    /** Empty when the game has not loaded a replacement yet (or custom textures were off). */
    static Map<String, Used> used(File root) {
        final Map<String, Used> out = new java.util.HashMap<>();
        if (root == null) return out;
        final java.util.Set<String> seenLines = new java.util.HashSet<>();
        try (java.io.BufferedReader in = new java.io.BufferedReader(new java.io.InputStreamReader(
                new java.io.FileInputStream(new File(root, "Loaded.txt")), java.nio.charset.StandardCharsets.UTF_8))) {
            for (String line; (line = in.readLine()) != null; ) {
                final int tab = line.indexOf('\t');
                if (tab < 0 || !seenLines.add(line)) continue;
                final String path = line.substring(tab + 1).replace('\\', '/');
                final int slash = path.indexOf('/');
                final Used u = out.computeIfAbsent(slash < 0 ? LOOSE : path.substring(0, slash), k -> new Used());
                if (line.startsWith("ok")) {
                    ++u.ok;
                } else {
                    ++u.failed;
                    if (u.failedSample == null) u.failedSample = path.substring(path.lastIndexOf('/') + 1);
                }
            }
        } catch (java.io.IOException e) {
            // no file yet
        }
        return out;
    }

    private static void scan(File dir, boolean recursive, Info info) {
        final File[] children = dir.listFiles();
        if (children == null) return;
        for (File f : children) {
            final String name = f.getName();
            if (f.isDirectory()) {
                if (recursive && !name.startsWith(".")) scan(f, true, info);
            } else if (isTextureName(name)) {
                final int kind = kind(f);
                if (kind == OTHER) {
                    ++info.unreadable;
                    if (info.unreadableSample == null) info.unreadableSample = name;
                    continue;
                }
                ++info.valid;
                info.bytes += f.length();
                if (kind == DDS) ++info.dds;
                else if (name.toLowerCase(java.util.Locale.ROOT).endsWith(".dds")) ++info.renamed;
                if (!info.keys.add(key(name))) ++info.duplicates;
            } else if (isImage(name) && !name.toLowerCase(java.util.Locale.ROOT).contains("_mip")) {
                ++info.bad;
                if (info.badSample == null) info.badSample = name;
            }
        }
    }
}
