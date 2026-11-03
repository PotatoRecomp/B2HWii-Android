package com.shiinobu.basara;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.util.SparseIntArray;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;

/**
 * The on-screen Classic Controller, laid out like the Classic Controller Pro: left stick and face
 * buttons on the outside under the thumbs, D-pad and right stick further in, the four shoulder
 * buttons along the top corners, -, Home and + at the bottom centre.
 *
 * <p>Every finger is tracked on its own and stays live wherever it slides:
 * <ul>
 * <li>Over the buttons, whatever is under the finger is pressed, so a thumb can roll from one face
 * button onto the next. Neighbouring face buttons overlap a little: a thumb on the seam holds both.
 * A finger that lands on empty space presses a button as soon as it slides onto one.</li>
 * <li>On the D-pad the direction follows the finger through all eight directions, and a finger that
 * slides onto it takes it over.</li>
 * <li>A stick belongs to the finger that pressed it, until that finger lifts, wherever it goes.</li>
 * </ul>
 */
final class TouchControlsView extends View {
    /** Bits as the native side expects them (VirtualRemote::TouchButton). */
    static final int LEFT = 0x0001, RIGHT = 0x0002, UP = 0x0004, DOWN = 0x0008;
    static final int B = 0x0010, A = 0x0020, X = 0x0040, PLUS = 0x0080, MINUS = 0x0100;
    static final int Y = 0x0200, L = 0x0400, R = 0x0800, ZL = 0x1000, ZR = 0x2000, HOME = 0x4000;
    private static final int NATIVE_MASK = 0x7FFF;
    /** The settings gear: never sent to the game, opens the in-game menu when lifted on it. */
    static final int MENU = 0x8000;

    private static final int MODE_BUTTONS = 1;  // the finger presses whatever button it is over
    private static final int MODE_PAD = 2;      // the finger steers the D-pad until lifted
    private static final int MODE_STICK_L = 3;  // the finger holds the left stick until lifted
    private static final int MODE_STICK_R = 4;  // the same for the right stick
    private static final int MODE_CAMERA = 5;   // a drag on the empty right side turns the camera

    /** Camera drag: finger speed (screen heights a second) that gives a full right-stick tilt. */
    private static final float CAMERA_FULL_SPEED = 0.35f;
    /** Share of CAMERA_FULL_SPEED under which a drag is a resting finger, not a turn. */
    private static final float CAMERA_DEADZONE = 0.12f;
    /** The stick falls back to centre this long after the finger stops moving. */
    private static final long CAMERA_IDLE_MS = 60;

    private static final int SHAPE_ROUND = 0, SHAPE_PILL = 1;
    private static final float STICK_DEAD_ZONE = 0.08f;
    /** Full tilt this far from the centre, as a fraction of the drawn radius: a short thumb move. */
    private static final float STICK_FULL_TILT = 0.7f;
    private static final float MIN_SCALE = 0.5f, MAX_SCALE = 2f;

    // Layout editor toolbar, left to right.
    private static final String[] TOOLS = {"Smaller", "Bigger", "Stick", "Reset", "Done"};
    private static final int TOOL_STICK = 2, TOOL_RESET = 3;

    // One warm off-white for everything drawn and one vermilion for what is pressed: the colours
    // of ink and seal on paper, which leaves the game's own picture the loudest thing on screen.
    private static final int INK_R = 14, INK_G = 15, INK_B = 18;
    private static final int PAPER_R = 244, PAPER_G = 240, PAPER_B = 230;
    private static final int SEAL_R = 208, SEAL_G = 58, SEAL_B = 38;

    interface Listener {
        void onTouchInput(int buttons);
        /** The sticks, -1..1 with +y up. */
        void onTouchSticks(float leftX, float leftY, float rightX, float rightY);
        void onMenu();
        /** A tap on the game while its menus are up (menu mode), as fractions of the view. */
        void onTap(float x, float y);
        /** A finger held on the game's menus (menu mode), as fractions of the view; held is false
         *  when it is lifted. The game's sliders follow it. */
        void onPointer(float x, float y, boolean held);
        /** The layout editor closed; layout is what setLayout takes ("" for the default). */
        void onLayoutDone(String layout, boolean fixedStick);
    }

