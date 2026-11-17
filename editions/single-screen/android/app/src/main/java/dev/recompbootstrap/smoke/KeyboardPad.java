package dev.recompbootstrap.smoke;

import android.view.KeyEvent;

/**
 * Discord item 10 (2026-11-16): a physical keyboard plays as an Xbox 360 pad (a mouse already works: Android's pointer is a finger).
 * A fixed layout, the PC game's key where the 360 button does the same thing on foot:
 *   W A S D left stick   arrows right stick   Shift / Enter A   Backspace / R B   Space X   F Y
 *   Tab LB   Q RB   Z LT   X RT   Ctrl L3   C R3   V Back   Esc / M Start   I J K L D-pad
 * Only a real, alphabetic keyboard counts (never the on-screen keyboard: Android lists it as virtual); a pad's own keys stay the pad's.
 * The game gets the keyboard merged into the controller's state (PrivateGamepadInput.publish): buttons OR'd, the larger trigger, and a
 * keyboard stick only where the controller's stick rests.
 */
final class KeyboardPad {
    private int keys; // held layout keys, one bit each (BIT_*)
    private static final int W=1,A=2,S=4,D=8,UP=16,LEFT=32,DOWN=64,RIGHT=128,LT=256,RT=512;
    private int buttons;
    private boolean used;

    /** A device that types: a real (not virtual) alphabetic keyboard that is not a game controller. */
    static boolean keyboard(boolean virtual,boolean alphabetic,boolean controller) {return !virtual && alphabetic && !controller;}

    /** The 360 button of a layout key (XINPUT_GAMEPAD bits), 0 when the key is a stick/trigger key or not in the layout. */
    static int button(int code) {
        switch(code) {
            case KeyEvent.KEYCODE_SHIFT_LEFT: case KeyEvent.KEYCODE_SHIFT_RIGHT: case KeyEvent.KEYCODE_ENTER: case KeyEvent.KEYCODE_NUMPAD_ENTER: return 0x1000;
            case KeyEvent.KEYCODE_DEL: case KeyEvent.KEYCODE_R: return 0x2000;
            case KeyEvent.KEYCODE_SPACE: return 0x4000;
            case KeyEvent.KEYCODE_F: return 0x8000;
            case KeyEvent.KEYCODE_TAB: return 0x100;
            case KeyEvent.KEYCODE_Q: return 0x200;
            case KeyEvent.KEYCODE_CTRL_LEFT: case KeyEvent.KEYCODE_CTRL_RIGHT: return 0x40;
            case KeyEvent.KEYCODE_C: return 0x80;
            case KeyEvent.KEYCODE_V: return 0x20;
            case KeyEvent.KEYCODE_ESCAPE: case KeyEvent.KEYCODE_M: return 0x10;
            case KeyEvent.KEYCODE_I: return 1;
            case KeyEvent.KEYCODE_K: return 2;
            case KeyEvent.KEYCODE_J: return 4;
            case KeyEvent.KEYCODE_L: return 8;
            default: return 0;
        }
    }
    private static int axisKey(int code) {
        switch(code) {
            case KeyEvent.KEYCODE_W: return W; case KeyEvent.KEYCODE_A: return A; case KeyEvent.KEYCODE_S: return S; case KeyEvent.KEYCODE_D: return D;
            case KeyEvent.KEYCODE_DPAD_UP: return UP; case KeyEvent.KEYCODE_DPAD_LEFT: return LEFT;
            case KeyEvent.KEYCODE_DPAD_DOWN: return DOWN; case KeyEvent.KEYCODE_DPAD_RIGHT: return RIGHT;
            case KeyEvent.KEYCODE_Z: return LT; case KeyEvent.KEYCODE_X: return RT;
            default: return 0;
        }
    }
    /** A key went down or up; false when it is not in the layout (the key is left to Android). Two keys on one button: either holds it. */
    boolean key(int code,boolean down) {
        final int axis=axisKey(code);
        if(axis!=0){keys=down?keys|axis:keys&~axis;used=true;return true;}
        final int mask=button(code);
        if(mask==0)return false;
        held[slot(code)]=down;
        int b=0;for(int i=0;i<held.length;++i)if(held[i])b|=button(CODES[i]);
        buttons=b;used=true;return true;
    }
    private static final int[] CODES={KeyEvent.KEYCODE_SHIFT_LEFT,KeyEvent.KEYCODE_SHIFT_RIGHT,KeyEvent.KEYCODE_ENTER,KeyEvent.KEYCODE_NUMPAD_ENTER,
            KeyEvent.KEYCODE_DEL,KeyEvent.KEYCODE_R,KeyEvent.KEYCODE_SPACE,KeyEvent.KEYCODE_F,KeyEvent.KEYCODE_TAB,KeyEvent.KEYCODE_Q,
            KeyEvent.KEYCODE_CTRL_LEFT,KeyEvent.KEYCODE_CTRL_RIGHT,KeyEvent.KEYCODE_C,KeyEvent.KEYCODE_V,KeyEvent.KEYCODE_ESCAPE,KeyEvent.KEYCODE_M,
            KeyEvent.KEYCODE_I,KeyEvent.KEYCODE_K,KeyEvent.KEYCODE_J,KeyEvent.KEYCODE_L};
    private final boolean[] held=new boolean[CODES.length];
    private static int slot(int code){for(int i=0;i<CODES.length;++i)if(CODES[i]==code)return i;return -1;}

    void clear() {keys=0;buttons=0;java.util.Arrays.fill(held,false);}
    /** A layout key was pressed since the pad was made (the keyboard is then the game's controller even with no pad connected). */
    boolean used() {return used;}
    int buttons() {return buttons;}
    int leftTrigger() {return (keys&LT)!=0?255:0;}
    int rightTrigger() {return (keys&RT)!=0?255:0;}
    /** {x, y} in XInput units (y up positive); a diagonal is on the stick's circle, not its corner. */
    private int[] stick(int up,int left,int down,int right) {
        final int x=((keys&right)!=0?1:0)-((keys&left)!=0?1:0), y=((keys&up)!=0?1:0)-((keys&down)!=0?1:0);
        final int full=(x!=0 && y!=0)?23170:32767; // 32767 / sqrt(2)
        return new int[]{x*full,y*full};
    }
    int[] left() {return stick(W,A,S,D);}
    int[] right() {return stick(UP,LEFT,DOWN,RIGHT);}

    /** The controller's state {buttons, LT, RT, LX, LY, RX, RY} with this keyboard merged in (see the class comment). */
    int[] merge(int[] pad) {
        final int[] l=left(), r=right();
        final boolean padLeft=pad[3]!=0 || pad[4]!=0, padRight=pad[5]!=0 || pad[6]!=0;
        return new int[]{pad[0]|buttons,Math.max(pad[1],leftTrigger()),Math.max(pad[2],rightTrigger()),
                padLeft?pad[3]:l[0],padLeft?pad[4]:l[1],padRight?pad[5]:r[0],padRight?pad[6]:r[1]};
    }
}
