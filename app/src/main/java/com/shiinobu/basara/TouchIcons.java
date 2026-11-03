package com.shiinobu.basara;

import android.graphics.Canvas;
import android.graphics.DashPathEffect;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;

/**
 * The touch controls' faces ("Ink & Seal"): every control is drawn in an 80x80 box centred on 40,40
 * and scaled onto the screen, ink fill with a paper outline, a vermilion seal mark, and vermilion
 * when held. Alphas are the design's at the default opacity (0.6) and follow the opacity setting.
 */
final class TouchIcons {
    private static final int INK = 0x0E0F12, INK_INNER = 0x14161B, PAPER = 0xF4F0E6;
    private static final int SEAL = 0xD03A26, SEAL_BRIGHT = 0xE11D48;
    private static final float DEFAULT_OPACITY = 0.6f;

    // Face buttons: seal arc and dot on the outer side, the letter.
    private static final Path A_SEAL = svg("M 64.2 28.5 A 26.5 26.5 0 0 1 64.2 51.5");
    private static final Path A_GLYPH = svg("M 32.5 50 L 40 29.5 L 47.5 50 M 35.2 43.2 H 44.8");
    private static final Path B_MARK = svg("M 35 66.2 A 26.5 26.5 0 0 0 45 66.2");
    private static final Path B_GLYPH = svg("M 34 30 V 50 H 42.5 C 45.5 50 47.2 48.2 47.2 45.2 C 47.2 42.2 45.2 40 42 40 "
            + "H 34 M 34 30 H 41.5 C 44.3 30 46 31.6 46 35 C 46 38.2 44.2 40 41.5 40");
    private static final Path X_SEAL = svg("M 28.5 15.8 A 26.5 26.5 0 0 1 51.5 15.8");
    private static final Path X_GLYPH = svg("M 33 30 L 47 50 M 47 30 L 33 50");
    private static final Path Y_SEAL = svg("M 15.8 32 A 26.5 26.5 0 0 0 15.8 48");
    private static final Path Y_GLYPH = svg("M 32.5 30 L 40 40.5 L 47.5 30 M 40 40.5 V 50");

    // Shoulders: L and R bevel the outer top corner, ZL and ZR are deeper with a seal rule.
    private static final Path L_BODY = svg("M 22 21 H 64 A 10 10 0 0 1 74 31 V 49 A 10 10 0 0 1 64 59 H 16 "
            + "A 10 10 0 0 1 6 49 V 35 Q 6 21 22 21 Z");
    private static final Path L_RULE = svg("M 22 26.5 H 60");
    private static final Path L_GLYPH = svg("M 35 32 V 48 H 46");
    private static final Path R_BODY = svg("M 16 21 H 58 Q 74 21 74 35 V 49 A 10 10 0 0 1 64 59 H 16 "
            + "A 10 10 0 0 1 6 49 V 31 A 10 10 0 0 1 16 21 Z");
    private static final Path R_RULE = svg("M 20 26.5 H 58");
    private static final Path R_GLYPH = svg("M 34 48 V 32 H 42 C 45 32 46.8 33.8 46.8 36.5 C 46.8 39.2 45 41 42 41 "
            + "H 34 M 41 41 L 46.8 48");
    private static final Path Z_BODY = svg("M 16 19 H 64 A 8 8 0 0 1 72 27 V 46 A 15 15 0 0 1 57 61 H 23 "
            + "A 15 15 0 0 1 8 46 V 27 A 8 8 0 0 1 16 19 Z");
    private static final Path Z_RULE = svg("M 28 24.5 H 52");
    private static final Path ZL_GLYPH = svg("M 27 33.5 H 38 L 27 47.5 H 38 M 44 33.5 V 47.5 H 53.5");
    private static final Path ZR_GLYPH = svg("M 25.5 33.5 H 36.5 L 25.5 47.5 H 36.5 M 42 47.5 V 33.5 H 49.5 "
            + "C 52.2 33.5 54 35.1 54 37.8 C 54 40.3 52.2 42 49.5 42 H 42 M 48.5 42 L 54 47.5");

