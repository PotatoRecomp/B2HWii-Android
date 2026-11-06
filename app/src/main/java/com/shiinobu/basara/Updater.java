package com.shiinobu.basara;

import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/**
 * Updates from the project's GitHub releases: finds the latest release, streams its Android APK
 * into a PackageInstaller session while checking it against the SHA-256 GitHub lists for the file,
 * and commits it. Android installs it over this app only when it is signed with the same key; the
 * first update asks the player to allow installs from this app and to confirm (UpdateReceiver).
 */
final class Updater {
    private static final String LATEST_RELEASE =
            "https://api.github.com/repos/PotatoRecomp/B2HWii-Android/releases/latest";
    private static final String APK_SUFFIX = "-android-arm64.apk";
    /** Every release, newest first: the Beta looks for its pre-releases here. */
    private static final String ALL_RELEASES =
            "https://api.github.com/repos/PotatoRecomp/B2HWii-Android/releases?per_page=30";
    private static final int TIMEOUT_MS = 15000;

    /** The latest release's Android download. */
    static final class Release {
        String version;   // "0.2.1", from the tag "v0.2.1"
        String apkUrl;
        long size;
        String sha256;    // lowercase hex, or null when GitHub lists none
        int beta;         // a Beta's number, from the tag "v0.0.1-beta.5" (0: not a Beta)
        /** Older versions must update: `<!-- basara-min-version: 1.1 -->` in the release text. */
        String minVersion;

        /** Must `installed` update before it can play? */
        boolean required(String installed) {
            return minVersion != null && installed != null && isNewer(minVersion, installed);
        }

        Bundle toBundle() {
            final Bundle bundle = new Bundle();
            bundle.putString("version", version);
            bundle.putString("apkUrl", apkUrl);
            bundle.putLong("size", size);
            bundle.putString("sha256", sha256);
            bundle.putInt("beta", beta);
            bundle.putString("minVersion", minVersion);
            return bundle;
        }

        static Release from(Bundle bundle) {
            if (bundle == null || bundle.getString("apkUrl") == null) {
                return null;
            }
            final Release release = new Release();
            release.version = bundle.getString("version");
            release.apkUrl = bundle.getString("apkUrl");
            release.size = bundle.getLong("size", -1);
            release.sha256 = bundle.getString("sha256");
            release.beta = bundle.getInt("beta", 0);
            release.minVersion = bundle.getString("minVersion");
            return release;
        }
    }

    interface Progress { void update(long done, long total); }

    private Updater() {}

    /**
     * The latest release (apkUrl null when it has no Android download), or null when nothing is
     * released yet. Network: call off the UI thread.
     */
    static Release latest() throws IOException {
        final HttpURLConnection connection = open(releaseUrl());
        connection.setRequestProperty("Accept", "application/vnd.github+json");
        try {
            final int code;
            try {
                code = connection.getResponseCode();
            } catch (IOException e) {
                throw new IOException("couldn't reach GitHub. Check the internet connection and try again.");
            }
            if (code == HttpURLConnection.HTTP_NOT_FOUND) {
                return null;  // nothing released yet
            }
            if (code != HttpURLConnection.HTTP_OK) {
                throw new IOException("GitHub answered " + code);
            }
            final JSONObject json = new JSONObject(readAll(connection.getInputStream()));
            final Release release = new Release();
            release.version = json.getString("tag_name").replaceFirst("^[vV]", "");
            final java.util.regex.Matcher min = java.util.regex.Pattern
                    .compile("<!--\\s*basara-min-version:\\s*([0-9.]+)\\s*-->").matcher(json.optString("body", ""));
            if (min.find()) release.minVersion = min.group(1);
            final JSONArray assets = json.getJSONArray("assets");
            for (int i = 0; i < assets.length(); ++i) {
                final JSONObject asset = assets.getJSONObject(i);
                if (asset.getString("name").endsWith(APK_SUFFIX)) {
                    release.apkUrl = asset.getString("browser_download_url");
                    release.size = asset.optLong("size", -1);
                    final String digest = asset.optString("digest", "");
                    release.sha256 = digest.startsWith("sha256:") ? digest.substring(7).toLowerCase() : null;
                    return release;
                }
            }
            return release;  // apkUrl null: no Android download
        } catch (JSONException e) {
            throw new IOException("unreadable answer from GitHub: " + e.getMessage());
        } finally {
            connection.disconnect();
        }
    }

