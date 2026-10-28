# New Vegas on the howdeploy fork

Source preparation, 2026-10-28. Development branch: `main`. Upstream baseline:
[`1ac9b6cf`](https://github.com/autorunhq/autorun/commit/1ac9b6cf37b90f2d5783dd7ef389bebbe1c68877).
The FEX/Vulkan NRO has been cross-compiled for installation. This is not a
measured FPS improvement or a confirmed game launch.

## What comes from upstream

- FEX for x86/x64 and the alternative Box64 x86 runtime.
- Mesa 26 OpenGL and NVK Vulkan, with DXVK for New Vegas's Direct3D 9.
- Direct startup of `FalloutNV.exe`: the runtime recognises the hardware
  launcher check and patches that call in memory. It does not rewrite the EXE
  on the card. Unrecognised code is left alone.
- The pinned game-fixes index and its `falloutnv` profile: 44 game settings,
  Low/Medium/High/Ultra presets, resolution, shadows and draw distances, edited
  through the game's settings screen. Preferences are prepared before launch.
- SD caching, opt-in loading of mapped files on demand, shader-worker affinity,
  multichannel audio and the current DirectShow/component setup.

Two reviewed community changes are also backported with their original authors
and source commit IDs:

- [PR #86](https://github.com/autorunhq/autorun/pull/86): tags queued file
  descriptors with their client, avoiding another thread's channel when the
  matching descriptor is queued.
- [PR #90](https://github.com/autorunhq/autorun/pull/90): reclaims already-ended
  threads and waits up to 20 seconds for a free thread slot instead of failing
  immediately. The wait happens outside the memory-mapping lock and stops on
  shutdown. It does not raise the system's 96-thread limit; a permanent lack
  of slots can still fail. These PRs were open when imported, not upstream
  release guarantees or New Vegas performance measurements.

The fork retains Russian UI, authenticated FTP, independent session logs, a
bounded event log and downloadable diagnostic ZIPs. Windows NUL and local
keyboard/mouse hook support are carried forward to the new Wine protocol.
The New Vegas profile's setting labels and quality presets are translated.

## Build preparation

```sh
git clone --branch main https://github.com/howdeploy/autorun.git
cd autorun
git submodule update --init game-fixes horizon-dlls
sh build.sh
```

The default build uses FEX and the Mesa 26 SDK from the pinned switch-dev
image. `sh build.sh --x86` selects the alternative Box64-only runtime. The
packager honours `WINE_NX_LLVM_MINGW`, including the Linux toolchain selected
by `build.sh`. The build also prepares the upstream boot bundle; building it
does not install it on a console. Docker and toolchain downloads need network
access and disk space. The installation build uses the supported
`build-amd64-components.sh` path without a boot bundle; no boot payloads are
embedded in that NRO. Container Git trust is scoped to the mounted source and
its pinned Box64/FFmpeg directories.

## Migration from our TB4/RU15 installation

Preserve the existing `switch/wine` tree before a console trial, including
games, registry, configuration and `drive_c/users`. Keep a separate copy of
`drive_c/users/steamuser/Documents/My Games/FalloutNV`, especially `Saves` and
the INI files; also retain any older `users/wine` profile.

Use a complete package built from `main`. Replacing only a historical RU15 NRO
does not supply the new DLL layout or game profiles. Current Windows modules
come from the pinned `horizon-dlls` repository or the launcher's DLL manager.
The archive includes the game-fixes index; profiles can therefore be discovered
offline. Additional fix files and DLLs must already be on the card or downloaded.

Upstream Quick setup can offer forwarder, Hekate and kernel changes. Those are
a separate console operation; do not accept them as part of a source update.
First inspect the running firmware, Atmosphere, memory mode and existing boot
configuration. The GOG New Vegas executable relocates; it is not a reason by
itself to install the legacy 32-bit forwarder or patch the kernel.

Add the game's actual `FalloutNV.exe`, not `FalloutNVLauncher.exe`. Use the
game's own installed files. A store client that must remain running is still
incompatible with Autorun's single-process model. NVSE/xNVSE and its plugins
have not been verified here; establish the base game first.

## Main-menu music investigation, 2026-10-28

The captured 13:04 launch uses our `main-nv1` runtime, FEX and DXVK in a
39-bit address space. It opens `Data/Music/Special/MainTitle.mp3`, then fails
to create the DirectShow MPEG-I Stream Splitter with `0x8007000e`.
`winegstreamer` maps failure to create its Unix parser to `E_OUTOFMEMORY`;
Horizon implements its decoder transforms but not that parser. The error is
therefore not evidence that the entire console ran out of RAM. Runtime
samples still show about 2 GB of free heap and advancing display submissions.
They do not establish that the main menu became usable.

Quartz now falls back to a native Layer III audio splitter when GStreamer
cannot create its filter, following the existing native AVI fallback. It
reuses the AVI splitter's sample delivery, allocator and seeking machinery,
indexes complete MP3 frames, handles ID3v2/v1 tags, and accepts MPEG-1/2/2.5
Layer III with fixed sample rate/channel layout and variable bitrate. MPEG
header tables and lengths follow the older
[Wine MPEG splitter](https://github.com/wine-mirror/wine/blob/27da4fa4498c3b6e0e46c72a9cdd13b620726e92/dlls/quartz/mpegsplit.c).
It does not provide MPEG video or program-stream demuxing. Working GStreamer
and AVI paths retain their behavior.

This is a source correction for a confirmed media compatibility gap, not a
verified fix for the reported splash-screen stall. It changes `quartz.dll`;
replacing only the runtime NRO cannot install it. The installed DLL manifest
still describes the pinned upstream payload. Any console trial needs the
rebuilt x86 Quartz DLL and a retained original, without silently overwriting
that manifest. The x86 Quartz DLL has been cross-compiled with the pinned LLVM
toolchain. The current audio bundle resolves Quartz's 187 named imports against
14 i386 dependency modules, and DirectSound's 82 imports against five modules.
A local installation keeps its provenance in
`horizon-dlls/local-falloutnv-mp3.json`, separately from the upstream manifest;
a subsequent Windows DLL update can replace the local fix. The 14:29 console
trial and subsequent host API tests are described below. Verbose traces were
enabled in the captured session, which
reached the 64 MiB log limit; use a separate trace-disabled comparison before
drawing performance conclusions.

## Post-patch session and audio source review, 2026-10-28

The user's 14:29 session loads the system quartz.dll successfully, with verbose
traces off. An FTP readback matches the custom MP3 payload's SHA-256; the earlier
MPEG splitter creation error is absent. This does not establish audible music
or an interactive main menu. Later the display counter stays at 10563 and file
reads stop; the stall watcher records 120 seconds without a new frame. During
the final four CPU intervals, `wine_dsound_mixer` uses 99.3% of a core while the
main thread waits. About 1804 MiB of heap remains free. The native snapshots do
not resolve the guest function in FEX's translated code, so the exact cause of
that loop remains unconfirmed.

The audio-path source review fixes these independent defects together:

- DirectSound published and freed its notification array without taking the
  buffer lock used by the mixer. It now sorts a replacement privately, swaps
  pointer and count under the exclusive lock, and preserves the previous array
  if allocation fails. Oversized counts cannot overflow its allocation size
  or signed count field.
- The native AVI/MP3 splitter now begins a downstream flush before waiting for
  a delivery lock or joining a streaming thread. Decommitting its own allocator
  alone cannot unblock a decoder's Receive. All streams are unblocked before
  any thread is joined, and each flush ends after the corresponding thread exits.
- Allocator-commit and streaming-thread creation failures now propagate to the
  graph and clean up streams already started instead of reporting success.
- DirectSound skips empty sample reads before circular-buffer arithmetic,
  preserves scratch allocations when realloc fails, and uses silence instead
  of dereferencing a failed allocation or mixing stale temporary data.
- The DirectShow audio renderer now joins its old worker and releases queued
  samples on flush before resetting EOS and starting a new worker. Previously
  the replacement worker could observe the old EOS flag and exit immediately,
  or keep playing pre-seek samples. Stop uses the same worker cleanup; destroying
  the renderer also frees the queue allocation. EOS received during a flush is
  rejected, and pending completion is suppressed when flushing.
- Dynamic audio formats are freed after inspection and copied before replacing
  the current format, so failed allocations leave the current format intact.
  Truncated format blocks and invalid sample lengths are rejected. Failed
  DirectSound position, lock, unlock, clock and flush-wait calls no longer feed
  uninitialized values into rendering. Resetting 8-bit PCM uses unsigned silence
  (0x80).

Audio format validation keeps both the 16-byte PCMWAVEFORMAT and the 18-byte
WAVEFORMATEX form of PCM. It never reads or compares cbSize for the short PCM
header. Non-PCM formats still require the full header and their declared extra
bytes. This matches the existing DirectSound format copier and the Windows
[PCM cbSize convention](https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatex).

The follow-up x86 DLLs were cross-compiled with the pinned LLVM toolchain and
their named imports checked against the pinned runtime modules. The upstream
main head was checked through Git and remains `1ac9b6cf`.

Host checks under Wine 11.17, in an isolated prefix with a temporary silent audio
sink, cover the actual new DLL code:

- `quartz:dsoundrender`: 1004 checks, no failures or skips; 15 existing expected
  failures remain marked todo. Connection, flushing and EOS tests run with
  both the short PCM and full WAVEFORMATEX headers. The fixed EOS-during-flush
  expectation is now a normal assertion.
- `dsound:dsound`: 30074 checks, no failures or skips; 76 existing expected
  failures remain marked todo. A worker repeatedly replaces and clears the
  notification array while the mixer is playing.
- [Mixer allocation regression](../horizon-wine/tests/dsound_mixer.c): the real
  mixer implementation handles zero input and forced realloc failures while
  retaining old allocations, emitting silence and advancing the resampler phase.
- [Native MP3 graph regression](../horizon-wine/tests/native_mp3_graph.c): the
  user's GOG MainTitle.mp3 reports 125.352 seconds and completes three short
  segments, including seek after EOS and a stop/restart. GStreamer is disabled
  so the graph uses the native splitter; the game itself is not launched.

Only the DOS-header builtin marker of the host-test DLL copies is changed so
Linux Wine will load them as native modules; the installed payload retains the
original marker. Code and other payload bytes are identical. These host tests
do not exercise Horizon/FEX, NVK or the Horizon FFmpeg decoder bridge. Allocator
commit and thread-creation failures have been reviewed but not fault-injected.
The reported console stall remains unverified until a fresh game trial.
`horizon-wine/check-audio.sh` tests the separate audout backend and is not a
substitute for these DLL checks.

The verified x86 audio bundle was installed over Autorun FTP on 2026-10-28 at
15:44 MSK. Both system syswow64 DLLs were staged, downloaded for SHA-256
verification, activated and read back again. Previous versions remain beside
them with the suffix `.before-fnv-audio-20261028-154410`, and are also backed up
on the PC. The upstream DLL manifest and the per-game verbose=0 setting were
verified unchanged; the local patch provenance record was updated. Restart the
Autorun process before the console trial so the new DLLs are loaded.

## First comparison, when console testing is requested

Start with FEX and DXVK, frame generation off, verbose traces off, the Low
preset, anti-aliasing off and 1280x720. These are proposed comparison settings,
not a community-certified performance preset. Keep clocks fixed and record
CPU/GPU/RAM clocks, console model, runtime and renderer versions.

Use one save and repeatable routes inside Doc Mitchell's house and outdoors
in Goodsprings. Record loading separately from play. Compare 720p with 960x540
before deciding whether pixel work is limiting performance; an unchanged frame
rate alone does not identify the precise CPU bottleneck. Compare FEX with Box64
as a separate run. Leave the checked RGSS Box64 build off.

Keep raw session logs and record average FPS, 1% low, frame-time spikes,
loading time, memory use and crashes. Runtime frame counters include loading
and menus; total frames divided by the whole runtime is not gameplay FPS.
Use a separate profiler run for `[THREADS]`, `[SERVER]` and `[PROF]`, because
diagnostics can change performance. Test sound, controls and save/load too.

## Evidence and limits

- [Upstream New Vegas startup change](https://github.com/autorunhq/autorun/commit/1d763c1c).
- [Pinned New Vegas settings profile](https://github.com/autorunhq/autorun-game-fixes/blob/f9aad4c44c2c555fb71348b1bfd8857a5e5475ca/profiles/falloutnv.json).
- [Community report, 2026-10-14](https://t.me/NintendoHints/978): an observer
  estimates 30–40 FPS from [this 2026-10-11 video](https://www.youtube.com/watch?v=qSHFxX9HZrQ).
  The video's description does not specify clocks, settings or runtime version.
  This is not a reproducible benchmark or a guarantee for this fork.

The FEX/DXVK/VKD3D runtime was cross-compiled in the pinned switch-dev image
on 2026-10-28 while preparing its installation. The Linux package verifier
uses the same `WINE_NX_LLVM_MINGW` toolchain as the build. Existing upstream
tests are retained; the targeted host DLL checks above have been run. UI checks
and console game launches have not been run by the agent. Interactive gameplay
and performance remain unverified.
