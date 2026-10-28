# Field notes: LonaRPG and Fallout New Vegas

Snapshot assembled on 2026-10-28 from the owner's Switch Wiki, source trees,
dated logs/analysis, NROs and deployment receipts. These are experience reports,
not universal settings. Raw private logs, games and the personal Wiki are not
distributed with this skill. Public code paths are relative to howdeploy/autorun;
the extracted measurements below identify their original sessions.

## Current evidence, including the owner's latest correction

| Route | Established outcome | Still unknown |
|---|---|---|
| LonaRPG 0.10.8.31, our standalone mkxp-z/RetroArch port | Owner played the game and completed the tutorial on preceding custom builds; reconfirmed that the custom engine worked on 2026-10-28 | Complete acceptance of stability-1, all menus/mods, save/reload and long-session performance |
| LonaRPG through the new Autorun mkxp module | Both NROs built, entry-point branch inspected, installed by FTP with SHA-256 readback; native library entry added | Owner explicitly had **not tested the new build** as of the latest 2026-10-28 report; handoff, return and gameplay through this integration remain unverified |
| Fallout New Vegas GOG, owner's current setup | Owner reported on 2026-10-28 that the game works and they ran around during the introduction | No new run/log/build hash was supplied with that report; do not attach it to a precise NRO or infer FPS, tested audio/save behavior, full-game compatibility or long-session stability |

The Fallout report supersedes the earlier overall "gameplay unverified" status.
It does not invalidate the older stalled sessions or prove which individual
audio/runtime change was causal. Owner-reported gameplay is evidence and must
be recorded even when no new log is available.

## Lona: narrow runtime improvements versus a working game

The early path used x86 RGSS through Wine/Box64. Real fixes included Windows
`NUL`, x86 CRT consistency, keyboard-state/hooks and callback chaining.
Recoverable memory faults had to be handled before synchronous SD diagnostics.
These fixes removed individual barriers; none alone established a usable title.

An experimental 11-byte RGSS patch produced the original runtime's
`File corrupted!` response. The original DLL was restored. Smaller Box64 blocks
did not fix that early failure. Interpreter mode was useful for diagnosis,
not a guaranteed fast or semantically complete baseline.

### The hidden presentation boundary

RGSS could import scripts and create service GL windows while actually drawing
through GDI/ddraw. Giving the screen to a GL surface at creation stopped the
compositor before a real game present. Zero drawable swaps, a frozen/black
screen and continuing Ruby work had to be explained together.

The first offscreen correction regressed make-current with an empty target;
later pbuffer/surfaceless handling and handoff on a visible window's first
SwapBuffers addressed that boundary. A growing compositor counter still did
not prove the intended game image. Inspect `winnx_opengl.c` and compositor
ownership, rather than diagnosing the final Ruby log message as the cause.

### RU15 removed a specific measured expense

`horizon-wine/source/wow64_box64_dynarec.c` retains the historical phased
experiment. Ending the checked phase invalidates all guest translations,
not only RGSS's: other DLL blocks otherwise keep entry-time validation.
The entry section remains interpreted. This is not complete page-based SMC.

Sanitized extract from session
`Game-feaa01a1-20261025-082445-a912ba10_profiler`:

| Observation | Recorded value | What it establishes |
|---|---|---|
| Live BUILD | `howdeploy-tb4-ru-dynarec-phased-15` | This was RU15, unlike an earlier mislabeled/stale-process run |
| `block_tests`, 26–546 seconds | 15,113,686 at both endpoints; delta 0 over 520 s | Repeated checked-entry work ceased in this interval |
| `translator_locks` | 15,177,241 → 15,178,647; delta 1,406, about 2.70/s | The old very high dispatch/lock expense was removed |
| Last published JSON-cache marker | 2,500 hits / 1 miss | The cache was used; this sparse marker is not a final total |
| Drawable swaps | 0 in the recorded late heartbeat | The run did not establish a usable title image |

