package com.shiinobu.basara;

import android.app.Activity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Settings > Controls > Gamepad Buttons: which gamepad button presses each Classic Controller button.
 * Saved as "a=b,b=a,..." (AppSettings.gamepadMap, Config.toml [input] gamepad_map), read by the game
 * in VirtualRemote::SetGamepadMap: the Classic button, then the gamepad button by its SDL name. ""
 * is the default layout, by position as in Dolphin.
 */
final class GamepadButtons {
    /** Classic buttons in the list's order: config key, letter, what it does in the game. */
    private static final String[][] CLASSIC = {
        {"a", "A", "SPECIAL"}, {"b", "B", "JUMP"}, {"x", "X", "SKILL"}, {"y", "Y", "ATTACK"},
        {"l", "L", "SWITCH"}, {"r", "R", "GUARD"}, {"zl", "ZL", "BASARA"}, {"zr", "ZR", "LOCK"},
        {"plus", "+", null}, {"minus", "−", null}, {"home", "HOME", null},
        {"up", "D-pad Up", null}, {"down", "D-pad Down", null}, {"left", "D-pad Left", null},
        {"right", "D-pad Right", null},
    };

    /** The default: Classic A on the right face button, B bottom, X top, Y left (SDL: a bottom, b right). */
    static final String DEFAULT = "a=b,b=a,x=y,y=x,l=lefttrigger,r=righttrigger,zl=leftshoulder,"
            + "zr=rightshoulder,plus=start,minus=back,home=guide,up=dpup,down=dpdown,left=dpleft,right=dpright";

    private final Activity activity;
    private final AppSettings settings;
    private final Runnable changed;

    GamepadButtons(Activity activity, AppSettings settings, Runnable changed) {
        this.activity = activity;
        this.settings = settings;
        this.changed = changed;
    }

    /** One line for the settings card. */
    static String summary(AppSettings settings) {
        return settings.gamepadMap().isEmpty()
                ? Tr.t("Default: by position, like a Wii Classic Controller (A right, B bottom, X top, Y left).")
                : Tr.t("Set by you.");
    }

    /** The list of Classic buttons; tapping one asks for its gamepad button. */
    void show() {
        final Map<String, String> map = parse(settings.gamepadMap());
        final Sheet sheet = new Sheet(activity, Tr.t("Gamepad Buttons"),
                Tr.t("Tap a button, then press the gamepad button you want for it."));
        for (String[] classic : CLASSIC) {
            final String source = map.get(classic[0]);
            sheet.action(title(classic), source == null ? Tr.t("Not on the gamepad") : sourceName(source),
                    false, false, () -> ask(classic));
        }
        sheet.action(Tr.t("Reset to Default"), null, false, true, () -> {
            settings.setGamepadMap("");
            changed.run();
            show();
        });
        sheet.action(Tr.t("Done"), null, true, false, null);
        sheet.show();
    }

    private void ask(String[] classic) {
        final Sheet sheet = new Sheet(activity, Tr.t("Press a gamepad button"), title(classic));
        final boolean[] done = {false};
        final java.util.function.Consumer<String> pick = source -> {
            if (done[0]) return;
            done[0] = true;
            assign(classic[0], source);
            sheet.dismiss();
        };
        sheet.onGamepad((dialog, keyCode, event) -> {
            if (!fromGamepad(event.getSource()) && !fromGamepad(deviceSources(event.getDevice()))) return false;
            final String source = keySource(keyCode);
            if (source == null) return false;
            if (event.getAction() == KeyEvent.ACTION_DOWN) pick.accept(source);
            return true;
        }, event -> {
            if ((!fromGamepad(event.getSource()) && !fromGamepad(deviceSources(event.getDevice())))
                    || event.getAction() != MotionEvent.ACTION_MOVE) return false;
            final String source = motionSource(event);
            if (source != null) pick.accept(source);
            return true;
        });
        sheet.action(Tr.t("Not on the gamepad"), null, false, false, () -> assign(classic[0], null));
        sheet.action(Tr.t("Cancel"), null, false, false, null);
        sheet.onDismiss(this::show);
        sheet.show();
    }

    /** Gives `key` the gamepad button `source` (null: none); a Classic button that had it gets this one's old one. */
    private void assign(String key, String source) {
        final Map<String, String> map = parse(settings.gamepadMap());
        final String old = map.get(key);
        if (source != null) {
            for (Map.Entry<String, String> entry : map.entrySet()) {
                if (!entry.getKey().equals(key) && source.equals(entry.getValue())) {
                    entry.setValue(old);
                }
            }
        }
        map.put(key, source);
        final StringBuilder text = new StringBuilder();
        for (String[] classic : CLASSIC) {
            final String value = map.get(classic[0]);
            if (value == null) continue;
            if (text.length() > 0) text.append(',');
            text.append(classic[0]).append('=').append(value);
        }
        // An empty string means the default, so a gamepad with no buttons at all keeps one marker.
        settings.setGamepadMap(text.length() == 0 ? "none=none" : text.toString());
        changed.run();
    }

