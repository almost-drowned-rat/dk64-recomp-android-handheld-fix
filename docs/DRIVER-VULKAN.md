# Custom Vulkan driver (Turnip via libadrenotools)

This document describes **how** support for custom Vulkan drivers (Mesa **Turnip**) was implemented in
the Android port of DK64: Recompiled — the same mechanism used by emulators such as Vita3K, yuzu/sudachi
and Skyline, and the inspiration for other Android ports of RT64 recomps.

*Written by deivid22srk for the original port (in Portuguese), translated for the handheld fork. Parts
that describe the original `SetupActivity` refer to an earlier version: the driver is now managed from the
"GPU Driver" entry in the game's launcher menu.*

---

## 1. Motivation: the proprietary driver bug

On Qualcomm devices with old Adreno drivers (e.g. **moto g34 5G**, Snapdragon 695, vendor driver from the
`AU_LINUX_ANDROID_LA.UM.9.14` branch — an Android 9 base running under Android 15), the game crashed right
after selecting the ROM, on the `RT64 Present` thread:

```text
Fatal signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x0
Cause: null pointer dereference
  #00 libvulkan.so (vulkan::driver::GetRefreshCycleDurationGOOGLE+36)
  #01..#03 libmain.so (RT64 — PresentQueue::threadLoop)
```

Root cause: on Android the `VK_GOOGLE_display_timing` extension is **emulated by the framework** (the
system `libvulkan.so` implements `vulkan::driver::…` on top of ANativeWindow/SurfaceFlinger) and is always
reported as supported. RT64 (plume) enables the extension and queries the display's real refresh rate with
`vkGetRefreshCycleDurationGOOGLE`; the framework/proprietary driver implementation on this device
dereferences a null pointer.

The lasting fix is not to work around every proprietary driver bug but to **allow replacing the driver**:
Mesa **Turnip** (open source, for Adreno a6xx/a7xx) implements modern Vulkan (and the extensions RT64 uses)
correctly. With Turnip active, `VK_GOOGLE_display_timing` works and the crash disappears.
*(See 2.4: the crash later turned out to have a different cause.)*

## 2. How libadrenotools works

