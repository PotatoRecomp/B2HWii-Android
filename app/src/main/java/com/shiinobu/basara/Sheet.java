package com.shiinobu.basara;

import android.app.Activity;
import android.app.Dialog;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * A rounded dark card over a dimmed screen, in the launcher's colours: a title and line under it, then
 * content rows (steps, notes) and big action buttons. Used for Help and the in-game Back menu.
 */
final class Sheet {
    private final Activity activity;
    private final Dialog dialog;
    private final LinearLayout body;
    private final LinearLayout actions;
    private boolean horizontal;
    private ScrollView scroll;
    private final LinearLayout card;

    Sheet(Activity activity, String title, String subtitle) {
        this.activity = activity;
        body = new LinearLayout(activity);
        body.setOrientation(LinearLayout.VERTICAL);

        final TextView heading = text(title, 22, Color.WHITE);
        heading.setTypeface(Typeface.DEFAULT_BOLD);
        body.addView(heading);
        if (subtitle != null) {
            final TextView line = text(subtitle, 14, LauncherActivity.MUTED);
            line.setPadding(0, dp(4), 0, 0);
            body.addView(line);
        }

        actions = new LinearLayout(activity);
        actions.setOrientation(LinearLayout.VERTICAL);

        card = new LinearLayout(activity);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(24), dp(22), dp(24), dp(20));
        card.setBackground(rounded(LauncherActivity.SURFACE, dp(24)));
        card.addView(body);
        card.addView(actions, matchWidth(dp(6)));
        scroll = new ScrollView(activity);
        scroll.setVerticalScrollBarEnabled(false);
        scroll.addView(card);

