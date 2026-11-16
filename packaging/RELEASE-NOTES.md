# FivePocket {version} — the first update

**Play the 2013 open-world classic natively on Android, the AYN Thor, iPad and iPhone, from your own Xbox 360 discs.**
FivePocket is a static recompilation with a new renderer, not an emulator. No game code, data or shaders are in any
download: the FivePocket Builder makes everything from your own discs, on your own computer.

**This update needs new game code.** Run this version's builder on your discs again (Android: build a new package and
import it; iOS and Duo: build and sign the app again). With older game code the new rows say "Not available in this
version".

## New

- **Sandbox** (both Android apps): a second choice on the Play tab, next to Story Mode. Free roam on its own save, copied
  from your story saves the first time; your story saves are never changed. Story missions are held back and the trainer
  is always on. Sandbox needs at least one story save.
- **GTA Online's world, offline** (both Android apps): the Online entry. Everything runs on the device; nothing connects to
  any online service. Tested on the Thor: free roam, the race tutorial, the phone and its apps (including Snapmatic), and
  the game's own Interaction Menu by holding Select. Online free roam also checked on the Fold4. What you buy (clothes,
  haircuts, Ammu-Nation, cars) is still there after a restart.
- **Switch Character** in Sandbox (experimental): Michael, Franklin or Trevor, outdoors, right where you stand.
- **Sandbox** (iOS): the same Play tab choice; it plays on its own copy of your story saves and never changes them.
- **Vibration** (Off / Light / Full): the game's rumble, on the Thor Edition and on iOS.
- **Performance presets** (iOS): Battery (30 fps cap) and Max (60 fps cap).
- **Mods** (iOS): a warning when the game stopped last time with mods on, Start Once Without Mods, and adding a mod as a
  folder or a zip file from the device.
- **Builder window** (Windows): double-click "FivePocket Builder.exe" to pick your discs and press BUILD; it runs the
  same steps as builder.exe and makes the same files.
- **Interaction Menu button** (FivePocket, touch controls): in the online world, a button in the top row of the touch
  controls opens the game's Interaction Menu with one tap. Checked on the Fold4. (iOS gets it in the next update.)

## Changed

- Thor Edition: **Hold Select Opens Menu** is now Off by default, so holding Select in the online world opens the game's
  own Interaction Menu. Turn the setting back on in Settings > Controls.
- The Play tab's first item is called Story Mode wherever Sandbox is offered.
- Android: the apps ask for the vibration permission (granted at install, no prompt) and show the release's version number.

## Fixed

- Explosions: fireballs, smoke and most fire now appear. Checked on the Thor and the Fold4.
- Samsung phones whose picture froze while the sound went on (Exynos / Xclipse): when the graphics driver stops answering,
  the game now switches to its backup drawing path. Tested on the Thor by forcing that stop; not yet confirmed on an
  affected Samsung phone, so a log from one helps.
- Holes in the clouds on phones whose GPU lacks one depth feature (Mali, Xclipse).
- Ocean water near the beach breaking up on GPUs that round texture reads slightly low. Tested on the Thor with that
  rounding forced; not yet tested on an affected phone.
- Controllers: the right stick turns the camera on pads whose sticks were read from the wrong axes. Checked on the Thor
  and the Fold4.
- FivePocket (single screen): camera swipes on the touch screen turn the view directly and evenly. Checked on the Fold4.
- Thor Edition, tested on the AYN Thor (the single-screen app uses the same game code, not re-tested with this build):
  purchases in the online world (barber, clothes, Ammu-Nation, cash, bought cars) survive a restart; Los Santos Customs
  changes are saved when you leave the shop; the online character keeps the weapons and ammo the game saved; connecting or
  disconnecting headphones no longer stops the game (tested with a simulated route change); after an error stop the app
  shows its stop message instead of crashing.
- Builder window: SETTINGS and the licence checkbox can be clicked again (the tabs were covered by the home page).

## Known issues

- Clothes shop (online world): after buying, backing out can leave the shop menu stuck. Use the trainer's teleport.
- Los Santos Customs: after you leave, the car may not drive. Get out and back in, or use the trainer.
- Deathmatches and other jobs can hang when you select them.
- The Interaction Menu's header picture can look smeared.
- Finishing a race can crash the game.
- Windows: Microsoft Defender can wrongly flag `XenosRecomp.exe` (inside the builder) as "Trojan:Win32/Bearfoos.A!ml" and
  quarantine it, and the build then stops at shader-fixes. It is a false positive on a program built from the open
  source. Restore it from Protection history, allow the builder's folder, and start the build again.

## Editions

| Edition | Device | How you get it |
|---|---|---|
| **FivePocket** | Android 8.0+, arm64 + Vulkan, Snapdragon 8-class or better | `FivePocket-{version}-android.zip` + the builder |
| **FivePocket Thor Edition** | AYN Thor (dual screen) | `FivePocket-Thor-Edition-{version}-android.zip` + the builder |
| **FivePocket iOS** | iPhone / iPad, iOS 17.4+ (8 GB devices recommended) | the Mac builder makes the app; you sign it with your Apple ID |
| **FivePocket Duo** | the Duo (iPhone19,4), iOS 27.2 tested | the Mac builder makes the app; you sign it with your Apple ID |

Builders: the Android editions build on Windows (x86-64, one builder per edition, or the Thor Edition on Windows on ARM);
the iOS and Duo apps build on a Mac (Apple silicon, Xcode).

## Updating

- Android: install the new app over the old one. **Do not uninstall first: uninstalling deletes your saves.** Rebuild your
  package with the {version} builder; the {version} app accepts only the {version} package.
- iOS and Duo: build and sign with the same bundle identifier as before; your imported files and saves stay.

## Before you start

- You need your own Xbox 360 discs (Disc 1 and Disc 2) as images. FivePocket never downloads game files.
- **Never share what you build** (disc images, the bundle, the .ipa/.apk you build, game files, saves). See LEGAL.md.
- FivePocket is free and has no affiliation with the game's publisher or developer, Microsoft, Apple, Google or AYN.

Source: `FivePocket-{version}-source.tar.xz` (GPL-3.0-or-later). Checksums: `SHA256SUMS` and the table below.
Community and help: the FivePocket Discord, https://discord.gg/mZvqCsxgxT
