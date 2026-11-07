package com.shiinobu.basara;

import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;

/**
 * The touch controls' faces, in two styles on one blue theme (A green, B red, X blue, Y amber):
 * <ul>
 * <li>Modern: translucent round buttons with one thin ring, a silhouette with a vertical gradient
 * (TouchGlyphs) and the controller letter in a badge set into the top of the ring; a D-pad of four
 * round keys.</li>
 * <li>Classic: no pictures. A large gradient letter with a small caption, a seal dot toward the
 * outside of the diamond, pill shoulders and a cross D-pad.</li>
 * </ul>
 * Every control is drawn in an 80x80 box centred on 40,40 and scaled onto the screen, so the shaders
 * are made once in box space. Alphas follow the opacity setting; held controls stay clear.
 */
final class TouchIcons {
    /** One set of colours and the shaders made from it. */
    private static final class Palette {
        final int ring, ringHi, ringLo, fillB, ic2, label;
        final Shader base, hot, glyph, glyphHeld, letter, letterSmall, knob, knobGlow, knobGlowHeld;
        Palette(int ring, int ringHi, int ringLo, int fillA, int fillB, int ic1, int ic2, int ic3, int glow, int label) {
            this.ring = ring;
            this.ringHi = ringHi;
            this.ringLo = ringLo;
            this.fillB = fillB;
            this.ic2 = ic2;
            this.label = label;
            base = new RadialGradient(40f, 31f, 52f, rgb(fillA), rgb(fillB), Shader.TileMode.CLAMP);
            hot = new RadialGradient(40f, 32f, 27f, argb(0.45f, glow), 0, Shader.TileMode.CLAMP);
            final int[] stops = {rgb(ic1), rgb(ic2), rgb(ic3)};
            final float[] at = {0f, 0.55f, 1f};
            glyph = new LinearGradient(0f, 0f, 0f, TouchGlyphs.BOX, stops, at, Shader.TileMode.CLAMP);
            glyphHeld = new LinearGradient(0f, 0f, 0f, TouchGlyphs.BOX,
                    new int[] {rgb(0xFFFFFF), rgb(0xFFFFFF), rgb(ic2)}, new float[] {0f, 0.6f, 1f}, Shader.TileMode.CLAMP);
            letter = new LinearGradient(0f, 21f, 0f, 52f, stops, at, Shader.TileMode.CLAMP);
            letterSmall = new LinearGradient(0f, 27f, 0f, 53f, stops, at, Shader.TileMode.CLAMP);
            // Knob shaders are in the knob's own space (centred on 0,0).
            knob = new RadialGradient(-0.1f * KNOB, -0.2f * KNOB, 1.4f * KNOB, rgb(ic1), rgb(ic2), Shader.TileMode.CLAMP);
            knobGlow = new RadialGradient(0f, 0f, 2.2f * KNOB, argb(0.4f, glow), 0, Shader.TileMode.CLAMP);
            knobGlowHeld = new RadialGradient(0f, 0f, 2.4f * KNOB, argb(0.7f, glow), 0, Shader.TileMode.CLAMP);
        }
    }

    private static final Palette BLUE = new Palette(0x2F7FE0, 0x8CC4FF, 0x0D2C5C, 0x0F1D33, 0x05090F,
            0xE6F3FF, 0x4EA4FF, 0x10408A, 0x3C96FF, 0xDCECFF);
    private static final Palette GREEN = new Palette(0x2FAE5F, 0x8BE3A8, 0x0D4224, 0x10291A, 0x050C08,
            0xE3FBE9, 0x48D17C, 0x0F6A33, 0x3CD278, 0xD8F7E3);
    private static final Palette RED = new Palette(0xD8343E, 0xFF8A7A, 0x5E1016, 0x2A1013, 0x0C0607,
            0xFFE1D8, 0xFF5A4A, 0x8E1620, 0xFF4632, 0xFFE3DC);
    private static final Palette AMBER = new Palette(0xE0A92A, 0xFFE08A, 0x5A420C, 0x2A2210, 0x0D0A04,
            0xFFF6D6, 0xF2BE3C, 0x7A5510, 0xFFBE3C, 0xFFF2CC);
    private static final Palette WHITE = new Palette(0xD9DEE6, 0xFFFFFF, 0x4A4F58, 0x24272C, 0x0A0B0D,
            0xFFFFFF, 0xE3E7EE, 0x8A93A3, 0xFFFFFF, 0xFFFFFF);

