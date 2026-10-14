# DK64 Handheld — Donkey Kong 64: Recompiled for Android handhelds

Fork of [deivid22srk/dk64-recomp-android](https://github.com/deivid22srk/dk64-recomp-android)
(the Android port of [Donkey Kong 64: Recompiled](https://github.com/Rainchus/Donkey-Kong-64-Recompiled))
for Android handhelds with a built-in controller. Tested on the **AYN Odin 2 Portal**
(1920×1080, Android 13).

What this fork changes (details in [CHANGELOG.md](CHANGELOG.md)):

- Updated to upstream Donkey Kong 64: Recompiled `3b0c36d` (game version 1.0.3)
- Own app ID `com.deivid22srk.dk64recomp.handheld` ("DK64 Handheld") — installs next to the original app
- No touch overlay — play with the built-in controller; touch still works as a mouse in the menus
- Options menu: hold **Start + Select** for about one second (Select alone no longer opens it)
- Real full screen, no black strip from the navigation bar
- Fixed audio crackling and pitch jumps
- Fixed a frozen picture after overlays and the missing restart after loading a ROM

> ⚠️ **No game assets and no prebuilt APK.** The APK contains code recompiled from the ROM, so it is not
> distributed here. Build it yourself with your own legally obtained ROM of **Donkey Kong 64 (USA, NTSC-U 1.0)**,
> sha1 `cf806ff2603640a748fca5026ded28802f1f4a50` — see *Local (sem CI)* below.

Branch `handheld` is the fork's branch; `main` stays at the original port.

## About the Android port

Based on deivid22srk's port of [Donkey Kong 64: Recompiled](https://github.com/Rainchus/Donkey-Kong-64-Recompiled)
to **native Android** via the NDK, keeping the whole original stack:
[N64ModernRuntime](https://github.com/Killklli/N64ModernRuntime) +
[RT64](https://github.com/rt64/rt64) (Vulkan) + RmlUi + SDL2.

## Installing on the device

1. Install the APK (allow "Install unknown apps").
2. Open the app — it goes straight to the **game menu**. On the first start the UI assets are extracted
   automatically.
3. If no ROM is loaded yet, the first menu entry is **"Load ROM"**: select the file (`.z64`, `.n64` or `.v64` —
   byte order is converted automatically) in the Android file picker and wait for the validation. A message
   appears and the **app restarts by itself** to finish the setup; afterwards the entry reads **"Start Game"**.

   > The selected ROM is copied to the app's internal storage and checked by hash (pick once: later starts open
   > on "Start Game" directly). The restart runs the game start in a clean process — the same principle as the
   > "GPU Driver" menu (Vulkan is initialised once per process).

### Requirements

- Android 8.0+ (arm64-v8a)
- **GPU with Vulkan 1.1+**
- A controller (built in, Bluetooth or USB); touch works as a mouse in the menus
- The *online* mod store is disabled on Android (local `.nrm`/`.rtz` mods work: put them in
  `Android/data/com.deivid22srk.dk64recomp.handheld/files/mods/`)

### Changing the ROM later

The runtime keeps the validated ROM in its internal config and checks its hash on every start. To choose
another ROM, clear the app's data (or just its storage) — the menu offers **"Load ROM"** again on the next start.

## Building

The build has two stages:

1. **Codegen on the host** — decompress the ROM with the script from the
   [DK64 decompilation](https://gitlab.com/dk64_decomp/dk64), build
   [N64Recomp](https://github.com/N64Recomp/N64Recomp)/`RSPRecomp` and run `us.toml`, `n_aspMain.toml` and
   `patches.toml`; plus the host tool `file_to_c`.
2. **APK via Gradle + NDK** — `android/gradlew assembleDebug` with `externalNativeBuild`
   (CMake orchestrated in `android/app/CMakeLists.txt`).

Details: [DESIGN.md](DESIGN.md) · upstream README: [README-UPSTREAM.md](README-UPSTREAM.md) ·
desktop build: [BUILDING.md](BUILDING.md).

### Local build

Requires a Linux host (WSL works) with clang, lld, ninja, cmake, python3, JDK 17 and the Android SDK with
`platforms;android-34`, `ndk;27.2.12479018` and `cmake;3.22.1`.

```bash
# 1) Codegen
git submodule update --init --recursive   # vcpkg is not needed for Android
# put your ROM at rom-work/baserom.us.z64
(cd rom-work && python3 ../lib/dk64_decomp/tools/generate_decompressed_rom.py)
cp rom-work/baserom.us.decompressed.z64 donkeykong64.decompressed.us.z64
# build N64Recomp/RSPRecomp (commit 2b6f05688de2abc7d86da5b4a89b84c2c6acbabe) and run:
./N64Recomp us.toml && ./RSPRecomp n_aspMain.toml
make -C patches CC=clang LD=ld.lld && ./N64Recomp patches.toml
cmake -S lib/rt64/src/tools/file_to_c -B build-filetoc -G Ninja && cmake --build build-filetoc

# 2) APK
git -C lib/rt64 apply ../../android/patches/rt64-android.patch
git -C lib/rt64/src/contrib/plume apply ../../../../../android/patches/plume-android.patch
git -C lib/rt64/src/contrib/zstd apply ../../../../../android/patches/zstd-android.patch
git -C lib/N64ModernRuntime apply ../../android/patches/n64modernruntime-android.patch
git -C lib/RecompFrontend apply ../../android/patches/recompfrontend-android.patch
cd android && ./gradlew assembleDebug -PHOST_FILE_TO_C="$PWD/../build-filetoc/file_to_c"
```

### CI with a private inputs repository (optional)

The workflows in `.github/workflows/` (from the original port) download the build ROM from a release in a
separate **private** repository (`dk64-recomp-build-inputs`, secret `PRIVATE_REPO_TOKEN`), so the ROM never
lands in this public repository. `scripts/setup-build-inputs-repo.sh` sets this up (needs the
[gh CLI](https://cli.github.com/), `zip`/`unzip` and a classic PAT with `repo` scope):

```bash
GH_TOKEN=ghp_... ROM_ZIP=path/to/Donkey.Kong.64.zip \
  ./scripts/setup-build-inputs-repo.sh --purge-public
```

## Vulkan driver (Turnip) — optional

On some devices the proprietary Adreno driver has Vulkan bugs (e.g. a `SIGSEGV` in
`vkGetRefreshCycleDurationGOOGLE` on the `RT64 Present` thread, seen on the moto g34 5G). The port integrates
**libadrenotools** (like Vita3K, yuzu/sudachi and other RT64 recomp ports): you can install a **Mesa Turnip**
driver as a .zip and the game uses it instead of the system driver — **directly from the game menu**, entry
**"GPU Driver"** (below Exit in the launcher).

1. Download a Turnip driver — accepted formats: **adrenotools** `.zip` (e.g.
   [K11MCH1/AdrenoToolsDrivers](https://github.com/K11MCH1/AdrenoToolsDrivers), `Turnip_vX.Y.Z_R*.zip`,
   `Gmem`/`Sysmem` variants), **Winlator/WN-Turnip** `.zip` (without `meta.json` — the app detects the soname)
   or a plain `.so`. **Mind the GPU generation**: Adreno 6xx needs the **a6xx** builds, a7xx/a8xx builds expose
   no GPU there.
2. In the game menu: **GPU Driver → Install driver (.zip)...** → pick the zip (or `.so`). The app **tests** the
   driver right away (creates a `VkInstance` and enumerates the GPUs, the same path RT64 takes): if it lists the
   GPU ("installed and verified: Adreno (TM) …") it is ready; if no Vulkan GPU is listed, the installation is
   **refused** with instructions — otherwise the game would fail with "Unable to find compatible graphics device".
3. The app restarts to load the new driver (Vulkan is initialised once per process).
4. Back to the system driver: **GPU Driver → Use system driver** (only shown while a custom driver is active).

Requires Adreno + arm64; Android 10+ recommended. Implementation details: [docs/DRIVER-VULKAN.md](docs/DRIVER-VULKAN.md).

## Technical notes

- `libmain.so` (static SDL2 + runtime + RT64) is loaded via `SDLActivity` (`System.loadLibrary("main")` → `SDL_main`)
- App paths are passed in via `getArguments()` (filesDir/externalFilesDir)
- **All file interaction happens in the game's own UI**: "Load ROM" and "GPU Driver" open Android's DocumentsUI
  through a non-blocking JNI bridge (`android/native/compat/file_bridge.cpp`); the result is dispatched per
  frame in `draw_hook` (patch of `ui_state.cpp`), in the same context as the menu callbacks
- `nativefiledialog-extended` and `curl` are replaced by *stubs* on Android (ROM dialog = SAF via DocumentsUI;
  mod store offline)
- Patches are applied to the submodules with `git apply` (`android/patches/*.patch`) — the submodules stay upstream
- Vulkan via volk (`VK_NO_PROTOTYPES`): system driver or a custom Turnip via libadrenotools
- 16 KB page alignment; minSdk 26; arm64-v8a

## Credits and license

This fork:
- [deivid22srk/dk64-recomp-android](https://github.com/deivid22srk/dk64-recomp-android) — the Android port
  this fork is based on (NDK build, SDLActivity integration, SAF file bridge, libadrenotools/Turnip support,
  lifecycle handling)
- [thorskin6657-sudo/dk64-recomp-android](https://github.com/thorskin6657-sudo/dk64-recomp-android) — reference
  for the separate app identity
- [GreenNaugahyde/ZeldaWWHDRecompAndroid](https://github.com/GreenNaugahyde/ZeldaWWHDRecompAndroid) —
  full-screen approach (`hideSystemBars`, MPL-2.0), via the Wind Waker Odin fork

Game and runtime:
- [Rainchus/Donkey-Kong-64-Recompiled](https://github.com/Rainchus/Donkey-Kong-64-Recompiled) — DK64: Recompiled
  by Rainchus, Ballaam, Killklli, Green Bean and UmedMuzl (see [README-UPSTREAM.md](README-UPSTREAM.md))
- [N64Recomp](https://github.com/N64Recomp/N64Recomp) by Mr-Wiseguy — static recompiler
- [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) (fork used: [Killklli](https://github.com/Killklli/N64ModernRuntime))
- [RecompFrontend](https://github.com/Killklli/RecompFrontend) — launcher and config menus (UI design by thecozies)
- [RT64](https://github.com/rt64/rt64) by Dario (DarioSamo) — renderer, with [plume](https://github.com/renderbag/plume)
- [Donkey Kong 64 decompilation](https://gitlab.com/dk64_decomp/dk64) — headers and ROM tools
- [libadrenotools](https://github.com/bylaws/libadrenotools) by bylaws — custom Vulkan drivers
- [SDL2](https://www.libsdl.org/), [RmlUi](https://github.com/mikke89/RmlUi), [lunasvg](https://github.com/sammycage/lunasvg),
  [zstd](https://github.com/facebook/zstd) and the other libraries listed in the submodules

License: GPL-3.0, see [LICENSE](LICENSE) / [COPYING](COPYING). Source code of all changes is in this repository.

*Fan project, not affiliated with Nintendo or Rare. Donkey Kong 64 is © Nintendo / Rare.*