    // D-pad: one cross, an arm lit per direction, chevrons, and ticks for the diagonals.
    private static final Path PAD = svg("M 33 6 H 47 A 4 4 0 0 1 51 10 V 29 H 70 A 4 4 0 0 1 74 33 V 47 "
            + "A 4 4 0 0 1 70 51 H 51 V 70 A 4 4 0 0 1 47 74 H 33 A 4 4 0 0 1 29 70 V 51 H 10 "
            + "A 4 4 0 0 1 6 47 V 33 A 4 4 0 0 1 10 29 H 29 V 10 A 4 4 0 0 1 33 6 Z");
    private static final Path ARM_UP = svg("M 33 7 H 47 A 3 3 0 0 1 50 10 V 30 H 30 V 10 A 3 3 0 0 1 33 7 Z");
    private static final Path ARM_DOWN = svg("M 30 50 H 50 V 70 A 3 3 0 0 1 47 73 H 33 A 3 3 0 0 1 30 70 Z");
    private static final Path ARM_LEFT = svg("M 10 30 H 30 V 50 H 10 A 3 3 0 0 1 7 47 V 33 A 3 3 0 0 1 10 30 Z");
    private static final Path ARM_RIGHT = svg("M 50 30 H 70 A 3 3 0 0 1 73 33 V 47 A 3 3 0 0 1 70 50 H 50 Z");
    private static final Path CHEV_UP = svg("M 35.5 19 L 40 13.5 L 44.5 19");
    private static final Path CHEV_DOWN = svg("M 35.5 61 L 40 66.5 L 44.5 61");
    private static final Path CHEV_LEFT = svg("M 19 35.5 L 13.5 40 L 19 44.5");
    private static final Path CHEV_RIGHT = svg("M 61 35.5 L 66.5 40 L 61 44.5");
    private static final Path DIAG_UL = svg("M 22 25 L 25 22");
    private static final Path DIAG_UR = svg("M 58 25 L 55 22");
    private static final Path DIAG_DL = svg("M 22 55 L 25 58");
    private static final Path DIAG_DR = svg("M 58 55 L 55 58");

    // Sticks: a dashed well with compass ticks, and a cap with its own ticks and a seal dot.
    private static final Path WELL_TICKS = svg("M 40 4 V 8 M 40 72 V 76 M 4 40 H 8 M 72 40 H 76");
    private static final Path WELL_CROSS = svg("M 37 40 H 43 M 40 37 V 43");
    private static final Path CAP_TICKS = svg("M 40 26.5 V 29.5 M 40 50.5 V 53.5 M 26.5 40 H 29.5 M 50.5 40 H 53.5");

    // System buttons.
    private static final Path MINUS_GLYPH = svg("M 31 40 H 49");
    private static final Path PLUS_GLYPH = svg("M 31 40 H 49 M 40 31 V 49");
    private static final Path HOME_GLYPH = svg("M 29.5 40.5 L 40 30.5 L 50.5 40.5 M 33 38.5 V 49.5 H 47 V 38.5");
    private static final Path GEAR_GLYPH = gear();

    static final float FACE_R = 32f, SYSTEM_R = 23f, HOME_R = 25f, PAD_R = 34f, WELL_R = 34f, CAP_R = 16.5f;

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final DashPathEffect dash = new DashPathEffect(new float[] {6f, 4f}, 0f);
    private float k = 1f;  // opacity relative to the design's

    TouchIcons() {
        fill.setStyle(Paint.Style.FILL);
        line.setStyle(Paint.Style.STROKE);
        line.setStrokeCap(Paint.Cap.ROUND);
        line.setStrokeJoin(Paint.Join.ROUND);
    }

    void setOpacity(float opacity) { k = opacity / DEFAULT_OPACITY; }

    private int idle(int rgb, float alpha) { return color(rgb, alpha * k); }
    /** Held controls stay clear even at a low opacity setting. */
    private int held(int rgb, float alpha) { return color(rgb, alpha * Math.max(k, 1f)); }
    private static int color(int rgb, float alpha) {
        return (Math.round(Math.max(0f, Math.min(1f, alpha)) * 255) << 24) | (rgb & 0xFFFFFF);
    }

    /** Moves the canvas so the 80x80 box sits centred on cx,cy at `unit` pixels per step. */
    private static void enter(Canvas c, float cx, float cy, float unit, boolean down) {
        c.save();
        c.translate(cx, cy);
        c.scale(unit, unit);
        if (down) c.scale(0.94f, 0.94f);
        c.translate(-40f, -40f);
    }

    private void body(Canvas c, Path shape, float circleR, boolean down) {
        fill.setColor(down ? held(SEAL, 0.78f) : idle(INK, 0.45f));
        line.setColor(down ? held(0xFFFFFF, 1f) : idle(PAPER, 0.85f));
        line.setStrokeWidth(down ? 2.35f : 1.95f);
        if (shape != null) {
            c.drawPath(shape, fill);
            c.drawPath(shape, line);
        } else {
            c.drawCircle(40, 40, circleR, fill);
            c.drawCircle(40, 40, circleR, line);
        }
    }

