package dev.recompbootstrap.smoke;

/** 2026-11-17: draws a setup page in the Enhanced frame the way this edition's setup screens do (one 16:9 panel). */
final class SetupFrame {
    private SetupFrame() { }

    static void show(UiKit.SceneView root, MenuScenes.Page page) {
        root.show(EnhancedScenes.build(page, root));
    }
}
