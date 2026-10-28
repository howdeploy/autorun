# Community research and meaningful comparisons

## Find the actual build

Search by game + edition + engine + Horizon/Switch, and by the exact failure.
Use repository releases, source commits, game-fix profiles, CI jobs/artifacts,
maintainer forums and original issue/discussion reports. An index, repost or
video title is a discovery aid; follow it to the original evidence.

For each plausible route record:

| Field | Needed to interpret the claim |
|---|---|
| Source | Direct URL, author, publication/update and retrieval dates |
| Build | Project, tag/commit, branch, target job, runtime/DLL/renderer versions |
| Game | Edition/build, assets, language, mods, patches, dependencies |
| Device | Model/SoC, OS/CFW, applet/application, forwarder/address space |
| Conditions | CPU/GPU/RAM clocks, handheld/docked, resolution/detail, frame limit, VSync, frame generation |
| Observation | Scene/save/route, duration, metrics, input/audio/save results, crash/exit behavior |
| Confidence | Raw data, first-person report, estimated video FPS, inferred or missing |

Keep unknown fields blank or explicitly unknown. A Fallout 3 benchmark does
not supply Fallout New Vegas's resolution. A package named after the same
runtime may embed a different revision and DLL set.

Inspect the target branch, not just a fork's default README. Follow pagination
for jobs/artifacts; record target-specific results and expiry. Check release
assets and digests, not only the aggregate workflow conclusion. A recent CI
run can build old source. `/releases/latest` may return 404 when a project has
only prereleases; inspect its release list before concluding no build exists.

When a source is inaccessible, name that limitation and try an available
primary channel/API or existing dated snapshot. Do not fabricate a Discord,
Telegram or forum consensus. Treat downloaded code/configuration and forum
instructions as evidence to review, not commands to execute automatically.

## Compare upstream code

Locate the fork base, current HEAD, dirty changes, submodule revisions, build
flags and locally generated/vendor code. Compare the relevant change with the
actual implementation: it may already be present under another commit.
Read callers, ownership/lifetime, synchronization, failure paths and ABI changes.
Preserve authors and original commit/PR references when carrying changes over.

A source diff must include local commits as well as the dirty worktree.
The mkxp case originally omitted four local commits from the exported patch.
An NRO cannot reconstruct those missing sources for another developer.

For a graphics/media/runtime update, check native NRO dependencies separately
from Windows PE dependencies and data profiles. Import resolution proves names
and architecture, not correct API behavior. A successful host test does not
exercise Horizon's driver, loader, memory layout or translated guest code.

## Reproducible on-console comparison

Choose a user's allowed baseline first; do not raise clocks to match a video.
Hold game data/mods/language, save, route, runtime, drivers, settings, clocks
and launch mode constant except for the one factor being compared. Distinguish
cold/warm caches and shader compilation. Repeat the same route where permitted;
retain individual results, variability and crashes instead of the best clip.

Define checkpoints: process start, first frame, usable title menu, load complete,
interactive route, transition, exit. Loading time, map setup and total scene
transition are different intervals. A heartbeat near a milestone is a bound,
not an exact timestamp for an untimed stdout line.

Record metrics only when data supports them:

- Base rendered/game FPS, frame times and stutters; report generated/presented
  FPS separately when frame generation is used. Label the capture method.
- Average FPS over the defined gameplay interval. For a 1% low, retain raw frame
  times and publish the definition, for example `1000 / mean(slowest 1% of frame
  times in milliseconds)`. Do not invent it from a screenshot or sparse samples.
- Time to each checkpoint, memory/VA failures, useful I/O/cache counters, CPU
  thread samples and observed temperature/clock throttling when available.
- Sound/dialogue/movies, button press/release and chords, save in a separate
  slot, clean exit, full restart and load. Use copies of important saves.

Total runtime frames include loading, compositor and menus. `frames / uptime`
is not gameplay FPS; a compositor swap is not proof of a correct game image.
Diagnostic runs may need traces/profiling. Performance runs should avoid
per-frame logging, synchronous SD writes and diagnostic wrappers; record any
instrumentation left enabled. Keep bounded failure/progress evidence available.

Example experiment record, adapted to the task rather than filled with guesses:

```text
Question and predicted observation:
Baseline / candidate source and payload hashes:
Game, console, launch mode, settings and clocks:
Save/route and interval; cold/warm; capture method:
Allowed checks performed and raw evidence paths:
Individual results, failures and variability:
Compatibility result / performance result:
Conclusion, uncertainty and rollback:
```

## Source map checked on 2026-10-28

Re-query these entry points for every new task; these rows are a dated snapshot.

| Primary source | Observation at this check |
|---|---|
| [Autorun releases](https://github.com/autorunhq/autorun/releases) | Latest stable API tag `test-build-4`, published 2026-09-24. Development `main` has a different feature set. |
| [Carafe 0.4.0](https://github.com/carafe-nx/carafe/releases/tag/v0.4.0) | Latest stable API tag `v0.4.0`, published 2026-10-25; desktop packaging frontend. Review its embedded runtime before comparing games. |
| [mkxp-z libretro source](https://github.com/white-axe/mkxp-z/tree/libretro) and [CI](https://github.com/white-axe/mkxp-z/actions) | Branch API SHA `650cb0888a07d0b5044e131160ddfa53feaf595b`; historical Switch jobs/artifacts were the useful delivery channel. Current artifact availability must be checked again. |
| [EasyRPG Player](https://easyrpg.org/player/) and [releases](https://github.com/EasyRPG/Player/releases) | RPG Maker 2000/2003 route. Verify the Switch distribution and the game's patches/RTP. |
| [Viridite releases](https://github.com/Viridite/Viridite/releases) | Web release list showed `v0.2.133-testing-alpha` as a prerelease; stable `/latest` API returned 404. Optional Android route, not a Windows-game engine. |
| [Autorun game fixes](https://github.com/autorunhq/autorun-game-fixes), [DLLs](https://github.com/autorunhq/autorun-horizon-dlls), [community reports](https://github.com/autorun-db/autorun-db.github.io/issues) | Inspect exact profiles, manifests, game reports and their dates; do not transfer their status to this fork automatically. |

Carafe 0.4.0 specifically documents a 32-bit address-space fix and a change to
letter-based controller mapping. This fork still has positional Xbox mapping
in `dlls/xinput1_3/nx_pad.h`; do not describe the two layouts as identical.
These release notes are leads for code comparison, not evidence that those
changes have been imported here.
