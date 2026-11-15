package dev.recompbootstrap.smoke;

import static dev.recompbootstrap.smoke.SettingsCoreTestKit.check;
import static dev.recompbootstrap.smoke.SettingsCoreTestKit.same;
import static dev.recompbootstrap.smoke.SettingsCoreTestKit.test;

import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Update 1 W1-9, the Performance page's preset row (PerformancePresets; design UPDATE-1-FEATURES-DESIGN §4.2, review C10): a
 * preset sets exactly its four rows and never a World row; the row shows the matching preset or Custom whoever changed the rows;
 * settings.json never holds it (the rows decide when it is read back) and no game file is planned for it; a preset the build
 * cannot honour is not offered and the row names it; the names avoid "Balanced" and the game's name; the texts stay placeholders
 * (settings only, no measured claims) until the preset runs (E-P1). Run with java -ea.
 */
public final class PerformancePresetTest {
    /** The four rows each preset sets, as choice ids, held here independently of PerformancePresets. */
    private static final String[] KEYS = {"frameCap", "renderScalePercent", "scaler", "frameGeneration"};
    private static final String[][] EXPECTED = {
            null,
            {"30", "100", "fsr1", "off"},     // Battery
            {"auto", "100", "fsr1", "off"},   // Adaptive
            {"60", "100", "fsr1", "off"}};    // Max (the defaults)
    private static final String[] WORLD = {"worldTraffic", "worldPedestrians", "worldParkedCars", "worldScenarioPedestrians",
            "worldDrawDistance"};
    private static final String P = SettingsSchema.PERFORMANCE_PRESET;

    private static int id(String key, String choiceId) {
        int at = Arrays.asList(SettingsSchema.spec(key).choiceIds).indexOf(choiceId);
        check(at >= 0, key + " has the choice " + choiceId);
        return at;
    }

    /** Settings with everything a player may have changed outside the four rows, and the four rows on no preset. */
    private static Settings lived() {
        return Settings.defaults().with("worldTraffic", 35).with("worldPedestrians", 50).with("worldDrawDistance", 80)
                .with("fpsOverlay", 1).with("fsrSharpnessTenths", 7).with("frameCap", id("frameCap", "40"))
                .with("renderScalePercent", id("renderScalePercent", "88")).with("scaler", id("scaler", "gsr1"));
    }

    private static int rowOf(MenuModel model, String label) {
        for (int i = 0; i < model.rowCount(); ++i) if (label.equals(model.rowLabel(i))) return i;
        throw new AssertionError("no row " + label);
    }

