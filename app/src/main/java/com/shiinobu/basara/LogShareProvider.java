package com.shiinobu.basara;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;

/**
 * Hands the zip LogShare made (cache/share/<name>) to the app the player shares it with, read only.
 * Nothing else is served: only plain names inside that one folder.
 */
public final class LogShareProvider extends ContentProvider {
    static String authority() { return BuildConfig.APPLICATION_ID + ".logs"; }

    static File dir(Context context) {
        final File dir = new File(context.getCacheDir(), "share");
        //noinspection ResultOfMethodCallIgnored
        dir.mkdirs();
        return dir;
    }

    private File file(Uri uri) throws FileNotFoundException {
        final String name = uri.getLastPathSegment();
        if (name == null || name.contains("/") || name.startsWith(".")) throw new FileNotFoundException(String.valueOf(uri));
        final File file = new File(dir(getContext()), name);
        if (!file.isFile()) throw new FileNotFoundException(String.valueOf(uri));
        return file;
    }

    @Override public boolean onCreate() { return true; }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (!"r".equals(mode)) throw new FileNotFoundException("read only");
        return ParcelFileDescriptor.open(file(uri), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] args, String sort) {
        final File file;
        try {
            file = file(uri);
        } catch (FileNotFoundException e) {
            return null;
        }
        final MatrixCursor cursor = new MatrixCursor(new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE}, 1);
        cursor.addRow(new Object[] {file.getName(), file.length()});
        return cursor;
    }

    @Override public String getType(Uri uri) { return "application/zip"; }
    @Override public Uri insert(Uri uri, ContentValues values) { return null; }
    @Override public int delete(Uri uri, String selection, String[] args) { return 0; }
    @Override public int update(Uri uri, ContentValues values, String selection, String[] args) { return 0; }
}
