package com.shiinobu.basara;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * The app's look, shared by the launcher, its settings and the in-game menu: near-black surfaces with
 * thin borders, small chips, one red accent. Every view is built in code.
 */
final class Ui {
    private Ui() {}

    static final int BG = Color.rgb(10, 11, 13);
    static final int ELEVATED = Color.rgb(14, 16, 20);
    static final int SURFACE = Color.rgb(18, 20, 24);
    static final int ACTIVE = Color.rgb(26, 29, 36);
    static final int BORDER = Color.rgb(34, 38, 46);
    static final int BORDER_STRONG = Color.rgb(46, 52, 64);
    static final int TEXT = Color.rgb(243, 244, 246);
    static final int SECONDARY = Color.rgb(156, 163, 175);
    static final int MUTED = Color.rgb(107, 114, 128);
    static final int ACCENT = Color.rgb(220, 38, 38);
    static final int ACCENT_PRESSED = Color.rgb(225, 29, 72);
    static final int ACCENT_SUBTLE = Color.argb(36, 220, 38, 38);
    static final int WARNING = Color.rgb(245, 158, 11);
    static final int SUCCESS = Color.rgb(16, 185, 129);
    static final int DANGER_TEXT = Color.rgb(252, 165, 165);
    static final int DANGER_FILL = Color.argb(20, 220, 38, 38);
    static final int DANGER_BORDER = Color.argb(77, 220, 38, 38);

    static final Typeface MEDIUM = Typeface.create("sans-serif-medium", Typeface.NORMAL);
    static final Typeface MONO = Typeface.MONOSPACE;

    static int dp(Context context, float value) {
        return Math.round(value * context.getResources().getDisplayMetrics().density);
    }

    /** A filled rounded box with a 1 dp border (none when `border` is 0). */
    static GradientDrawable box(Context context, int fill, int border, float radiusDp) {
        final GradientDrawable shape = new GradientDrawable();
        shape.setCornerRadius(dp(context, radiusDp));
        shape.setColor(fill);
        if (border != 0) shape.setStroke(Math.max(1, dp(context, 1)), border);
        return shape;
    }

    /** `box`, rippling when touched. */
    static Drawable touchBox(Context context, int fill, int border, float radiusDp) {
        final GradientDrawable mask = box(context, Color.WHITE, 0, radiusDp);
        return new RippleDrawable(ColorStateList.valueOf(Color.argb(48, 255, 255, 255)),
                box(context, fill, border, radiusDp), mask);
    }

    static TextView text(Context context, String value, float sp, int color) {
        final TextView view = new TextView(context);
        view.setText(value);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        view.setTextColor(color);
        view.setIncludeFontPadding(true);
        return view;
    }

    /** Small capitals with wide spacing: a label over a group. */
    static TextView caption(Context context, String value) {
        final TextView view = text(context, value.toUpperCase(java.util.Locale.ROOT), 10.5f, MUTED);
        view.setTypeface(Typeface.DEFAULT_BOLD);
        view.setLetterSpacing(0.08f);
        return view;
    }

    static ImageView icon(Context context, int drawable, int tint, float sizeDp) {
        final ImageView view = new ImageView(context);
        view.setImageResource(drawable);
        view.setImageTintList(ColorStateList.valueOf(tint));
        view.setLayoutParams(new LinearLayout.LayoutParams(dp(context, sizeDp), dp(context, sizeDp)));
        return view;
    }

    /** A choice chip: the chosen one has a red border on a red tint. */
    static TextView chip(Context context, String label, boolean active, boolean enabled, View.OnClickListener onClick) {
        final TextView chip = text(context, label, 13, active ? TEXT : SECONDARY);
        chip.setTypeface(active ? Typeface.DEFAULT_BOLD : MEDIUM);
        chip.setGravity(Gravity.CENTER);
        chip.setPadding(dp(context, 12), dp(context, 7), dp(context, 12), dp(context, 7));
        chip.setMinWidth(dp(context, 44));
        chip.setBackground(active ? touchBox(context, ACCENT_SUBTLE, ACCENT, 8) : touchBox(context, BG, BORDER, 8));
        if (!enabled) chip.setAlpha(0.4f);
        if (enabled && onClick != null) chip.setOnClickListener(onClick);
        return chip;
    }

