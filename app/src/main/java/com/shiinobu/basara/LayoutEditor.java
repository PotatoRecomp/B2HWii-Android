package com.shiinobu.basara;

import android.app.Activity;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

/**
 * The touch layout editor's panel, over the paused game: what is selected, its size (or every
 * control's), the controls' opacity, the left stick's mode, Reset, Cancel and Save. The controls
 * themselves are dragged and pinched on TouchControlsView. The panel sits at the top and moves to the
 * bottom when the selected control is under it, and fades while a control is being dragged.
 */
final class LayoutEditor implements TouchControlsView.EditListener {
    interface Host {
        AppSettings settings();
        /** The editor closed (saved or cancelled): the game can carry on. */
        void onEditorClosed();
    }

    private static final int SIZE_MIN = Math.round(TouchControlsView.MIN_SCALE * 100);
    private static final int SIZE_MAX = Math.round(TouchControlsView.MAX_SCALE * 100);
    private static final int OPACITY_MIN = 10, OPACITY_MAX = 100, STEP = 5;

    private final Activity activity;
    private final TouchControlsView touch;
    private final Host host;
    private final FrameLayout root;
    private final LinearLayout panel;
    private final TextView selection, sizeValue, opacityValue, stickChip, resetChip;
    private final SeekBar size, opacity;
    private boolean open, atBottom, quiet;
    private String savedLayout = "";
    private boolean savedFixed;
    private float savedOpacity = 0.6f;

    LayoutEditor(Activity activity, TouchControlsView touch, Host host) {
        this.activity = activity;
        this.touch = touch;
        this.host = host;
        root = new FrameLayout(activity);  // not clickable: touches off the panel reach the controls
        root.setVisibility(View.GONE);

        panel = new LinearLayout(activity);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(14), dp(10), dp(14), dp(10));
        panel.setBackground(Ui.box(activity, Color.argb(242, 14, 16, 20), Ui.BORDER_STRONG, 14));
        panel.setElevation(dp(6));
        panel.setClickable(true);

