package com.shiinobu.basara;

import android.graphics.Insets;
import android.os.Build;
import android.view.DisplayCutout;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

/**
 * Status and navigation bars. Android 11 and later use WindowInsetsController (unchanged from before);
 * Android 10 has no such API and takes the older system UI flags instead.
 */
final class SystemBars {
    private SystemBars() {}

    private static final int LAYOUT_EDGE_TO_EDGE = View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN;

    /** Drawn edge to edge with the bars hidden; a swipe shows them for a moment. */
    @SuppressWarnings("deprecation")
    static void hide(Window window) {
        if (window == null) return;
        if (Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(false);
            final WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
            return;
        }
        window.getDecorView().setSystemUiVisibility(LAYOUT_EDGE_TO_EDGE | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    /** Drawn edge to edge under visible bars. */
    @SuppressWarnings("deprecation")
    static void show(Window window) {
        if (window == null) return;
        if (Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(false);
            final WindowInsetsController controller = window.getInsetsController();
            if (controller != null) controller.show(WindowInsets.Type.systemBars());
            return;
        }
        window.getDecorView().setSystemUiVisibility(LAYOUT_EDGE_TO_EDGE);
    }

    /** Edge to edge, leaving the bars as they are. */
    @SuppressWarnings("deprecation")
    static void edgeToEdge(Window window) {
        if (window == null) return;
        if (Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(false);
            return;
        }
        final View decor = window.getDecorView();
        decor.setSystemUiVisibility(decor.getSystemUiVisibility() | LAYOUT_EDGE_TO_EDGE);
    }

    /** Insets used up by the listener: WindowInsets.CONSUMED is API 30. */
    @SuppressWarnings("deprecation")
    static WindowInsets consumed(WindowInsets insets) {
        if (Build.VERSION.SDK_INT >= 30) return WindowInsets.CONSUMED;
        return insets.consumeSystemWindowInsets().consumeDisplayCutout().consumeStableInsets();
    }

    /** The room the bars (and, with cutout, the display cutout) take. */
    @SuppressWarnings("deprecation")
    static Insets bars(WindowInsets insets, boolean cutout) {
        if (Build.VERSION.SDK_INT >= 30) {
            return insets.getInsets(cutout ? WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout()
                    : WindowInsets.Type.systemBars());
        }
        Insets bars = insets.getSystemWindowInsets();
        final DisplayCutout notch = cutout ? insets.getDisplayCutout() : null;
        if (notch != null) {
            bars = Insets.max(bars, Insets.of(notch.getSafeInsetLeft(), notch.getSafeInsetTop(),
                    notch.getSafeInsetRight(), notch.getSafeInsetBottom()));
        }
        return bars;
    }
}