    /** The on/off switch: a red track with a white knob when on. */
    static View toggle(Context context, boolean on) {
        final LinearLayout track = new LinearLayout(context);
        track.setGravity((on ? Gravity.END : Gravity.START) | Gravity.CENTER_VERTICAL);
        track.setPadding(dp(context, 3), 0, dp(context, 3), 0);
        track.setBackground(on ? box(context, ACCENT, ACCENT, 11) : box(context, BG, BORDER_STRONG, 11));
        final View knob = new View(context);
        final GradientDrawable dot = new GradientDrawable();
        dot.setShape(GradientDrawable.OVAL);
        dot.setColor(on ? Color.WHITE : SECONDARY);
        knob.setBackground(dot);
        track.addView(knob, new LinearLayout.LayoutParams(dp(context, 15), dp(context, 15)));
        track.setLayoutParams(new LinearLayout.LayoutParams(dp(context, 40), dp(context, 23)));
        return track;
    }

    /** A button with an icon and a label: primary (red), plain, or danger. */
    static LinearLayout button(Context context, int iconRes, String label, int style, View.OnClickListener onClick) {
        final LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(dp(context, 12), dp(context, 10), dp(context, 12), dp(context, 10));
        final int fill = style == PRIMARY ? ACCENT : style == DANGER ? DANGER_FILL : SURFACE;
        final int border = style == PRIMARY ? ACCENT_PRESSED : style == DANGER ? DANGER_BORDER : BORDER;
        final int color = style == PRIMARY ? Color.WHITE : style == DANGER ? DANGER_TEXT : TEXT;
        row.setBackground(touchBox(context, fill, border, 8));
        if (iconRes != 0) {
            row.addView(icon(context, iconRes, style == PRIMARY ? Color.WHITE : style == DANGER
                    ? Color.rgb(248, 113, 113) : SECONDARY, 16));
        }
        final TextView name = text(context, label, 13, color);
        name.setTypeface(style == PRIMARY ? Typeface.DEFAULT_BOLD : MEDIUM);
        final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.leftMargin = iconRes != 0 ? dp(context, 9) : 0;
        row.addView(name, params);
        row.setClickable(true);
        row.setOnClickListener(onClick);
        return row;
    }

    static final int PLAIN = 0, PRIMARY = 1, DANGER = 2;

    static LinearLayout.LayoutParams matchWidth(int topMarginPx) {
        final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.topMargin = topMarginPx;
        return params;
    }

    /** Children left to right, starting a new line when the next one does not fit. */
    static final class FlowLayout extends ViewGroup {
        private final int gap;

        FlowLayout(Context context, int gap) {
            super(context);
            this.gap = gap;
        }

        @Override
        protected void onMeasure(int widthSpec, int heightSpec) {
            final int max = MeasureSpec.getSize(widthSpec) - getPaddingLeft() - getPaddingRight();
            final boolean bounded = MeasureSpec.getMode(widthSpec) != MeasureSpec.UNSPECIFIED;
            int x = 0, y = 0, lineH = 0, widest = 0;
            for (int i = 0; i < getChildCount(); ++i) {
                final View c = getChildAt(i);
                if (c.getVisibility() == GONE) continue;
                c.measure(MeasureSpec.makeMeasureSpec(bounded ? max : 0,
                                bounded ? MeasureSpec.AT_MOST : MeasureSpec.UNSPECIFIED),
                        MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED));
                if (x > 0 && bounded && x + c.getMeasuredWidth() > max) {
                    x = 0;
                    y += lineH + gap;
                    lineH = 0;
                }
                x += c.getMeasuredWidth();
                widest = Math.max(widest, x);
                x += gap;
                lineH = Math.max(lineH, c.getMeasuredHeight());
            }
            final int w = bounded ? MeasureSpec.getSize(widthSpec) : widest + getPaddingLeft() + getPaddingRight();
            setMeasuredDimension(w, resolveSize(y + lineH + getPaddingTop() + getPaddingBottom(), heightSpec));
        }

        @Override
        protected void onLayout(boolean changed, int l, int t, int r, int b) {
            final int max = r - l - getPaddingRight();
            int x = getPaddingLeft(), y = getPaddingTop(), lineH = 0;
            for (int i = 0; i < getChildCount(); ++i) {
                final View c = getChildAt(i);
                if (c.getVisibility() == GONE) continue;
                final int cw = c.getMeasuredWidth(), ch = c.getMeasuredHeight();
                if (x > getPaddingLeft() && x + cw > max) {
                    x = getPaddingLeft();
                    y += lineH + gap;
                    lineH = 0;
                }
                c.layout(x, y, x + cw, y + ch);
                x += cw + gap;
                lineH = Math.max(lineH, ch);
            }
        }
    }
}
