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
The console log confirmed that NUL opens successfully and the game reaches Ruby
script execution. Successful gameplay is still unconfirmed.

## Thread-local Windows hooks

Revision 4 implements thread-local in-context hook registration, chain lookup,
`CallNextHookEx` and unhooking in the Horizon server. The original Wine client
dispatches keyboard/mouse callbacks and honors their return values. Shared
queue hook counts are updated, registrations run newest first, and removal
during a callback keeps the old handle alive until the outermost chain ends.
Thread termination frees its hooks and invalidates their user handles.

This covers the local `WH_KEYBOARD` registration in F1AltEnterF12.dll and the
local `WH_MOUSE` registration in LonaMouseWheel.dll. Their DLLs and Ruby code are
not patched or disabled. Global, low-level, journaling and out-of-context hooks
remain unsupported and return an error. Other-process hook injection is not
provided. The protocol layouts are checked at compile time against Wine's
server header.

The F1/F12 callback also calls `SetKeyboardState` on keyboard events. Revision 4
adds its server request using Horizon's existing shared input state and Wine's
desktop-state snapshot rules: synchronous overrides survive until the physical
key changes; asynchronous state is changed only when explicitly requested.
This retains Horizon's existing shared-input model rather than introducing
separate input objects for each thread. `[HZKBD] set_key_state` records the first
16 calls and any errors, without dumping the keyboard-state array.

`[HZHOOK]` records registration/removal results and the first 16 server-side
callback deliveries per hook. A delivery log is not proof that the guest
callback executed successfully. Compilation is the only validation performed;
actual input filtering, callback chaining and game startup need a console run.
The [SetWindowsHookExW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowshookexw)
and [CallNextHookEx](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-callnexthookex)
contracts, together with the pinned Wine `server/hook.c` and `dlls/win32u/hook.c`,
are the implementation references.

## Revision 7: distinguish display handoff and presentation

The user's 500-second RU6 run reached Main and early DataManager, with
successful keyboard callback returns. No visible menu was established.
Aggregate frames stopped at 975; drawable swaps stayed at zero during imports.

Revision 7 retains NXGL/NXCOMP in events, records entry/return for the first four
swaps and power-of-two EGL failures, and reports swap_attempts/swap_failures.
gl_frames now counts only successful eglSwapBuffers returns. Success does not
prove that the buffers contain the intended game image.

The paired LonaRPG working-copy patch explicitly shows its loading sprite,
requests the initial transition and resets timing before infrequent loading
updates. It adds bounded DataManager, Mouse/Bitmap and first-scene markers.
The native RGSS DLL, mods, saves and HDD original are retained. This is a
candidate fix for the invisible loading screen, not a claim of compatibility.

RU7 compiled and was installed with SHA-256 readback. Loading-screen, main-menu
and Joy-Con checks are pending. The agent ran no tests or game sessions.

### Checked dynarec comparison build

`-DWINE_NX_BOX64_CHECK_CODE=ON` requires `WINE_NX_BOX64_DYNAREC=ON` and builds
`howdeploy-tb4-ru-dynarec-checked-7`. Its generated, pinned Box64 source marks
every translated block `always_test=1`, retaining checked jump-table entries
instead of making them clean after the first successful hash. The build forces
CALLRET=0, BIGBLOCK=0 and FORWARD=0 after loading sidecar settings, and logs the
effective settings. Defaults and other builds retain their existing behavior.

This targets RGSS code modified without a memory-protection or instruction-cache
notification. It is a diagnostic comparison, not complete SMC support: code
changed inside an executing block can still escape an entry-time check. It also
adds hash/dispatch overhead. The checked NRO was installed at the user's request
after preserving the interpreter RU7 result. An accidental .bak mouse-script
import was also removed, so a behavior change cannot be attributed to the CPU
backend alone. No game launch or tests have validated this variant yet.

## Revision 6: restore handled memory faults before logging

The RU5 console run confirmed the quiet profile, but its event stream stopped
after about 80 seconds while the user kept the black window open for two to five
minutes. The last records were permission-write faults; there was no fatal
exception verdict, Ruby marker or exit. This does not establish why logging or
execution stopped, nor prove that the compositor caused the black window.

The Horizon exception handler previously formatted and synchronously flushed
two fault records before calling `virtual_handle_fault`, even for recoverable
write-watch faults. Revision 6 follows the pinned Wine ARM64 signal handler:
attempt virtual-memory repair first, count successful repairs without stdio,
and retain the full diagnostic/guest-exception path for unresolved faults.
`faults_handled` in the existing heartbeat reports these repairs. This reduces
exception-context file I/O; its effect on this game's hang remains unverified.

The independent stall watcher now repeats its stack snapshots every thirty
seconds after the initial snapshots, instead of becoming silent after the
first twenty seconds of slow startup. No CPU backend, game script or graphics
stack change accompanies this revision. Compilation is the only execution
validation performed by the agent; the game remains for the user's check.

## Revision 5: durable diagnostic events

Alongside the capped detailed session log, revision 5 writes
`Game-…log.events.log`. It keeps the newest two 1 MiB event segments; the older
segment ends in `.previous.log`. Each segment contains its own build/session
header and events have monotonic millisecond timestamps. Rotation discards older
event history; this is bounded retention, not an unlimited crash archive.
Detailed syscall output cannot consume this event budget. The current launcher
has `autorun_events.log`; game event files survive reopening the launcher.

All three open log files are flushed by the existing 200 ms flusher. Critical
events and Ruby failures request an immediate flush. Card errors, process death
inside a write and bounded-lock drops can still lose data. `[LOG]` records the
event path or failure to open it.

`[PROGRESS]` is emitted every ten seconds even without new reads or frames, in
both verbose and quiet modes. `[HZUNIMPL]` records the first unsupported server
request and power-of-two repetition counts without requiring verbose traces.
Hook registration and keyboard-state markers also survive quiet runs.
`[NXHOOK] enter/return` records the first sixteen callback attempts per hook type
and unsuccessful returns, including NTSTATUS, reply size and result. It does not
record the keyboard-state array. Registration/delivery alone is still not proof
that a guest callback returned successfully.

ZIP reports preserve the first 4 MiB and the newest tail of oversized logs,
within the existing 16 MiB per-file budget, with an omission marker and exact
byte counts in `report.txt`. The exporter build is labeled separately from the
`[BUILD]` belonging to each retained game session.

### LonaRPG script diagnostics

`wine-nx-probe/tools/lonarpg-diagnostics/` provides an RGSS3/Ruby-1.9-compatible
logger and a host-side data repacker for the specifically audited 0.9.5.0.4
bootstrap. It records `[RUBY] BEGIN/END/FAIL/TRACE`, elapsed wall-clock script time
and up to 32 backtrace entries. Native event timestamps provide the independent
monotonic timeline. The bootstrap starts logging before F1/F12 initialization;
the instrumented external loader records exceptions before its existing modal
dialog. Its rescue/continue behavior and successful return value are retained.

The runtime retains these tagged stdout records beyond the ordinary 2,000-line
stdout forwarding budget. No RGSS DLL patch, disabled mod, new graphics backend
or CPU backend change is part of this diagnostic package.

The repacker verifies the original bootstrap SHA-256, modifies only known code
anchors, writes the patched binary and readable `LoadUp.rb` into a new output
directory, and verifies serialization by decoding data without evaluation.
The actual game, callbacks and ZIP export have not been executed by the agent;
only compilation, static review and data/hash validation were performed.

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
file is larger, its head and tail are included and recorded in `report.txt`. That text also
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