    public static void main(String[] args) {
        SettingsCoreTestKit.begin("PerformancePresetTest");

        test("a preset sets its four rows and nothing else; World rows are never written", () -> {
            Settings base = lived();
            same(PerformancePresets.CUSTOM, base.get(P), "(the base is on no preset)");
            for (int preset = PerformancePresets.BATTERY; preset <= PerformancePresets.MAX; ++preset) {
                Settings applied = base.with(P, preset);
                String name = PerformancePresets.LABELS[preset];
                for (int i = 0; i < KEYS.length; ++i) same(id(KEYS[i], EXPECTED[preset][i]), applied.get(KEYS[i]), name + ": " + KEYS[i]);
                Set<String> allowed = new HashSet<>(Arrays.asList(KEYS));
                allowed.add(P);
                for (String key : applied.changedKeys(base)) check(allowed.contains(key), name + " changed " + key);
                for (String key : WORLD) same(base.get(key), applied.get(key), name + " keeps " + key);
                same(preset, applied.get(P), name + ": the row shows it");
            }
            same(Settings.defaults(), Settings.defaults().with(P, PerformancePresets.MAX), "Max is the defaults (the original game)");
        });

        test("the row follows the four rows, whoever changes them: a match shows its preset, anything else Custom", () -> {
            Settings s = Settings.defaults();
            same(PerformancePresets.MAX, s.get(P), "defaults: Max");
            s = s.with("frameCap", id("frameCap", "40"));
            same(PerformancePresets.CUSTOM, s.get(P), "40 fps by hand: Custom");
            s = s.with("frameCap", id("frameCap", "30"));
            same(PerformancePresets.BATTERY, s.get(P), "30 fps by hand with the other three at Battery's values: Battery");
            s = s.with("scaler", id("scaler", "gsr1"));
            same(PerformancePresets.CUSTOM, s.get(P), "another scaler: Custom");
            same(s, s.with(P, PerformancePresets.CUSTOM), "choosing Custom changes no row");
            same(PerformancePresets.MAX, Settings.defaults().with(P, PerformancePresets.CUSTOM).get(P), "Custom over matching rows: still Max");
            same(PerformancePresets.BATTERY, lived().with(P, PerformancePresets.BATTERY).with("worldTraffic", 60).get(P),
                    "a World row does not touch the preset");
        });

        test("settings.json never holds the preset; read back, the rows decide", () -> {
            Settings battery = Settings.defaults().with(P, PerformancePresets.BATTERY);
            String json = SettingsJson.encode(battery, SettingsCoreTestKit.caps(SettingsCoreTestKit.ALL_NAMES));
            check(!json.contains("performancePreset"), "not written: " + json);
            same(battery, SettingsJson.decode(json).settings, "read back: Battery again, from the rows");
            String stale = SettingsJson.encode(Settings.defaults()).replace("{", "{\"performancePreset\":\"battery\",");
            SettingsJson.Decoded decoded = SettingsJson.decode(stale);
            same(PerformancePresets.MAX, decoded.settings.get(P), "a stale preset word in a file: the 60 fps rows say Max");
            same(Settings.defaults(), decoded.settings, "and nothing else changes");
            check(decoded.warnings.isEmpty() && !decoded.missing.contains(P), "no warning, and the preset is never called missing");
        });

        test("the game files: no file for the preset; a preset plans what its rows plan", () -> {
            Capabilities all = SettingsCoreTestKit.caps(SettingsCoreTestKit.ALL_NAMES);
            for (int preset = PerformancePresets.BATTERY; preset <= PerformancePresets.MAX; ++preset) {
                Settings viaPreset = lived().with(P, preset);
                Settings byHand = lived();
                for (int i = 0; i < KEYS.length; ++i) byHand = byHand.with(KEYS[i], id(KEYS[i], EXPECTED[preset][i]));
                same(byHand, viaPreset, PerformancePresets.LABELS[preset] + ": the same settings as the four rows set by hand");
                Map<String, byte[]> plan = RuntimeMapping.plan(viaPreset, all, new SettingsCoreTestKit.MemoryFiles());
                for (String path : plan.keySet()) check(!path.toLowerCase().contains("preset"), "no planned file names the preset: " + path);
            }
        });

        test("a build without the automatic cap: Adaptive is not a step, and the row names it", () -> {
            Capabilities v458 = SettingsCoreTestKit.v458();
            check(!v458.offered(P, PerformancePresets.ADAPTIVE), "Adaptive not offered");
            check(PerformancePresets.reason(v458, PerformancePresets.ADAPTIVE).startsWith("Frame rate cap: "), "its reason is the cap row's");
            check(v458.offered(P, PerformancePresets.BATTERY), "Battery offered (v458 has the 30 fps limiter)");
            MenuModel model = new MenuModel();
            model.loaded(Settings.defaults(), true, Arrays.<String>asList(), Arrays.<String>asList(), v458);
            int row = rowOf(model, "Preset");
            check(model.focusOn(row), "the preset row takes the focus");
            same("Max (default)", model.rowValue(row), "starts on Max");
            model.nav(NavInput.LEFT, false);
            same("Battery", model.rowValue(row), "LEFT skips Adaptive");
            same("30 fps (original console cap)", model.rowValue(rowOf(model, "Frame rate cap")), "the cap row moved with it");
            model.nav(NavInput.LEFT, false);
            same("Battery", model.rowValue(row), "Custom is never a step: LEFT stops");
            check(!model.rowCanStep(row, -1), "and the LEFT arrow is drawn inactive");
            check(model.rowSecondLine(row).contains(PerformancePresets.TEXT_UNAVAILABLE + "Adaptive"), "the line names Adaptive: " + model.rowSecondLine(row));
            check(model.rowSecondLine(row).length() <= 100, "one line (" + model.rowSecondLine(row).length() + ")");
            model.focusOn(rowOf(model, "Scaler"));
            model.nav(NavInput.RIGHT, false);
            same("Custom", model.rowValue(row), "the scaler changed by hand: Custom");
            model.focusOn(rowOf(model, "Scaler"));
            model.nav(NavInput.LEFT, false);
            same("Battery", model.rowValue(row), "and back: Battery again");
        });

        test("a build that reads none of the four rows' files: the preset row is disabled with them", () -> {
            Capabilities v669 = SettingsCoreTestKit.v669();
            check(!v669.anyChoice(P), "no choice");
            MenuModel model = new MenuModel();
            model.loaded(Settings.defaults(), true, Arrays.<String>asList(), Arrays.<String>asList(), v669);
            check(!model.rowEnabled(rowOf(model, "Preset")), "disabled");
        });

        test("names and texts: no \"Balanced\", no game name, the handed-out build's words; placeholders without measured claims", () -> {
            List<String> texts = new java.util.ArrayList<>(Arrays.asList(PerformancePresets.LABELS));
            texts.addAll(Arrays.asList(PerformancePresets.NOTES));
            texts.add(SettingsPages.TITLES[SettingsPages.PERFORMANCE]);
            texts.add(SettingsPages.explain(P));
            for (String text : texts) {
                String lower = text.toLowerCase();
                check(!lower.contains("balanced"), "no \"Balanced\": " + text);
                check(!lower.contains("grand theft") && !lower.contains("gta") && !lower.contains("los santos"), "no game name: " + text);
                check(ShipWordsTest.forbiddenIn(text) == null, "the handed-out build's words: " + text);
            }
            for (String note : PerformancePresets.NOTES) {
                String lower = note.toLowerCase();
                for (String claim : new String[] {"measured", "save", "power", "cooler", "watt", " ms", "longer"}) {
                    check(!lower.contains(claim), "a placeholder until E-P1 claims nothing (" + claim + "): " + note);
                }
            }
            same("Performance", SettingsPages.TITLES[SettingsPages.PERFORMANCE], "the category's name");
        });

        SettingsCoreTestKit.end();
    }
}
