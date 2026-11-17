package dev.recompbootstrap.smoke;

/**
 * Update 1 Online mode (lead 2026-11-08, from device run t-onlleave86-20261108-0328): when the Online leave closes the game
 * window (state bit 26), the player comes back to this app's menu on the Online tile, not to Android's home screen.
 *
 * The game window starts the menu activity with EXTRA = ONLINE and FLAGS. These flags bring the menu's existing task to
 * the front (a new task only when there is none, for example after a start by a measurement script). The menu takes the
 * word and puts the Play tab's highlight on Online the next time it draws a column that has Online. Pure Java: the
 * rule is host-tested.
 */
final class LaunchReturn {
    private LaunchReturn() {}

    /** The intent extra the game window sets, and its one value. */
    static final String EXTRA = "return_to", ONLINE = "online";
    /** Intent.FLAG_ACTIVITY_NEW_TASK | FLAG_ACTIVITY_CLEAR_TOP | FLAG_ACTIVITY_SINGLE_TOP: the existing menu comes forward
     *  with its state (onNewIntent); there is never a second menu above the game window. */
    static final int FLAGS = 0x10000000 | 0x04000000 | 0x20000000;

    /**
     * Where the Play tab's highlight goes for a return word. The index of the Online tile in playOrder for ONLINE;
     * -1 when the word is not ONLINE (null, unknown) or the column has no Online tile (yet). The caller keeps the word
     * until a column with Online is drawn.
     */
    static int column(int[] playOrder, String returnTo) {
        if (!ONLINE.equals(returnTo) || playOrder == null) return -1;
        for (int i = 0; i < playOrder.length; i++) if (playOrder[i] == LauncherMenu.ONLINE) return i;
        return -1;
    }
}
