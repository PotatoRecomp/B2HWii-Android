package com.shiinobu.basara;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * The app's look, shared by the start screen, the in-game menu and every dialog: Dear ImGui's default
 * dark style (square corners, one pixel borders, blue frames and grabs), after the menu hikki
 * (datfooldive) made for DefJam-FFNY-Android. Every view is built in code.
 */
final class Ui {
    private Ui() {}

    // Dear ImGui StyleColorsDark
    static final int WINDOW = 0xF00F0F0F;   // WindowBg
    static final int TITLE = 0xFF294A7A;    // TitleBgActive
    static final int FRAME = 0xFF1D2F48;    // FrameBg
    static final int BUTTON = 0xFF1F3B5E;   // Button
    static final int BUTTON_ON = 0xFF3369AD;  // TabActive
    static final int GRAB = 0xFF3D85E0;     // SliderGrab
    static final int GRAB_ON = 0xFF4296FA;  // SliderGrabActive, CheckMark
    static final int SELECTED = 0x504296FA; // Header

    // The older names, kept for every view that uses them
    static final int BG = 0xFF0F0F0F;
    static final int ELEVATED = 0xFF0F0F0F;
    static final int SURFACE = FRAME;
    static final int ACTIVE = BUTTON;
    static final int BORDER = 0xFF4A4A55;
    static final int BORDER_STRONG = 0xFF4A4A55;
    static final int TEXT = Color.WHITE;
    static final int SECONDARY = 0xFFC8C8C8;
    static final int MUTED = 0xFF808080;    // TextDisabled
    static final int ACCENT = BUTTON_ON;
    static final int ACCENT_PRESSED = GRAB_ON;
    static final int ACCENT_SUBTLE = BUTTON_ON;
    static final int WARNING = 0xFFE0C070;
    static final int SUCCESS = 0xFF6CC070;
    static final int DANGER_TEXT = 0xFFFF8080;
    static final int DANGER_FILL = BUTTON;
    static final int DANGER_BORDER = BORDER;

    static final Typeface MEDIUM = Typeface.DEFAULT;
    static final Typeface MONO = Typeface.MONOSPACE;

    static int dp(Context context, float value) {
        return Math.round(value * context.getResources().getDisplayMetrics().density);
    }

    /** A filled square box with a one pixel border (none when `border` is 0). `radiusDp` is not used: ImGui's corners are square. */
    static GradientDrawable box(Context context, int fill, int border, float radiusDp) {
        final GradientDrawable shape = new GradientDrawable();
        shape.setColor(fill);
        if (border != 0) shape.setStroke(1, border);
        return shape;
    }

    /** `box`, lighter while pressed. */
    static Drawable touchBox(Context context, int fill, int border, float radiusDp) {
        return new RippleDrawable(ColorStateList.valueOf(Color.argb(60, 66, 150, 250)),
                box(context, fill, border, 0), box(context, Color.WHITE, 0, 0));
    }

    static TextView text(Context context, String value, float sp, int color) {
        final TextView view = new TextView(context);
        view.setText(value);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        view.setTextColor(color);
        view.setIncludeFontPadding(true);
        return view;
    }

    /** A dim label over a group, ImGui's SeparatorText. */
    static TextView caption(Context context, String value) {
        return text(context, value.toUpperCase(java.util.Locale.ROOT), 11, MUTED);
    }

    static ImageView icon(Context context, int drawable, int tint, float sizeDp) {
        final ImageView view = new ImageView(context);
        view.setImageResource(drawable);
        view.setImageTintList(ColorStateList.valueOf(tint));
        view.setLayoutParams(new LinearLayout.LayoutParams(dp(context, sizeDp), dp(context, sizeDp)));
        return view;
    }

    /** An ImGui button: the chosen one in the brighter blue. */
    static TextView chip(Context context, String label, boolean active, boolean enabled, View.OnClickListener onClick) {
        final TextView chip = text(context, label, 13, TEXT);
        chip.setGravity(Gravity.CENTER);
        chip.setSingleLine(true);
        chip.setPadding(dp(context, 10), dp(context, 5), dp(context, 10), dp(context, 5));
        chip.setMinWidth(dp(context, 30));
        chip.setMinHeight(dp(context, 28));
        chip.setBackground(touchBox(context, active ? BUTTON_ON : BUTTON, 0, 0));
        if (!enabled) chip.setAlpha(0.4f);
        if (enabled && onClick != null) chip.setOnClickListener(onClick);
        return chip;
    }

