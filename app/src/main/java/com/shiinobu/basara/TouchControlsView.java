package com.shiinobu.basara;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.CornerPathEffect;
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

    private static final int MODE_BUTTONS = 1;  // the finger presses whatever button it is over
    private static final int MODE_PAD = 2;      // the finger steers the D-pad until lifted
    private static final int MODE_STICK_L = 3;  // the finger holds the left stick until lifted
    private static final int MODE_STICK_R = 4;  // the same for the right stick

    private static final int SHAPE_ROUND = 0, SHAPE_PILL = 1;
    private static final float STICK_DEAD_ZONE = 0.12f;

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
        new Button(PLUS, null, SHAPE_ROUND, 1.25f),
    };
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

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path shape = new Path();
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

    void setOpacity(float opacity) { this.opacity = opacity; invalidate(); }

    void setHaptics(boolean haptics) { this.haptics = haptics; }

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

    private boolean showsControls() { return controlsVisible && !menuMode; }

    /** Lets go of everything, e.g. when the app loses focus or a menu opens. */
    void release() {
        pointerModes.clear();
        pointerBits.clear();
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

        leftStick.r = 0.185f * u;
        leftStick.x = leftEdge + 0.04f * u + leftStick.r;
        leftStick.y = 0.50f * u;
        padR = 0.145f * u;
        padX = leftStick.x + 0.34f * u;
        padY = 0.80f * u;

        // The diamond, as on the controller: A right, B bottom, X top, Y left.
        final float faceX = rightEdge - 0.20f * u, faceY = 0.50f * u, face = 0.125f * u, faceR = 0.078f * u;
        place(A, faceX + face, faceY, faceR);
        place(B, faceX, faceY + face, faceR);
        place(X, faceX, faceY - face, faceR);
        place(Y, faceX - face, faceY, faceR);
        rightStick.r = 0.125f * u;
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
        for (Stick stick : new Stick[] {leftStick, rightStick}) {
            stick.homeX = stick.x;
            stick.homeY = stick.y;
        }
        invalidate();
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
        final int action = e.getActionMasked();
        if (action == MotionEvent.ACTION_CANCEL) {
            endDrag(e.getX(), e.getY());
            release();
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
            if (mode == MODE_STICK_L || mode == MODE_STICK_R) {
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
        } else {
            pointerModes.put(id, MODE_BUTTONS);
            pointerBits.put(id, buttonsAt(x, y));
            if (buttonsAt(x, y) == 0) freeTaps.put(id, true);
        }
    }

    /** The empty left side under the shoulder pills, where a thumb grabs the left stick. */
    private boolean inLeftStickZone(float x, float y) {
        return showsControls() && leftStick.finger < 0 && x < getWidth() * 0.4f && y > getHeight() * 0.18f;
    }

    private void slide(int id, float x, float y) {
        final int mode = pointerModes.get(id, 0);
        if (mode == MODE_STICK_L) {
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

    /** The knob follows the finger out to the rim; the middle is a dead zone. */
    private void steer(Stick stick, float x, float y) {
        float nx = (x - stick.x) / stick.r, ny = (y - stick.y) / stick.r;
        final float length = (float) Math.hypot(nx, ny);
        if (length > 1f) {
            nx /= length;
            ny /= length;
        }
        if (length < STICK_DEAD_ZONE) {
            nx = ny = 0f;
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
            if (b.hit(x, y)) bits |= b.bit;
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
        if (!showsControls()) return;
        final int base = Math.round(255 * opacity);
        drawStick(canvas, leftStick, base);
        drawStick(canvas, rightStick, base);
        drawPad(canvas, base);
        for (Button b : buttons) {
            drawButton(canvas, b, base);
        }
    }

    private void drawSurface(Canvas canvas, Button b, boolean down, int base) {
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(down ? seal(base + 60) : ink(base * 3 / 4));
        stroke.setStrokeWidth(Math.max(1.5f, getHeight() * 0.0035f));
        stroke.setColor(down ? seal(base + 60) : paper(base * 4 / 5));
        rect.set(b.x - b.halfWidth, b.y - b.r, b.x + b.halfWidth, b.y + b.r);
        canvas.drawRoundRect(rect, b.r, b.r, fill);
        canvas.drawRoundRect(rect, b.r, b.r, stroke);
    }

    private void drawButton(Canvas canvas, Button b, int base) {
        final boolean down = (held & b.bit) != 0;
        drawSurface(canvas, b, down, base);
        final int glyph = paper(down ? 255 : base + 90);
        if (b.label != null) {
            text.setColor(glyph);
            text.setTextSize(b.shape == SHAPE_PILL ? b.r * 1.05f : b.r * 1.15f);
            canvas.drawText(b.label, b.x, b.y - (text.descent() + text.ascent()) / 2, text);
            return;
        }
        stroke.setColor(glyph);
        stroke.setStrokeWidth(Math.max(2f, b.r * 0.14f));
        final float arm = b.r * 0.42f;
        if (b.bit == MINUS || b.bit == PLUS) {
            canvas.drawLine(b.x - arm, b.y, b.x + arm, b.y, stroke);
            if (b.bit == PLUS) canvas.drawLine(b.x, b.y - arm, b.x, b.y + arm, stroke);
        } else {
            // A house: roof and walls in one line.
            final float wall = arm * 0.72f;
            shape.reset();
            shape.moveTo(b.x - arm * 1.1f, b.y - arm * 0.05f);
            shape.lineTo(b.x, b.y - arm);
            shape.lineTo(b.x + arm * 1.1f, b.y - arm * 0.05f);
            shape.moveTo(b.x - wall, b.y - arm * 0.2f);
            shape.lineTo(b.x - wall, b.y + arm * 0.85f);
            shape.lineTo(b.x + wall, b.y + arm * 0.85f);
            shape.lineTo(b.x + wall, b.y - arm * 0.2f);
            canvas.drawPath(shape, stroke);
        }
    }

    /** One outline with rounded corners, the pressed arm lit from the inside. */
    private void drawPad(Canvas canvas, int base) {
        final float arm = padR * 0.34f, reach = padR;
        shape.reset();
        shape.moveTo(padX - arm, padY - reach);
        shape.lineTo(padX + arm, padY - reach);
        shape.lineTo(padX + arm, padY - arm);
        shape.lineTo(padX + reach, padY - arm);
        shape.lineTo(padX + reach, padY + arm);
        shape.lineTo(padX + arm, padY + arm);
        shape.lineTo(padX + arm, padY + reach);
        shape.lineTo(padX - arm, padY + reach);
        shape.lineTo(padX - arm, padY + arm);
        shape.lineTo(padX - reach, padY + arm);
        shape.lineTo(padX - reach, padY - arm);
        shape.lineTo(padX - arm, padY - arm);
        shape.close();
        final CornerPathEffect corners = new CornerPathEffect(arm * 0.35f);
        fill.setStyle(Paint.Style.FILL);
        fill.setPathEffect(corners);
        fill.setColor(ink(base * 3 / 4));
        canvas.drawPath(shape, fill);
        fill.setPathEffect(null);
        stroke.setPathEffect(corners);
        stroke.setStrokeWidth(Math.max(1.5f, getHeight() * 0.0035f));
        stroke.setColor(paper(base * 4 / 5));
        canvas.drawPath(shape, stroke);
        stroke.setPathEffect(null);
        drawPadArm(canvas, base, LEFT, -1, 0);
        drawPadArm(canvas, base, RIGHT, 1, 0);
        drawPadArm(canvas, base, UP, 0, -1);
        drawPadArm(canvas, base, DOWN, 0, 1);
    }

    private void drawPadArm(Canvas canvas, int base, int bit, int ux, int uy) {
        final boolean down = (held & bit) != 0;
        final float inner = padR * 0.40f, outer = padR * 0.90f, half = padR * 0.26f;
        final float cx = padX + ux * (inner + outer) / 2, cy = padY + uy * (inner + outer) / 2;
        if (down) {
            final float hx = ux != 0 ? (outer - inner) / 2 : half, hy = uy != 0 ? (outer - inner) / 2 : half;
            rect.set(cx - hx, cy - hy, cx + hx, cy + hy);
            fill.setColor(seal(base + 60));
            canvas.drawRoundRect(rect, half * 0.35f, half * 0.35f, fill);
        }
        // A short rule across the arm instead of an arrow: enough to find it by eye.
        stroke.setStrokeWidth(Math.max(2f, padR * 0.045f));
        stroke.setColor(paper(down ? 255 : base + 40));
        final float mark = padR * 0.11f;
        final float mx = padX + ux * padR * 0.68f, my = padY + uy * padR * 0.68f;
        canvas.drawLine(mx - uy * mark, my - ux * mark, mx + uy * mark, my + ux * mark, stroke);
    }

    private void drawStick(Canvas canvas, Stick stick, int base) {
        final boolean active = stick.finger >= 0;
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(ink(base * 3 / 5));
        canvas.drawCircle(stick.x, stick.y, stick.r, fill);
        stroke.setStrokeWidth(Math.max(1.5f, getHeight() * 0.0035f));
        stroke.setColor(paper(base * 4 / 5));
        canvas.drawCircle(stick.x, stick.y, stick.r, stroke);
        // Compass ticks on the rim.
        stroke.setColor(paper(base / 2));
        final float tickIn = stick.r * 0.86f, tickOut = stick.r * 0.95f;
        canvas.drawLine(stick.x, stick.y - tickOut, stick.x, stick.y - tickIn, stroke);
        canvas.drawLine(stick.x, stick.y + tickIn, stick.x, stick.y + tickOut, stroke);
        canvas.drawLine(stick.x - tickOut, stick.y, stick.x - tickIn, stick.y, stroke);
        canvas.drawLine(stick.x + tickIn, stick.y, stick.x + tickOut, stick.y, stroke);
        // The knob travels so its edge reaches the rim, and stays on the ring at full tilt.
        final float knobR = stick.r * 0.46f, travel = stick.r - knobR;
        final float kx = stick.x + stick.valueX * travel, ky = stick.y + stick.valueY * travel;
        fill.setColor(active ? seal(base + 60) : paper(base * 2 / 3));
        canvas.drawCircle(kx, ky, knobR, fill);
        stroke.setColor(active ? paper(200) : ink(base));
        canvas.drawCircle(kx, ky, knobR * 0.62f, stroke);
    }
}