    /** The controls' colour; A, B, X and Y keep their own. */
    static final int COLOR_GOLD = 0, COLOR_RED = 1, COLOR_BLUE = 2, COLOR_WHITE = 3;
    private static final Palette[] THEMES = {AMBER, RED, BLUE, WHITE};
    private Palette theme = BLUE;

    void setColor(int color) { theme = color >= 0 && color < THEMES.length ? THEMES[color] : BLUE; }

    /** The colours of the sticks, D-pad and every button but A, B, X and Y. */
    private Palette chrome() { return theme; }

    /** A face button's colours (A green, B red, X blue, Y amber); everything else is the theme's. */
    private Palette paletteOf(int bit) {
        switch (bit) {
            case TouchControlsView.A: return GREEN;
            case TouchControlsView.B: return RED;
            case TouchControlsView.Y: return AMBER;
            case TouchControlsView.X: return BLUE;
            default: return theme;
        }
    }

    /** Button radius in the box: the ring's outer edge. */
    private static final float R = 38f, D = 2f * R;
    private static final float WELL = 38f, KNOB = 0.18f * 2f * WELL;
    /** Classic pills: width over height. */
    static final float PILL_RATIO = 1.62f;

    // Classic: the D-pad's cross, its arms, chevrons, and the Home outline (24 box).
    private static final Path CROSS = svg("M 33 6 H 47 A 4 4 0 0 1 51 10 V 29 H 70 A 4 4 0 0 1 74 33 V 47 "
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
    private static final Path HOUSE = svg("M 5 11.5 L 12 5 L 19 11.5 M 7.5 10 V 18.5 H 16.5 V 10");

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path tri = new Path();
    private final RectF cell = new RectF();
    private final RectF rect = new RectF();
    private float opacity = 0.6f;

    private final Shader gloss = new LinearGradient(40f, 2f, 40f, 40f, argb(0.15f, 0xFFFFFF), 0, Shader.TileMode.CLAMP);
    private final Shader wellShade = new RadialGradient(40f, 40f, WELL, argb(0.05f, 0), argb(0.32f, 0),
            Shader.TileMode.CLAMP);

    TouchIcons() {
        fill.setStyle(Paint.Style.FILL);
        line.setStyle(Paint.Style.STROKE);
        line.setStrokeCap(Paint.Cap.ROUND);
        line.setStrokeJoin(Paint.Join.ROUND);
        text.setTextAlign(Paint.Align.CENTER);
        text.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD));
    }

    void setOpacity(float opacity) { this.opacity = Math.max(0f, Math.min(1f, opacity)); }

    private static int rgb(int rgb) { return 0xFF000000 | rgb; }
    private static int argb(float alpha, int rgb) { return (Math.round(alpha * 255) << 24) | (rgb & 0xFFFFFF); }

    /** Sets a paint's colour at `alpha` of the current opacity. */
    private void tint(Paint p, int rgb, float alpha, boolean held) {
        p.setShader(null);
        p.setColor(rgb(rgb));
        p.setAlpha(Math.round(255 * Math.max(0f, Math.min(1f, alpha * level(held)))));
    }

    private void shade(Paint p, Shader s, float alpha, boolean held) {
        p.setColor(0xFFFFFFFF);
        p.setShader(s);
        p.setAlpha(Math.round(255 * Math.max(0f, Math.min(1f, alpha * level(held)))));
    }

    private float level(boolean held) { return held ? Math.max(opacity, 0.95f) : opacity; }

    /** Moves the canvas so the 80x80 box sits centred on cx,cy at `unit` pixels per step. */
    private static void enter(Canvas c, float cx, float cy, float unit, boolean down) {
        c.save();
        c.translate(cx, cy);
        c.scale(unit, unit);
        if (down) c.scale(0.94f, 0.94f);
        c.translate(-40f, -40f);
    }

    /** Classic: the disc under every round button: fill, held light, glow, gloss and the three-part ring. */
    private void disc(Canvas c, Palette p, boolean down, boolean hot) {
        shade(fill, p.base, 1f, down);
        c.drawCircle(40f, 40f, R, fill);
        if (down) {
            tint(fill, 0xFFFFFF, 0.1f, true);
            c.drawCircle(40f, 40f, R, fill);
        }
        if (hot || down) {
            shade(fill, p.hot, down ? 1.5f : 1f, down);
            c.drawCircle(40f, 40f, R, fill);
        }
        shade(fill, gloss, 1f, down);
        c.drawCircle(40f, 40f, R, fill);
        tint(line, down ? 0xFFFFFF : p.ring, 1f, down);
        line.setStrokeWidth(2.3f);
        c.drawCircle(40f, 40f, R - 1.15f, line);
        tint(line, p.ringLo, 1f, down);
        line.setStrokeWidth(1.7f);
        c.drawCircle(40f, 40f, R - 3.15f, line);
        tint(line, p.ringHi, 0.9f, down);
        line.setStrokeWidth(0.75f);
        c.drawCircle(40f, 40f, R - 4.4f, line);
    }

    private void caption(Canvas c, Palette p, String caption, float size, float spacing, boolean down) {
        text.setTextSize(size);
        text.setLetterSpacing(spacing);
        final float fit = 0.74f * D / Math.max(1f, text.measureText(caption));
        if (fit < 1f) text.setTextSize(size * fit);
        tint(text, p.label, 1f, down);
        text.setShadowLayer(1.6f, 0f, 0.8f, argb(0.9f * level(down), 0));
        c.drawText(caption, 40f, 2f + 0.85f * D - text.descent() * 0.4f, text);
        text.clearShadowLayer();
        text.setLetterSpacing(0f);
    }

    /**
     * Modern: a round button of radius `r` on screen: a translucent disc with one thin ring in the
     * button's colours, `glyph` from TouchGlyphs centred inside, and `key` (the controller letter, null
     * for none) in a badge set into the top of the ring. `hot` adds a glow. `caption` and `keyBelow`
     * are not drawn in this style.
     */
    void button(Canvas c, int bit, Path glyph, String caption, String key, float cx, float cy, float r,
            boolean down, boolean hot, boolean keyBelow) {
        final Palette p = paletteOf(bit);
        enter(c, cx, cy, r / R, down);

        final float C = 40f;
        final float mainR = 30.5f;
        final float by = C - mainR; // 9.5f, sits right on top rim
        final boolean hasKey = key != null;
        final boolean wide = hasKey && key.length() > 1; // "ZL", "ZR"
        final float rx = wide ? 10.5f : 8.0f;
        final float ry = 8.0f;
        final float gap = (wide ? 10.5f : 8.0f) * 1.15f;

        // 1. Translucent disc in the button's colours.
        shade(fill, p.base, down ? 0.75f : 0.55f, down);
        c.drawCircle(C, C, mainR, fill);
        if (hasKey) {
            cell.set(C - rx, by - ry, C + rx, by + ry);
            c.drawRoundRect(cell, ry, ry, fill);
        }

        // 2. Held light and glow.
        if (down) {
            tint(fill, 0xFFFFFF, 0.12f, true);
            c.drawCircle(C, C, mainR - 1f, fill);
        }
        if (down || hot) {
            shade(fill, p.hot, down ? 1.5f : 1f, down);
            c.drawCircle(C, C, mainR - 1f, fill);
        }
        if (down && hasKey) {
            tint(fill, p.ring, 1f, true);
            c.drawRoundRect(cell, ry, ry, fill);
        }

        // 3. The ring, open at the top where the badge sits.
        tint(line, down ? 0xFFFFFF : p.ring, 1f, down);
        line.setStrokeWidth(down ? 2.8f : 2.0f);
        if (hasKey) {
            rect.set(C - mainR + 1f, C - mainR + 1f, C + mainR - 1f, C + mainR - 1f);
            final float halfDeg = (float) Math.toDegrees(Math.asin(Math.min(gap / mainR, 1f)));
            c.drawArc(rect, -90f + halfDeg, 360f - 2f * halfDeg, false, line);
        } else {
            c.drawCircle(C, C, mainR - 1f, line);
        }

        // 4. The controller letter's badge.
        if (hasKey) {
            tint(line, down ? 0xFFFFFF : p.ringHi, down ? 1.0f : 0.85f, down);
            line.setStrokeWidth(down ? 2.2f : 1.6f);
            c.drawRoundRect(cell, ry, ry, line);
            text.setTextSize(wide ? 9.5f : 11.5f);
            tint(text, down ? 0xFFFFFF : p.label, 1f, down);
            c.drawText(key, C, by - (text.descent() + text.ascent()) / 2f + 0.5f, text);
        }

        // 5. The glyph, centred.
        if (glyph != null) {
            final float size = 0.46f * D;
            c.save();
            c.translate(C - size / 2f, C + mainR * 0.04f - size / 2f);
            final float k = size / TouchGlyphs.BOX;
            c.scale(k, k);
            shade(fill, down ? p.glyphHeld : p.glyph, 1f, down);
            c.drawPath(glyph, fill);
            c.restore();
        }

        c.restore();
    }


    /**
     * Classic: a round button with its letter, a small caption under it (null for none) and, for
     * the face buttons, a seal dot on the side facing out of the diamond (`seal`: 0 none, 1 right,
     * 2 bottom, 3 top, 4 left).
     */
    void classic(Canvas c, int bit, String letter, String caption, float cx, float cy, float r,
            boolean down, boolean hot, int seal) {
        final Palette p = paletteOf(bit);
        enter(c, cx, cy, r / R, down);
        disc(c, p, down, hot);
        tint(line, p.ringHi, 0.28f, down);
        line.setStrokeWidth(0.8f);
        c.drawCircle(40f, 40f, 0.7f * R, line);
        letter(c, p, letter, caption != null ? 37f : 40f, caption != null ? 0.46f * D : 0.5f * D, down);
        if (caption != null) caption(c, p, caption, 0.11f * D, 0.12f, down);
        if (seal != 0) {
            final float dot = 0.035f * D, edge = 2f + 0.085f * D + dot;
            final float sx = seal == 1 ? 80f - edge : seal == 4 ? edge : 40f;
            final float sy = seal == 2 ? 80f - 2f - 0.05f * D - dot : seal == 3 ? edge : 40f;
            tint(fill, p.ringHi, 1f, down);
            c.drawCircle(sx, sy, dot, fill);
        }
        c.restore();
    }

    /** Classic shoulder: a pill `hw` x `hh` (half sizes) on screen with its letters and caption. */
    void classicPill(Canvas c, int bit, String letter, String caption, float cx, float cy, float hw, float hh,
            boolean down, boolean hot) {
        final Palette p = paletteOf(bit);
        enter(c, cx, cy, hh / R, down);
        final float half = R * hw / hh;
        cell.set(40f - half, 2f, 40f + half, 78f);
        shade(fill, p.base, 1f, down);
        c.drawRoundRect(cell, R, R, fill);
        if (hot || down) {
            shade(fill, p.hot, down ? 1.5f : 1f, down);
            c.drawRoundRect(cell, R, R, fill);
        }
        shade(fill, gloss, 1f, down);
        c.drawRoundRect(cell, R, R, fill);
        tint(line, down ? 0xFFFFFF : p.ring, 1f, down);
        line.setStrokeWidth(2.3f);
        cell.inset(1.15f, 1.15f);
        c.drawRoundRect(cell, R, R, line);
        tint(line, p.ringLo, 1f, down);
        line.setStrokeWidth(1.7f);
        cell.inset(2f, 2f);
        c.drawRoundRect(cell, R, R, line);
        tint(line, p.ringHi, 0.28f, down);
        line.setStrokeWidth(0.8f);
        cell.inset(8f, 8f);
        c.drawRoundRect(cell, R, R, line);
        letter(c, p, letter, caption != null ? 37f : 40f, 0.42f * D, down);
        if (caption != null) caption(c, p, caption, 0.11f * D, 0.12f, down);
        c.restore();
    }

    /** Classic −, + (a letter), Home (an outline) and the gear (its glyph), without captions. */
    void classicSystem(Canvas c, int bit, String letter, Path glyph, float cx, float cy, float r, boolean down) {
        final Palette p = paletteOf(bit);
        enter(c, cx, cy, r / R, down);
        disc(c, p, down, false);
        if (bit == TouchControlsView.HOME) {
            c.save();
            c.translate(40f - 0.26f * D, 40f - 0.26f * D);
            c.scale(0.52f * D / 24f, 0.52f * D / 24f);
            line.setShader(null);
            line.setColor(0xFFFFFFFF);
            line.setShader(p.letterSmall);
            line.setAlpha(Math.round(255 * level(down)));
            line.setStrokeWidth(2.2f);
            c.drawPath(HOUSE, line);
            line.setShader(null);
            c.restore();
        } else if (glyph != null) {
            final float size = 0.52f * D, k = size / TouchGlyphs.BOX;
            c.save();
            c.translate(40f - size / 2f, 40f - size / 2f);
            c.scale(k, k);
            shade(fill, down ? p.glyphHeld : p.glyph, 1f, down);
            c.drawPath(glyph, fill);
            c.restore();
        } else {
            letter(c, p, letter, 40f, 0.56f * D, down);
        }
        c.restore();
    }

    private void letter(Canvas c, Palette p, String letter, float centreY, float size, boolean down) {
        text.setTextSize(size);
        final float y = centreY - (text.descent() + text.ascent()) / 2f;
        tint(text, 0, 0.75f, down);
        c.drawText(letter, 40f, y + 1.8f, text);
        if (down) tint(text, 0xFFFFFF, 1f, true);
        else shade(text, p.letter, 1f, false);
        c.drawText(letter, 40f, y, text);
        text.setShader(null);
    }

    /** A stick: a translucent well with one thin ring, and a ringed knob moved by vx, vy (-1..1). */
    void stick(Canvas c, float cx, float cy, float r, float vx, float vy, boolean active) {
        enter(c, cx, cy, r / WELL, false);
        final Palette p = chrome();
        shade(fill, wellShade, 1f, active);
        c.drawCircle(40f, 40f, WELL, fill);
        tint(line, active ? p.ringHi : p.ring, active ? 1.0f : 0.85f, active);
        line.setStrokeWidth(active ? 2.5f : 1.8f);
        c.drawCircle(40f, 40f, WELL - 1.2f, line);

        final float travel = WELL - KNOB;
        c.translate(40f + vx * travel, 40f + vy * travel);
        if (active) {
            shade(fill, p.knobGlowHeld, 1f, true);
            c.drawCircle(0f, 0f, 2.4f * KNOB, fill);
        }
        shade(fill, p.base, active ? 0.85f : 0.7f, active);
        c.drawCircle(0f, 0f, KNOB, fill);
        tint(line, active ? 0xFFFFFF : p.ring, 1f, active);
        line.setStrokeWidth(active ? 2.6f : 2.0f);
        c.drawCircle(0f, 0f, KNOB - 1f, line);
        tint(fill, active ? 0xFFFFFF : p.ringHi, active ? 1.0f : 0.85f, active);
        c.drawCircle(0f, 0f, 3.2f, fill);

        c.restore();
    }

    /** Modern D-pad: four separate round keys with a chevron, `r` = half its width on screen. */
    void pad(Canvas c, float cx, float cy, float r, int heldBits) {
        enter(c, cx, cy, r / R, false);
        final Palette p = chrome();

        final float C = 40f;
        final float dist = 24.5f;
        final float btnR = 11.5f;

        final int[] bits = {TouchControlsView.UP, TouchControlsView.DOWN, TouchControlsView.LEFT, TouchControlsView.RIGHT};
        final float[][] dirs = {{0f, -1f}, {0f, 1f}, {-1f, 0f}, {1f, 0f}};

        for (int i = 0; i < 4; i++) {
            final float bx = C + dirs[i][0] * dist;
            final float by = C + dirs[i][1] * dist;
            final boolean lit = (heldBits & bits[i]) != 0;

            tint(fill, p.fillB, lit ? 0.75f : 0.55f, lit);
            c.drawCircle(bx, by, btnR, fill);
            if (lit) {
                tint(fill, p.ringHi, 0.3f, true);
                c.drawCircle(bx, by, btnR - 0.7f, fill);
            }
            tint(line, lit ? 0xFFFFFF : p.ring, lit ? 1.0f : 0.85f, lit);
            line.setStrokeWidth(lit ? 1.8f : 1.3f);
            c.drawCircle(bx, by, btnR - 0.7f, line);
            drawChevron(c, p, bx, by, dirs[i][0], dirs[i][1], lit);
        }

        c.restore();
    }

    private void drawChevron(Canvas c, Palette p, float cx, float cy, float dx, float dy, boolean lit) {
        tint(line, lit ? 0xFFFFFF : p.ic2, 1f, lit);
        line.setStrokeWidth(lit ? 1.9f : 1.5f);
        tri.reset();
        final float t = 4.6f;
        final float tx = cx + dx * t, ty = cy + dy * t;
        final float b1x = cx - dx * t * 0.4f - dy * t, b1y = cy - dy * t * 0.4f + dx * t;
        final float b2x = cx - dx * t * 0.4f + dy * t, b2y = cy - dy * t * 0.4f - dx * t;
        tri.moveTo(b1x, b1y);
        tri.lineTo(tx, ty);
        tri.lineTo(b2x, b2y);
        c.drawPath(tri, line);
    }

    /** Classic D-pad: one cross, the held arms lit, chevrons and a centre dot. */
    void classicPad(Canvas c, float cx, float cy, float r, int heldBits) {
        final Palette p = chrome();
        enter(c, cx, cy, r / 34f, false);
        shade(fill, p.base, 1f, false);
        c.drawPath(CROSS, fill);
        final boolean up = (heldBits & TouchControlsView.UP) != 0, down = (heldBits & TouchControlsView.DOWN) != 0;
        final boolean left = (heldBits & TouchControlsView.LEFT) != 0;
        final boolean right = (heldBits & TouchControlsView.RIGHT) != 0;
        tint(fill, 0xFFFFFF, 0.22f, true);
        if (up) c.drawPath(ARM_UP, fill);
        if (down) c.drawPath(ARM_DOWN, fill);
        if (left) c.drawPath(ARM_LEFT, fill);
        if (right) c.drawPath(ARM_RIGHT, fill);
        tint(line, p.ring, 1f, false);
        line.setStrokeWidth(2f);
        c.drawPath(CROSS, line);
        c.save();
        c.translate(40f, 40f);
        c.scale(0.9f, 0.9f);
        c.translate(-40f, -40f);
        tint(line, p.ringHi, 0.35f, false);
        line.setStrokeWidth(0.7f);
        c.drawPath(CROSS, line);
        c.restore();
        chevron(c, p, CHEV_UP, up);
        chevron(c, p, CHEV_DOWN, down);
        chevron(c, p, CHEV_LEFT, left);
        chevron(c, p, CHEV_RIGHT, right);
        tint(fill, p.ringHi, 1f, false);
        c.drawCircle(40f, 40f, 3f, fill);
        c.restore();
    }

    private void chevron(Canvas c, Palette p, Path path, boolean lit) {
        tint(line, lit ? 0xFFFFFF : p.ic2, 1f, lit);
        line.setStrokeWidth(lit ? 2.8f : 2.4f);
        c.drawPath(path, line);
    }

    /** SVG path data, absolute commands only (M L H V C Q A Z), as used by TouchGlyphs. */
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

    /** SVG endpoint arc to Path.arcTo (circles and unrotated ellipses). */
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
