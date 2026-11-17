package dev.recompbootstrap.smoke;

/** 2026-11-17: draws a setup page in the Enhanced frame the way this edition's setup screens do (the panel's frame, touch scale on a touch screen). */
final class SetupFrame {
    private SetupFrame() { }

    static void show(UiKit.SceneView root, MenuScenes.Page page) {
        page.touchIcons = root.touchIcons();
        MenuLayout.Frame sf = root.frame();
        float sk = page.touchIcons ? EnhancedScenes.touchScale(root.xdpi(), root.unit(), sf.h) : 1f;
        Scene ss = EnhancedScenes.build(page, root, sf.h, sk, page.touchIcons);
        ss.panelW = sf.w;
        ss.panelH = sf.h;
        root.show(ss);
    }
}
