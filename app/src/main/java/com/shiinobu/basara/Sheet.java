package com.shiinobu.basara;

import android.app.Activity;
import android.app.Dialog;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * A dialog as an ImGui window over the dimmed screen: a title bar, then text and content rows, then
 * the buttons. Short buttons sit side by side at the bottom right (the first one added on the right);
 * a button with a line under its name gets a full-width row of its own.
 */
final class Sheet {
    private final Activity activity;
    private final Dialog dialog;
    private final LinearLayout body;
    private final LinearLayout actions;
    private LinearLayout bar;
    private ScrollView scroll;
    private final LinearLayout window;
    private final LinearLayout inner;

    Sheet(Activity activity, String title, String subtitle) {
        this.activity = activity;
        body = new LinearLayout(activity);
        body.setOrientation(LinearLayout.VERTICAL);
        if (subtitle != null) body.addView(text(subtitle, 13, Ui.SECONDARY));

        actions = new LinearLayout(activity);
        actions.setOrientation(LinearLayout.VERTICAL);

        inner = new LinearLayout(activity);
        inner.setOrientation(LinearLayout.VERTICAL);
        inner.setPadding(dp(10), dp(8), dp(10), dp(10));
        inner.addView(body);
        inner.addView(actions, matchWidth(0));
        scroll = new ScrollView(activity);
        scroll.setVerticalScrollBarEnabled(false);
        scroll.addView(inner);

        window = new LinearLayout(activity);
        window.setOrientation(LinearLayout.VERTICAL);
        window.setBackground(Ui.box(activity, Ui.WINDOW, Ui.BORDER, 0));
        window.setPadding(1, 1, 1, 1);
        window.addView(Ui.titleBar(activity, title, null), matchWidth(0));
        window.addView(scroll, matchWidth(0));

        dialog = new Dialog(activity);
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
        dialog.setContentView(window);
        final Window w = dialog.getWindow();
        if (w != null) {
            w.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
            w.addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
            w.setDimAmount(0.65f);
            final int width = Math.min(dp(520),
                    Math.round(activity.getResources().getDisplayMetrics().widthPixels * 0.9f));
            w.setLayout(width, WindowManager.LayoutParams.WRAP_CONTENT);
        }
        dialog.setOnShowListener(d -> SystemBars.hide(w));
    }