    private void dim(Canvas c, Path p, float circleR, boolean down) {
        line.setColor(down ? held(0xFFFFFF, 0.8f) : idle(PAPER, 0.32f));
        line.setStrokeWidth(1.35f);
        if (p != null) c.drawPath(p, line);
        else c.drawCircle(40, 40, circleR, line);
    }

    private void glyph(Canvas c, Path p, boolean down, float width) {
        line.setColor(down ? held(0xFFFFFF, 1f) : idle(PAPER, 0.92f));
        line.setStrokeWidth(down ? width + 0.3f : width);
        c.drawPath(p, line);
    }

    private void seal(Canvas c, Path p, boolean down, float width) {
        line.setColor(down ? held(0xFFFFFF, 1f) : idle(SEAL, 0.88f));
        line.setStrokeWidth(width);
        c.drawPath(p, line);
    }

    private void sealDot(Canvas c, float x, float y, float r, boolean down) {
        fill.setColor(down ? held(0xFFFFFF, 1f) : idle(SEAL, 0.9f));
        c.drawCircle(x, y, r, fill);
    }

    /** A, B, X or Y, `r` = the button's radius on screen. */
    void face(Canvas c, int which, float cx, float cy, float r, boolean down) {
        enter(c, cx, cy, r / FACE_R, down);
        body(c, null, FACE_R, down);
        dim(c, null, 26.5f, down);
        switch (which) {
            case TouchControlsView.A:
                seal(c, A_SEAL, down, 2.1f);
                sealDot(c, 68.5f, 40f, 1.8f, down);
                glyph(c, A_GLYPH, down, 2.2f);
                break;
            case TouchControlsView.B:
                line.setColor(down ? held(0xFFFFFF, 1f) : idle(PAPER, 0.85f));
                line.setStrokeWidth(1.6f);
                c.drawPath(B_MARK, line);
                glyph(c, B_GLYPH, down, 2.2f);
                break;
            case TouchControlsView.X:
                seal(c, X_SEAL, down, 2.1f);
                sealDot(c, 40f, 11.5f, 1.8f, down);
                glyph(c, X_GLYPH, down, 2.2f);
                break;
            default:
                seal(c, Y_SEAL, down, 2.1f);
                sealDot(c, 11.5f, 40f, 1.8f, down);
                glyph(c, Y_GLYPH, down, 2.2f);
                break;
        }
        c.restore();
    }

    /** L, R, ZL or ZR inside the pill's box (half width `hw`, half height `hh`). */
    void shoulder(Canvas c, int which, float cx, float cy, float hw, float hh, boolean down) {
        // The shapes span 68 x 42 steps; fit the box without stretching the strokes.
        enter(c, cx, cy, Math.min(2f * hw / 68f, 2f * hh / 42f), down);
        switch (which) {
            case TouchControlsView.L:
                body(c, L_BODY, 0, down);
                dim(c, L_RULE, 0, down);
                glyph(c, L_GLYPH, down, 2.2f);
                break;
            case TouchControlsView.R:
                body(c, R_BODY, 0, down);
                dim(c, R_RULE, 0, down);
                glyph(c, R_GLYPH, down, 2.2f);
                break;
            default:
                body(c, Z_BODY, 0, down);
                seal(c, Z_RULE, down, 2.1f);
                glyph(c, which == TouchControlsView.ZL ? ZL_GLYPH : ZR_GLYPH, down, 2.2f);
                break;
        }
        c.restore();
    }

    /** −, HOME, + or the settings gear, `r` = the button's radius on screen. */
    void system(Canvas c, int which, float cx, float cy, float r, boolean down) {
        final boolean home = which == TouchControlsView.HOME;
        final float own = home ? HOME_R : SYSTEM_R;
        enter(c, cx, cy, r / own, down);
        body(c, null, own, down);
        if (home) {
            line.setColor(down ? held(0xFFFFFF, 1f) : idle(SEAL, 0.88f));
            line.setStrokeWidth(1.4f);
            c.drawCircle(40, 40, 20f, line);
            glyph(c, HOME_GLYPH, down, 2.0f);
            sealDot(c, 40f, 43f, 1.8f, down);
        } else {
            dim(c, null, 18.5f, down);
            glyph(c, which == TouchControlsView.MINUS ? MINUS_GLYPH
                    : which == TouchControlsView.PLUS ? PLUS_GLYPH : GEAR_GLYPH, down, 2.2f);
            if (which == TouchControlsView.MENU) sealDot(c, 40f, 40f, 1.8f, down);
        }
        c.restore();
    }

