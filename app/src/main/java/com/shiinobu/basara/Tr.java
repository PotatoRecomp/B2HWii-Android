package com.shiinobu.basara;

import android.content.Context;
import android.util.Log;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/**
 * The launcher's language. The source keeps the English text; a translation is a file in assets/lang
 * (one "English<TAB>translation" pair a line, \n for a line break), and any text missing from it
 * stays English. Auto picks Indonesian on a phone set to Indonesia (its language or region), English
 * everywhere else.
 */
final class Tr {
    static final int AUTO = 0, ENGLISH = 1, INDONESIAN = 2;
    private static final Map<String, String> table = new HashMap<>();
    private static int loaded = -1;

    /** Loads the language the settings ask for; call again after the setting changes. */
    static void init(Context context) {
        final int language = resolve(new AppSettings(context).language());
        if (language == loaded) return;
        loaded = language;
        table.clear();
        if (language != INDONESIAN) return;
        try (BufferedReader in = new BufferedReader(new InputStreamReader(
                context.getAssets().open("lang/id.txt"), StandardCharsets.UTF_8))) {
            String line;
            while ((line = in.readLine()) != null) {
                final int tab = line.indexOf('\t');
                if (tab <= 0 || line.startsWith("#")) continue;
                table.put(line.substring(0, tab).replace("\\n", "\n"), line.substring(tab + 1).replace("\\n", "\n"));
            }
        } catch (Exception e) {
            Log.w("Basara", "no translation", e);
        }
    }

    /** Auto: Indonesian when the phone's language or region is Indonesia's, else English. */
    static int resolve(int setting) {
        if (setting != AUTO) return setting;
        final Locale locale = Locale.getDefault();
        final String language = locale.getLanguage();
        return "in".equals(language) || "id".equals(language) || "ID".equals(locale.getCountry())
                ? INDONESIAN : ENGLISH;
    }

    static String t(String english) {
        final String translated = table.get(english);
        return translated != null ? translated : english;
    }

    private Tr() {}
}
