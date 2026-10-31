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

        final LinearLayout card = new LinearLayout(activity);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(24), dp(22), dp(24), dp(20));
        card.setBackground(rounded(LauncherActivity.SURFACE, dp(24)));
        card.addView(body);
        card.addView(actions, matchWidth(dp(6)));
        final ScrollView scroll = new ScrollView(activity);
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
            if (window == null) return;
            window.setDecorFitsSystemWindows(false);
            final WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
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
