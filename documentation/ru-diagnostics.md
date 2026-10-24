# Russian UI and Wi-Fi diagnostics

This fork starts at upstream `test-build-4`, commit
`9b7170505f9084fee0ffe1a237b08557a6ab2868`. It keeps the upstream launcher layout,
controller navigation and runtime data format. The existing Home, Library,
Settings and per-game screens are the UI reference; no redesign is intended.

## Install over an existing TB4 installation

These binaries are **runtime NROs**, not a complete Wine distribution. Keep the
TB4 prefix, DLLs and game files. To use the existing forwarder, replace
`switch/wine/wine-nx-runtime.nro` with the selected fork NRO under that exact name.
An additional NRO can instead be opened separately, but an existing 32-bit
forwarder can still send games back to its configured original path. Check the
runtime build and address-space size in the log. No firmware, kernel or partition
changes are needed.

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

## Windows NUL device

Revision 3 maps the Windows names `\\??\\NUL`, `\\DosDevices\\NUL` and
`\\Device\\Null` to an in-memory Horizon character device. Reads return EOF,
writes discard data, and seek returns zero; no backing file is created on SD.
Each open has independent access flags. The server exposes a character-device
descriptor and `FILE_DEVICE_NULL` metadata instead of treating NUL as a missing
file in the Wine prefix. `[HZNULL]` in the Horizon trace records open results.

This addresses an observed failed `CreateFileW("NUL")` during RGSS initialization.
Whether it resolves the subsequent game crash still needs a console run.

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
3. Select **Create report and open FTP** in Diagnostics, then **Start FTP**.
4. Connect a normal FTP client to the Switch IP and saved port (default **5000**)
   with your saved username and password. Download the newest ZIP from `/reports`.
   No URL token needs to be copied. The former temporary HTTP download is replaced
   by this FTP flow.

## Transfer games, settings and logs

Open **Settings → System → FTP server** / **Настройки → Система → FTP-сервер**.
Configure a username, password and port once, then select **Start FTP**. The
account is saved privately in `switch/wine/config/ftp.txt`; anonymous access is
disabled and FTP commands/passwords are not written to the diagnostic log.
Use passive FTP mode, as used by standard clients and Python's `ftplib`.

FTP `/` is `switch/wine`, not the SD root. Upload games into `/drive_c`, their
sidecar settings beside the executables, global settings into `/config`, and
download logs from `/logs` or ZIP reports from `/reports`. File transfers,
directory creation, rename, removal and resuming transfers use the embedded
[ftpsrv](../wine-nx-probe/third_party/ftpsrv/README.md) implementation.
Device paths, parent traversal and symlinks cannot select files outside this
root. Active and passive data connections are restricted to the control peer.

Keep the server screen open until transfers finish; **B** or **Stop FTP** closes
the server and disconnects clients. The server is not running during gameplay.
Launcher and per-game settings are reloaded afterward; registered library entries
are reloaded too. Add newly uploaded executables using **+ → Add game**. Options
outside the launcher's live settings may still require restarting Autorun.
Wait for a transfer to finish before closing the screen or launching a game.

No Sphaira transition, SSH, external upload or public cloud is involved. LAN
isolation or routing rules can still prevent connectivity; this feature does not
require disabling a PC VPN. Normal FTP does not encrypt traffic; use it on your
local network.

The completed ZIP also remains in `switch/wine/reports/`. Reports include up to
64 newest `.log` files plus eligible `stdout.txt` / `stderr.txt` from `logs/`,
limited to 16 MiB per file and 64 MiB of uncompressed log data per report. If a
file is larger, its tail is included and recorded in `report.txt`. That text also
contains the runtime build, report time, UI language, global trace/profiler
settings, address-space size, emuMMC status and skipped-file count.

Game files, saves, stdin and raw settings files are not collected. **Log content
is not redacted:** paths, arguments and diagnostic output may contain private
information. Review it before posting it publicly. A failed export can leave a
`.partial` file; only completed `.zip` files should be downloaded as reports.

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
to cover Cyrillic rendering, language persistence, FTP authentication, transfers,
root confinement, config reload, server shutdown and the game's behavior.
