---
name: switch-game-porting
description: Research, diagnose and adapt PC-game ports for a user's Nintendo Switch, using Autorun/Wine, native Horizon engines or libretro cores. Use for selecting community builds, comparing performance, investigating logs, reviewing compatibility changes and preparing a reversible deployment. Applies to an existing game/runtime, not Nintendo console emulation or routine firmware maintenance.
---

# Switch game porting

Deliver a working route for the user's game and console, or a specific,
evidenced blocker and next experiment. Do not promise universal compatibility.
Prefer an existing compatible engine or a narrow runtime correction over
rewriting the game. A launcher integration is separate from engine compatibility.

This skill is portable. It needs neither the original author's Wiki, paths,
console, credentials nor other skills. With a local Autorun checkout, inspect
its actual code; otherwise use the linked public sources and identify what
cannot be established. Respect the current user's scope and execution rules.
The reference implementation is [howdeploy/autorun](https://github.com/howdeploy/autorun),
compared with [upstream Autorun](https://github.com/autorunhq/autorun). Keep the
user's chosen fork and branch; those links do not authorize replacing it.

## Establish the current target

Inspect available files and records before asking for missing information:

- Game edition/version, language/mods, executable architecture, data format,
  engine, assets/RTP, launcher and DLL dependencies; preserve the original copy.
- Switch model/SoC, Horizon and CFW versions, applet/application launch mode,
  forwarder and address space, storage, and actual CPU/GPU/RAM clocks when known.
  Do not infer clock capability from another owner's OLED or Mariko report.
- Installed NRO and DLL versions/hashes, game-specific configuration, save paths,
  runtime logs and matching source/ELF. Identify a stale live process separately
  from an updated file on SD.
- User's success target and authorized work: research, code, build, install,
  tests and gameplay are distinct. Continue work already authorized; a skill is
  not permission to change firmware, clocks or run an unrequested game test.

Record unknowns as unknown. If local knowledge conflicts with current code or
a fresh owner report, retain the dated history and correct the current status.

## Choose and research the route

Read [runtime and engine routing](references/runtimes.md) for the relevant path.
Research the current project releases, correct branches, Switch CI jobs and
game reports. Use [research and comparison](references/research-and-measurement.md)
to distinguish a supported release, a development build and an anecdote.
Start from primary sources; search public forums/issues for first-person tests.
Capture the exact link, date, revision, hardware and test conditions.

Do not treat absence from Releases as absence of a port, or a failed CI matrix
as a failed Switch job. Do not transplant another game's settings, clocks or
ABI assumptions. Check existing native engines before extending a Windows
compatibility layer; choose by evidence, not by the word "native".

## Diagnose before tuning

Separate loading, execution, presentation, audio, input and save persistence.
Follow the failing path through callers and the platform boundary; inspect
the relevant implementation and its dependencies, not only a function export.
Use the actual log/build pair and state the extent of code review.

Classify the next change as a dependency/configuration correction, runtime API
fix, game-specific adapter or measured performance optimization. Explain the
expected observable change. Fix the shared cause at the narrowest appropriate
layer, preserving error handling and the game's behavior.

Examples and failed approaches are in [field notes](references/field-notes.md).
Read the relevant case when its symptom appears; historical recipes are not
defaults for a new game. A black frame, an HRESULT named out-of-memory, a busy
thread or the last log line alone does not diagnose the failure.

## Implement and compare

Preserve existing work and game data. For upstream changes, compare with the
fork, read prerequisites and interface changes, retain attribution, and verify
that the built artifact includes the intended code. Do not import an entire
PR or alter boot configuration solely because it is newer.

Choose one falsifiable experiment at a time. Where testing is authorized,
use checks that exercise the changed code and a repeatable on-console route.
Compare like-for-like runs; separate diagnostic overhead from normal play,
and separate compatibility success from speed. If tests/builds are not
authorized, complete permitted source work and identify the pending checks.

Keep game-specific behavior out of global defaults unless the shared contract
justifies it. Cache/precompute only reproducible data with input/version guards,
fresh mutable results where required, and a working fallback. Do not remove
music, lighting, mods or save rules to advertise a compatibility fix.

## Deploy and hand off

For an authorized install, use [deployment and evidence](references/deployment.md).
Prepare the actual package, its provenance and rollback before asking for any
remaining approval. Inspect an existing installer before reuse: a script from
one owner's installation may have different roots or destructive assumptions.

Close with what changed, where, the exact observed outcome, evidence and remaining
limits. Use explicit statuses: source reviewed; built; statically checked;
installed/readback verified; owner-reported gameplay; directly measured gameplay;
save/reload verified; long-session tested. These can coexist and are not synonyms.
Attribute a new owner report even when no matching log or artifact identity is
yet available; do not dismiss it or invent details it did not provide.

If the same attempt fails again without new evidence, inspect the specific
unchanged assumption instead of requesting repeated reconnects or shipping
another speculative build. Stop dependent work on a real access/authorization
blocker and state exactly what would unblock it.
