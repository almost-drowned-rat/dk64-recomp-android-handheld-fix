# Changelog — DK64 Handheld

Fork of [deivid22srk/dk64-recomp-android](https://github.com/deivid22srk/dk64-recomp-android)
for Android handhelds with a built-in controller. Tested on the AYN Odin 2 Portal
(1920×1080, Android 13).

## 0.1 — 2026-10-14

Based on deivid22srk/dk64-recomp-android `e3f76fe` (2026-09-05).

### Upstream
- Game code updated to Rainchus/Donkey-Kong-64-Recompiled `3b0c36d` (2026-09-29, 47 commits
  after the port's base `c6730d2`), incl. game version 1.0.3.
- Submodules on the upstream revisions: N64ModernRuntime `8fe786b`, RecompFrontend `57939cc`,
  rt64 `e437f2a`, dk64_decomp `cf8b767`, vcpkg `b3ae22a`.
- Android patches (`android/patches/`) adjusted to the new submodule revisions (context only).
- `-falign-functions=16` from the upstream CMakeLists also in the Android build.

### Changed
- Own app ID `com.deivid22srk.dk64recomp.handheld`, app name "DK64 Handheld" — installs next to
  the original app, which stays untouched.
- Touch overlay removed completely (stick, Z/L/R, C buttons, A/B, START, ≡ and the toggle icon).
  Touch still works as a mouse in the menus.
- Options menu: hold **Start + Select** for about one second (in game only). Select alone no longer
  opens it; Start alone pauses the game as usual and is held back from the game while Select is
  pressed with it. Close the menu with Select or Back. Escape on a keyboard is unchanged.
- Real full screen: system bars hidden via `WindowInsetsController` — no black strip at the bottom.
  A swipe from the edge shows the bars for a moment.

### Fixed
- Audio crackling and pitch jumps: the queued audio time was computed with the game's sample rate
  (22050 Hz) instead of the output rate (48000 Hz), so samples were skipped far too early and the
  queue then ran dry. Android additionally keeps a larger margin (three VIs) and skips only from
  200 ms on.
- Frozen picture after a pause in which the surface survives (overlay, dialog activity): the present
  queue is now released again in `onResume` if the surface is still valid.
- The app now reopens by itself after loading a ROM or changing the GPU driver. The old
  AlarmManager restart was blocked by Android 10+ as a background activity start.
