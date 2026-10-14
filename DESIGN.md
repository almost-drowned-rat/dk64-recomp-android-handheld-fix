# DESIGN — Android port of DK64-Recompiled (architecture decisions)

Design document of the original Android port by deivid22srk, translated to English. Notes on what the
handheld fork changed are in [CHANGELOG.md](CHANGELOG.md).

## Decisions (settled)
- ROM at runtime: APK **without** ROM; scan of `Android/data/<app id>/files/` + SAF picker
- ROM for the build: release asset `build-inputs` in a separate PRIVATE repository (`dk64-recomp-build-inputs`;
  the workflow downloads it via the secret `PRIVATE_REPO_TOKEN` — the ROM never lands in the public repository)
- Controller first; touch only for menus · minSdk 26 · arm64-v8a · debug APK · public repository
  (the original port's docs were in Brazilian Portuguese)

## Port tree (new repository)
```
/                          upstream code (pinned submodules, vcpkg with update=none)
android/                   Gradle project
  app/CMakeLists.txt       ANDROID NATIVE ORCHESTRATION (does not edit the submodules' CMake!)
  app/build.gradle.kts     AGP 8.5.2, externalNativeBuild -> app/CMakeLists.txt
  native/stubs/            nfd_stub.c, curl_stub.cpp (exact symbols, see below)
  native/compat/           android_paths.cpp/.h — path globals injected via argv
  patches/                 .patch files applied with `git apply` to the submodules (CI + local)
    rt64-android.patch
    plume-android.patch
    recompfrontend-android.patch
  app/src/main/            AndroidManifest.xml, MainActivity.java, res/, java org.libsdl.app (vendored SDL2 2.30.8)
.github/workflows/build.yml  full build (host codegen + Gradle debug APK)
```

## CMake strategy (android/app/CMakeLists.txt) — WITHOUT editing submodules
- Own root (does NOT include the upstream root CMakeLists, which requires vcpkg/system SDL)
- SDL2 2.30.8 via FetchContent (SDL_STATIC=ON, SDL_SHARED=OFF, SDL_TEST=OFF;
  the real name of the test option in the 2.30 series is SDL_TEST — SDL_TEST_LIBRARY does not exist).
  Drift guard in CMake: the SDL_MAJOR/MINOR/MICRO_VERSION constants of the vendored Java code
  must equal the SDL C version (SDLActivity checks nativeGetVersion() at
  startup and aborts with 'SDL C/Java version mismatch' — an error that only shows up on the device).
  When bumping the version, copy android-project/app/src/main/java from the SDL tarball.
  Include compatibility:
  `${CMAKE_BINARY_DIR}/sdl2inc/SDL2/*` (copy of the headers, EXCEPT
  SDL_config.h/SDL_revision.h) for `#include "SDL2/SDL.h"`
  + `${sdl2_BINARY_DIR}/include-config-$<LOWER_CASE:$<CONFIG>>/SDL2`
  (generated SDL_config.h — only written at GENERATE time via file(GENERATE), hence
  added as a genex; consumers and the library use the SAME config)
  + `${sdl2_BINARY_DIR}/include` (generated SDL2/SDL_revision.h)
  + `${sdl2_SOURCE_DIR}/include` (fallback: the tarball's SDL_config.h dispatches
  to SDL_config_android.h — correct for Android, but last in order)
- **Fake config packages** (GLOBAL IMPORTED targets) for the submodules' find_package:
  - SDL2: `SDL2_DIR` -> SDL2Config.cmake defining `SDL2::SDL2` (INTERFACE IMPORTED GLOBAL) + vars SDL2_INCLUDE_DIRS/SDL2_LIBRARIES
  - CURL: `CURL_DIR` -> CURLConfig.cmake defining `CURL::libcurl` (GLOBAL) -> `curl_stub`
  - freetype: `CMAKE_MODULE_PATH` += `android/cmake/Modules/FindFreetype.cmake` (uses the `freetype` target from FetchContent). The port calls `find_package(Freetype REQUIRED)` right after MakeAvailable — required: RmlUi links `Freetype::Freetype` and freetype 2.13.2 does not create that alias in-tree (only on install/export)
- Stubs compiled and linked here: `nfd` (static, replaces rt64's add_subdirectory via patch) and `curl_stub`
- Submodule patches (git apply in CI BEFORE the build):
  - rt64: (1) file_to_c/tools `if(NOT ANDROID)`; (2) nfd `if(NOT ANDROID)`; (3) DXC for ANDROID uses CMAKE_HOST_SYSTEM_PROCESSOR; (4) `PLUME_SDL_VULKAN_ENABLED`/`RT64_SDL_WINDOW_VULKAN` also on ANDROID (rt64 CMake L77+L304); (5) rt64_application_window.cpp: `__ANDROID__` branches with static_assert -> SDL implementation
  - plume: reorders the `RenderWindow` typedef — `PLUME_SDL_VULKAN_ENABLED` (SDL_Window*) now wins BEFORE `__ANDROID__` (ANativeWindow*), removing the 6 uses of ANativeWindow* in the active paths (rt64_application_window.cpp and the SDL_Vulkan_* sites in plume_vulkan.cpp compile with SDL_Window*); with that, `appCore.window = window_handle` in recompui/rt64_render_context.cpp (branch `__linux__ || __ANDROID__`) also compiles without a cast
  - RecompFrontend: file.cpp (get_app_folder_path/get_program_path/get_asset_path/open_file_dialog) with `__ANDROID__` branches calling android_paths.h; recompui CMake: nothing (find_packages resolved via the fakes)
- Global definitions before add_subdirectory: HLSL_CPU, FFX_GCC, IMGUI_IMPL_VULKAN_NO_PROTOTYPES, PLUME_SDL_VULKAN_ENABLED, RT64_SDL_WINDOW_VULKAN, RT64_STATIC=TRUE, RECOMP_ANDROID
- After add_subdirectory: inject the SDL2 includes into recompui/recompinput/rt64/main
- Final target: `add_library(main SHARED ...)` with the upstream sources (src/main/*.cpp, src/game/*.cpp, generated rsp/n_aspMain.cpp, RecompiledFuncs/*.c glob, PatchesLib, icon_bytes.c generated via the host file_to_c in PATH)
- Link: PatchesLib RecompiledFuncs recompui recompinput librecomp ultramodern rt64 nfd SDL2-static vulkan android log ${CMAKE_DL_LIBS}; flags: -fno-strict-aliasing (recomp sources), `-Wl,-z,max-page-size=16384`, STL c++_shared (via Gradle)
- src/main/main.cpp (file of this repository — EDITABLE): `#ifdef __ANDROID__ #include <SDL_main.h> #else #define SDL_MAIN_HANDLED #endif` + argv parsing (filesDir, extFilesDir) -> android_paths

## Android runtime (after the review by agent 6-b)
- **SetupActivity** (launcher): checks Vulkan >= 1.1 (warning), ensures a ROM (.z64/.n64/.v64 via
  scan of filesDir/extFilesDir or SAF picker with a background copy) and copied assets,
  then hands off to MainActivity and calls finish()
  *(later removed: the app now opens directly in SDL and the ROM is chosen in the game's launcher menu)*
- **MainActivity extends SDLActivity**: standard SDL lifecycle (super.onCreate always called);
  getLibraries()={"main"}; getArguments() = {filesDir, extFilesDir}
  ⚠️ SDL_android.c (nativeRunMain) sets argv[0]="app_process" and copies getArguments()
  starting at argv[1] — which is why we do NOT pass the app name in getArguments()
- Copy of the APK assets (assets/ + recompcontrollerdb.txt) to filesDir on the first run
  (marker .assets_version); mapping: APK assets/<x> -> filesDir/assets/<x>, except
  recompcontrollerdb.txt -> filesDir/recompcontrollerdb.txt
- `recompui::file::open_file_dialog` (Android): scans extFilesDir/filesDir for *.z64/*.n64/*.v64 ->
  callback(path) — the runtime checks the hash, byteswaps and stores it in <config>/DK64.z64
- RT64 userPaths (rt64_user_paths.cpp): the `__linux__` branch, active on Android, uses
  getenv("HOME") and would fall back to getpwuid()->pw_dir (NULL for an app uid -> crash).
  main.cpp does `SDL_setenv("HOME", filesDir)` before any init → RT64 writes
  rt64-imgui.ini/rt64.log/games to filesDir/.config/rt64 (RT64's shader cache is
  RAM-only in this version; nothing in cwd, which is "/")
- Target framerate: the game calls `recomp_get_target_framerate` (src/game/recomp_api.cpp)
  -> `ultramodern::get_target_framerate(60/frame_divisor)` (ultramodern/src/events.cpp)
  -> rr_option: Original (20 FPS), Manual (rr_manual_value) or Display (real Hz
  via `renderer_context->get_display_framerate()`; on Android = SDL_GetWindowDisplayIndex)
- Threads: ultramodern uses pthread_setname_np (bionic API 26+, minSdk 26 OK) and
  does not use sem_open — no API level problem
- Manifest: sensor landscape, largeHeap, hardwareAccelerated=true (default of the SDL 2.30.8 template),
  VIBRATE for SDL haptics, optional uses-feature, full configChanges

## CI (build.yml, ubuntu-latest)
1. checkout submodules recursive (vcpkg skipped via update=none)
2. apt: ninja-build lld clang llvm (MIPS ELF patches) ; Java 17
3. ROM: gh release download build-inputs from the PRIVATE repository (secret PRIVATE_REPO_TOKEN) -> unzip -> check sha1 cf806ff...
4. host codegen: clone N64Recomp @ 2b6f05688de2abc7d86da5b4a89b84c2c6acbabe, build N64RecompCLI+RSPRecomp (ninja)
5. decompression: cp ROM -> baserom.us.z64 (tmp) ; python3 generate_decompressed_rom.py ; mv -> donkeykong64.decompressed.us.z64
6. ./N64Recomp us.toml ; ./RSPRecomp n_aspMain.toml ; make -C patches CC=clang LD=ld.lld ; ./N64Recomp patches.toml
7. host file_to_c: cmake lib/rt64/src/tools/file_to_c -> PATH
8. git apply the patches to the submodules
9. ./gradlew assembleDebug --no-daemon ; upload-artifact app-debug.apk
10. (Optional for debugging failures: gradle --scan? no; logs in CI)

## Stubs (exact symbols collected via grep)
- nfd: NFD_Init, NFD_Quit, NFD_OpenDialogN, NFD_OpenDialogMultipleN, NFD_PickFolderN, NFD_SaveDialogN,
  NFD_FreePathN, NFD_PathSet_GetCount, NFD_PathSet_GetPathN, NFD_PathSet_Free (return NFD_ERROR/empty NFD_OKAY)
- curl: curl_global_init, curl_easy_init/setopt/perform/getinfo/cleanup/strerror, curl_slist_append/free_all
  (verified: the stub covers all uses in ui_mod_discovery_http.cpp; `curl_global_initialize` is a method of ui_mod_discovery itself, not a libcurl symbol)

## Open risks (watch in CI)
- sse2neon: do main.cpp/recomp use x86 intrinsics? sse2neon is in N64ModernRuntime's include path (upstream arm64 Linux compiles — good reference)
- curl headers in recompui: own stub curl/curl.h (android/native/stubs/include/curl/curl.h) to compile ui_mod_discovery_http — symbols checked: the stub covers all uses (curl_global_init/easy_*/slist_*; `curl_global_initialize` is a method of ui_mod_discovery itself, not a libcurl symbol)
- Warnings: -Wno-* on the recomp targets (like upstream); "no relevant warnings" = no errors, and new warnings from our own code handled
- volk/Vulkan-Headers/VulkanMemoryAllocator/D3D12MemoryAllocator are NESTED submodules of plume — CI needs `submodules: recursive` (already in build.yml)
- rt64 CMake L102-103 sets ANDROID_PLATFORM=android-24/ANDROID_ABI=arm64-v8a unconditionally (dead override after the NDK toolchain, inert; the real minSdk comes from Gradle: 26)
