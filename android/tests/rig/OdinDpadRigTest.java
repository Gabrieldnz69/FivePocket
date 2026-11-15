package dev.recompbootstrap.smoke;

import static dev.recompbootstrap.smoke.Rig.*;

import java.io.File;

/**
 * One D-pad RIGHT press on the AYN Thor's built-in "Odin Controller" (trainer-mods 2026-11-09, owner bug "D-pad right at
 * 'Press D-pad right to browse glasses' does nothing"; lead 19:4x). The controller is one input device (device 14 on the
 * owner's Thor, /dev/input/event9) that reports the D-pad twice for every press: as the key BTN_DPAD_RIGHT (Android
 * KEYCODE_DPAD_RIGHT, 22) and as the hat axis ABS_HAT0X (+1, then 0). Both reach PrivateGamepadInput (dispatchKeyEvent /
 * dispatchGenericMotionEvent). What the game must receive is exactly one press and one release of the Xbox D-pad RIGHT bit,
 * whichever of the two sources arrives first or lets go first: a second press would select twice in a game menu, and an
 * early release from one source while the other still holds would cut the press short.
 *
 * Run by the window rig like GameWindowRigTest (the android.* stand-ins of fake/ first on the class path).
 */
public final class OdinDpadRigTest {
    private static final int ODIN = 14, DPAD_RIGHT = 22;

    private static Win odin() {
        pad(ODIN, "Odin Controller");
        return new Win(false, false);
    }

    public static void main(String[] args) throws Exception {
        scratch = new File(args.length > 0 ? args[0] : System.getProperty("java.io.tmpdir"), "odin-dpad-rig");
        check("key first, hat second; key released first, hat last: one RIGHT press from the key's down to the hat's release", () -> {
            Win w = odin();
            w.down(100, DPAD_RIGHT, ODIN);
            w.moveAt(104, ODIN, HATX, 1f);
            w.up(300, DPAD_RIGHT, ODIN);
            w.moveAt(303, ODIN, HATX, 0f);
            checksim.Sim.runUntil(1000);
            same("RG+@100 RG-@303", edges(), "what the game was given");
        });
        check("hat first, key second; hat released first, key last: one RIGHT press from the hat's +1 to the key's release", () -> {
            Win w = odin();
            w.moveAt(100, ODIN, HATX, 1f);
            w.down(104, DPAD_RIGHT, ODIN);
            w.moveAt(300, ODIN, HATX, 0f);
            w.up(303, DPAD_RIGHT, ODIN);
            checksim.Sim.runUntil(1000);
            same("RG+@100 RG-@303", edges(), "what the game was given");
        });
        check("the key's whole press inside the hat's: still one RIGHT press, released with the hat", () -> {
            Win w = odin();
            w.moveAt(100, ODIN, HATX, 1f);
            w.down(110, DPAD_RIGHT, ODIN);
            w.up(200, DPAD_RIGHT, ODIN);
            w.moveAt(300, ODIN, HATX, 0f);
            checksim.Sim.runUntil(1000);
            same("RG+@100 RG-@300", edges(), "what the game was given");
        });
        System.out.println(passed + " passed, " + failed + " failed" + (failed == 0 ? "" : ": " + failures));
        if (failed != 0) System.exit(1);
    }
}
