package com.shiinobu.basara;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.net.HttpURLConnection;

/**
 * The testers' Beta (BuildConfig.BETA) stops once it is switched off or the release is out:
 * <ul>
 * <li>the latest GitHub release is RELEASE_VERSION or later, or</li>
 * <li>beta-channel.json in shiinobu/basara2heroes-beta-android says
 * {"enabled": false, "message": "..."}, or {"min_beta": N} with this Beta's number below N.</li>
 * </ul>
 * The status file sits in the release repo; while that repo is private GitHub answers 404
 * without a login (treated as no file). It is checked even without ONLINE_UPDATES.
 * No status file, or no network, leaves the Beta playable until a check sees it end; LauncherActivity
 * keeps that answer (AppSettings.betaEnded), so going offline afterwards doesn't reopen it.
 * Testing: debug.basara.update_url and debug.basara.beta_url point both checks at a local server.
 */
final class BetaGate {
    static final String RELEASE_VERSION = "1.0";
    private static final String BETA_STATUS =
            "https://raw.githubusercontent.com/shiinobu/basara2heroes-beta-android/main/beta-channel.json";

    private BetaGate() {}

    /** Null while the Beta may run; otherwise why it ended, for the tester. Network: off the UI thread. */
    static String check() throws IOException {
        if (BuildConfig.ONLINE_UPDATES) {
            final Updater.Release release = Updater.latest();
            if (release != null && !Updater.isNewer(RELEASE_VERSION, release.version)) {
                return "Basara " + release.version + " is out.";
            }
        }
        final HttpURLConnection connection =
                Updater.open(Updater.debugProperty("debug.basara.beta_url", BETA_STATUS));
        try {
            final int code = connection.getResponseCode();
            if (code == HttpURLConnection.HTTP_NOT_FOUND) {
                return null;
            }
            if (code != HttpURLConnection.HTTP_OK) {
                throw new IOException("GitHub answered " + code);
            }
            final JSONObject status = new JSONObject(Updater.readAll(connection.getInputStream()));
            if (!status.optBoolean("enabled", true)) {
                return status.optString("message", "The Beta has been switched off.");
            }
            if (BuildConfig.VERSION_CODE < status.optInt("min_beta", 0)) {
                return status.optString("min_beta_message",
                        "This Beta is too old. Ask the developer for the new one.");
            }
            return null;
        } catch (JSONException e) {
            throw new IOException("unreadable beta-channel.json: " + e.getMessage());
        } finally {
            connection.disconnect();
        }
    }
}