Earlier RU14 analysis observed roughly a million checks/locks per second.
The runs were not a clean repeated A/B to the same menu checkpoint. Do not
advertise a corresponding FPS multiplier or say RU15 accomplished nothing.
Full SMC needs executable-page/write-watch/alias handling, invalidation across
block boundaries, active-block lifetime and lock ordering; disabling validation
or the global lock is not a generic optimization for another game.

### PC-side prepared JSON

The earlier Wine-path preparation produced a 15,742,675-byte Marshal cache from
2,662 JSON files / 5,238 unique content-and-encoding variants. Historical host
checks compared 5,238 variants; console cache-hit markers confirmed actual use.
The lookup guarded the parser version, encoding, length and CRC followed by
full byte comparison. Every hit restored fresh mutable objects; stale/missing
data fell back to the original parser. Original game files were retained.

This reduced parsing work, not I/O, every script import, SMC or all loading.
It was not automatically present in the later native mkxp package. Other
proposed map/NPC caches and whole-game AOT were not implemented results.

## Lona: use an existing native implementation, then adapt boundaries

The useful upstream was `white-axe/mkxp-z` **libretro**, not merely the default
branch of a similarly named Switch fork. A historical CI matrix failed overall
while the stage1, Switch core, standard frontend and bootstrap jobs succeeded.
Those jobs supplied actual NRO/ELF artifacts. This avoided rewriting RGSS.

The native frontend is ARM64; this branch's Ruby environment goes through
WASI/WASM/wasm2c. Private x86 Bitmap pointers, removed Ruby APIs and Windows
calls still needed adapters. A loading picture could freeze after a Ruby
exception/short-lived OSD message; it was not always slow loading.

The custom port added checked bitmap/palette operations, fonts, input, writable
overlay and filesystem fixes. Source is layered: upstream `650cb08`, four
local commits through `e590890`, and the later working-tree changes. The
public cumulative `horizon-wine/engines/mkxp-z/mkxp-source.patch` now includes
both local commits and dirty changes; game-specific profiles/assets are separate.

### Profile the expensive operation, keep game behavior

In session `retroarch__2026_10_25__19_52_26.log`, `Game_Map.setup` intervals
were 40.548 s (NoerRecRoom), 37.675 s (NoerTavern), and 16.850 s (a repeat
OvermapMain entry). The 34–38 s gaps in the two slow locations lay between
`refresh_chs` and the next lighting marker. These are map-setup intervals,
not full transitions or isolated timings of each child function.

That log's SHA-256 is
`e8d8a1697b7b47eb90ef9d6a7e87b26c11f4c8e3c52ef22300a6b8c831f08393`.
Code showed needless pixel unpack/pack with no active filters and expensive
Ruby palette work with active filters. Stability-1 added a native palette
operation preserving color keys, filter order, alpha and BGRA/row semantics;
it retained one current character result and lazily created menu pages.
These implemented changes do not have a published matched before/after
console timing for the new package.

A separate input/lag run showed Alt/ZL released while `Game_Map.update` grew
to about 147–148 ms. Camera work was small; the last new button was not the
diagnosed cause. Popup text redrawing and shared-icon disposal were corrected
without deleting lighting, damage or burn duration. Heavy per-frame diagnostic
wrappers were removed after investigation.

Font investigation distinguished file loading, registered family and fallback;
fonts were kept in RAM. A temporary loading boost's 1020→1785→1020 MHz change
was logged. That is a historical observation for this console, not an approved
profile for another Switch, and not a measured 1.75× total loading improvement.

### Save correctness survived the optimization scope

One failure happened when reading a PNG preview after the state file had
already been written. Stability-1 separated those operations; it introduced
pending writes, validation, two backup generations, guarded load/restore and
runtime rename corrections. It also preserved intentional game deletion rules.
Power-loss durability and the complete newest-package save/load cycle were
not established by compilation or partial owner testing.

