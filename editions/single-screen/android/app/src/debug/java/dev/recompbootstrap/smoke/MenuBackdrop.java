package dev.recompbootstrap.smoke;

import android.graphics.Bitmap;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

/**
 * menu-redesign (2026-10-25, DESIGN.md 6; the same file in the Thor and the one-screen trees): the backdrop of the game's pause menu -
 * the game's own picture, reduced, blurred and a little greyed - made from one PixelCopy when the in-game menu opens (SideMenuView). It
 * is saved as files/settings/menu-backdrop.png, the file the launcher draws behind its menu (UiKit.loadBackdrop), so the launcher shows
 * the player's last scene. Nothing of it is in the APK. blur() is pure Java (tests/SideMenuTest).
 */
final class MenuBackdrop {
    private MenuBackdrop() { }

    /** The copy's size: a 16:9 picture small enough that the blur costs well under a millisecond. */
    static final int W = 320, H = 180;
    /** The blur's radius in the copy's pixels (DESIGN.md: 1.4 % of the height), and how much colour is kept (75 %). */
    static final int RADIUS = 3;
    static final float SATURATION = 0.75f;
    static final String FILE = "settings/menu-backdrop.png";

    /**
     * Three box blurs across and down (close to a gaussian), then the colour pulled a quarter of the way to grey. The picture's edges
     * repeat their last pixel. Alpha is set opaque.
     */
    static int[] blur(int[] argb, int w, int h, int radius) {
        int[] a = argb.clone(), b = new int[a.length];
        for (int pass = 0; pass < 3; ++pass) {
            box(a, b, w, h, radius, true);
            box(b, a, w, h, radius, false);
        }
        for (int i = 0; i < a.length; ++i) {
            int r = a[i] >> 16 & 255, g = a[i] >> 8 & 255, bl = a[i] & 255;
            float grey = 0.299f * r + 0.587f * g + 0.114f * bl;
            r = Math.round(grey + (r - grey) * SATURATION);
            g = Math.round(grey + (g - grey) * SATURATION);
            bl = Math.round(grey + (bl - grey) * SATURATION);
            a[i] = 0xFF000000 | r << 16 | g << 8 | bl;
        }
        return a;
    }

    private static void box(int[] in, int[] out, int w, int h, int radius, boolean across) {
        int lines = across ? h : w, length = across ? w : h, span = 2 * radius + 1;
        for (int line = 0; line < lines; ++line) {
            int sr = 0, sg = 0, sb = 0;
            for (int k = -radius; k <= radius; ++k) {
                int p = in[index(line, Math.max(0, Math.min(length - 1, k)), w, across)];
                sr += p >> 16 & 255;
                sg += p >> 8 & 255;
                sb += p & 255;
            }
            for (int x = 0; x < length; ++x) {
                out[index(line, x, w, across)] = 0xFF000000 | (sr / span) << 16 | (sg / span) << 8 | (sb / span);
                int leave = in[index(line, Math.max(0, x - radius), w, across)], enter = in[index(line, Math.min(length - 1, x + radius + 1), w, across)];
                sr += (enter >> 16 & 255) - (leave >> 16 & 255);
                sg += (enter >> 8 & 255) - (leave >> 8 & 255);
                sb += (enter & 255) - (leave & 255);
            }
        }
    }

    private static int index(int line, int x, int w, boolean across) {
        return across ? line * w + x : x * w + line;
    }

    /** The copy, blurred, as a new bitmap. */
    static Bitmap blurred(Bitmap copy) {
        int w = copy.getWidth(), h = copy.getHeight();
        int[] px = new int[w * h];
        copy.getPixels(px, 0, w, 0, 0, w, h);
        return Bitmap.createBitmap(blur(px, w, h, RADIUS), w, h, Bitmap.Config.ARGB_8888);
    }

    /** Writes the picture for the launcher, off the UI thread, through a temporary file (a reader never sees half a picture). */
    static void save(File filesDir, Bitmap picture) {
        new Thread(() -> {
            File target = new File(filesDir, FILE), temp = new File(filesDir, FILE + ".tmp");
            File dir = target.getParentFile();
            if (dir != null && !dir.isDirectory() && !dir.mkdirs()) return;
            try (FileOutputStream out = new FileOutputStream(temp)) {
                if (!picture.compress(Bitmap.CompressFormat.PNG, 100, out)) return;
            } catch (IOException | RuntimeException failed) {
                return;
            }
            if (!temp.renameTo(target)) temp.delete();
        }, "menu-backdrop").start();
    }
}