        // 1: title, what is selected, how it works.
        final LinearLayout head = new LinearLayout(activity);
        head.setGravity(Gravity.CENTER_VERTICAL);
        head.addView(Ui.icon(activity, R.drawable.ic_move, Ui.TEXT, 16));
        final TextView title = Ui.text(activity, Tr.t("Edit Layout"), 14, Ui.TEXT);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        head.addView(title, margins(dp(8), 0));
        selection = Ui.text(activity, "", 12, Ui.TEXT);
        selection.setTypeface(Ui.MEDIUM);
        selection.setPadding(dp(10), dp(3), dp(10), dp(3));
        selection.setBackground(Ui.box(activity, Ui.ACTIVE, Ui.BORDER_STRONG, 10));
        head.addView(selection, margins(dp(10), 0));
        final TextView hint = Ui.text(activity, Tr.t("Drag to move · Pinch to resize"), 11, Ui.MUTED);
        hint.setGravity(Gravity.END);
        hint.setSingleLine(true);
        hint.setEllipsize(android.text.TextUtils.TruncateAt.END);
        head.addView(hint, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        // Moves the panel between the top and the bottom, off whatever it covers.
        final TextView swap = chip("⇅", v -> place(!atBottom));
        swap.setTextSize(15);
        swap.setPadding(dp(10), dp(2), dp(10), dp(2));
        swap.setContentDescription(Tr.t("Move panel"));
        head.addView(swap, margins(dp(10), 0));
        panel.addView(head, Ui.matchWidth(0));

        // 2: size and opacity side by side.
        final LinearLayout sliders = new LinearLayout(activity);
        sliders.setGravity(Gravity.CENTER_VERTICAL);
        size = slider(SIZE_MIN, SIZE_MAX);
        sizeValue = value();
        opacity = slider(OPACITY_MIN, OPACITY_MAX);
        opacityValue = value();
        sliders.addView(sliderGroup(Tr.t("Size"), size, sizeValue), new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        final LinearLayout.LayoutParams second = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);
        second.leftMargin = dp(16);
        sliders.addView(sliderGroup(Tr.t("Opacity"), opacity, opacityValue), second);
        panel.addView(sliders, Ui.matchWidth(dp(8)));
        size.setOnSeekBarChangeListener(new Listener() {
            @Override public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                final int percent = SIZE_MIN + progress * STEP;
                sizeValue.setText(percent + "%");
                if (!fromUser || quiet) return;
                applySize(percent);
            }
        });
        opacity.setOnSeekBarChangeListener(new Listener() {
            @Override public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                final int percent = OPACITY_MIN + progress * STEP;
                opacityValue.setText(percent + "%");
                if (fromUser) touch.setOpacity(percent / 100f);
            }
        });

        // 3: stick mode and resets on the left, Cancel and Save on the right.
        final LinearLayout actions = new LinearLayout(activity);
        actions.setGravity(Gravity.CENTER_VERTICAL);
        stickChip = chip("", v -> {
            touch.setFixedStick(!touch.fixedStick());
            refresh();
        });
        actions.addView(stickChip);
        resetChip = chip(Tr.t("Reset Control"), v -> {
            final String key = touch.selected();
            if (key == null) return;
            touch.resetElement(key);
            refresh();
        });
        actions.addView(resetChip, margins(dp(6), 0));
        actions.addView(chip(Tr.t("Reset All"), v -> {
            touch.resetElement(null);
            touch.beginScaleAll();
            refresh();
        }), margins(dp(6), 0));
        actions.addView(new View(activity), new LinearLayout.LayoutParams(0, 1, 1f));
        actions.addView(Ui.button(activity, 0, Tr.t("Cancel"), Ui.PLAIN, v -> cancel()));
        actions.addView(Ui.button(activity, R.drawable.ic_save, Tr.t("Save"), Ui.PRIMARY, v -> save()), margins(dp(8), 0));
        panel.addView(actions, Ui.matchWidth(dp(8)));

        root.addView(panel, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.CENTER_HORIZONTAL));
        activity.addContentView(root, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
        touch.setEditListener(this);
    }

    boolean isOpen() { return open; }

    /** Opens over the game, which the host has paused; what is saved now is what Cancel goes back to. */
    void open() {
        final AppSettings settings = host.settings();
        savedLayout = settings.touchLayout();
        savedFixed = settings.fixedStick();
        savedOpacity = settings.touchOpacity();
        touch.setOpacity(savedOpacity);
        touch.setFixedStick(savedFixed);
        touch.setEditMode(true);
        touch.beginScaleAll();
        final int width = Math.min(dp(620), Math.round(activity.getResources().getDisplayMetrics().widthPixels * 0.72f));
        panel.getLayoutParams().width = width;
        place(false);
        panel.setAlpha(1f);
        root.setVisibility(View.VISIBLE);
        open = true;
        refresh();
    }

    void save() {
        if (!open) return;
        final AppSettings settings = host.settings();
        settings.setTouchLayout(touch.layout());
        settings.setFixedStick(touch.fixedStick());
        settings.setTouchOpacity(touch.opacity());
        close();
    }

    void cancel() {
        if (!open) return;
        touch.setEditMode(false);
        touch.setLayout(savedLayout);
        touch.setFixedStick(savedFixed);
        touch.setOpacity(savedOpacity);
        close();
    }

    private void close() {
        open = false;
        touch.setEditMode(false);
        root.setVisibility(View.GONE);
        host.onEditorClosed();
    }

    @Override
    public void onEditSelect(String key) {
        if (key == null) touch.beginScaleAll();  // the size slider scales everything from here
        refresh();
        if (key != null) placeAwayFrom(key);
    }

    @Override
    public void onEditDrag(boolean active) {
        panel.animate().alpha(active ? 0.15f : 1f).setDuration(120).start();
        if (!active && touch.selected() != null) placeAwayFrom(touch.selected());
    }

    @Override
    public void onEditScaled(String key) {
        showSize(Math.round(touch.scaleOf(key) * 100));
    }

    /** Size: the selected control's, or every control's together (relative, from 100%). */
    private void applySize(int percent) {
        final String key = touch.selected();
        if (key != null) touch.setScale(key, percent / 100f);
        else touch.scaleAll(percent / 100f);
    }

    private void refresh() {
        final String key = touch.selected();
        selection.setText(key != null ? TouchControlsView.nameOf(key) : Tr.t("All Controls"));
        showSize(key != null ? Math.round(touch.scaleOf(key) * 100) : 100);
        quiet = true;
        opacity.setProgress((Math.round(touch.opacity() * 100) - OPACITY_MIN) / STEP);
        quiet = false;
        stickChip.setText(touch.fixedStick() ? Tr.t("Left Stick: Fixed") : Tr.t("Left Stick: Floating"));
        resetChip.setEnabled(key != null);
        resetChip.setAlpha(key != null ? 1f : 0.4f);
    }

    private void showSize(int percent) {
        quiet = true;
        size.setProgress((Math.max(SIZE_MIN, Math.min(SIZE_MAX, percent)) - SIZE_MIN) / STEP);
        sizeValue.setText(percent + "%");
        quiet = false;
    }

    /** Top, unless the control sits under the panel there: then the bottom (and back). */
    private void placeAwayFrom(String key) {
        if (panel.getHeight() == 0) return;
        final float[] b = touch.bounds(key);
        final int w = root.getWidth(), h = root.getHeight(), pw = panel.getWidth(), ph = panel.getHeight();
        final float left = (w - pw) / 2f, right = left + pw;
        final boolean underTop = b[0] + b[2] > left && b[0] - b[2] < right && b[1] - b[2] < dp(8) + ph;
        final boolean underBottom = b[0] + b[2] > left && b[0] - b[2] < right && b[1] + b[2] > h - dp(8) - ph;
        if (!atBottom && underTop) place(true);
        else if (atBottom && underBottom) place(false);
    }

    private void place(boolean bottom) {
        atBottom = bottom;
        final FrameLayout.LayoutParams params = (FrameLayout.LayoutParams) panel.getLayoutParams();
        params.gravity = (bottom ? Gravity.BOTTOM : Gravity.TOP) | Gravity.CENTER_HORIZONTAL;
        params.topMargin = params.bottomMargin = dp(8);
        panel.setLayoutParams(params);
    }

    private LinearLayout sliderGroup(String label, SeekBar bar, TextView value) {
        final LinearLayout row = new LinearLayout(activity);
        row.setGravity(Gravity.CENTER_VERTICAL);
        final TextView name = Ui.caption(activity, label);
        row.addView(name);
        final LinearLayout.LayoutParams minus = new LinearLayout.LayoutParams(dp(30), dp(30));
        minus.leftMargin = dp(8);
        row.addView(stepButton("−", bar, -1), minus);
        row.addView(bar, new LinearLayout.LayoutParams(0, dp(32), 1f));
        row.addView(stepButton("+", bar, 1), new LinearLayout.LayoutParams(dp(30), dp(30)));
        row.addView(value, margins(dp(4), 0));
        return row;
    }

    /** A round − or + beside a slider: one step, for exact sizes without a steady finger. */
    private TextView stepButton(String sign, SeekBar bar, int direction) {
        final TextView b = Ui.text(activity, sign, 16, Ui.TEXT);
        b.setGravity(Gravity.CENTER);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setBackground(Ui.touchBox(activity, Ui.SURFACE, Ui.BORDER_STRONG, 15));
        b.setLayoutParams(new LinearLayout.LayoutParams(dp(30), dp(30)));
        b.setOnClickListener(v -> {
            final int next = Math.max(0, Math.min(bar.getMax(), bar.getProgress() + direction));
            if (next == bar.getProgress()) return;
            bar.setProgress(next);
            if (bar == size) applySize(SIZE_MIN + next * STEP);
            else touch.setOpacity((OPACITY_MIN + next * STEP) / 100f);
        });
        return b;
    }

    private SeekBar slider(int min, int max) {
        final SeekBar bar = new SeekBar(activity);
        bar.setMax((max - min) / STEP);
        bar.setProgressTintList(ColorStateList.valueOf(Ui.TEXT));
        bar.setThumbTintList(ColorStateList.valueOf(Color.WHITE));
        bar.setProgressBackgroundTintList(ColorStateList.valueOf(Ui.BORDER_STRONG));
        return bar;
    }

    private TextView value() {
        final TextView v = Ui.text(activity, "", 12, Ui.TEXT);
        v.setTypeface(Ui.MONO);
        v.setGravity(Gravity.END);
        v.setMinWidth(dp(40));
        return v;
    }

    private TextView chip(String label, View.OnClickListener onClick) {
        final TextView chip = Ui.text(activity, label, 12, Ui.TEXT);
        chip.setTypeface(Ui.MEDIUM);
        chip.setGravity(Gravity.CENTER);
        chip.setSingleLine(true);
        chip.setPadding(dp(10), dp(7), dp(10), dp(7));
        chip.setBackground(Ui.touchBox(activity, Ui.SURFACE, Ui.BORDER_STRONG, 8));
        chip.setOnClickListener(onClick);
        return chip;
    }

    private LinearLayout.LayoutParams margins(int left, int top) {
        final LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        params.leftMargin = left;
        params.topMargin = top;
        return params;
    }

    private int dp(float value) { return Ui.dp(activity, value); }

    private abstract static class Listener implements SeekBar.OnSeekBarChangeListener {
        @Override public void onStartTrackingTouch(SeekBar bar) {}
        @Override public void onStopTrackingTouch(SeekBar bar) {}
    }
}
