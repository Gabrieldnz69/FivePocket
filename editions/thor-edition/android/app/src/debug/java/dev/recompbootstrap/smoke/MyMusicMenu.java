package dev.recompbootstrap.smoke;

/**
 * My Music (Update 1 P6): what the side menu's My Music page needs from the player (MyMusicPlayer on the device, a fake on the host).
 * Every call returns at once: the player does its work on its own thread.
 */
interface MyMusicMenu {
    String ID_NEXT = "mm:next", ID_PREVIOUS = "mm:prev", ID_RESCAN = "mm:rescan";

    /** The page's info line: what the music is doing and why (MyMusicRules.text). */
    String info();

    /** Next Track, Previous Track or Look For New Music (the ids above). */
    void action(String id);

    /** The rows were saved: the player takes the new mode, volume and order. */
    void settings(Settings saved);

    /** Playable tracks found in the music folder. */
    int tracks();
}