    private static Map<String, String> parse(String text) {
        final Map<String, String> map = new LinkedHashMap<>();
        for (String entry : (text.isEmpty() ? DEFAULT : text).split(",")) {
            final int eq = entry.indexOf('=');
            if (eq > 0) map.put(entry.substring(0, eq).trim(), entry.substring(eq + 1).trim());
        }
        return map;
    }

    private static String title(String[] classic) {
        return classic[2] == null ? classic[1] : classic[1] + "  ·  " + classic[2];
    }

    /** Cheap USB pads are joysticks whose key events arrive with the keyboard source (as SDLActivity notes). */
    private static int deviceSources(InputDevice device) {
        return device == null ? 0 : device.getSources();
    }

    private static boolean fromGamepad(int source) {
        return (source & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    /** Android's gamepad key codes as SDL names them (SDL's Android joystick code maps them the same way). */
    private static String keySource(int keyCode) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_BUTTON_A: return "a";
            case KeyEvent.KEYCODE_BUTTON_B: return "b";
            case KeyEvent.KEYCODE_BUTTON_X: return "x";
            case KeyEvent.KEYCODE_BUTTON_Y: return "y";
            case KeyEvent.KEYCODE_BUTTON_L1: return "leftshoulder";
            case KeyEvent.KEYCODE_BUTTON_R1: return "rightshoulder";
            case KeyEvent.KEYCODE_BUTTON_L2: return "lefttrigger";
            case KeyEvent.KEYCODE_BUTTON_R2: return "righttrigger";
            case KeyEvent.KEYCODE_BUTTON_THUMBL: return "leftstick";
            case KeyEvent.KEYCODE_BUTTON_THUMBR: return "rightstick";
            case KeyEvent.KEYCODE_BUTTON_START: return "start";
            case KeyEvent.KEYCODE_BUTTON_SELECT:
            case KeyEvent.KEYCODE_BACK: return "back";
            case KeyEvent.KEYCODE_BUTTON_MODE: return "guide";
            case KeyEvent.KEYCODE_DPAD_UP: return "dpup";
            case KeyEvent.KEYCODE_DPAD_DOWN: return "dpdown";
            case KeyEvent.KEYCODE_DPAD_LEFT: return "dpleft";
            case KeyEvent.KEYCODE_DPAD_RIGHT: return "dpright";
            default:
                // Generic HID pads report BUTTON_1..16; SDL numbers them as joystick buttons 20..35.
                if (keyCode >= KeyEvent.KEYCODE_BUTTON_1 && keyCode <= KeyEvent.KEYCODE_BUTTON_16) {
                    return "joy" + (20 + keyCode - KeyEvent.KEYCODE_BUTTON_1);
                }
                return null;
        }
    }

    /** Triggers that only report as an axis, and a D-pad that reports as a hat. */
    private static String motionSource(MotionEvent event) {
        if (Math.max(event.getAxisValue(MotionEvent.AXIS_LTRIGGER), event.getAxisValue(MotionEvent.AXIS_BRAKE)) > 0.6f) {
            return "lefttrigger";
        }
        if (Math.max(event.getAxisValue(MotionEvent.AXIS_RTRIGGER), event.getAxisValue(MotionEvent.AXIS_GAS)) > 0.6f) {
            return "righttrigger";
        }
        final float hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X);
        final float hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y);
        if (hatY < -0.5f) return "dpup";
        if (hatY > 0.5f) return "dpdown";
        if (hatX < -0.5f) return "dpleft";
        if (hatX > 0.5f) return "dpright";
        return null;
    }

    private static String sourceName(String source) {
        switch (source) {
            case "a": return "A / Cross (bottom)";
            case "b": return "B / Circle (right)";
            case "x": return "X / Square (left)";
            case "y": return "Y / Triangle (top)";
            case "leftshoulder": return "LB / L1";
            case "rightshoulder": return "RB / R1";
            case "lefttrigger": return "LT / L2";
            case "righttrigger": return "RT / R2";
            case "leftstick": return "L3 (left stick press)";
            case "rightstick": return "R3 (right stick press)";
            case "start": return "Start";
            case "back": return "Select / Back";
            case "guide": return "Home / Guide";
            case "dpup": return "D-pad Up";
            case "dpdown": return "D-pad Down";
            case "dpleft": return "D-pad Left";
            case "dpright": return "D-pad Right";
            default:
                if (source.startsWith("joy")) {
                    try {
                        return Tr.t("Button") + " " + (Integer.parseInt(source.substring(3)) - 19);
                    } catch (NumberFormatException ignored) {
                    }
                }
                return source;
        }
    }
}