    /** A step: its mark ("1", "?", "!") dim before a line, and text under it. */
    Sheet step(String mark, String title, String about) {
        final LinearLayout row = new LinearLayout(activity);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setPadding(dp(8), dp(6), dp(8), dp(6));
        row.setBackgroundColor(Ui.FRAME);
        final TextView badge = text(mark.length() == 1 && Character.isDigit(mark.charAt(0)) ? mark + "." : mark,
                13, Ui.GRAB_ON);
        row.addView(badge, new LinearLayout.LayoutParams(dp(18), LinearLayout.LayoutParams.WRAP_CONTENT));
        final LinearLayout words = new LinearLayout(activity);
        words.setOrientation(LinearLayout.VERTICAL);
        words.addView(text(title, 13, Ui.TEXT));
        if (about != null) words.addView(text(about, 12, Ui.MUTED));
        row.addView(words, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        body.addView(row, matchWidth(dp(6)));
        return this;
    }

    /** A dim heading between groups. */
    Sheet heading(String title) {
        final TextView view = Ui.caption(activity, title);
        view.setPadding(0, dp(10), 0, 0);
        body.addView(view);
        body.addView(Ui.line(activity));
        return this;
    }

    /** A warning: red text in a frame. */
    Sheet warning(String title, String about) {
        body.addView(warningBox(activity, title, about), matchWidth(dp(6)));
        return this;
    }

    /** The warning frame, shared with other screens. */
    static LinearLayout warningBox(Activity activity, String title, String about) {
        final LinearLayout box = new LinearLayout(activity);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(Ui.dp(activity, 8), Ui.dp(activity, 6), Ui.dp(activity, 8), Ui.dp(activity, 6));
        box.setBackground(Ui.box(activity, Ui.FRAME, 0, 0));
        box.addView(Ui.text(activity, "! " + title, 13, WARNING_TEXT));
        if (about != null) box.addView(Ui.text(activity, about, 12, Ui.SECONDARY));
        return box;
    }

    static final int WARNING_FILL = Ui.FRAME;
    static final int WARNING_TEXT = Ui.DANGER_TEXT;

    /** Keeps the buttons in view under the content: only what is above them scrolls (a long list). */
    Sheet pinActions() {
        window.removeView(scroll);
        scroll.removeView(inner);
        inner.removeView(body);
        inner.removeView(actions);
        scroll = new ScrollView(activity);
        scroll.setVerticalScrollBarEnabled(false);
        scroll.addView(body);
        inner.addView(scroll, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        inner.addView(actions, matchWidth(0));
        window.addView(inner, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        return this;
    }

    /** Any view in the body (a progress bar, a list). */
    Sheet view(View view) {
        body.addView(view, matchWidth(dp(6)));
        return this;
    }

    /** A wrapped text paragraph. */
    Sheet paragraph(String value) {
        final TextView view = text(value, 13, Ui.TEXT);
        view.setPadding(0, dp(6), 0, 0);
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
        final TextView button = button(label, false, danger);
        button.setOnClickListener(v -> run.accept(button));
        addToBar(button);
        return button;
    }

    /** Plain dim text, such as the version. */
    Sheet note(String value) {
        final TextView view = text(value, 11, Ui.MUTED);
        view.setPadding(0, dp(8), 0, 0);
        body.addView(view);
        return this;
    }

    /** A button: `primary` in the brighter blue, `danger` with red text. */
    Sheet action(String label, String about, boolean primary, boolean danger, Runnable run) {
        final View.OnClickListener click = v -> {
            dialog.dismiss();
            if (run != null) run.run();
        };
        if (about == null) {
            final TextView button = button(label, primary, danger);
            button.setOnClickListener(click);
            addToBar(button);
            return this;
        }
        // With a line under it: a full-width button of its own, above the short ones.
        final LinearLayout button = new LinearLayout(activity);
        button.setOrientation(LinearLayout.VERTICAL);
        button.setPadding(dp(10), dp(6), dp(10), dp(6));
        button.setBackground(Ui.touchBox(activity, primary ? Ui.BUTTON_ON : Ui.BUTTON, 0, 0));
        button.setClickable(true);
        button.setOnClickListener(click);
        button.addView(text(label, 13, danger ? WARNING_TEXT : Ui.TEXT));
        button.addView(text(about, 11, primary ? Color.argb(210, 255, 255, 255) : Ui.SECONDARY));
        actions.addView(button, bar == null ? actions.getChildCount() : actions.indexOfChild(bar),
                matchWidth(dp(4)));
        return this;
    }

    /** Kept for older callers: the buttons already share a row when they are short. */
    Sheet horizontal() {
        dialog.setCanceledOnTouchOutside(false);
        return this;
    }

    /** Back (or tapping outside) closes it as if this ran. */
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

    // --- pieces --------------------------------------------------------------------------------

    /** The row of short buttons at the bottom right, under a separator; the newest goes on the left. */
    private void addToBar(View button) {
        if (bar == null) {
            actions.addView(Ui.line(activity));
            bar = new LinearLayout(activity);
            bar.setOrientation(LinearLayout.HORIZONTAL);
            bar.setGravity(Gravity.END);
            actions.addView(bar, matchWidth(0));
        }
        final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, dp(30));
        params.leftMargin = dp(4);
        bar.addView(button, 0, params);
    }

    private TextView button(String label, boolean primary, boolean danger) {
        final TextView button = text(label.toUpperCase(java.util.Locale.ROOT), 13, danger ? WARNING_TEXT : Ui.TEXT);
        button.setGravity(Gravity.CENTER);
        button.setSingleLine(true);
        button.setMinWidth(dp(80));
        button.setPadding(dp(10), 0, dp(10), 0);
        button.setBackground(Ui.touchBox(activity, primary ? Ui.BUTTON_ON : Ui.BUTTON, 0, 0));
        button.setClickable(true);
        return button;
    }

    private TextView text(String value, int sp, int color) {
        return Ui.text(activity, value, sp, color);
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
