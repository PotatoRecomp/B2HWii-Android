package com.shiinobu.basara;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.DashPathEffect;
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
 * buttons along the top corners, -, the menu gear, Home and + at the bottom centre.
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
 *
 * <p>Layout editing (setEditMode): every control, each face button on its own, can be dragged, and
 * resized by pinching or from the editor panel (LayoutEditor) between MIN_SCALE and MAX_SCALE.
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

    private static final float STICK_DEAD_ZONE = 0.08f;
    /** Full tilt this far from the centre, as a fraction of the drawn radius: a short thumb move. */
    private static final float STICK_FULL_TILT = 0.7f;
    /** A control's size, as a multiple of its default: the player picks anything in between. */
    static final float MIN_SCALE = 0.5f, MAX_SCALE = 2f;

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
    }

    /** What the layout editor's panel follows. */
    interface EditListener {
        /** The selected control changed (null: none). */
        void onEditSelect(String key);
        /** A drag or pinch started (true) or ended (false). */
        void onEditDrag(boolean active);
        /** The selected control's size changed by a pinch. */
        void onEditScaled(String key);
    }

    private static final class Button {
        final int bit;
        final String key;      // the editor's name for it
        final Path glyph;
        final String caption;  // inside the button, null for none
        final String letter;   // badge, null for none
        final boolean hot, small;
        final float reach;     // hit area as a multiple of the drawn size
        float x, y, r, halfWidth;  // halfWidth > r: a pill (Classic shoulders)
        Button(int bit, String key, Path glyph, String caption, String letter, boolean hot, boolean small, float reach) {
            this.bit = bit;
            this.key = key;
            this.glyph = glyph;
            this.caption = caption;
            this.letter = letter;
            this.hot = hot;
            this.small = small;
            this.reach = reach;
        }
        boolean hit(float px, float py) {
            final float radius = r * reach;
            final float dx = Math.max(Math.abs(px - x) - Math.max(0f, halfWidth - r), 0f), dy = py - y;
            return dx * dx + dy * dy <= radius * radius;
        }
    }

    private static final class Stick {
        float x, y, r;
        float homeX, homeY;     // where it is drawn at rest; a floating grab moves x, y meanwhile
        float valueX, valueY;   // -1..1, screen y down
        int finger = -1;
    }

    private final Button[] buttons;
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
    // "face" (all four face buttons together) is only read from older layouts.
    static final String[] ELEMENTS = {"lstick", "rstick", "pad", "A", "B", "X", "Y", "L", "ZL", "R", "ZR",
        "minus", "menu", "home", "plus"};
    private final java.util.HashMap<String, float[]> custom = new java.util.HashMap<>();
    private boolean editing;
    private String selected;
    private EditListener editListener;
    private int editFinger = -1, pinchFinger = -1;
    private float grabDx, grabDy, pinchStartDistance, pinchStartScale;
    private float guideX = Float.NaN, guideY = Float.NaN;  // alignment lines shown while dragging
    private final java.util.HashMap<String, Float> scaleSnapshot = new java.util.HashMap<>();
    private float faceX, faceY, faceSpread;  // the face buttons' centre and half extent

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final DashPathEffect dash;
    private final TouchIcons icons = new TouchIcons();
    private final RectF rect = new RectF();

    TouchControlsView(Context context) {
        super(context);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeCap(Paint.Cap.ROUND);
        text.setTextAlign(Paint.Align.CENTER);
        text.setTypeface(Typeface.create("sans-serif-medium", Typeface.BOLD));
        final float density = getResources().getDisplayMetrics().density;
        dash = new DashPathEffect(new float[] {6f * density, 5f * density}, 0f);
        setFocusable(false);
        buttons = new Button[] {
            new Button(A, "A", TouchGlyphs.SKILL, "SPECIAL", "A", true, false, 1.22f),
            new Button(B, "B", TouchGlyphs.JUMP, "JUMP", "B", false, false, 1.22f),
            new Button(X, "X", TouchGlyphs.STRONG, "SKILL", "X", false, false, 1.22f),
            new Button(Y, "Y", TouchGlyphs.ATTACK, "ATTACK", "Y", false, false, 1.22f),
            new Button(L, "L", TouchGlyphs.SWITCH, "SWITCH", "L", false, false, 1.25f),
            new Button(ZL, "ZL", TouchGlyphs.BASARA, "BASARA", "ZL", true, false, 1.25f),
            new Button(R, "R", TouchGlyphs.GUARD, "GUARD", "R", false, false, 1.25f),
            new Button(ZR, "ZR", TouchGlyphs.LOCK, "LOCK", "ZR", false, false, 1.25f),
            new Button(MINUS, "minus", TouchGlyphs.MINUS, null, "−", false, true, 1.25f),
            new Button(MENU, "menu", TouchGlyphs.MENU, null, null, false, true, 1.3f),
            new Button(HOME, "home", TouchGlyphs.HOME, null, null, false, true, 1.25f),
            new Button(PLUS, "plus", TouchGlyphs.PLUS, null, "+", false, true, 1.25f),
        };
    }

    void setListener(Listener listener) { this.listener = listener; }

    void setEditListener(EditListener editListener) { this.editListener = editListener; }

    void setOpacity(float opacity) { this.opacity = opacity; icons.setOpacity(opacity); invalidate(); }

    float opacity() { return opacity; }

    void setHaptics(boolean haptics) { this.haptics = haptics; }

    /** Fixed: the left stick only answers on itself; floating: anywhere on the empty left side. */
    private boolean fixedStick;
    void setFixedStick(boolean fixed) {
        fixedStick = fixed;
        invalidate();
    }

    boolean fixedStick() { return fixedStick; }

    /** Button faces: Modern (pictures) or Classic (letters, pill shoulders, cross D-pad). */
    static final int STYLE_MODERN = 0, STYLE_CLASSIC = 1;
    private int style = STYLE_MODERN;
    void setStyle(int style) {
        if (this.style == style) return;
        this.style = style;
        layoutControls(getWidth(), getHeight());
    }

    void setColor(int color) { icons.setColor(color); invalidate(); }

    private boolean touchCamera = false;
    private boolean invertCamera = true;
    void setInvertCamera(boolean on) { invertCamera = on; }
    /** Touch camera on: a drag on the empty right side turns the camera, and the right stick is hidden. */
    void setTouchCamera(boolean on) {
        touchCamera = on;
        if (!on && cameraId >= 0) endCamera();
        if (on && rightStick.finger >= 0) {
            releaseStick(rightStick);
            sendSticks();
        }
        invalidate();
    }

    private boolean rightStickShown() { return !touchCamera; }

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
                        clampScale(Float.parseFloat(v[2]))});
                } catch (NumberFormatException ignored) {
                    // a damaged entry keeps its default place
                }
            }
        }
        layoutControls(getWidth(), getHeight());
    }

    /** The layout as setLayout takes it ("" for the default one). */
    String layout() {
        final StringBuilder out = new StringBuilder();
        for (java.util.Map.Entry<String, float[]> e : custom.entrySet()) {
            final float[] v = e.getValue();
            if (out.length() > 0) out.append(';');
            out.append(e.getKey()).append('=').append(String.format(java.util.Locale.ROOT, "%.4f,%.4f,%.3f",
                v[0], v[1], v[2]));
        }
        return out.toString();
    }

    private static float clampScale(float s) { return Math.max(MIN_SCALE, Math.min(MAX_SCALE, s)); }

    /** The layout editor: drag a control to move it, pinch it (or use the panel) to resize it. */
    void setEditMode(boolean on) {
        editing = on;
        selected = null;
        editFinger = pinchFinger = -1;
        guideX = guideY = Float.NaN;
        if (on) bakeFace();
        release();
        invalidate();
    }

    boolean editing() { return editing; }

    /**
     * An older layout moved the face buttons as one ("face"); the editor moves each on its own, so
     * the group's place and size are written out per button first.
     */
    private void bakeFace() {
        final float[] face = custom.get("face");
        if (face == null || getWidth() == 0) return;
        for (String key : new String[] {"A", "B", "X", "Y"}) {
            if (custom.containsKey(key)) continue;
            final Button b = button(bitOf(key));
            custom.put(key, new float[] {b.x / getWidth(), b.y / getHeight(), face[2]});
        }
        custom.remove("face");
        layoutControls(getWidth(), getHeight());
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
        final float fx = rightEdge - 0.19f * u, fy = 0.50f * u, face = 0.115f * u, faceR = 0.07f * u;
        place(A, fx + face, fy, faceR);
        place(B, fx, fy + face, faceR);
        place(X, fx, fy - face, faceR);
        place(Y, fx - face, fy, faceR);
        rightStick.r = 0.115f * u;
        rightStick.x = fx - 0.34f * u;
        rightStick.y = 0.80f * u;

        if (style == STYLE_CLASSIC) {
            // Pills along the top corners.
            final float pillY = 0.085f * u, pillWidth = 0.085f * u, pillR = pillWidth / TouchIcons.PILL_RATIO,
                    gap = 0.02f * u;
            placePill(L, leftEdge + pillWidth, pillY, pillR, pillWidth);
            placePill(ZL, leftEdge + 3 * pillWidth + gap, pillY, pillR, pillWidth);
            placePill(R, rightEdge - pillWidth, pillY, pillR, pillWidth);
            placePill(ZR, rightEdge - 3 * pillWidth - gap, pillY, pillR, pillWidth);
        } else {
            // Round, along the top corners, low enough for their letter badge to show.
            final float shoulderR = 0.062f * u, shoulderY = 0.13f * u, shoulderGap = 0.04f * u;
            place(L, leftEdge + 0.01f * u + shoulderR, shoulderY, shoulderR);
            place(ZL, leftEdge + 0.01f * u + 3 * shoulderR + shoulderGap, shoulderY, shoulderR);
            place(R, rightEdge - 0.01f * u - shoulderR, shoulderY, shoulderR);
            place(ZR, rightEdge - 0.01f * u - 3 * shoulderR - shoulderGap, shoulderY, shoulderR);
        }

        // -, the gear, Home and + in one row at the bottom centre.
        final float centreY = 0.925f * u, centreR = 0.042f * u, centreGap = 0.125f * u;
        place(MINUS, w / 2f - 1.5f * centreGap, centreY, centreR);
        place(MENU, w / 2f - 0.5f * centreGap, centreY, centreR);
        place(HOME, w / 2f + 0.5f * centreGap, centreY, centreR);
        place(PLUS, w / 2f + 1.5f * centreGap, centreY, centreR);

        final float[] group = custom.get("face");
        if (group != null) transformFace(group[0] * w, group[1] * h, group[2], fx, fy);
        for (java.util.Map.Entry<String, float[]> e : custom.entrySet()) {
            if (e.getKey().equals("face")) continue;
            final float[] v = e.getValue();
            transform(e.getKey(), v[0] * w, v[1] * h, v[2]);
        }
        for (Stick stick : new Stick[] {leftStick, rightStick}) {
            stick.homeX = stick.x;
            stick.homeY = stick.y;
        }
        measureFace();
        excludeSystemGestures();
        invalidate();
    }

    /** faceX, faceY, faceSpread: the centre and half extent of the four face buttons, wherever they are. */
    private void measureFace() {
        float minX = Float.MAX_VALUE, minY = Float.MAX_VALUE, maxX = -Float.MAX_VALUE, maxY = -Float.MAX_VALUE;
        for (int bit : new int[] {A, B, X, Y}) {
            final Button b = button(bit);
            minX = Math.min(minX, b.x - b.r);
            maxX = Math.max(maxX, b.x + b.r);
            minY = Math.min(minY, b.y - b.r);
            maxY = Math.max(maxY, b.y + b.r);
        }
        faceX = (minX + maxX) / 2f;
        faceY = (minY + maxY) / 2f;
        faceSpread = Math.max(maxX - minX, maxY - minY) / 2f;
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
        final float rh = Math.min(limit, faceSpread * 2.4f) / 2f;
        rects.add(new android.graphics.Rect(Math.round(faceX - faceSpread * 1.6f), Math.round(faceY - rh),
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
            case "A": return A;
            case "B": return B;
            case "X": return X;
            case "Y": return Y;
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
    float[] bounds(String key) {
        switch (key) {
            case "lstick": return new float[] {leftStick.homeX, leftStick.homeY, leftStick.r};
            case "rstick": return new float[] {rightStick.homeX, rightStick.homeY, rightStick.r};
            case "pad": return new float[] {padX, padY, padR};
            default: {
                final Button b = button(bitOf(key));
                return new float[] {b.x, b.y, Math.max(b.r, b.halfWidth)};
            }
        }
    }

    /** The name the editor shows for an element. */
    static String nameOf(String key) {
        switch (key) {
            case "lstick": return Tr.t("Left Stick");
            case "rstick": return Tr.t("Right Stick");
            case "pad": return Tr.t("D-Pad");
            case "A": return "A · Special";
            case "B": return "B · Jump";
            case "X": return "X · Skill";
            case "Y": return "Y · Attack";
            case "L": return "L · Camera";
            case "ZL": return "ZL · BASARA";
            case "R": return "R · Parry";
            case "ZR": return "ZR · Lock On";
            case "minus": return "−";
            case "plus": return "+";
            case "home": return "HOME";
            default: return Tr.t("Menu");
        }
    }

    /** Moves an element's centre to (x, y) and multiplies its size by scale (from the default). */
    private void transform(String key, float x, float y, float scale) {
        switch (key) {
            case "lstick": leftStick.x = x; leftStick.y = y; leftStick.r *= scale; break;
            case "rstick": rightStick.x = x; rightStick.y = y; rightStick.r *= scale; break;
            case "pad": padX = x; padY = y; padR *= scale; break;
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

    /** An older layout's face group: the diamond moved to (x, y) and scaled about its centre. */
    private void transformFace(float x, float y, float scale, float cx, float cy) {
        for (int bit : new int[] {A, B, X, Y}) {
            final Button b = button(bit);
            b.x = x + (b.x - cx) * scale;
            b.y = y + (b.y - cy) * scale;
            b.r *= scale;
            b.halfWidth *= scale;
        }
    }

    private String elementAt(float x, float y) {
        String best = null;
        float bestDistance = Float.MAX_VALUE;
        for (String key : ELEMENTS) {
            if (key.equals("rstick") && !rightStickShown()) continue;
            final float[] b = bounds(key);
            final float reach = Math.max(b[2] * 1.15f, 0.06f * getHeight());  // small ones stay easy to grab
            final float d = (float) Math.hypot(x - b[0], y - b[1]);
            if (d <= reach && d / reach < bestDistance) {
                best = key;
                bestDistance = d / reach;
            }
        }
        return best;
    }

    String selected() { return selected; }

    float scaleOf(String key) {
        final float[] v = custom.get(key);
        return v != null ? v[2] : 1f;
    }

    /** Sets one element's size (a multiple of its default), keeping its centre. */
    void setScale(String key, float scale) {
        final float[] b = bounds(key);
        custom.put(key, new float[] {b[0] / getWidth(), b[1] / getHeight(), clampScale(scale)});
        layoutControls(getWidth(), getHeight());
    }

    /** Remembers every size, so scaleAll can scale them together from there. */
    void beginScaleAll() {
        scaleSnapshot.clear();
        for (String key : ELEMENTS) scaleSnapshot.put(key, scaleOf(key));
    }

    /** Every element at its size from beginScaleAll times `factor`, each kept within the limits. */
    void scaleAll(float factor) {
        if (scaleSnapshot.isEmpty()) beginScaleAll();
        for (String key : ELEMENTS) {
            final float[] b = bounds(key);
            custom.put(key, new float[] {b[0] / getWidth(), b[1] / getHeight(), clampScale(scaleSnapshot.get(key) * factor)});
        }
        layoutControls(getWidth(), getHeight());
    }

    /** Back to the default place and size: one element, or all of them (null). */
    void resetElement(String key) {
        if (key == null) custom.clear();
        else custom.remove(key);
        layoutControls(getWidth(), getHeight());
    }

    private void select(String key) {
        if (key == null ? selected == null : key.equals(selected)) return;
        selected = key;
        invalidate();
        if (editListener != null) editListener.onEditSelect(key);
    }

    private void editTouch(MotionEvent e) {
        final int action = e.getActionMasked();
        final float snap = 0.018f * getHeight();
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            final int i = e.getActionIndex();
            final float x = e.getX(i), y = e.getY(i);
            if (editFinger >= 0) {
                // A second finger on a held control: pinch to resize it.
                if (pinchFinger < 0 && selected != null) {
                    final int j = e.findPointerIndex(editFinger);
                    if (j >= 0) {
                        pinchFinger = e.getPointerId(i);
                        pinchStartDistance = Math.max(1f, (float) Math.hypot(x - e.getX(j), y - e.getY(j)));
                        pinchStartScale = scaleOf(selected);
                    }
                }
                return;
            }
            final String hit = elementAt(x, y);
            select(hit);
            if (hit != null) {
                final float[] b = bounds(hit);
                editFinger = e.getPointerId(i);
                grabDx = b[0] - x;
                grabDy = b[1] - y;
                if (editListener != null) editListener.onEditDrag(true);
                if (haptics) performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK);
            }
        } else if (action == MotionEvent.ACTION_MOVE && editFinger >= 0 && selected != null) {
            final int i = e.findPointerIndex(editFinger);
            if (i < 0) return;
            if (pinchFinger >= 0) {
                final int j = e.findPointerIndex(pinchFinger);
                if (j < 0) return;
                final float distance = (float) Math.hypot(e.getX(i) - e.getX(j), e.getY(i) - e.getY(j));
                setScale(selected, pinchStartScale * distance / pinchStartDistance);
                if (editListener != null) editListener.onEditScaled(selected);
                return;
            }
            float x = Math.max(0f, Math.min(getWidth(), e.getX(i) + grabDx));
            float y = Math.max(0f, Math.min(getHeight(), e.getY(i) + grabDy));
            // Snap to the screen's centre lines and to the other controls' centres.
            guideX = guideY = Float.NaN;
            float bestX = snap, bestY = snap;
            if (Math.abs(x - getWidth() / 2f) < bestX) { bestX = Math.abs(x - getWidth() / 2f); guideX = getWidth() / 2f; }
            if (Math.abs(y - getHeight() / 2f) < bestY) { bestY = Math.abs(y - getHeight() / 2f); guideY = getHeight() / 2f; }
            for (String key : ELEMENTS) {
                if (key.equals(selected)) continue;
                final float[] o = bounds(key);
                if (Math.abs(x - o[0]) < bestX) { bestX = Math.abs(x - o[0]); guideX = o[0]; }
                if (Math.abs(y - o[1]) < bestY) { bestY = Math.abs(y - o[1]); guideY = o[1]; }
            }
            if (!Float.isNaN(guideX)) x = guideX;
            if (!Float.isNaN(guideY)) y = guideY;
            custom.put(selected, new float[] {x / getWidth(), y / getHeight(), scaleOf(selected)});
            layoutControls(getWidth(), getHeight());
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP
                || action == MotionEvent.ACTION_CANCEL) {
            final int id = e.getPointerId(e.getActionIndex());
            if (action == MotionEvent.ACTION_CANCEL || id == editFinger || id == pinchFinger) {
                final boolean wasActive = editFinger >= 0;
                editFinger = pinchFinger = -1;
                guideX = guideY = Float.NaN;
                invalidate();
                if (wasActive && editListener != null) editListener.onEditDrag(false);
            }
        }
    }

    private void place(int bit, float x, float y, float r) {
        final Button b = button(bit);
        b.x = x;
        b.y = y;
        b.r = r;
        b.halfWidth = r;
    }

    private void placePill(int bit, float x, float y, float r, float halfWidth) {
        place(bit, x, y, r);
        button(bit).halfWidth = halfWidth;
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
        } else if (rightStickShown() && onStick(rightStick, x, y)) {
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

    /** The empty left side under the shoulder buttons, where a thumb grabs the left stick. */
    private boolean inLeftStickZone(float x, float y) {
        return !fixedStick && showsControls() && leftStick.finger < 0 && x < getWidth() * 0.4f && y > getHeight() * 0.22f;
    }

    /** The empty right side, where a drag turns the camera (the right stick when not held). */
    private boolean inCameraZone(float x, float y) {
        return touchCamera && showsControls() && cameraId < 0 && rightStick.finger < 0
            && x >= getWidth() * 0.4f && y > getHeight() * 0.22f;
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
            // Invert Camera reverses the right stick on both axes while it is held (a camera drag
            // already applies it to the values it sets).
            final float flip = invertCamera && rightStick.finger >= 0 ? -1f : 1f;
            listener.onTouchSticks(leftStick.valueX, -leftStick.valueY, flip * rightStick.valueX,
                    -flip * rightStick.valueY);
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

    @Override
    protected void onDraw(Canvas canvas) {
        if (!showsControls()) {
            // Only the gear, kept readable: the way back to the menu.
            icons.setOpacity(Math.max(opacity, 0.47f));
            drawButton(canvas, button(MENU));
            return;
        }
        if (editing) drawEditBackdrop(canvas);
        icons.setOpacity(opacity);
        icons.stick(canvas, leftStick.x, leftStick.y, leftStick.r, leftStick.valueX, leftStick.valueY,
                leftStick.finger >= 0);
        if (rightStickShown()) {
            icons.stick(canvas, rightStick.x, rightStick.y, rightStick.r, rightStick.valueX, rightStick.valueY,
                    rightStick.finger >= 0);
        }
        if (style == STYLE_CLASSIC) icons.classicPad(canvas, padX, padY, padR, held & (LEFT | RIGHT | UP | DOWN));
        else icons.pad(canvas, padX, padY, padR, held & (LEFT | RIGHT | UP | DOWN));
        for (Button b : buttons) drawButton(canvas, b);
        if (editing) drawEditMarks(canvas);
    }

    private void drawButton(Canvas canvas, Button b) {
        final boolean down = (held & b.bit) != 0 || (b.bit == MENU && gearFinger >= 0);
        if (style != STYLE_CLASSIC) {
            icons.button(canvas, b.bit, b.glyph, b.caption, b.letter, b.x, b.y, b.r, down, b.hot, b.small);
        } else if (b.halfWidth > b.r * 1.01f) {
            icons.classicPill(canvas, b.bit, b.letter, b.caption, b.x, b.y, b.halfWidth, b.r, down, b.hot);
        } else if (b.small) {
            icons.classicSystem(canvas, b.bit, b.letter, b.bit == MENU ? b.glyph : null, b.x, b.y, b.r, down);
        } else {
            final int seal = b.bit == A ? 1 : b.bit == B ? 2 : b.bit == X ? 3 : b.bit == Y ? 4 : 0;
            icons.classic(canvas, b.bit, b.letter, b.caption, b.x, b.y, b.r, down, b.hot, seal);
        }
    }

    /** The paused game, dimmed, with a faint grid and the centre lines to line controls up against. */
    private void drawEditBackdrop(Canvas canvas) {
        final float w = getWidth(), h = getHeight();
        canvas.drawColor(Color.argb(150, 0, 0, 0));
        stroke.setPathEffect(null);
        stroke.setStrokeWidth(1f);
        stroke.setColor(Color.argb(18, 255, 255, 255));
        for (int i = 1; i < 10; ++i) {
            canvas.drawLine(w * i / 10f, 0, w * i / 10f, h, stroke);
            canvas.drawLine(0, h * i / 10f, w, h * i / 10f, stroke);
        }
        stroke.setColor(Color.argb(45, 255, 255, 255));
        canvas.drawLine(w / 2f, 0, w / 2f, h, stroke);
        canvas.drawLine(0, h / 2f, w, h / 2f, stroke);
    }

    /** Every control's grab area, the selected one outlined and named, and the snap guides. */
    private void drawEditMarks(Canvas canvas) {
        final float h = getHeight(), density = getResources().getDisplayMetrics().density;
        stroke.setPathEffect(dash);
        stroke.setStrokeWidth(1.2f * density);
        stroke.setColor(Color.argb(90, 255, 255, 255));
        for (String key : ELEMENTS) {
            if (key.equals(selected) || (key.equals("rstick") && !rightStickShown())) continue;
            final float[] b = bounds(key);
            canvas.drawCircle(b[0], b[1], b[2] * 1.12f, stroke);
        }
        stroke.setPathEffect(null);
        stroke.setColor(Color.argb(200, 255, 214, 102));
        stroke.setStrokeWidth(1.5f * density);
        if (!Float.isNaN(guideX)) canvas.drawLine(guideX, 0, guideX, h, stroke);
        if (!Float.isNaN(guideY)) canvas.drawLine(0, guideY, getWidth(), guideY, stroke);
        if (selected == null) return;
        final float[] b = bounds(selected);
        stroke.setColor(Color.WHITE);
        stroke.setStrokeWidth(2.5f * density);
        canvas.drawCircle(b[0], b[1], b[2] * 1.12f, stroke);
        // Its name and size in a tag above it (below it when it sits at the top).
        final String tag = nameOf(selected) + "  " + Math.round(scaleOf(selected) * 100) + "%";
        text.setTextSize(12f * density);
        final float tw = text.measureText(tag) + 16f * density, th = 22f * density;
        float ty = b[1] - b[2] * 1.12f - 8f * density - th;
        if (ty < 4f * density) ty = b[1] + b[2] * 1.12f + 8f * density;
        final float tx = Math.max(4f * density, Math.min(getWidth() - tw - 4f * density, b[0] - tw / 2f));
        rect.set(tx, ty, tx + tw, ty + th);
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(Color.argb(230, 14, 16, 20));
        canvas.drawRoundRect(rect, th / 2f, th / 2f, fill);
        text.setColor(Color.WHITE);
        canvas.drawText(tag, rect.centerX(), rect.centerY() - (text.descent() + text.ascent()) / 2f, text);
    }
}