    /** The D-pad, `r` = half its width on screen; `held` = the direction bits down now. */
    void pad(Canvas c, float cx, float cy, float r, int heldBits) {
        enter(c, cx, cy, r / PAD_R, false);
        fill.setColor(idle(INK, 0.45f));
        c.drawPath(PAD, fill);
        final boolean up = (heldBits & TouchControlsView.UP) != 0, down = (heldBits & TouchControlsView.DOWN) != 0;
        final boolean left = (heldBits & TouchControlsView.LEFT) != 0;
        final boolean right = (heldBits & TouchControlsView.RIGHT) != 0;
        fill.setColor(held(SEAL, 0.78f));
        if (up) c.drawPath(ARM_UP, fill);
        if (down) c.drawPath(ARM_DOWN, fill);
        if (left) c.drawPath(ARM_LEFT, fill);
        if (right) c.drawPath(ARM_RIGHT, fill);
        line.setColor(idle(PAPER, 0.85f));
        line.setStrokeWidth(1.95f);
        c.drawPath(PAD, line);
        diagonal(c, DIAG_UL, up && left);
        diagonal(c, DIAG_UR, up && right);
        diagonal(c, DIAG_DL, down && left);
        diagonal(c, DIAG_DR, down && right);
        dim(c, null, 5.5f, false);
        sealDot(c, 40f, 40f, 2.2f, false);
        chevron(c, CHEV_UP, up);
        chevron(c, CHEV_DOWN, down);
        chevron(c, CHEV_LEFT, left);
        chevron(c, CHEV_RIGHT, right);
        c.restore();
    }

    private void diagonal(Canvas c, Path p, boolean lit) {
        line.setColor(lit ? held(SEAL_BRIGHT, 1f) : idle(PAPER, 0.32f));
        line.setStrokeWidth(lit ? 2.8f : 1.6f);
        c.drawPath(p, line);
    }

    private void chevron(Canvas c, Path p, boolean lit) {
        line.setColor(lit ? held(0xFFFFFF, 1f) : idle(PAPER, 0.92f));
        line.setStrokeWidth(lit ? 2.6f : 2.1f);
        c.drawPath(p, line);
    }

    /** A stick: the well of radius `r` on screen, the cap moved by `vx`,`vy` (-1..1). */
    void stick(Canvas c, float cx, float cy, float r, float vx, float vy, boolean active) {
        enter(c, cx, cy, r / WELL_R, false);
        fill.setColor(idle(INK_INNER, 0.32f));
        c.drawCircle(40, 40, WELL_R, fill);
        line.setPathEffect(dash);
        dim(c, null, WELL_R, false);
        line.setPathEffect(null);
        dim(c, WELL_TICKS, 0, false);
        dim(c, WELL_CROSS, 0, false);
        final float travel = WELL_R - CAP_R;
        final float kx = 40f + vx * travel, ky = 40f + vy * travel;
        if (active) {
            line.setColor(held(SEAL_BRIGHT, 1f));
            line.setStrokeWidth(2.2f);
            c.drawLine(40, 40, kx, ky, line);
        }
        c.translate(kx - 40f, ky - 40f);
        fill.setColor(active ? held(SEAL, 0.78f) : idle(INK, 0.45f));
        line.setColor(active ? held(0xFFFFFF, 1f) : idle(PAPER, 0.85f));
        line.setStrokeWidth(active ? 2.35f : 1.95f);
        c.drawCircle(40, 40, CAP_R, fill);
        c.drawCircle(40, 40, CAP_R, line);
        dim(c, null, 10.2f, active);
        dim(c, CAP_TICKS, 0, active);
        sealDot(c, 40f, 40f, 2.2f, active);
        c.restore();
    }

    /** A cog of eight teeth round a hole, in the 80x80 box. */
    private static Path gear() {
        final Path p = new Path();
        final float outer = 12.5f, inner = 9f;
        final int teeth = 8;
        for (int t = 0; t < teeth * 4; ++t) {
            final double angle = Math.PI * 2 * t / (teeth * 4) - Math.PI / (teeth * 4);
            final int phase = t % 4;
            final float radius = phase == 1 || phase == 2 ? outer : inner;
            final float x = 40f + (float) Math.cos(angle) * radius, y = 40f + (float) Math.sin(angle) * radius;
            if (t == 0) p.moveTo(x, y);
            else p.lineTo(x, y);
        }
        p.close();
        p.addCircle(40f, 40f, 4.2f, Path.Direction.CW);
        return p;
    }