        dialog = new Dialog(activity);
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
        dialog.setContentView(scroll);
        final Window window = dialog.getWindow();
        if (window != null) {
            window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            window.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
            window.setDimAmount(0.6f);
            final int width = Math.min(dp(560),
                    Math.round(activity.getResources().getDisplayMetrics().widthPixels * 0.9f));
            window.setLayout(width, WindowManager.LayoutParams.WRAP_CONTENT);
        }
        dialog.setOnShowListener(d -> {
            SystemBars.hide(window);
        });
    }

    /** A numbered step (or a mark such as "?" or "!"): a coloured circle, a bold line and text under it. */
    Sheet step(String mark, String title, String about) {
        final LinearLayout row = new LinearLayout(activity);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setPadding(dp(14), dp(12), dp(14), dp(12));
        row.setBackground(rounded(LauncherActivity.RAISED, dp(16)));
        final TextView badge = text(mark, 15, Color.WHITE);
        badge.setTypeface(Typeface.DEFAULT_BOLD);
        badge.setGravity(Gravity.CENTER);
        final GradientDrawable circle = new GradientDrawable();
        circle.setShape(GradientDrawable.OVAL);
        circle.setColor(LauncherActivity.ACCENT);
        badge.setBackground(circle);
        row.addView(badge, new LinearLayout.LayoutParams(dp(32), dp(32)));
        final LinearLayout words = new LinearLayout(activity);
        words.setOrientation(LinearLayout.VERTICAL);
        final TextView name = text(title, 16, Color.WHITE);
        name.setTypeface(LauncherActivity.MEDIUM);
        words.addView(name);
        if (about != null) {
            final TextView line = text(about, 14, LauncherActivity.MUTED);
            line.setPadding(0, dp(2), 0, 0);
            words.addView(line);
        }
        final LinearLayout.LayoutParams wordsParams =
                new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        wordsParams.leftMargin = dp(14);
        row.addView(words, wordsParams);
        body.addView(row, matchWidth(dp(12)));
        return this;
    }

    /** A small heading between groups of steps. */
    Sheet heading(String title) {
        final TextView view = text(title.toUpperCase(java.util.Locale.ROOT), 12, LauncherActivity.FAINT);
        view.setTypeface(Typeface.DEFAULT_BOLD);
        view.setLetterSpacing(0.08f);
        view.setPadding(dp(4), dp(18), 0, 0);
        body.addView(view);
        return this;
    }

    /** A red-tinted box with a bold warning line: for risky steps. */
    Sheet warning(String title, String about) {
        body.addView(warningBox(activity, title, about), matchWidth(dp(12)));
        return this;
    }

    /** The warning box, shared with the settings screen (Cheats). */
    static LinearLayout warningBox(Activity activity, String title, String about) {
        final float density = activity.getResources().getDisplayMetrics().density;
        final LinearLayout box = new LinearLayout(activity);
        box.setOrientation(LinearLayout.VERTICAL);
        final int pad = Math.round(12 * density);
        box.setPadding(pad, Math.round(10 * density), pad, Math.round(10 * density));
        final GradientDrawable shape = new GradientDrawable();
        shape.setCornerRadius(Math.round(14 * density));
        shape.setColor(WARNING_FILL);
        shape.setStroke(Math.max(1, Math.round(density)), WARNING_TEXT);
        box.setBackground(shape);
        final TextView name = new TextView(activity);
        name.setText("⚠  " + title);
        name.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        name.setTextColor(WARNING_TEXT);
        name.setTypeface(Typeface.DEFAULT_BOLD);
        box.addView(name);
        if (about != null) {
            final TextView line = new TextView(activity);
            line.setText(about);
            line.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
            line.setTextColor(Color.rgb(255, 205, 200));
            line.setPadding(0, Math.round(2 * density), 0, 0);
            box.addView(line);
        }
        return box;
    }

    static final int WARNING_FILL = Color.rgb(64, 22, 24);
    static final int WARNING_TEXT = Color.rgb(255, 120, 110);

    /**
     * Keeps the action buttons in view under the card's content: only the title, text and rows above
     * them scroll (a long list).
     */
    Sheet pinActions() {
        scroll.removeView(card);
        card.removeView(body);
        card.removeView(actions);
        scroll = new ScrollView(activity);
        scroll.setVerticalScrollBarEnabled(false);
        scroll.setVerticalFadingEdgeEnabled(true);
        scroll.setFadingEdgeLength(dp(16));
        scroll.addView(body);
        card.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        card.addView(actions, matchWidth(dp(6)));
        dialog.setContentView(card);
        return this;
    }

    /** Any view in the body (a progress bar, a list). */
    Sheet view(View view) {
        body.addView(view, matchWidth(dp(12)));
        return this;
    }

    /** A wrapped text paragraph. */
    Sheet paragraph(String value) {
        final TextView view = text(value, 14, Color.rgb(200, 206, 212));
        view.setPadding(dp(4), dp(10), 0, 0);
        body.addView(view);
        return this;
    }

    /** False: Back and taps outside do nothing (work in progress). */
    Sheet cancelable(boolean cancelable) {
        dialog.setCancelable(cancelable);
        dialog.setCanceledOnTouchOutside(cancelable);
        return this;
    }

    void scrollToTop() {
        scroll.scrollTo(0, 0);
    }

    void dismiss() {
        dialog.dismiss();
    }

    /**
     * A button that keeps the sheet open: `run` gets its label, to change it (e.g. "Stopping…").
     * Returns the button, so it can be disabled.
     */
    View actionStay(String label, boolean danger, java.util.function.Consumer<TextView> run) {
        final LinearLayout button = new LinearLayout(activity);
        button.setGravity(Gravity.CENTER_VERTICAL);
        button.setPadding(dp(18), dp(12), dp(18), dp(12));
        button.setBackground(rounded(LauncherActivity.RAISED, dp(18)));
        final TextView name = text(label, 16, danger ? WARNING_TEXT : Color.WHITE);
        name.setTypeface(Typeface.DEFAULT_BOLD);
        button.addView(name);
        button.setClickable(true);
        button.setOnClickListener(v -> run.accept(name));
        actions.addView(button, matchWidth(dp(10)));
        return button;
    }

    /** Plain faint text, such as the version. */
    Sheet note(String value) {
        final TextView view = text(value, 12, LauncherActivity.FAINT);
        view.setPadding(dp(4), dp(14), 0, 0);
        body.addView(view);
        return this;
    }

    /** A full-width button: `primary` is filled with the accent, `danger` is red text on grey. */
    Sheet action(String label, String about, boolean primary, boolean danger, Runnable run) {
        final LinearLayout button = new LinearLayout(activity);
        button.setOrientation(LinearLayout.VERTICAL);
        button.setGravity(Gravity.CENTER_VERTICAL);
        button.setPadding(dp(18), dp(12), dp(18), dp(12));
        button.setBackground(rounded(primary ? LauncherActivity.ACCENT : LauncherActivity.RAISED, dp(18)));
        button.setClickable(true);
        button.setOnClickListener(v -> {
            dialog.dismiss();
            if (run != null) run.run();
        });
        final TextView name = text(label, 16, danger ? Color.rgb(255, 120, 110) : Color.WHITE);
        name.setTypeface(Typeface.DEFAULT_BOLD);
        button.addView(name);
        if (about != null) {
            final TextView line = text(about, 13, primary ? Color.argb(220, 255, 255, 255) : LauncherActivity.MUTED);
            button.addView(line);
        }
        if (horizontal) {
            final LinearLayout.LayoutParams params =
                    new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f);
            params.topMargin = dp(14);
            params.leftMargin = actions.getChildCount() == 0 ? 0 : dp(10);
            button.setGravity(Gravity.TOP);
            button.setMinimumHeight(dp(96));
            actions.addView(button, params);
        } else {
            actions.addView(button, matchWidth(dp(10)));
        }
        return this;
    }

    /** Actions side by side, sharing the width (a wider card); and taps outside the card do nothing. */
    Sheet horizontal() {
        horizontal = true;
        actions.setOrientation(LinearLayout.HORIZONTAL);
        actions.setBaselineAligned(false);
        dialog.setCanceledOnTouchOutside(false);
        final Window window = dialog.getWindow();
        if (window != null) {
            window.setLayout(Math.round(activity.getResources().getDisplayMetrics().widthPixels * 0.86f),
                    WindowManager.LayoutParams.WRAP_CONTENT);
        }
        return this;
    }

    /** Back (or tapping outside, unless horizontal) closes it as if this ran. */
    Sheet onCancel(Runnable run) {
        dialog.setOnCancelListener(d -> run.run());
        return this;
    }

    /**
     * Gamepad input goes to `keys` first, and stick, trigger and hat moves to `motion`, before the
     * sheet's own buttons see them; each returns true for an event it took.
     */
    Sheet onGamepad(android.content.DialogInterface.OnKeyListener keys,
                    java.util.function.Predicate<android.view.MotionEvent> motion) {
        dialog.setOnKeyListener(keys);
        final Window window = dialog.getWindow();
        if (window != null && motion != null) {
            // A motion event only reaches a focused view, so it is taken at the window instead.
            final Window.Callback inner = window.getCallback();
            window.setCallback((Window.Callback) java.lang.reflect.Proxy.newProxyInstance(
                    Window.Callback.class.getClassLoader(), new Class<?>[] {Window.Callback.class},
                    (proxy, method, args) -> {
                        if (method.getName().equals("dispatchGenericMotionEvent")
                                && motion.test((android.view.MotionEvent) args[0])) {
                            return true;
                        }
                        try {
                            return method.invoke(inner, args);
                        } catch (java.lang.reflect.InvocationTargetException e) {
                            throw e.getCause();
                        }
                    }));
        }
        return this;
    }

    Sheet onDismiss(Runnable run) {
        dialog.setOnDismissListener(d -> run.run());
        return this;
    }

    void show() {
        dialog.show();
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