    /** An ImGui checkbox: a square frame with a tick when on. */
    static View toggle(Context context, boolean on) {
        final TextView box = text(context, on ? "✓" : "", 15, GRAB_ON);
        box.setGravity(Gravity.CENTER);
        box.setIncludeFontPadding(false);
        box.setBackgroundColor(FRAME);
        box.setLayoutParams(new LinearLayout.LayoutParams(dp(context, 28), dp(context, 28)));
        return box;
    }

    /** An ImGui button with an optional icon: primary (brighter blue), plain, or danger (red text). */
    static LinearLayout button(Context context, int iconRes, String label, int style, View.OnClickListener onClick) {
        final LinearLayout row = new LinearLayout(context);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER);
        row.setPadding(dp(context, 12), dp(context, 6), dp(context, 12), dp(context, 6));
        row.setMinimumHeight(dp(context, 30));
        row.setBackground(touchBox(context, style == PRIMARY ? BUTTON_ON : BUTTON, 0, 0));
        final int color = style == DANGER ? DANGER_TEXT : TEXT;
        if (iconRes != 0) row.addView(icon(context, iconRes, color, 15));
        final TextView name = text(context, label, 13, color);
        name.setSingleLine(true);
        final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.leftMargin = iconRes != 0 ? dp(context, 7) : 0;
        row.addView(name, params);
        row.setClickable(true);
        row.setOnClickListener(onClick);
        return row;
    }

    static final int PLAIN = 0, PRIMARY = 1, DANGER = 2;

    /** ImGui's separator: one pixel of the border colour across. */
    static View line(Context context) {
        final View rule = new View(context);
        rule.setBackgroundColor(BORDER);
        final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 1);
        params.setMargins(0, dp(context, 4), 0, dp(context, 4));
        rule.setLayoutParams(params);
        return rule;
    }

    /** An ImGui window's title bar: the title on the left, `right` (small, dim) on the right or nothing. */
    static LinearLayout titleBar(Context context, String title, String right) {
        final LinearLayout bar = new LinearLayout(context);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setBackgroundColor(TITLE);
        bar.setPadding(dp(context, 8), dp(context, 4), dp(context, 8), dp(context, 4));
        final TextView name = text(context, title, 14, TEXT);
        name.setSingleLine(true);
        name.setEllipsize(android.text.TextUtils.TruncateAt.END);
        bar.addView(name, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        if (right != null) bar.addView(text(context, right, 11, Color.argb(140, 255, 255, 255)));
        return bar;
    }

    static LinearLayout.LayoutParams matchWidth(int topMarginPx) {
        final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.topMargin = topMarginPx;
        return params;
    }

    /**
     * ImGui's SliderInt: a flat frame, a narrow grab at the value and the value written across the
     * middle. A finger anywhere on it takes the grab there; `released` runs when it lets go.
     */
    static final class Slider extends View {
        interface Listener {
            void changed(int step);
            void released(int step);
        }

        private final int steps;
        private int step;
        private String label = "";
        private boolean held;
        private Listener listener;
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final float grab;

        Slider(Context context, int steps, int step) {
            super(context);
            this.steps = Math.max(steps, 1);
            this.step = Math.max(0, Math.min(step, this.steps));
            grab = dp(context, 10);
            paint.setTextSize(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, 13,
                    context.getResources().getDisplayMetrics()));
            paint.setTextAlign(Paint.Align.CENTER);
        }

        void setListener(Listener listener) { this.listener = listener; }

        void setLabel(String value) {
            label = value;
            invalidate();
        }

        @Override
        protected void onDraw(Canvas canvas) {
            final float w = getWidth(), h = getHeight(), pad = 2;
            paint.setColor(FRAME);
            canvas.drawRect(0, 0, w, h, paint);
            final float x = pad + (w - 2 * pad - grab) * step / steps;
            paint.setColor(held ? GRAB_ON : GRAB);
            canvas.drawRect(x, pad, x + grab, h - pad, paint);
            paint.setColor(TEXT);
            canvas.drawText(label, w / 2, h / 2 - (paint.descent() + paint.ascent()) / 2, paint);
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            if (!isEnabled()) return false;
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    held = true;
                    getParent().requestDisallowInterceptTouchEvent(true);
                    // fall through: the press moves the grab as a drag does
                case MotionEvent.ACTION_MOVE: {
                    final float span = Math.max(getWidth() - grab, 1);
                    final int at = Math.max(0, Math.min(steps, Math.round((e.getX() - grab / 2) / span * steps)));
                    if (at != step) {
                        step = at;
                        if (listener != null) listener.changed(at);
                    }
                    invalidate();
                    return true;
                }
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    held = false;
                    invalidate();
                    if (listener != null) listener.released(step);
                    return true;
                default:
                    return true;
            }
        }
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
