package com.shiinobu.basara;

import java.io.IOException;

/** Unpacks the player's ISO or RVZ into the game folder, or looks into it (libbasara_disc.so, runtime/disc). */
final class DiscImport {
    static {
        System.loadLibrary("basara_disc");
    }

    private DiscImport() {}

    interface Listener {
        /** False stops the extraction. Called on the extracting thread. */
        boolean onProgress(long done, long total);
    }

    /** What extract returns when the listener stopped it. */
    static final String CANCELLED = "";

    /** The disc's first 8 bytes (game ID and revision); thrown when the file can't be read as a disc. */
    static native byte[] readHeader(int fd) throws IOException;

    /** Null when the game is in `outDir`, CANCELLED, or why it failed. */
    static native String extract(int fd, String outDir, long freeBytes, Listener listener);

    /** The sizes of the game's files at `paths` ("files/linkData.afs"), -1 for one it lacks; thrown as readHeader. */
    static native long[] fileSizes(int fd, String[] paths) throws IOException;
}