Backups placed in an autoloaded script directory had executed as extra code;
they were moved out. Missing file-cache entries required a bounded directory
refresh, rather than assuming a startup index was forever complete.

## Fallout: treat media as a separate compatibility boundary

Source history and details: `documentation/new-vegas.md`. Our main integration
carried FEX/Mesa/NVK, startup/profile support and reviewed thread/fd fixes.
This did not make historical TB4 NROs equivalent to current main.

A session opening `Data/Music/Special/MainTitle.mp3` could not instantiate the
GStreamer-backed MPEG splitter. The HRESULT was `0x8007000e`, but heap reports
still had roughly 2 GB free: the error mapping was not proof of whole-console
OOM. A native Layer III fallback was added in `dlls/quartz/avisplit.c`.
It did not implement arbitrary MPEG video/program streams.

Subsequent source fixes addressed DirectSound notification lifetime/locking,
allocation failures, splitter shutdown/flush, renderer EOS/worker races and
short PCM headers. Historical targeted host checks exercised real DLL paths,
including MP3 seek/restart; they did not test Horizon/FEX/NVK. Older console
sessions still stalled with the mixer consuming nearly one core. A host
symbolized snapshot did not identify the translated guest loop's exact cause.

The community-guided trial used FEX, Sarek 1.14.1, game-local DSOAL r697 /
OpenAL Soft r10853 and LAV Filters 0.83 x86. Our selected exact media versions
were explicit trial choices; the community report did not specify all of them.
The bundle preserved system DLLs/manifests, used a per-game dsound override,
and registered filters on restart. Both setup operations exited successfully;
registry readback confirmed the override and LAV classes. Those are setup
results, distinct from the owner's subsequent gameplay report above.

The New Vegas [community post](https://t.me/AutoRunn/11247), recorded during the
earlier research, reported about 30 FPS outdoors, drops to 20, at
1785/768/2200 MHz and 40–50 FPS at 2499/768/2733 MHz. It omitted the exact
New Vegas resolution and full settings. The post could not be freshly retrieved
while assembling this skill; these remain historical attributed claims, not
our measurements, complete presets or clock recommendations. The primary
[GOG issue](https://github.com/autorunhq/autorun/issues/22) separately reports
startup after renaming `Data/Music`; that diagnostic bypass removes music and
must not be advertised as full media compatibility.

## Built artifacts inspected for this snapshot

| Artifact | Bytes | SHA-256 |
|---|---|---|
| Standalone Lona stability-1 NRO | 58,664,276 | `1b0978cf907b29fe6249792ebc93688bd69160c5365eb63749cbfdb6b2c08751` |
| Autorun main-mkxp1 runtime NRO | 41,988,144 | `c205bb664d421ebb579e3b05488d4a5d3eee1a827e2574cb5cdddf5aeb1f1a56` |
| Generic mkxp module NRO | 58,666,788 | `dbbb331f14375330f3efe18b8f5c14e1680fe759401f545a145b521e3bb00cb8` |

These hashes were re-read from local NRO files for this documentation task;
the prior FTP receipts establish installation/readback for the relevant files.
No new console session was run to create this skill. The owner's latest
Fallout report does not identify which of those runtime revisions was live.

## Mistakes worth keeping in the workflow

- Updated file versus stale process: verify both hash and next session BUILD.
- Last line versus cause: follow the execution/display/audio boundaries.
- Unknown export versus semantics: check API behavior and object lifetime.
- New label versus old implementation: inspect source revision and flags.
- Successful cache/counter optimization versus finished port: report each result.
- Correct save versus broken preview: do not delete the progress to fix its image.
- Current Wiki versus historical project README: reconcile their dates and code.
- Known working standalone engine versus a new launcher module: test the handoff.
- All-file rebuilds and repeated cable/password changes versus a precise next
  observation: use the smallest justified iteration and preserve rollback.