    /** SVG path data, absolute commands only (M L H V C Q A Z), as used above. */
    static Path svg(String data) {
        final Path p = new Path();
        final String[] tok = data.trim().replace(",", " ").split("\\s+");
        int i = 0;
        float x = 0, y = 0, startX = 0, startY = 0;
        char cmd = 'M';
        while (i < tok.length) {
            final char first = tok[i].charAt(0);
            if (Character.isLetter(first)) {
                cmd = first;
                ++i;
                if (cmd == 'Z') {
                    p.close();
                    x = startX;
                    y = startY;
                    continue;
                }
            }
            switch (cmd) {
                case 'M':
                    x = f(tok[i++]); y = f(tok[i++]);
                    p.moveTo(x, y);
                    startX = x; startY = y;
                    cmd = 'L';  // further pairs are lines
                    break;
                case 'L':
                    x = f(tok[i++]); y = f(tok[i++]);
                    p.lineTo(x, y);
                    break;
                case 'H':
                    x = f(tok[i++]);
                    p.lineTo(x, y);
                    break;
                case 'V':
                    y = f(tok[i++]);
                    p.lineTo(x, y);
                    break;
                case 'C': {
                    final float x1 = f(tok[i++]), y1 = f(tok[i++]), x2 = f(tok[i++]), y2 = f(tok[i++]);
                    x = f(tok[i++]); y = f(tok[i++]);
                    p.cubicTo(x1, y1, x2, y2, x, y);
                    break;
                }
                case 'Q': {
                    final float x1 = f(tok[i++]), y1 = f(tok[i++]);
                    x = f(tok[i++]); y = f(tok[i++]);
                    p.quadTo(x1, y1, x, y);
                    break;
                }
                case 'A': {
                    final float rx = f(tok[i++]), ry = f(tok[i++]);
                    final float rot = f(tok[i++]);
                    final boolean large = f(tok[i++]) != 0, sweep = f(tok[i++]) != 0;
                    final float ex = f(tok[i++]), ey = f(tok[i++]);
                    arc(p, x, y, rx, ry, rot, large, sweep, ex, ey);
                    x = ex; y = ey;
                    break;
                }
                default:
                    throw new IllegalArgumentException("svg command " + cmd);
            }
        }
        return p;
    }

    private static float f(String s) { return Float.parseFloat(s); }

    /** SVG endpoint arc to Path.arcTo (circles and unrotated ellipses, all the icons use). */
    private static void arc(Path p, float x0, float y0, float rx, float ry, float rotation,
            boolean large, boolean sweep, float x1, float y1) {
        if (rx == 0 || ry == 0) { p.lineTo(x1, y1); return; }
        final double dx = (x0 - x1) / 2.0, dy = (y0 - y1) / 2.0;
        double rxs = rx, rys = ry;
        final double lambda = (dx * dx) / (rxs * rxs) + (dy * dy) / (rys * rys);
        if (lambda > 1) { rxs *= Math.sqrt(lambda); rys *= Math.sqrt(lambda); }
        double num = rxs * rxs * rys * rys - rxs * rxs * dy * dy - rys * rys * dx * dx;
        final double den = rxs * rxs * dy * dy + rys * rys * dx * dx;
        double coef = den == 0 ? 0 : Math.sqrt(Math.max(0, num / den));
        if (large == sweep) coef = -coef;
        final double cxp = coef * rxs * dy / rys, cyp = -coef * rys * dx / rxs;
        final double cx = cxp + (x0 + x1) / 2.0, cy = cyp + (y0 + y1) / 2.0;
        final double start = Math.atan2((dy - cyp) / rys, (dx - cxp) / rxs);
        final double end = Math.atan2((-dy - cyp) / rys, (-dx - cxp) / rxs);
        double delta = end - start;
        if (sweep && delta < 0) delta += 2 * Math.PI;
        if (!sweep && delta > 0) delta -= 2 * Math.PI;
        final RectF oval = new RectF((float) (cx - rxs), (float) (cy - rys), (float) (cx + rxs), (float) (cy + rys));
        p.arcTo(oval, (float) Math.toDegrees(start), (float) Math.toDegrees(delta), false);
    }
}