    private static final class Button {
        final int bit;
        final String label;
        final int shape;
        final float reach;  // hit area as a multiple of the drawn size
        float x, y, r, halfWidth;
        Button(int bit, String label, int shape, float reach) {
            this.bit = bit;
            this.label = label;
            this.shape = shape;
            this.reach = reach;
        }
        boolean hit(float px, float py) {
            final float radius = r * reach;
            final float dx = Math.max(Math.abs(px - x) - (shape == SHAPE_PILL ? halfWidth - r : 0f), 0f);
            final float dy = py - y;
            return dx * dx + dy * dy <= radius * radius;
        }
    }

    private static final class Stick {
        float x, y, r;
        float homeX, homeY;     // where it is drawn at rest; a floating grab moves x, y meanwhile
        float valueX, valueY;   // -1..1, screen y down
        int finger = -1;
    }

    private final Button[] buttons = {
        new Button(A, "A", SHAPE_ROUND, 1.22f), new Button(B, "B", SHAPE_ROUND, 1.22f),
        new Button(X, "X", SHAPE_ROUND, 1.22f), new Button(Y, "Y", SHAPE_ROUND, 1.22f),
        new Button(L, "L", SHAPE_PILL, 1.3f), new Button(ZL, "ZL", SHAPE_PILL, 1.3f),
        new Button(R, "R", SHAPE_PILL, 1.3f), new Button(ZR, "ZR", SHAPE_PILL, 1.3f),
        new Button(MINUS, null, SHAPE_ROUND, 1.25f), new Button(HOME, null, SHAPE_ROUND, 1.25f),
        new Button(PLUS, null, SHAPE_ROUND, 1.25f), new Button(MENU, null, SHAPE_ROUND, 1.3f),
    };
    private int gearFinger = -1;  // the finger on the gear, or -1
    private final Stick leftStick = new Stick(), rightStick = new Stick();
    private float padX, padY, padR;
    private final SparseIntArray pointerModes = new SparseIntArray();  // pointer id -> MODE_*
    private final SparseIntArray pointerBits = new SparseIntArray();   // pointer id -> bits it holds
    private int held;
    private Listener listener;
    private float opacity = 0.6f;
    private boolean controlsVisible = true;
    private final android.util.SparseBooleanArray freeTaps = new android.util.SparseBooleanArray();
    private boolean menuMode;  // the game's menus are up: no controller, taps go to the game
    private boolean haptics = true;
    private final android.util.SparseArray<float[]> tapStarts = new android.util.SparseArray<>();
    private int dragId = -1;  // the finger the game's menus follow (onPointer), or -1
    private final android.util.SparseLongArray tapTimes = new android.util.SparseLongArray();

    // The player's layout: element key -> {centre x / width, centre y / height, size multiplier}.
    private static final String[] ELEMENTS = {"lstick", "rstick", "pad", "face", "L", "ZL", "R", "ZR",
        "minus", "home", "plus", "menu"};
    private final java.util.HashMap<String, float[]> custom = new java.util.HashMap<>();
    private boolean editing;
    private String selected;
    private int editFinger = -1;
    private float grabDx, grabDy;
    private float faceX, faceY, faceSpread;  // the face diamond's centre and its buttons' distance

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path shape = new Path();
    private final TouchIcons icons = new TouchIcons();
    private final RectF rect = new RectF();

