package dev.recompbootstrap.smoke;

/**
 * A controller's touchpad on the game window (upd1-wave3 android-dualsense, 2026-11-06). Pure Java, host-tested
 * (tests/DualSenseInputTest.java); PrivateTraceActivity.dispatchTouchEvent asks it for every touch event.
 *
 * A DualSense (and a DualShock 4) gives Android its touchpad as an input device of its own, beside the pad, with the controller's vendor
 * number (Sony, 0x054C). Android treats such a touchpad as a pointing device: a pointer appears, and a click or a tap on the touchpad
 * reaches the window under the pointer as a touch (source mouse / touchpad, not touchscreen). On the game window that would be a finger
 * on the on-screen controls, the long-press gestures (map, wheels) or the side menu's edge, wherever the pointer happens to be.
 * (INFERRED from Android's input stack; NEEDS DEVICE CHECK on the owner's Fold: local-analysis/.agents/upd1-wave3/android-dualsense/DEVICE-CHECK.txt)
 *
 * The rule: a touch event that is not from a touchscreen and comes from a Sony device is dropped by the game window. Touchscreen events
 * are never dropped, whatever the device (a stylus is a touchscreen source too); a mouse of any other maker keeps today's behaviour.
 * The touchpad's click is not mapped to a game button: Android cannot tell its physical click from a tap, and the DualSense's Create
 * button already is the 360 Back (KEYCODE_BUTTON_SELECT).
 */
final class PadTouchpad {
    private PadTouchpad() { }

    /** android.view.InputDevice.SOURCE_TOUCHSCREEN (class pointer | 0x1000), a platform constant. */
    static final int SOURCE_TOUCHSCREEN = 0x00001002;
    /** The USB / Bluetooth vendor number of Sony Interactive Entertainment's controllers (DualShock 4, DualSense, DualSense Edge). */
    static final int SONY_VENDOR = 0x054C;

    /** Whether the game window drops a touch event of this source from a device with this vendor number. */
    static boolean ignore(int source, int vendorId) {
        final boolean touchscreen = (source & SOURCE_TOUCHSCREEN) == SOURCE_TOUCHSCREEN;
        return !touchscreen && vendorId == SONY_VENDOR;
    }
}