    /**
     * The newest Beta pre-release ("v0.0.1-beta.5" with a "-beta-android-arm64.apk" download), or null
     * when there is none. Network: call off the UI thread.
     */
    static Release latestBeta() throws IOException {
        final HttpURLConnection connection = open(debugProperty("debug.basara.beta_releases_url", ALL_RELEASES));
        connection.setRequestProperty("Accept", "application/vnd.github+json");
        try {
            final int code;
            try {
                code = connection.getResponseCode();
            } catch (IOException e) {
                throw new IOException("couldn't reach GitHub. Check the internet connection and try again.");
            }
            if (code == HttpURLConnection.HTTP_NOT_FOUND) return null;
            if (code != HttpURLConnection.HTTP_OK) throw new IOException("GitHub answered " + code);
            final JSONArray releases = new JSONArray(readAll(connection.getInputStream()));
            Release best = null;
            for (int r = 0; r < releases.length(); ++r) {
                final JSONObject json = releases.getJSONObject(r);
                if (json.optBoolean("draft", false)) continue;
                final java.util.regex.Matcher tag = java.util.regex.Pattern
                        .compile("^[vV]?([0-9.]+)-beta\\.([0-9]+)$").matcher(json.optString("tag_name", ""));
                if (!tag.matches()) continue;
                final int number = Integer.parseInt(tag.group(2));
                if (best != null && number <= best.beta) continue;
                final JSONArray assets = json.getJSONArray("assets");
                for (int i = 0; i < assets.length(); ++i) {
                    final JSONObject asset = assets.getJSONObject(i);
                    if (!asset.getString("name").endsWith("-beta" + APK_SUFFIX)) continue;
                    final Release release = new Release();
                    release.version = tag.group(1) + " Beta " + number;
                    release.beta = number;
                    release.apkUrl = asset.getString("browser_download_url");
                    release.size = asset.optLong("size", -1);
                    final String digest = asset.optString("digest", "");
                    release.sha256 = digest.startsWith("sha256:") ? digest.substring(7).toLowerCase() : null;
                    best = release;
                    break;
                }
            }
            return best;
        } catch (JSONException e) {
            throw new IOException("unreadable answer from GitHub: " + e.getMessage());
        } finally {
            connection.disconnect();
        }
    }

    /** The Beta's number (its versionCode), or 0 for the release. */
    static int installedBeta(Context context) {
        if (!BuildConfig.BETA) return 0;
        try {
            return (int) context.getPackageManager().getPackageInfo(context.getPackageName(), 0).getLongVersionCode();
        } catch (PackageManager.NameNotFoundException e) {
            return 0;
        }
    }

    /** "0.0.1 Beta 5", "0.0.1 Final Beta" (BETA_LABEL), or "0.0.1" for the release: how the app names itself. */
    static String displayVersion(Context context) {
        final int beta = installedBeta(context);
        if (beta > 0 && !BuildConfig.BETA_LABEL.isEmpty()) return installedVersion(context) + " " + BuildConfig.BETA_LABEL;
        return installedVersion(context) + (beta > 0 ? " Beta " + beta : "");
    }

    static String installedVersion(Context context) {
        try {
            final PackageInfo info = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
            return info.versionName;
        } catch (PackageManager.NameNotFoundException e) {
            return "0";
        }
    }

    /** True when `remote` ("0.2.1") is a later version than `local`. */
    static boolean isNewer(String remote, String local) {
        final String[] a = remote.split("\\."), b = local.split("\\.");
        for (int i = 0; i < Math.max(a.length, b.length); ++i) {
            final int x = i < a.length ? number(a[i]) : 0, y = i < b.length ? number(b[i]) : 0;
            if (x != y) {
                return x > y;
            }
        }
        return false;
    }

    /** May the app hand APKs to the installer? If not, the player turns it on in system settings. */
    static boolean canInstall(Context context) {
        return context.getPackageManager().canRequestPackageInstalls();
    }

