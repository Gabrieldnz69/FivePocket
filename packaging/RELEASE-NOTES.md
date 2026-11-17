# FivePocket {version} — new menus

**Play the 2013 open-world classic natively on Android and the AYN Thor, from your own Xbox 360 discs.**
FivePocket is a static recompilation with a new renderer, not an emulator. No game code, data or shaders are in any
download: the FivePocket Builder makes everything from your own discs, on your own computer.

This release is for Android (FivePocket and the Thor Edition), the Windows builders and the Mac builder for iOS and Duo.
**iOS / Duo: new menus; Online next update.**

**This update needs new game code.** Run the {version} builder on your discs again and import the new package; the
{version} app accepts only a {version} package.

## New

- **New menus**: the launcher is drawn like the game's own Enhanced menus. The Play tab shows its choices as tiles (Story
  Mode, Sandbox, Online, Verify Game Data, Import Game Discs) over the loading-screen art from your discs; Settings,
  Controller and About keep the same tab strip on every screen size.
- **Verify Game Data** and **Import Game Discs** on the Play tab: Verify looks at every game file on the device again,
  disc by disc; Import opens the setup page (game code and both discs).
- **Locked until set up**: on a fresh install Story Mode, Sandbox and Online show a padlock and say "Set up the game to
  unlock." until setup is complete.
- **Setup pages** in the same frame as the menus, with the same wording on every step.
- **The game's own fonts**: once your game code is imported, every menu of the app (launcher, settings, setup pages,
  dialogs, the in-game side menu) uses the game's fonts from your discs. Before that, the app's own font.
- **Menu sounds**: the launcher plays the game's Select and Back sounds, made by the builder from your discs.
- **Online** on the Play tab when the game code has it (Thor Edition and single-screen).
- **Unsupported GPU**: About > Unsupported GPU can let the game try to start on a graphics chip below its minimum (off by
  default).

## Changed

- Builder (Windows x64 and ARM64): makes the menu fonts, the controller glyphs, the tile pictures and the Select / Back
  sounds from your discs and puts them into the package.
- Game library v1922.

## Fixed

- A leftover developer frame-rate readout could show up in the corner of the game. It no longer appears.
- Touch controls (single-screen app): the left stick no longer stays on screen after you lift your finger, open the side
  menu or switch controllers.
- Tile captions always fit inside their tiles.

## If you skipped 1.1.5

1.1.5 was taken down soon after it came out. Its Android and Windows changes are in this release too: Sandbox (free roam
on its own copy of your story saves), the Online world played offline on the device, Switch Character in Sandbox
(experimental), Vibration settings on the Thor Edition, the builder window for Windows ("FivePocket Builder.exe"), the
Interaction Menu button in the touch controls, and the 1.1.5 fixes (explosions, the backup drawing path when the graphics
driver stops answering, clouds on Mali / Xclipse, beach water, controller right-stick axes, touch camera swipes, online
purchases kept after a restart). The full list is in CHANGELOG.md.

## Known issues

- The other menu sounds (moving up and down or left and right, and the error sound) are not there yet: the game's one-time
  recording of them does not finish on a fresh install. The menus stay silent for those.
- The known issues listed for 1.1.5 in CHANGELOG.md that are not marked fixed above still apply (among them: the online
  clothes shop menu can stay stuck after buying, Deathmatches and other jobs can hang when selected, finishing a race can
  crash the game).
- Windows: Microsoft Defender can wrongly flag `XenosRecomp.exe` (inside the builder) as "Trojan:Win32/Bearfoos.A!ml" and
  quarantine it, and the build then stops at shader-fixes. It is a false positive on a program built from the open
  source. Restore it from Protection history, allow the builder's folder, and start the build again.

## Editions

| Edition | Device | How you get it |
|---|---|---|
| **FivePocket** | Android 8.0+, arm64 + Vulkan, Snapdragon 8-class or better | `FivePocket-{version}-android.zip` + the builder |
| **FivePocket Thor Edition** | AYN Thor (dual screen) | `FivePocket-Thor-Edition-{version}-android.zip` + the builder |
| **FivePocket iOS** | iPhone / iPad, iOS 17.4+ (8 GB devices recommended) | `FivePocket-iOS-Builder-{version}-macos-arm64.zip`: the Mac builder makes the app; you sign it with your Apple ID |
| **FivePocket Duo** | the Duo (iPhone19,4), iOS 27.2 tested | the same Mac builder; you sign it with your Apple ID |

Builders: the Android editions build on Windows (x86-64, one builder per edition, or the Thor Edition on Windows on ARM);
the iOS and Duo apps build on a Mac (Apple silicon, Xcode).

## Updating

- Android: install the new app over the old one. **Do not uninstall first: uninstalling deletes your saves.** Rebuild your package
  with the {version} builder; the {version} app accepts only the {version} package.
- iOS and Duo: build and sign with the same bundle identifier as before; your imported files and saves stay.

## Before you start

- You need your own Xbox 360 discs (Disc 1 and Disc 2) as images. FivePocket never downloads game files.
- **Never share what you build** (disc images, the bundle, the .apk / .ipa you build, game files, saves). See LEGAL.md.
- FivePocket is free and has no affiliation with the game's publisher or developer, Microsoft, Apple, Google or AYN.

Source: `FivePocket-{version}-source.tar.xz` (GPL-3.0-or-later). Checksums: `SHA256SUMS` and the table below.
Community and help: the FivePocket Discord, https://discord.gg/mZvqCsxgxT