    TouchControlsView(Context context) {
        super(context);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeCap(Paint.Cap.ROUND);
        stroke.setStrokeJoin(Paint.Join.ROUND);
        text.setTextAlign(Paint.Align.CENTER);
        text.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD));
        setFocusable(false);
    }

    void setListener(Listener listener) { this.listener = listener; }

    void setOpacity(float opacity) { this.opacity = opacity; icons.setOpacity(opacity); invalidate(); }

    void setHaptics(boolean haptics) { this.haptics = haptics; }

    /** Fixed: the left stick only answers on itself; floating: anywhere on the empty left side. */
    private boolean fixedStick;
    void setFixedStick(boolean fixed) {
        fixedStick = fixed;
        invalidate();
    }

    private boolean touchCamera = true;
    private boolean invertCamera = true;
    void setInvertCamera(boolean on) { invertCamera = on; }
    void setTouchCamera(boolean on) {
        touchCamera = on;
        if (!on && cameraId >= 0) endCamera();
    }

    void setControlsVisible(boolean visible) {
        controlsVisible = visible;
        release();
        invalidate();
    }

    /**
     * Menu mode, while the game's own menus are up: the controller is hidden, and a tap on the game
     * is passed on as one.
     */
    void setMenuMode(boolean on) {
        if (menuMode == on) return;
        menuMode = on;
        release();
        invalidate();
    }

    private boolean showsControls() { return editing || (controlsVisible && !menuMode); }

    /** Restores a layout saved from the editor ("" or garbage: the default one). */
    void setLayout(String layout) {
        custom.clear();
        if (layout != null) {
            for (String entry : layout.split(";")) {
                final String[] kv = entry.split("=");
                if (kv.length != 2) continue;
                final String[] v = kv[1].split(",");
                if (v.length != 3) continue;
                try {
                    custom.put(kv[0], new float[] {Float.parseFloat(v[0]), Float.parseFloat(v[1]),
                        Math.max(MIN_SCALE, Math.min(MAX_SCALE, Float.parseFloat(v[2])))});
                } catch (NumberFormatException ignored) {
                    // a damaged entry keeps its default place
                }
            }
        }
        layoutControls(getWidth(), getHeight());
    }

    private String encodeLayout() {
        final StringBuilder out = new StringBuilder();
        for (java.util.Map.Entry<String, float[]> e : custom.entrySet()) {
            final float[] v = e.getValue();
            if (out.length() > 0) out.append(';');
            out.append(e.getKey()).append('=').append(String.format(java.util.Locale.ROOT, "%.4f,%.4f,%.3f",
                v[0], v[1], v[2]));
        }
        return out.toString();
    }

    /** The layout editor: drag a control to move it, Smaller and Bigger resize the selected one. */
    void setEditMode(boolean on) {
        editing = on;
        selected = null;
        editFinger = -1;
        release();
        invalidate();
    }

    /** Lets go of everything, e.g. when the app loses focus or a menu opens. */
    void release() {
        pointerModes.clear();
        pointerBits.clear();
        cameraId = -1;
        removeCallbacks(cameraIdle);
        releaseStick(leftStick);
        releaseStick(rightStick);
        sendSticks();
        update(0);
    }

    private void releaseStick(Stick stick) {
        stick.finger = -1;
        stick.valueX = stick.valueY = 0f;
        stick.x = stick.homeX;
        stick.y = stick.homeY;
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        layoutControls(w, h);
    }

    @Override
    public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        layoutControls(getWidth(), getHeight());
        return super.onApplyWindowInsets(insets);
    }

    private void layoutControls(int w, int h) {
        if (w == 0 || h == 0) return;
        // Keep the thumbs' controls clear of a camera cutout on either short edge.
        float left = 0, right = 0;
        final WindowInsets insets = getRootWindowInsets();
        if (insets != null && insets.getDisplayCutout() != null) {
            left = insets.getDisplayCutout().getSafeInsetLeft();
            right = insets.getDisplayCutout().getSafeInsetRight();
        }
        final float u = h;  // everything scales with the short side
        final float leftEdge = left + 0.03f * u, rightEdge = w - right - 0.03f * u;

        leftStick.r = 0.16f * u;
        leftStick.x = leftEdge + 0.04f * u + leftStick.r;
        leftStick.y = 0.50f * u;
        padR = 0.13f * u;
        padX = leftStick.x + 0.34f * u;
        padY = 0.80f * u;

        // The diamond, as on the controller: A right, B bottom, X top, Y left.
        faceX = rightEdge - 0.19f * u;
        faceY = 0.50f * u;
        faceSpread = 0.115f * u;
        final float face = faceSpread, faceR = 0.07f * u;
        place(A, faceX + face, faceY, faceR);
        place(B, faceX, faceY + face, faceR);
        place(X, faceX, faceY - face, faceR);
        place(Y, faceX - face, faceY, faceR);
        rightStick.r = 0.115f * u;
        rightStick.x = faceX - 0.34f * u;
        rightStick.y = 0.80f * u;

        final float pillY = 0.085f * u, pillR = 0.05f * u, pillWidth = 0.085f * u, gap = 0.02f * u;
        placePill(L, leftEdge + pillWidth, pillY, pillR, pillWidth);
        placePill(ZL, leftEdge + 3 * pillWidth + gap, pillY, pillR, pillWidth);
        placePill(R, rightEdge - pillWidth, pillY, pillR, pillWidth);
        placePill(ZR, rightEdge - 3 * pillWidth - gap, pillY, pillR, pillWidth);

        final float centreY = 0.925f * u, centreR = 0.042f * u, centreGap = 0.125f * u;
        place(MINUS, w / 2f - centreGap, centreY, centreR);
        place(HOME, w / 2f, centreY, centreR);
        place(PLUS, w / 2f + centreGap, centreY, centreR);
        // The gear sits just right of ZL, clear of the performance bar in the top centre.
        final float gearR = 0.042f * u;
        place(MENU, leftEdge + 4 * pillWidth + gap + 0.035f * u + gearR, pillY, gearR);
        for (java.util.Map.Entry<String, float[]> e : custom.entrySet()) {
            final float[] v = e.getValue();
            transform(e.getKey(), v[0] * w, v[1] * h, v[2]);
        }
        for (Stick stick : new Stick[] {leftStick, rightStick}) {
            stick.homeX = stick.x;
            stick.homeY = stick.y;
        }
        excludeSystemGestures();
        invalidate();
    }

    /**
     * A thumb on a stick near the screen's edge otherwise starts the system's back gesture, which
     * cancels every touch: the stick dropped out mid-move. Android honours 200 dp per edge.
     */
    private void excludeSystemGestures() {
        final float limit = 200f * getResources().getDisplayMetrics().density;
        final java.util.List<android.graphics.Rect> rects = new java.util.ArrayList<>();
        final float lh = Math.min(limit, leftStick.r * 3f) / 2f;
        rects.add(new android.graphics.Rect(0, Math.round(leftStick.homeY - lh),
            Math.round(leftStick.homeX + leftStick.r * 1.4f), Math.round(leftStick.homeY + lh)));
        final float rh = Math.min(limit, (faceSpread + 0.08f * getHeight()) * 2f) / 2f;
        rects.add(new android.graphics.Rect(Math.round(faceX - faceSpread * 2f), Math.round(faceY - rh),
            getWidth(), Math.round(faceY + rh)));
        setSystemGestureExclusionRects(rects);
    }

    private Button button(int bit) {
        for (Button b : buttons) {
            if (b.bit == bit) return b;
        }
        throw new IllegalArgumentException("no button " + bit);
    }

    private static int bitOf(String key) {
        switch (key) {
            case "L": return L;
            case "ZL": return ZL;
            case "R": return R;
            case "ZR": return ZR;
            case "minus": return MINUS;
            case "home": return HOME;
            case "plus": return PLUS;
            case "menu": return MENU;
            default: return 0;
        }
    }

    /** {centre x, centre y, radius} of an element as laid out now. */
    private float[] bounds(String key) {
        switch (key) {
            case "lstick": return new float[] {leftStick.homeX, leftStick.homeY, leftStick.r};
            case "rstick": return new float[] {rightStick.homeX, rightStick.homeY, rightStick.r};
            case "pad": return new float[] {padX, padY, padR};
            case "face": return new float[] {faceX, faceY, faceSpread + button(A).r};
            default: {
                final Button b = button(bitOf(key));
                return new float[] {b.x, b.y, Math.max(b.r, b.halfWidth)};
            }
        }
    }

    /** Moves an element's centre to (x, y) and multiplies its size by scale (from the default). */
    private void transform(String key, float x, float y, float scale) {
        switch (key) {
            case "lstick": leftStick.x = x; leftStick.y = y; leftStick.r *= scale; break;
            case "rstick": rightStick.x = x; rightStick.y = y; rightStick.r *= scale; break;
            case "pad": padX = x; padY = y; padR *= scale; break;
            case "face":
                for (int bit : new int[] {A, B, X, Y}) {
                    final Button b = button(bit);
                    b.x = x + (b.x - faceX) * scale;
                    b.y = y + (b.y - faceY) * scale;
                    b.r *= scale;
                    b.halfWidth = b.r;
                }
                faceX = x;
                faceY = y;
                faceSpread *= scale;
                break;
            default: {
                final int bit = bitOf(key);
                if (bit == 0) return;
                final Button b = button(bit);
                b.x = x;
                b.y = y;
                b.r *= scale;
                b.halfWidth *= scale;
            }
        }
    }

    private String elementAt(float x, float y) {
        String best = null;
        float bestDistance = Float.MAX_VALUE;
        for (String key : ELEMENTS) {
            final float[] b = bounds(key);
            final float d = (float) Math.hypot(x - b[0], y - b[1]);
            if (d <= b[2] * 1.15f && d / b[2] < bestDistance) {
                best = key;
                bestDistance = d / b[2];
            }
        }
        return best;
    }

    private RectF toolRect(int i) {
        final float h = getHeight(), bw = 0.2f * h, bh = 0.1f * h, gap = 0.02f * h;
        final float left = getWidth() / 2f - (TOOLS.length * bw + (TOOLS.length - 1) * gap) / 2f;
        return new RectF(left + i * (bw + gap), 0.2f * h, left + i * (bw + gap) + bw, 0.2f * h + bh);
    }

    private void editTouch(MotionEvent e) {
        final int action = e.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            final int i = e.getActionIndex();
            final float x = e.getX(i), y = e.getY(i);
            for (int t = 0; t < TOOLS.length; ++t) {
                if (toolRect(t).contains(x, y)) {
                    tool(t);
                    return;
                }
            }
            if (editFinger >= 0) return;
            selected = elementAt(x, y);
            if (selected != null) {
                final float[] b = bounds(selected);
                editFinger = e.getPointerId(i);
                grabDx = b[0] - x;
                grabDy = b[1] - y;
            }
            invalidate();
        } else if (action == MotionEvent.ACTION_MOVE && editFinger >= 0 && selected != null) {
            final int i = e.findPointerIndex(editFinger);
            if (i < 0) return;
            final float x = Math.max(0f, Math.min(getWidth(), e.getX(i) + grabDx));
            final float y = Math.max(0f, Math.min(getHeight(), e.getY(i) + grabDy));
            custom.put(selected, new float[] {x / getWidth(), y / getHeight(), scaleOf(selected)});
            layoutControls(getWidth(), getHeight());
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP
                || action == MotionEvent.ACTION_CANCEL) {
            if (action == MotionEvent.ACTION_CANCEL || e.getPointerId(e.getActionIndex()) == editFinger) {
                editFinger = -1;
            }
        }
    }

    private float scaleOf(String key) {
        final float[] v = custom.get(key);
        return v != null ? v[2] : 1f;
    }

    private void tool(int t) {
        if (t == 0 || t == 1) {
            // Resizes the selected control, or all of them when none is selected.
            final float factor = t == 0 ? 0.9f : 1.1f;
            for (String key : selected != null ? new String[] {selected} : ELEMENTS) {
                final float[] b = bounds(key);
                final float scale = Math.max(MIN_SCALE, Math.min(MAX_SCALE, scaleOf(key) * factor));
                custom.put(key, new float[] {b[0] / getWidth(), b[1] / getHeight(), scale});
            }
            layoutControls(getWidth(), getHeight());
        } else if (t == TOOL_STICK) {
            fixedStick = !fixedStick;
            invalidate();
        } else if (t == TOOL_RESET) {
            custom.clear();
            selected = null;
            layoutControls(getWidth(), getHeight());
        } else {
            setEditMode(false);
            if (listener != null) listener.onLayoutDone(encodeLayout(), fixedStick);
        }
    }

    private void place(int bit, float x, float y, float r) {
        for (Button b : buttons) {
            if (b.bit == bit) { b.x = x; b.y = y; b.r = r; b.halfWidth = r; }
        }
    }

    private void placePill(int bit, float x, float y, float r, float halfWidth) {
        place(bit, x, y, r);
        for (Button b : buttons) {
            if (b.bit == bit) b.halfWidth = halfWidth;
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        if (editing) {
            editTouch(e);
            return true;
        }
        final int action = e.getActionMasked();
        if (action == MotionEvent.ACTION_CANCEL) {
            endDrag(e.getX(), e.getY());
            gearFinger = -1;
            release();
            return true;
        }
        // The gear works in every mode (game menus, controls hidden); its finger does nothing else.
        if ((action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) && gearFinger < 0
                && button(MENU).hit(e.getX(e.getActionIndex()), e.getY(e.getActionIndex()))) {
            gearFinger = e.getPointerId(e.getActionIndex());
            if (haptics) performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
            invalidate();
            return true;
        }
        if (gearFinger >= 0 && e.getPointerId(e.getActionIndex()) == gearFinger
                && (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP)) {
            final int i = e.getActionIndex();
            gearFinger = -1;
            invalidate();
            if (button(MENU).hit(e.getX(i), e.getY(i)) && listener != null) listener.onMenu();
            return true;
        }
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            final int i = e.getActionIndex();
            tapStarts.put(e.getPointerId(i), new float[] {e.getX(i), e.getY(i)});
            tapTimes.put(e.getPointerId(i), e.getEventTime());
            press(e.getPointerId(i), e.getX(i), e.getY(i));
            if (menuMode && dragId < 0) {
                dragId = e.getPointerId(i);
                sendPointer(e.getX(i), e.getY(i), true);
            }
        } else if (action == MotionEvent.ACTION_MOVE) {
            for (int i = 0; i < e.getPointerCount(); ++i) {
                slide(e.getPointerId(i), e.getX(i), e.getY(i));
                if (e.getPointerId(i) == dragId) sendPointer(e.getX(i), e.getY(i), true);
            }
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            final int i = e.getActionIndex();
            final int id = e.getPointerId(i);
            if (id == dragId) endDrag(e.getX(i), e.getY(i));
            final float[] start = tapStarts.get(id);
            // Taps go to the game on its menus, and anywhere off the controller otherwise.
            final boolean tappable = menuMode || freeTaps.get(id, false);
            if (tappable && start != null && listener != null
                    && Math.hypot(e.getX(i) - start[0], e.getY(i) - start[1]) < 0.06f * getHeight()
                    && e.getEventTime() - tapTimes.get(id) < 800 && getWidth() > 0 && getHeight() > 0) {
                listener.onTap(e.getX(i) / getWidth(), e.getY(i) / getHeight());
            }
            tapStarts.remove(id);
            tapTimes.delete(id);
            freeTaps.delete(id);
            final int mode = pointerModes.get(id, 0);
            if (mode == MODE_CAMERA) {
                endCamera();
            } else if (mode == MODE_STICK_L || mode == MODE_STICK_R) {
                releaseStick(mode == MODE_STICK_L ? leftStick : rightStick);
                sendSticks();
                invalidate();
            }
            pointerModes.delete(id);
            pointerBits.delete(id);
        }
        int combined = 0;
        for (int i = 0; i < pointerBits.size(); ++i) {
            combined |= pointerBits.valueAt(i);
        }
        update(combined);
        return true;
    }

    private void sendPointer(float x, float y, boolean held) {
        if (listener != null && getWidth() > 0 && getHeight() > 0) {
            listener.onPointer(x / getWidth(), y / getHeight(), held);
        }
    }

    private void endDrag(float x, float y) {
        if (dragId >= 0) {
            dragId = -1;
            sendPointer(x, y, false);
        }
    }

    private void press(int id, float x, float y) {
        if (onStick(leftStick, x, y)) {
            pointerModes.put(id, MODE_STICK_L);
            leftStick.finger = id;
            steer(leftStick, x, y);
        } else if (onStick(rightStick, x, y)) {
            pointerModes.put(id, MODE_STICK_R);
            rightStick.finger = id;
            steer(rightStick, x, y);
        } else if (onPad(x, y)) {
            pointerModes.put(id, MODE_PAD);
            pointerBits.put(id, padDirection(x, y));
        } else if (buttonsAt(x, y) == 0 && inLeftStickZone(x, y)) {
            // Floating left stick: a thumb that misses the drawn stick still moves, centred where it
            // landed, so a sloppy grab neither does nothing nor jumps straight to the rim.
            pointerModes.put(id, MODE_STICK_L);
            leftStick.finger = id;
            leftStick.x = x;
            leftStick.y = y;
            steer(leftStick, x, y);
        } else if (buttonsAt(x, y) == 0 && inCameraZone(x, y)) {
            // Drag to look: the right stick follows the finger's speed, in the finger's direction
            // (right turns right, up looks up). A tap that does not move still reaches the game.
            pointerModes.put(id, MODE_CAMERA);
            cameraId = id;
            cameraLastX = x;
            cameraLastY = y;
            cameraLastTime = android.os.SystemClock.uptimeMillis();
            freeTaps.put(id, true);
        } else {
            pointerModes.put(id, MODE_BUTTONS);
            pointerBits.put(id, buttonsAt(x, y));
            if (buttonsAt(x, y) == 0) freeTaps.put(id, true);
        }
    }

    /** The empty left side under the shoulder pills, where a thumb grabs the left stick. */
    private boolean inLeftStickZone(float x, float y) {
        return !fixedStick && showsControls() && leftStick.finger < 0 && x < getWidth() * 0.4f && y > getHeight() * 0.18f;
    }

    /** The empty right side, where a drag turns the camera (the right stick when not held). */
    private boolean inCameraZone(float x, float y) {
        return touchCamera && showsControls() && cameraId < 0 && rightStick.finger < 0
            && x >= getWidth() * 0.4f && y > getHeight() * 0.18f;
    }

    private int cameraId = -1;
    private float cameraLastX, cameraLastY;
    private long cameraLastTime;
    private final Runnable cameraIdle = () -> {
        if (rightStick.finger < 0 && (rightStick.valueX != 0f || rightStick.valueY != 0f)) {
            rightStick.valueX = rightStick.valueY = 0f;
            sendSticks();
        }
    };

    private void dragCamera(float x, float y) {
        final long now = android.os.SystemClock.uptimeMillis();
        final long dt = now - cameraLastTime;
        final float h = getHeight();
        if (dt <= 0 || h <= 0 || rightStick.finger >= 0) return;
        final float scale = 1000f / (dt * h * CAMERA_FULL_SPEED);
        final float dx = invertCamera ? cameraLastX - x : x - cameraLastX;
        final float vx = camAxis(dx * scale);
        final float vy = camAxis((y - cameraLastY) * scale);
        cameraLastX = x;
        cameraLastY = y;
        cameraLastTime = now;
        rightStick.valueX = vx;
        rightStick.valueY = vy;
        sendSticks();
        removeCallbacks(cameraIdle);
        postDelayed(cameraIdle, CAMERA_IDLE_MS);
    }

    private void endCamera() {
        cameraId = -1;
        removeCallbacks(cameraIdle);
        cameraIdle.run();
    }

    /**
     * The game's camera is not analog: a right stick below about 0.65 does not turn it, and 0.7 to 1
     * all turn it at one speed (above 1 the value wraps and turns it the wrong way). So any real drag
     * gives a full tilt.
     */
    private static float camAxis(float v) {
        if (Math.abs(v) < CAMERA_DEADZONE) return 0f;
        return v < 0f ? -1f : 1f;
    }

    private static float clampUnit(float v) {
        return v < -1f ? -1f : (v > 1f ? 1f : v);
    }

    private void slide(int id, float x, float y) {
        final int mode = pointerModes.get(id, 0);
        if (mode == MODE_CAMERA) {
            dragCamera(x, y);
        } else if (mode == MODE_STICK_L) {
            steer(leftStick, x, y);
        } else if (mode == MODE_STICK_R) {
            steer(rightStick, x, y);
        } else if (mode == MODE_PAD) {
            pointerBits.put(id, padDirection(x, y));
        } else if (mode == MODE_BUTTONS) {
            if (onPad(x, y)) {
                pointerModes.put(id, MODE_PAD);
                pointerBits.put(id, padDirection(x, y));
            } else {
                // A thumb drifting off a held button keeps holding it until lifted; sliding onto
                // other buttons still switches to them.
                final int bits = buttonsAt(x, y);
                if (bits != 0) pointerBits.put(id, bits);
            }
        }
    }

    private boolean onStick(Stick stick, float x, float y) {
        if (!showsControls() || stick.finger >= 0) return false;
        final float dx = x - stick.x, dy = y - stick.y, reach = stick.r * 1.35f;
        return dx * dx + dy * dy <= reach * reach;
    }

    /**
     * Full tilt at STICK_FULL_TILT of the radius; the knob follows the finger out to the rim. The
     * middle is a small dead zone, and the tilt starts from 0 at its edge instead of jumping.
     */
    private void steer(Stick stick, float x, float y) {
        final float reach = stick.r * STICK_FULL_TILT;
        float nx = (x - stick.x) / reach, ny = (y - stick.y) / reach;
        final float length = (float) Math.hypot(nx, ny);
        if (length < STICK_DEAD_ZONE) {
            nx = ny = 0f;
        } else {
            final float tilt = Math.min(1f, (length - STICK_DEAD_ZONE) / (1f - STICK_DEAD_ZONE));
            nx = nx / length * tilt;
            ny = ny / length * tilt;
        }
        if (nx == stick.valueX && ny == stick.valueY) return;
        stick.valueX = nx;
        stick.valueY = ny;
        sendSticks();
        invalidate();
    }

    private void sendSticks() {
        if (listener != null) {
            listener.onTouchSticks(leftStick.valueX, -leftStick.valueY, rightStick.valueX, -rightStick.valueY);
        }
    }

    private boolean onPad(float x, float y) {
        if (!showsControls()) return false;
        final float dx = x - padX, dy = y - padY, reach = padR * 1.2f;
        return dx * dx + dy * dy <= reach * reach;
    }

    /** Every button under the point (neighbours overlap on their seam). */
    private int buttonsAt(float x, float y) {
        if (!showsControls()) return 0;
        int bits = 0;
        for (Button b : buttons) {
            if (b.bit != MENU && b.hit(x, y)) bits |= b.bit;
        }
        return bits;
    }

    /** Eight-way: a direction counts within 67.5 degrees of its axis, so diagonals press two. */
    private int padDirection(float x, float y) {
        final float dx = x - padX, dy = y - padY;
        if (dx * dx + dy * dy < (0.22f * padR) * (0.22f * padR)) return 0;
        int bits = 0;
        final float slope = 0.4142136f;  // tan(22.5 degrees)
        if (Math.abs(dx) >= slope * Math.abs(dy)) bits |= dx < 0 ? LEFT : RIGHT;
        if (Math.abs(dy) >= slope * Math.abs(dx)) bits |= dy < 0 ? UP : DOWN;
        return bits;
    }

    private void update(int bits) {
        if (bits == held) return;
        final int pressed = bits & ~held;
        held = bits;
        if (pressed != 0 && haptics) {
            performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        }
        if (listener != null) {
            listener.onTouchInput(bits & NATIVE_MASK);
        }
        invalidate();
    }

    private int ink(int alpha) { return Color.argb(clamp(alpha), INK_R, INK_G, INK_B); }
    private int paper(int alpha) { return Color.argb(clamp(alpha), PAPER_R, PAPER_G, PAPER_B); }
    private int seal(int alpha) { return Color.argb(clamp(alpha), SEAL_R, SEAL_G, SEAL_B); }
    private static int clamp(int alpha) { return Math.max(0, Math.min(255, alpha)); }

    @Override
    protected void onDraw(Canvas canvas) {
        final int base = Math.round(255 * opacity);
        icons.setOpacity(Math.max(opacity, showsControls() ? 0f : 0.47f));
        if (!showsControls()) {
            drawGear(canvas, button(MENU), Math.max(base, 120));
            return;
        }
        drawStick(canvas, leftStick, base);
        drawStick(canvas, rightStick, base);
        drawPad(canvas, base);
        for (Button b : buttons) {
            if (b.bit == MENU) drawGear(canvas, b, base);
            else drawButton(canvas, b, base);
        }
        if (editing) drawEditor(canvas);
    }

    private void drawEditor(Canvas canvas) {
        final float h = getHeight();
        if (selected != null) {
            final float[] b = bounds(selected);
            stroke.setStrokeWidth(Math.max(2f, h * 0.006f));
            stroke.setColor(seal(255));
            canvas.drawCircle(b[0], b[1], b[2] * 1.12f, stroke);
        }
        text.setTextSize(0.045f * h);
        for (int t = 0; t < TOOLS.length; ++t) {
            final RectF r = toolRect(t);
            fill.setStyle(Paint.Style.FILL);
            fill.setColor(t == TOOLS.length - 1 ? seal(230) : ink(220));
            canvas.drawRoundRect(r, r.height() / 4, r.height() / 4, fill);
            text.setColor(paper(255));
            final String label = t == TOOL_STICK ? (fixedStick ? "Stick: Fixed" : "Stick: Float") : TOOLS[t];
            text.setTextSize(0.045f * h);
            final float fit = 0.9f * r.width() / Math.max(1f, text.measureText(label));
            if (fit < 1f) text.setTextSize(0.045f * h * fit);
            canvas.drawText(label, r.centerX(), r.centerY() - (text.descent() + text.ascent()) / 2, text);
        }
        text.setTextSize(0.035f * h);
        text.setColor(paper(255));
        final String hint = selected == null
            ? (fixedStick
                ? "Drag a control to move it. Fixed stick: only a touch on the stick moves it."
                : "Drag a control to move it. Floating stick: grabs anywhere on the empty left side.")
            : "Drag to move. Smaller and Bigger resize the outlined control.";
        canvas.drawText(hint, getWidth() / 2f, 0.36f * h, text);
    }

    /** The settings gear, the same face as − and +. */
    private void drawGear(Canvas canvas, Button b, int base) {
        icons.system(canvas, MENU, b.x, b.y, b.r, gearFinger >= 0);
    }

    private void drawButton(Canvas canvas, Button b, int base) {
        final boolean down = (held & b.bit) != 0;
        switch (b.bit) {
            case A: case B: case X: case Y:
                icons.face(canvas, b.bit, b.x, b.y, b.r, down);
                break;
            case L: case R: case ZL: case ZR:
                icons.shoulder(canvas, b.bit, b.x, b.y, b.halfWidth, b.r, down);
                break;
            default:
                icons.system(canvas, b.bit, b.x, b.y, b.r, down);
                break;
        }
    }

    private void drawPad(Canvas canvas, int base) {
        icons.pad(canvas, padX, padY, padR, held & (LEFT | RIGHT | UP | DOWN));
    }

    private void drawStick(Canvas canvas, Stick stick, int base) {
        icons.stick(canvas, stick.x, stick.y, stick.r, stick.valueX, stick.valueY, stick.finger >= 0);
    }
}