[libadrenotools](https://github.com/bylaws/libadrenotools) (bylaws) loads an alternative Vulkan driver
without root. The central call is:

```c
void *adrenotools_open_libvulkan(
    int dlopenMode,          // RTLD_NOW
    int featureFlags,        // ADRENOTOOLS_DRIVER_CUSTOM | ADRENOTOOLS_DRIVER_FILE_REDIRECT
    const char *tmpLibDir,   // writable dir (api < 29, no memfd)
    const char *hookLibDir,  // MUST be the app's nativeLibraryDir
    const char *customDriverDir,  // directory of the extracted driver (trailing slash)
    const char *customDriverName, // soname of the .so (e.g. "vulkan.ad07xx.so")
    const char *fileRedirectDir,  // driver shader cache
    void **userMappingHandle);    // unused (no GPU mapping import)
```

It does not dlopen Turnip directly (Android's linker namespaces would block that): instead it opens the
**system `libvulkan.so` inside an isolated namespace with ELF hooks** (ByteHook/linkernsbypass) that
redirect the loading of the internal ICD to the custom driver directory. The result is a "poisoned"
`libvulkan.so` *handle*: any `VkInstance` created from it uses Turnip.

Requirements and how the port meets them:

| Requirement | How we meet it |
|---|---|
| Hooks (`libmain_hook.so`, `libhook_impl.so`, `libfile_redirect_hook.so`) as real files in `nativeLibraryDir` | The hooks are `SHARED` targets of the same CMake build; Gradle packages every `.so` from the output directory and `useLegacyPackaging = true` (build.gradle.kts) extracts them to `nativeLibraryDir`. CI checks the APK contents |
| Driver in the app's **internal** storage (dlopen blocks the sdcard) | Always extracted to `filesDir/driver/installed/<id>/`; native code re-checks the prefix |
| `libadrenotools` only makes sense on Adreno/arm64 | `abiFilters = arm64-v8a` (adrenotools' CMake aborts on other ABIs) |

## 2.1 Real bug found in the field: "Unable to find compatible graphics device"

In the first release with the selector (moto g34 5G, Android 15), installing Turnip and starting the game
produced the message **"Unable to find compatible graphics device"** — the hook reported the custom driver
as active, but RT64 found NO Vulkan device.

Root cause (diagnosed from logcat): `nativeLibraryDir` was derived from `/proc/self/maps` by cutting the
line at the last `/` before `libmain.so`, which **kept the line's address columns in the path**:

```text
"744e00e000-744f0d4000 r-xp 00000000 fe:00 3397824  /data/app/.../lib/arm64"
└────────── /proc/self/maps columns ─────────────┘└─────── real dir ───────┘
```

adrenotools uses this value as the search path (`ld_library_path` / `default_library_path`) of the linker
namespace, which splits on `:` — and the device field (`fe:00`) contains **a colon**: both halves become
non-absolute paths and are dropped (`normalize_path - invalid input ... ignoring` in logcat). Consequence:
the *preload* of `libhook_impl.so` inside the driver namespace fails **silently** (`hook_impl.cpp` returns
`nullptr` without a log), no ICD is loaded and `vkEnumeratePhysicalDevices` returns 0 devices.

Fixes applied:

1. **Correct parsing** (`find_native_library_dir`): the pathname starts at the first `/` of the line; only
   file mappings ending exactly in `/libmain.so` are accepted (excluding embedded `base.apk!/...`), and the
   resulting directory is validated with `stat`, requiring the hooks (`libmain_hook.so`, `libhook_impl.so`)
   before any load.
2. **Pre-flight probe** (section 3.6): validates the driver empirically before the game — if it exposes no
   GPU it is dropped automatically (fallback to the system driver) and the installation is refused with a
   clear message.
3. **stderr → logcat** (section 3.7): `fprintf(stderr)` messages from plume/RT64 (e.g.
   `Unable to find devices that support Vulkan.`, `Missing required extension: ...`) now show up in logcat —
   before, they vanished into `/dev/null` in release builds, which made this diagnosis impossible in the
   first round.

## 2.2 Real bug in the field (round 2): the probe refused ANY driver in ~1 ms

With the parsing from 2.1 fixed, field testing revealed a second bug, this time in the **probe**
(section 3.6): right after installing the driver, the setup screen received `"error":"driver not loaded"`
within ~1 ms and rolled back the selection — for **any** driver, including valid Turnip builds.

Root cause: the probe is a **JNI call from the setup screen**, which runs **before the game's `main()`**.
The `filesDir` path used by native code (`androidport::internal_files_dir()`) is only filled by
`init_from_args()` with the argv injected by SDL (`nativeRunMain`) — which **had never run** at that point.
With an empty path, `files/driver/selected.txt` was never found, native code concluded "no driver selected"
and the probe returned "driver not loaded" without even trying to load the driver. Logcat gave it away:
`Driver installed` and `Driver probe` 1 ms apart — impossible for a linker namespace + dlopen +
`VkInstance` creation.

Fix (parity with the redahm-android port, validated on the same device):

1. The JNI `nativeProbeCustomDriver` now receives the paths from Java (`getFilesDir()` and
   `ApplicationInfo.nativeLibraryDir`) and injects them via `androidport::set_runtime_paths()` **before**
   any use. Since setup and game share the same process, the value also serves the load at game start.
2. `hookLibDir` is now **`nativeLibraryDir` from Java** — exactly the value the adrenotools documentation
   requires — with the `/proc/self/maps` scan kept only as a fallback.
3. Pre-validation with logs: `stat` of the exact file adrenotools will open
   (`customDriverDir + customDriverName`), with `errno`; without this, a corrupted installation became
   "driver not loaded" without any clue.
4. Flags in parity with redahm: only `ADRENOTOOLS_DRIVER_CUSTOM`. Turnip uses kgsl (`/dev/kgsl-3d0`) and
   does not need the `fopen` redirect; the extra hook (`libfile_redirect_hook.so`) in the driver namespace
   was a failure mode without benefit.

## 2.3 Real bug in the field (round 3): "Vulkan entry points missing in the driver"

User capture (logcat "when selecting the driver") after the 2.2 fix:

```text
custom driver: loading 'WN-Turnip-1.06-p Axxx' (lib=libvulkan_freedreno.so) from
  /data/user/0/.../files/driver/installed/wn-turnip-1.06-p_axxx.zip-mtfvxedt/ | flags=CUSTOM
custom driver: 'WN-Turnip-1.06-p Axxx' active (via adrenotools)
E vulkan  : invalid vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumeratePhysicalDevices") call
E vulkan  : invalid vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkGetPhysicalDeviceProperties") call
E vulkan  : invalid vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkDestroyInstance") call
custom driver: 'WN-Turnip-1.06-p Axxx' DROPPED — Vulkan entry points missing in the driver
```

This time loading (adrenotools) **worked** — it was the probe that dropped the freshly loaded driver. Two
clues close the diagnosis:

1. `adrenotools_open_libvulkan` **does not return the driver**; it returns a `dlopen_unique` of
   **`/system/lib64/libvulkan.so`** (a private copy of the system loader) with `libmain_hook.so` loaded
   `RTLD_GLOBAL` in the same namespace — the hook intercepts the `android_dlopen_ext` of the ICD
   enumeration and redirects `vulkan.*.so` to the custom driver. So the `vkGetInstanceProcAddr` obtained
   via `dlsym` is **the system loader's**.
2. By the Vulkan API contract, only **global** functions accept a `VK_NULL_HANDLE` instance
   (`vkCreateInstance`, `vkEnumerateInstance*`, `vkGetInstanceProcAddr`). For **instance-level** functions
   (`vkEnumeratePhysicalDevices`, `vkGetPhysicalDeviceProperties`, `vkDestroyInstance`) the loader
   **requires** a valid `VkInstance`; with `nullptr` it logs exactly the `invalid
   vkGetInstanceProcAddr(VK_NULL_HANDLE, ...)` errors above and returns `nullptr`.

The old probe requested **all** PFNs with `nullptr` **before** creating the instance — so it aborted while
looking up entry points and `vkCreateInstance` **never ran** (which is why no
`hook_android_dlopen_ext: loading custom driver:` hook log appears in the capture: the Turnip ICD was never
even loaded). A fully working driver would always have been dropped.

Fix in `run_probe_locked()` (`custom_driver.cpp`), in the order the Vulkan contract requires:

1. `vkCreateInstance` obtained with `nullptr` (global function) and called;
2. `vkEnumeratePhysicalDevices`, `vkGetPhysicalDeviceProperties` and `vkDestroyInstance` obtained **with
   the freshly created instance**;
3. instance destruction guaranteed on every error path.

Positive side effect: since the probe's `vkCreateInstance` now really runs, the Turnip ICD is already
loaded during installation — the probe now exercises exactly the path the game will use.

## 2.4 Real bug in the field (round 4): the original SIGSEGV was never "the driver's" — and the swapchain format choice

With the probe fixed (2.3), field testing confirmed that Turnip loaded completely —
`custom driver probe: device[0] = 'Turnip Adreno (TM) 619' (Vulkan API 1.3.354, vendor 0x5143)` — and that
the hook was re-applied at game start (`hook_impl: loading custom driver: .../libvulkan_freedreno.so`).
The game now initialised Vulkan with Turnip for real (Mesa shader compilation visible in logcat via the
`vendor.mesa.spirv.*` properties) — and crashed again, at the SAME offset as the original crash:

```text
W DK64Recomp-stderr: No compatible surface formats were found.
E vulkan  : VkGetPhysicalDeviceImageFormatProperties2 for AHB usage failed: -11
W DK64Recomp-stderr: vkCreateSwapchainKHR failed with error code 0xC4653600.
F libc    : Fatal signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x0 in tid (RT64 Present)
F DEBUG   : #00 pc 000000000002b2e4 /memfd:/system/lib64/libvulkan.so (deleted)
```

### 2.4.1 Full diagnosis (reconstructed from all logcats)

1. **The original crash was never a proprietary driver bug.** The offset `0x2b2e4` in
   `vulkan::driver::GetRefreshCycleDurationGOOGLE` belongs to the **framework loader** (the system
   `libvulkan.so` — adrenotools' `memfd` copy is the SAME binary loaded via `dlopen_unique`). It
   dereferences the swapchain handle passed in. With a **NULL swapchain** the result is always this SIGSEGV
   — with any ICD.
2. **Why was the swapchain NULL?** Because swapchain creation FAILED before, silently (without
   stderr→logcat there was no visibility): `No compatible surface formats were found` — plume did not find
   the format RT64 requested in the surface's format list.
3. **Why was the format not in the list?** RT64 requests `B8G8R8A8_UNORM` (`rt64_application.cpp`). The
   drivers on this device (proprietary **and** Turnip) expose the SDL window's native format —
   `R8G8B8A8_UNORM` — and RT64's recreation loop (`resize()`) then tried to create the swapchain with
   `pickedSurfaceFormat` still `{VK_FORMAT_UNDEFINED}` (upstream only picks the format in `initialize()`),
   failing in a loop in `vkCreateSwapchainKHR`.
4. **External confirmation:** the redahm-android port (same device, same Turnip zip, reported working by
   the user) picks the surface format dynamically and **prefers `R8G8B8A8_UNORM` on Android**
   (`vulkan_presenter.cpp`: `kFormat8888Primary = VK_FORMAT_R8G8B8A8_UNORM` in the `REX_PLATFORM_ANDROID`
   branch), with B8G8R8A8 as secondary. So the problem was the fixed format choice in upstream RT64/plume,
   not the driver.

### 2.4.2 Fixes in the plume patch

1. **Surface format fallback (Android)**: when the requested format (`B8G8R8A8_UNORM`) is not in the list,
   accept the first 32 bpp format with the same byte layout (`R8G8B8A8_UNORM` → `B8G8R8A8_UNORM` → SRGB
   variants). RGBA8 and BGRA8 have the same byte organisation: RT64's present pipeline (fixed to
   B8G8R8A8_UNORM upstream, see the TODO in `rt64_shader_library.cpp`) writes the components in the same
   byte order — no visible channel swap. If the list contains none of them, the error now logs the full
   list for diagnosis.
2. **`displayTiming = false` unconditionally on Android**: on Android the `VK_GOOGLE_display_timing`
   extension is **emulated by the framework loader**, which lists it as supported for any ICD — so
   "extension present" does not mean "implemented by the driver". With the previous guard (extension +
   custom driver active) RT64 enabled display timing with Turnip and called
   `vkGetRefreshCycleDurationGOOGLE` — which crashes in this device's loader when the swapchain is invalid
   (and is the same code that crashed in the original run). With `displayTiming` off, RT64 paces by SDL's
   refresh rate and the crash disappears; `dk64_adrenotools_custom_driver_active` is no longer referenced
   in plume (still exported by the native bridge).

## 2.5 Real bug in the field (round 5): the driver "disappears" after closing and reopening the app

Field report for the automatic restart: installing Turnip works (installation + probe OK), but **closing
and reopening the app** left the game on the system driver — the driver was "lost" or "did not load".
Three compounding root causes, all in the process lifecycle:

1. **The automatic restart NEVER fired (dead path, two bugs in a row).** `request_app_restart_after_delay`
   (main.cpp) calls `dk64::native_request_app_restart()` on a detached `std::thread` — a NATIVE thread
   **not attached to the JVM**. For it, `JavaVM::GetEnv` returns `JNI_EDETACHED` and `app_restart.cpp`
   dropped the request with only an ALOGW: the app promised "The app will now restart" and kept running
   with Vulkan already initialised by the previous driver. And even after attaching the thread (first fix),
   the dispatch used `FindClass("...MainActivity")` on that thread without Java frames — which resolves via
   the SYSTEM classloader and does NOT find app classes (the same finding as commit `cc41f96` in
   file_bridge). Fix: `AttachCurrentThread`/`DetachCurrentThread` in `native_request_app_restart` (same
   pattern as `call_java_request`), and `nativeRestartInit` (called in `onCreate`, with an app Java frame)
   now caches a GLOBAL reference to `MainActivity` + `handleNativeAppRestart()V` so the dispatch can use it
   from any thread (`dk64::cache_restart_entrypoints`).

2. **Reopening could end up in a second `SDL_main` in a DIRTY process.** `MainActivity.onDestroy` only
   killed the process when `isFinishing()`; system-initiated destruction (swipe in Recents — behaviour
   varies by OEM/version, "Don't keep activities", low-memory destruction) arrives with
   `isFinishing()==false` and the process survived with SDL dead and ALL native statics dirty (the driver's
   `g_state`, volk's table already bound to the previous driver, RT64/RmlUi context). Reopening ran a second
   `SDL_main` in that process: the fingerprint cache in `ensure_loaded_locked` short-circuited and Vulkan
   was already initialised with the old driver. Fix: UNCONDITIONAL `Process.killProcess` in `onDestroy`,
   BEFORE `super` (the SDLThread join in `super.onDestroy` could block the UI thread before the kill
   arrived, keeping the zombie). `configChanges` covers configuration recreations, so every `onDestroy` is a
   real end.

3. **Cold start depended on the `/proc/self/maps` fallback to find hookLibDir.**
   `androidport::native_library_dir()` was only filled through the JNI probe; on a normal start it was
   empty and `ensure_loaded_locked` fell back to the maps scan (fragile: any mapping anomaly broke the load
   on reopening, while the installation probe — which gets the path from Java — worked: the "works when
   installing, lost when reopening" pattern). Fix: a new JNI export `nativeSetRuntimePaths`
   (custom_driver.cpp) injects `ApplicationInfo.nativeLibraryDir` in `onCreate` via
   `androidport::set_native_library_dir` — a global NO other path writes at startup (no race with the
   SDLThread running `init_from_args` in parallel); the maps scan remains only as a safety net.

UX bonus: `handleNativeAppRestart` scheduled a +1.5 s alarm (`AlarmManager` + the launcher's
`PendingIntent`) BEFORE finishing, to reopen the app in a cold start with the new driver.
*Handheld fork: Android 10+ blocks that alarm as a background activity start, so the app stayed closed.
It now starts a transparent `RestartActivity` in its own process (`:restart`) before finishing, which waits
for the old process to end and reopens the app.*

## 3. Integration architecture

```text
Setup / GPU Driver menu                   custom_driver.cpp (native)              plume (patch)
┌───────────────────────────┐             ┌───────────────────────────┐           ┌──────────────────────────┐
│ SAF: open .zip            │             │ Reads filesDir/driver/    │           │ VulkanInterface ctor:    │
│ 2 passes over the zip:    │  selected.  │   selected.txt            │           │  dk64_adrenotools_get_   │
│  1) find/read meta.json   ├────────────>│ adrenotools_open_libvulkan├──────────>│  instance_proc_addr()    │
│  2) extract to installed/ │    .txt     │  → libvulkan handle       │ proc addr │  volkInitializeCustom(p) │
│ check libraryName/minApi  │             │  → dlsym vkGetInstance-   │  or NULL  │  volkLoadInstance(inst)  │
│ write selected.txt        │             │    ProcAddr               │           │ → ALL vk* via volk       │
└───────────────────────────┘             └───────────────────────────┘           └──────────────────────────┘
```

### 3.1 Java — selecting the .zip

- Buttons **"Install driver (.zip)..."** / **"Remove driver (back to system)"** + status of the active
  driver. *(In the original first version this was a separate SetupActivity with a manual START GAME button,
  so the screen stayed reachable on every launch even on a crashing device; today it is the "GPU Driver"
  entry of the game's launcher menu.)*
- **Accepted formats** (parity with redahm-android, which loads drivers fine on the moto g34 5G):
  1. **adrenotools** `.zip` (`meta.json` + `vulkan.ad*.so`): the first pass finds/reads `meta.json` (root
     or a subdirectory — its directory is the extraction root); the second pass extracts. Supported fields:
     `libraryName` (fallback `library`), `name`, `driverVersion`, `vendor`, `author`, `minApi` (checked
     against `Build.VERSION.SDK_INT`), `schemaVersion` (ignored).
  2. **Winlator/WN-Turnip** `.zip` (WITHOUT `meta.json`, libraries in subfolders): extracts all `*.so`
     **flattened into the root** of the installation (keeps the first copy of each basename) and detects the
     soname — preference: `libvulkan_freedreno.so` → `libvulkan_turnip.so` → `libvulkan.so.qualcomm` →
     `libvulkan*`/`vulkan.*` → any `.so` that is not Mesa's `libllvm-glnext`.
  3. A plain `.so`: copied as a single-file driver.
- Zip bomb limits: 128 entries, 512 MB uncompressed. **Zip slip** protection: rejects `..` and absolute
  paths and checks `canonicalPath`.
- Any validation failure → deletes the partial directory and reports an error.
- The selection is written to `filesDir/driver/selected.txt` as `KEY=VALUE` (`dir=`, `library=`, `name=`) —
  the same format native code reads; installing a new driver removes the previous ones; "Remove" deletes
  the selection, installations and caches.

### 3.2 Native — `android/native/compat/custom_driver.cpp`

- C bridge (`extern "C"`):
  - `dk64_adrenotools_get_instance_proc_addr()` — resolves the current selection and returns the driver's
    `vkGetInstanceProcAddr` via `dlsym(handle, "vkGetInstanceProcAddr")`;
  - `dk64_adrenotools_custom_driver_active()` — 1 if the custom driver is in use in this run (used by the
    display timing guard in plume).
- **Resolution by fingerprint**: the selection (`selected.txt`) is re-read on every query and compared with
  the fingerprint already resolved (`dir + library`); if it changed, everything is reloaded (including the
  probe). So switching drivers in the same process works — without this the old result would stay cached
  (the `call_once` of the first design).
- `hookLibDir` is **`ApplicationInfo.nativeLibraryDir` from Java** (JNI, `set_runtime_paths` —
  section 2.2), the exact value the adrenotools documentation requires; the fixed `/proc/self/maps` parsing
  (section 2.1) remains as a fallback. `tmpLibDir` (`driver/tmp`) is created under filesDir (only used on
  API < 29, where there is no memfd).
- `adrenotools_open_libvulkan` flags: **`ADRENOTOOLS_DRIVER_CUSTOM` only** (parity with redahm —
  section 2.2). Pre-validation with an `errno` log on the `stat` of the driver file before the call.
- Any failure (no selection, invalid directory, `adrenotools_open_libvulkan` returning null, empty `dlsym`,
  probe without GPU) → `NULL` and the game continues with the **system driver**. Logs in
  `adb logcat -s DK64Recomp`.

### 3.3 Renderer — patch `android/patches/plume-android.patch`

plume (RT64's Vulkan layer) uses **volk** with `VOLK_IMPLEMENTATION` (compiled inside `plume_vulkan.cpp`).
Three changes, all confined to `#if defined(__ANDROID__)`:

1. **Driver routing** — in the `VulkanInterface` constructor, if the bridge returns a proc addr we call
   `volkInitializeCustom(proc)`, which initialises volk's global table with Turnip's
   `vkGetInstanceProcAddr`; otherwise the default `volkInitialize()` (dlopen of the system `libvulkan.so`).
   Since `volkLoadInstance()` uses that table, **all** `vk*` calls in the binary (plume, RT64, imgui, VMA)
   go through the active driver.
2. **Surface through the active instance** — the `PLUME_SDL_VULKAN_ENABLED` path called
   `SDL_Vulkan_CreateSurface()`, which uses **SDL's own loader** (the system `libvulkan.so`) and would create
   a "cross-driver" *surface* incompatible with the Turnip instance. On Android we create the surface
   directly with `vkCreateAndroidSurfaceKHR` from volk's table, getting the `ANativeWindow*` via
   `SDL_GetWindowWMInfo` (`SDL_SYSWM_ANDROID`).
3. **`VK_GOOGLE_display_timing` guard** — originally `capabilities.displayTiming` was only enabled with a
   custom driver active; since round 4 (2.4.2) it is off unconditionally on Android, and RT64 paces by SDL's
   refresh rate.

### 3.4 Build — `android/app/CMakeLists.txt`

- `VK_NO_PROTOTYPES` defined **globally**: `vk*` calls no longer link against the NDK's `libvulkan.so` and
  become indirections through volk's pointers (defined in `plume_vulkan.o`). The explicit link of the NDK
  `vulkan` was removed on purpose — any call escaping the table would become a visible **link error**, never
  a silent mixed-driver bug.
- `add_subdirectory(lib/adrenotools)` (submodule) → static library `adrenotools` + shared hooks packaged in
  the APK; `custom_driver.cpp` is part of `MAIN_SOURCES` and links into `libmain.so`.

### 3.5 CI — `.github/workflows/build.yml`

- `lib/adrenotools` added to `git submodule update --init --recursive`.
- New step **"Check APK contents"**: fails the build if `libmain_hook.so`/`libhook_impl.so`/
  `libfile_redirect_hook.so` are not packaged (protects adrenotools' `nativeLibraryDir` requirement).

### 3.6 Driver validation (Vulkan probe)

Installing a Turnip build that **does not support the device's GPU** (e.g. `a7xx`/`a8xx` builds on an
Adreno 619/a6xx — the zips are per generation!) made the game fail much later with "Unable to find
compatible graphics device". Now the driver is **tested before it takes effect**:

1. `run_probe_locked()` (custom_driver.cpp) creates a `VkInstance` with the loaded driver's
   `vkGetInstanceProcAddr` (direct PFNs, without touching volk's global table) and calls
   `vkEnumeratePhysicalDevices` — exactly the path RT64 will take.
2. **0 devices** (or a failed `vkCreateInstance`) → the driver is **dropped** at game load (fallback to the
   system driver, with display timing off) and the installation is **refused** with a rollback of the
   selection.
3. Probe OK → the GPU name and API version appear in the UI ("installed and verified: Adreno (TM) 619
   (Vulkan 1.3.x)") and in logcat.

The JNI `nativeProbeCustomDriver(filesDir, nativeLibraryDir)` (same libmain.so as the game) receives the
paths from Java (bug of section 2.2) and returns the result as JSON:
`{"active":bool,"ok":bool,"devices":int,"device":str,"api":str,"error":str}`. The probe runs once per
selection (fingerprint), so installing another driver in the same session re-evaluates everything.

### 3.7 Diagnostics: stderr → logcat

plume/RT64 report video failures via `fprintf(stderr)` — invisible in logcat for release builds.
`androidport::redirect_stderr_to_logcat()` (android_paths.cpp, called at the start of `SDL_main`) creates
a `pipe`, points the process's `stderr` at it (`dup2`), and a reader thread forwards each line to logcat
under the tag **`DK64Recomp-stderr`** (level WARN). Failures such as
`Unable to find devices that support Vulkan.` / `Missing required extension: X` are then visible with
`adb logcat -s DK64Recomp-stderr`.

## 4. How to use (user summary)

1. Download a Turnip driver — e.g.:
   - releases of [K11MCH1/AdrenoToolsDrivers](https://github.com/K11MCH1/AdrenoToolsDrivers)
     (`Turnip_vX.Y.Z_R*.zip`; `Gmem`/`Sysmem` variants);
   - Winlator/WN-Turnip zips (e.g. `WN-Turnip-*.zip`) are accepted too — the app detects the soname even
     without `meta.json`.
   **Pick the folder for your GPU generation**: Adreno 6xx (e.g. the moto g34 5G's Adreno 619) →
   **a6xx** builds; Adreno 7xx → **a7xx**. Builds for another generation are refused automatically.
2. In the game menu: **GPU Driver → Install driver (.zip)...** → select the zip (or a driver `.so`). The app
   installs, loads and **tests** the driver: if it lists the GPU, it is ready; if it lists no Vulkan GPU,
   the installation is refused with instructions.
3. The app restarts; the driver takes effect on that start (native code reads the selection when
   `SDL_main` comes up).
4. To go back to the proprietary driver: **GPU Driver → Use system driver**.

Useful logs:

```bash
adb logcat -s DK64Recomp DK64Recomp-stderr hook_impl
```

Success: `custom driver: '...' active (via adrenotools)` followed by
`custom driver: probe OK — N Vulkan device(s), GPU '...'`. Refusal:
`custom driver: '...' DROPPED — probe exposed no Vulkan GPU (...)`. RT64/plume failures appear under the
`DK64Recomp-stderr` tag. *(The native log messages themselves are still in Portuguese.)*

### 4.1 Does the selection survive leaving/reopening the game?

Yes. The selection lives in `filesDir/driver/selected.txt` and the files in
`filesDir/driver/installed/<id>/` — both persist in the app's storage (they survive closing the app,
reopening it, and even APK updates). The menu shows the real state on every start and also signals when
the driver FILES have disappeared.

One lifecycle detail matters here and was fixed: leaving the game (Exit menu) only ended `SDL_main` and the
Activity — the Android PROCESS stayed alive (cached), and reopening the game ran a second `SDL_main` in the
SAME process with dirty runtime statics (`ultramodern::exited` still `true` — the main loop did not even
run; `game_status=Quit`; RT64/RmlUi context from the previous run). The symptom was "leaving the game and
entering again does not load / loses the driver". Today `MainActivity.onDestroy` (end of the game) ends the
process: **every relaunch starts clean** — statics reset and the driver reloaded from scratch from
`selected.txt` (`custom driver: loading '...'` in logcat on every start).

## 5. Known limitations

- **Adreno** on **arm64-v8a** only — Turnip does not exist for Mali/PowerVR; on other GPUs the system
  driver is used as usual.
- **Turnip zips are per GPU generation** (`vulkan.ad06xx.so` = a6xx, `vulkan.ad07xx.so` = a7xx,
  `vulkan.ad08xx.so` = a8xx; older builds used `libvulkan_freedreno.so`): installing the wrong generation
  no longer "breaks" the app — the probe refuses it — but it does not speed anything up either. Check your
  Adreno generation before downloading.
- Android 10+ recommended: below API 29 adrenotools needs the writable `tmpLibDir` (supported, but memfd is
  the preferred path).
- The Turnip variant matters: if a build shows graphical artefacts, try the latest `Gmem` or `Sysmem`
  variant.
- `ADRENOTOOLS_DRIVER_GPU_MAPPING_IMPORT` (GPU memory import) is not used — RT64 does not need it and it
  adds extra hook requirements.

## 6. Files involved

| File | Role |
|---|---|
| `lib/adrenotools` (submodule, bylaws/libadrenotools @ 8fae8ce) | Driver loading via hooks |
| `android/native/compat/custom_driver.h/.cpp` | Native bridge (selection → proc addr) |
| `android/patches/plume-android.patch` | volkInitializeCustom + own surface + displayTiming guard |
| `android/app/CMakeLists.txt` | `VK_NO_PROTOTYPES`, adrenotools, removal of the `vulkan` link |
| `android/app/src/main/java/…/GpuDriverInstaller.java` | Zip selection (SAF), extraction, validation, status |
| `android/app/build.gradle.kts` | `useLegacyPackaging = true` (hook requirement) |
| `.github/workflows/build.yml` | new submodule + hook check in the APK |
