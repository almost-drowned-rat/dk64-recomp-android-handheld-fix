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

> ⚠️ **No game assets.** The APK on the [Releases](../../releases) page contains only this project's code (like the
> upstream and original port releases): you need your own legally obtained ROM of **Donkey Kong 64 (USA, NTSC-U 1.0)**,
> sha1 `cf806ff2603640a748fca5026ded28802f1f4a50`, which the app asks for on the first start ("Load ROM").
> To build the APK yourself see [Building](#building) below.

Branch `handheld` is the fork's branch; `main` stays at the original port. For installing, the GPU driver
menu (Turnip) and technical notes see the [original port's README](https://github.com/deivid22srk/dk64-recomp-android#readme); the Turnip
integration is described in [docs/DRIVER-VULKAN.md](docs/DRIVER-VULKAN.md), the build architecture in
[DESIGN.md](DESIGN.md).

## Building

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
