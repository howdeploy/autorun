# Porting a PC game to your Switch

This fork includes a reusable agent workflow for researching a game, choosing
its runtime, diagnosing the failing boundary, making a focused change and
checking the result on a particular console. It supports Autorun/Wine work and
selection/adaptation of existing native Horizon engines. It does not make every
PC game compatible or treat a package installation as a gameplay test.

## Give an agent the workflow

The complete portable skill is
[`skills/switch-game-porting`](../skills/switch-game-porting/SKILL.md).
Copy that entire folder, including `references/` and `agents/`, into your
agent's skill directory. For Codex this is normally
`~/.codex/skills/switch-game-porting`; inspect an existing skill before replacing
it. Agents without a skill installer can read `SKILL.md` directly. There is no
dependency on the original owner's Wiki, local paths, credentials or plugins.

Example request:

```text
Use $switch-game-porting for my copy of GAME, version/edition VERSION.
The installed game and current logs are at PATHS. My Switch is MODEL,
with OS/CFW VERSIONS and this current runtime/launch method: DETAILS.
I want TARGET. Research current community builds, inspect the actual code
and prepare the smallest justified compatibility/performance change.
Preserve saves and existing settings. Work authorized now: SCOPE.
```

Unknown fields are fine: the agent should inspect available evidence and ask
only for information that changes the decision. Your current instructions
determine whether it may build, install or run tests/games.

## The work around Autorun

| Stage | Useful outcome |
|---|---|
| Identify | Game/engine/architecture/dependencies, console and launch mode, installed artifacts, settings and saved progress |
| Research | Current primary builds and game reports with dates, exact revisions and missing conditions identified |
| Select | Existing native engine when compatible, or an evidenced Autorun path; known blockers and why this route fits |
| Diagnose | A causal path through execution, presentation, audio, input or storage, with a falsifiable next experiment |
| Change | A narrow configuration/dependency/runtime/game adapter fix; attributed upstream changes and preserved fallback |
| Compare | Authorized checks on the changed code and comparable game scenarios; correctness and speed reported separately |
| Deploy | Actual package, hashes, staged readback, backups, activation journal and recovery instructions |
| Record | What worked, whose observation it is, exact artifact when known, remaining limits and next useful check |

The detailed instructions are maintained in one place:

- [Current-build research and benchmark method](../skills/switch-game-porting/references/research-and-measurement.md).
- [Engine choice, actual source layers and configuration traps](../skills/switch-game-porting/references/runtimes.md).
- [Deployment, saves, registry and evidence](../skills/switch-game-porting/references/deployment.md).
- [Our field notes, measurements, failed approaches and NRO identities](../skills/switch-game-porting/references/field-notes.md).

Start by checking releases **and** relevant development branches/CI. An old
stable Autorun package, a current source build and a Carafe game package are
not interchangeable merely because they share Wine ancestry. Read the actual
settings parser, DLL interfaces, dependencies and target changes before copying
a community preset or importing a fix. This is a research workflow, not a
permanent catalog of "best" versions or clock profiles.

## What the fork currently supplies

| Component | Where to inspect it |
|---|---|
| FEX/Box64, Wine, Mesa/NVK and Windows game launch | [Runtime overview](../horizon-wine/README.md), [technical guide](technical.md) and actual source/build flags |
| New Vegas startup, audio/DLL fixes, community media preparation and measured limits | [New Vegas notes](new-vegas.md), Quartz/DirectSound source, `horizon-wine/tools/prepare-falloutnv-media.py` and its FTP installer |
| Russian UI, authenticated FTP and session/event/report diagnostics | [Diagnostics](ru-diagnostics.md) and `horizon-wine/source/diagnostics.c`, `ftp_server*.c` |
| Native RPG Maker XP/VX/VX Ace content selection | [mkxp module](../horizon-wine/engines/mkxp-z/README.md), including source patches, packaging and the NRO handoff contract |

The mkxp module packages an already prepared native fork; it does not yet
bootstrap all engine dependencies from a clean clone. Native game-specific
adapters and assets remain separate. Other engines in the research workflow
are candidate routes, not modules already integrated into Autorun.

## Our current gameplay status

As of the owner's 2026-10-28 report, **Fallout New Vegas works and was played
during the introduction**. That report does not identify the exact live NRO
or establish measured FPS, long-session stability or complete save/audio tests.
Earlier stalled sessions remain useful historical evidence.

**LonaRPG worked on our custom standalone native mkxp engine**, including
owner-reported tutorial gameplay. The newest Autorun-to-mkxp integration was
built, installed and readback verified, but the owner explicitly has not tried
that new build. Its handoff/gameplay status remains pending. Previous engine
success is not a test of a new launcher path.

## Reporting a useful result

Supply a direct source/build identity, game version and scenario, console and
launch mode, effective settings/clocks, relevant logs and the checks performed.
Review logs before publishing them. Report frame generation separately from
base FPS and loading separately from gameplay. Say whether a result is your
observation, a raw measurement or another person's report.

An observed improvement can be valuable without being a finished port: RU15
removed repeated translator checks; a prepared JSON cache was used on console;
native palette processing replaced avoidable Ruby work. None of those facts
alone establishes a full-game FPS gain. Preserve this distinction when the
next user brings a different game or a different Switch.