    /**
     * Downloads `release` straight into an install session and commits it. Returns when the
     * system has it; UpdateReceiver hears the result (or asks the player to confirm). Off the UI
     * thread.
     */
    static void downloadAndInstall(Context context, Release release, Progress progress) throws IOException {
        final PackageInstaller installer = context.getPackageManager().getPackageInstaller();
        final PackageInstaller.SessionParams params =
                new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setAppPackageName(context.getPackageName());
        if (release.size > 0) {
            params.setSize(release.size);
        }
        if (Build.VERSION.SDK_INT >= 31) {
            // Once this app installed the game, Android 12+ lets later updates skip the confirmation.
            params.setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_NOT_REQUIRED);
        }
        final int sessionId = installer.createSession(params);
        final PackageInstaller.Session session = installer.openSession(sessionId);
        boolean committed = false;
        try {
            final MessageDigest sha256;
            try {
                sha256 = MessageDigest.getInstance("SHA-256");
            } catch (NoSuchAlgorithmException e) {
                throw new IOException("SHA-256 unavailable");
            }
            final HttpURLConnection connection = open(release.apkUrl);
            try {
                if (connection.getResponseCode() != HttpURLConnection.HTTP_OK) {
                    throw new IOException("the download answered " + connection.getResponseCode());
                }
                final long total = release.size > 0 ? release.size : connection.getContentLengthLong();
                try (InputStream in = connection.getInputStream();
                     OutputStream out = session.openWrite("base.apk", 0, total > 0 ? total : -1)) {
                    final byte[] buffer = new byte[1 << 16];
                    long done = 0;
                    for (int n; (n = in.read(buffer)) > 0; ) {
                        out.write(buffer, 0, n);
                        sha256.update(buffer, 0, n);
                        done += n;
                        progress.update(done, total);
                    }
                    session.fsync(out);
                }
            } finally {
                connection.disconnect();
            }
            if (release.sha256 != null && !release.sha256.equals(hex(sha256.digest()))) {
                throw new IOException("the download doesn't match the release's checksum");
            }
            final Intent result = new Intent(context, UpdateReceiver.class);
            final PendingIntent sender = PendingIntent.getBroadcast(context, sessionId, result,
                    PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_MUTABLE);
            session.commit(sender.getIntentSender());
            committed = true;
        } finally {
            if (!committed) {
                session.abandon();
            }
            session.close();
        }
    }

    /**
     * Testing: `adb shell setprop debug.basara.update_url http://localhost:PORT/latest.json` (with
     * `adb reverse`) reads a stand-in release from a local server. The installer still accepts only
     * an APK signed with this app's key.
     */
    private static String releaseUrl() {
        return debugProperty("debug.basara.update_url", LATEST_RELEASE);
    }

    /** A `debug.basara.*` system property, or `fallback` when it isn't set. */
    static String debugProperty(String name, String fallback) {
        try {
            final String value = (String) Class.forName("android.os.SystemProperties")
                    .getMethod("get", String.class).invoke(null, name);
            if (value != null && !value.isEmpty()) {
                return value;
            }
        } catch (ReflectiveOperationException | RuntimeException ignored) {
            // No override.
        }
        return fallback;
    }

    static HttpURLConnection open(String url) throws IOException {
        final HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
        connection.setConnectTimeout(TIMEOUT_MS);
        connection.setReadTimeout(TIMEOUT_MS);
        connection.setInstanceFollowRedirects(true);  // GitHub's downloads redirect to its file host
        connection.setRequestProperty("User-Agent", "Basara-Android-Updater");
        return connection;
    }

    static String readAll(InputStream in) throws IOException {
        try (InputStream stream = in) {
            final ByteArrayOutputStream out = new ByteArrayOutputStream();
            final byte[] buffer = new byte[8192];
            for (int n; (n = stream.read(buffer)) > 0; ) {
                out.write(buffer, 0, n);
            }
            return out.toString("UTF-8");
        }
    }

    private static int number(String part) {
        int value = 0;
        for (int i = 0; i < part.length() && Character.isDigit(part.charAt(i)); ++i) {
            value = value * 10 + (part.charAt(i) - '0');
        }
        return value;
    }

    private static String hex(byte[] bytes) {
        final StringBuilder text = new StringBuilder(bytes.length * 2);
        for (byte b : bytes) {
            text.append(Character.forDigit((b >> 4) & 0xF, 16)).append(Character.forDigit(b & 0xF, 16));
        }
        return text.toString();
    }
}
