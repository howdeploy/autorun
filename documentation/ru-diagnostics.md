# Russian UI and Wi-Fi diagnostics

This fork starts at upstream `test-build-4`, commit
`9b7170505f9084fee0ffe1a237b08557a6ab2868`. It keeps the upstream launcher layout,
controller navigation and runtime data format. The existing Home, Library,
Settings and per-game screens are the UI reference; no redesign is intended.

## Install alongside an existing TB4 installation

These binaries are **additional runtime NROs**, not a complete Wine distribution.
Keep the original `switch/wine/wine-nx-runtime.nro` and the rest of the TB4
installation. Copy the new NRO into `switch/wine/` under its supplied name and
open that file through a homebrew launcher. It uses the existing `switch/wine`
prefix, library and game files. Runtime settings remain shared with the original
app; back them up before experimenting. No firmware, kernel or partition changes
are needed.

- `autorun-ru-interpreter.nro`: Box64 interpreter, intended to investigate failures
  involving translated or self-modifying code. It is expected to be slow.
- `autorun-ru-dynarec.nro`, when provided: Box64 ARM64 dynarec build.

Both local build variants use stock devkitPro Mesa 20.1/OpenGL. They do **not**
include the Mesa 26/NVK Vulkan path from the official TB4 package; DXVK, VKD3D and
LSFG Vulkan features are unavailable in these binaries. They are not replacements
for the official runtime's graphics support. Building from the source tag also
does not reproduce the exact official TB4 binary revision.

No successful LonaRPG launch is claimed. The interpreter is a diagnostic option,
not a confirmed compatibility fix.

## Language

Russian is the initial language. Change it immediately under
**Settings → System → Language** / **Настройки → Система → Язык интерфейса**.
The choice is saved as `language=ru` or `language=en` in `launcher.txt`.
UI labels, help, controls and common messages are translated. Program names,
filenames, diagnostic logs, component credits and downloaded release notes keep
their original text. Strings without a translation fall back to English.

## Collect and download a report

1. Enable **Verbose traces** and/or **Profiler** under
   **Settings → System → Diagnostics** before launching the game. These are the
   global defaults; a game's own settings take precedence. Verbose logging can
   reduce performance, so leave it off for normal play.
2. Launch the game. After it ends, return to Autorun. If it crashes the runtime,
   reopen this NRO; the separate game-session log survives the restart.
3. Select **Download report over Wi-Fi** in Diagnostics.
4. Open the displayed HTTP URL in a browser on a PC on the same LAN. Keep the
   sharing screen open until the download finishes. Press **B** or choose
   **Stop sharing** to close the server. No Sphaira transition is required.

The listener is started only for this screen. Its port is assigned by the OS,
and each session has a random URL token. Only an exact GET of that report is
served; the URL cannot browse the SD card or select another file. No SSH, shell,
external upload or public cloud is involved. Anyone who obtains the URL while
the screen is open can download the report. LAN isolation or routing rules can
still prevent connectivity; this feature does not require disabling a PC VPN.

The completed ZIP also remains in `switch/wine/reports/`. Reports include up to
64 newest `.log` files plus eligible `stdout.txt` / `stderr.txt` from `logs/`,
limited to 16 MiB per file and 64 MiB of uncompressed log data per report. If a
file is larger, its tail is included and recorded in `report.txt`. That text also
contains the runtime build, report time, UI language, global trace/profiler
settings, address-space size, emuMMC status and skipped-file count.

Game files, saves, stdin and raw settings files are not collected. **Log content
is not redacted:** paths, arguments and diagnostic output may contain private
information. Review it before posting it publicly. A failed export can leave a
`.partial` file; incomplete reports are never served.

## Session logs

`autorun_runtime.log` still describes the current runtime invocation. Each game
launch additionally gets a file such as
`Game-1234abcd-20261024-180000-12345678_verbose_profiler.log`. The path hash
separates executables with the same basename, and time/tick suffixes keep earlier
sessions. `[SESSION]` records the target, trace/profiler flags and log size limit;
existing `[PROF]` output supplies performance samples.

Runtime and session log writers stop at approximately 64 MiB per file, with an
omission marker. This limit does not apply to independently written stdout or
stderr files. Old session logs and report ZIPs are retained, so storage use grows;
remove reports or old logs manually when no longer needed.

The updater checks **howdeploy/autorun**, never the upstream repository. These
diagnostic NRO releases are prereleases and are installed manually. The updater
continues to require a compatible full-package ZIP with a SHA-256 digest; a
standalone NRO is not an updater package.

## Build

The development build used the official image
`devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282`
on an x86_64 host. Its Switch libraries already include SDL2, SDL2_ttf, curl,
minizip and zstd. Install host `bison` and `flex` inside the build container for
Wine's IDL header generator. Mount this repository at `/work`.

Inside that container:

```sh
cd /work
sh wine-nx-probe/tools/bootstrap-box64-core.sh
mkdir -p wine-nx-probe/build-host-tools
cd wine-nx-probe/build-host-tools
/work/configure --enable-win64 --without-x --without-wayland --without-freetype --disable-tests
make -j4 tools/widl/widl
make -j4 include/all
cd /work
cmake -S wine-nx-probe -B wine-nx-probe/build-ru-interpreter -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/work/wine-nx-probe/cmake/switch-devkitA64.cmake \
  -DWINE_NX_PE_BUILD_DIR=/work/wine-nx-probe/build-host-tools \
  -DWINE_NX_BOX64_INTERPRETER=ON -DWINE_NX_BOX64_DYNAREC=OFF \
  -DWINE_NX_STOCK_MESA=ON -DCMAKE_BUILD_TYPE=Release
cmake --build wine-nx-probe/build-ru-interpreter --target wine-nx-runtime-nro -j4
```

The result is `wine-nx-probe/build-ru-interpreter/wine-nx-runtime.nro`.
For a dynarec variant use `build-ru-dynarec` and set
`WINE_NX_BOX64_DYNAREC=ON`, retaining `WINE_NX_BOX64_INTERPRETER=ON` for fallback.
All other arguments stay the same. PE DLLs are supplied by the existing TB4
installation; these commands build the native runtime only.

Validation for this change is compilation only. No host tests, GUI checks or
Switch runtime tests were run, at the user's request. Console checks still need
to cover Cyrillic rendering, language persistence, ZIP contents, LAN download,
server shutdown and the game's behavior.
