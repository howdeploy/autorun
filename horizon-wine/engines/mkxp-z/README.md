# Native mkxp-z module

This connects the fork's existing Horizon mkxp-z/libretro implementation to
Autorun's library. It is a separate native NRO: Autorun hands the selected
content to it through the homebrew loader before starting Wine. It does not
load an ARM engine into a Windows process.

## Player setup

Install a runtime built from this main branch and the module built below:

```
sdmc:/switch/wine/wine-nx-runtime.nro
sdmc:/switch/wine/engines/mkxp-z/mkxp-z.nro
```

In Autorun, choose **Add game**, browse the SD card and select either a game's
`Game.ini` (with RGSS `Scripts` in its `[Game]` section), or a `.mkxpz` archive.
Add it to the library and start its card. The card shows `mkxp-z`; selecting
the same game's `Game.exe` still uses Wine. Native games have library settings
without Wine, FEX, DXVK or Windows controller options. USB content and native
game HOME forwarders are not enabled in this integration.

For the existing native Lona installation, select
`sdmc:/mkxp/LonaRPG.mkxpz`. Keep that archive, its save overlay and the separate
`sdmc:/switch/LonaRPG-mkxp/LonaRPG.nro`. No game data is included in this module.
Games need their assets, fonts, RTP resources and any game-specific native
compatibility scripts; recognizing XP/VX/VX Ace content does not establish
compatibility with every game's scripts or DLL calls.

An ordinary exit returns to Autorun. Closing the application through HOME
ends it directly. Handoff diagnostics are in `switch/wine/logs/autorun_runtime.log`
and `switch/wine/mkxp-z-module.log`; core diagnostics remain under `/mkxp/logs`.
The native save directory remains `/mkxp/saves`, using the same RetroArch
configuration and title-based overlay as the standalone port. Different games
with identical internal titles can share that core's save namespace.

## Build from our existing native fork

The local native tree is `switch-homebrew/mkxp-native`. Its engine is based on
[white-axe/mkxp-z, libretro](https://github.com/white-axe/mkxp-z/tree/650cb0888a07d0b5044e131160ddfa53feaf595b),
with our bitmap, font, filesystem, input and WASI changes. The frontend is
[RetroArch v1.22.2](https://github.com/libretro/RetroArch/tree/69a4f0ea1e8aaf442ae4858f2e7f2b31a1776576)
with our existing Horizon patches. Their source changes are recorded in
`mkxp-source.patch` and `retroarch-source.patch`; game scripts and assets are
separate and are not part of these patches. The repository's mkxp patch is
cumulative against `650cb08`: it includes the four local commits through
`e590890bcde0e9c86a8e1d86758487c04e59dfe6` and subsequent working-tree changes.
Apply it to that upstream base, not to an already patched local tree. The
build-output receipt instead records its actual local HEAD and diff from
that HEAD. See the upstream trees for licenses and dependencies (mkxp-z GPL-2.0
or later; RetroArch GPL-3.0 or later).

Use the existing `mkxp-libnx-build:v4-10-0` Docker image and a native tree whose
core and RetroArch objects have already been built from that source. The
native tree's `build_switch.sh` builds those prerequisites. Then, from the
Autorun repository root:

```sh
sh horizon-wine/engines/mkxp-z/build.sh ../mkxp-native \
    horizon-wine/build-mkxp-module
```

The output directory must be new. The script copies the frontend into it,
links our existing static core and a small `--wrap=main` entry point, then
packages `mkxp-z.nro` with its own RomFS. The standalone Lona entry point and
the original native tree are preserved. The output records source revisions,
patches and hashes. The module inherits the native fork's loading/performance
behavior and game compatibility; this integration changes content selection
and return to Autorun. It does not merge or reconfigure the Windows runtime.

The loader contract is exactly three arguments:

```
"sdmc:/switch/wine/engines/mkxp-z/mkxp-z.nro" --autorun "sdmc:/mkxp/LonaRPG.mkxpz"
```

Both ends validate SD paths and content, and Autorun checks the module's NRO
header before handing off. Paths containing quotes or control characters are
rejected. A build-time assertion bounds the command line against the loader's
argument buffer. No console launch or gameplay validation is implied by the
source integration; validate start, input, sound, save/load and return on the
console before treating another game as supported.

## Installation status: 2026-10-28

Both NROs built successfully using the existing native core and runtime
dependencies. Inspection of the module's ELF confirms that `_start` branches
to `__wrap_main`. FTP staging and final readback matched these SHA-256 values:

| File | SHA-256 |
|---|---|
| `wine-nx-runtime.nro` | `c205bb664d421ebb579e3b05488d4a5d3eee1a827e2574cb5cdddf5aeb1f1a56` |
| `engines/mkxp-z/mkxp-z.nro` | `dbbb331f14375330f3efe18b8f5c14e1680fe759401f545a145b521e3bb00cb8` |

Library entry 3 selects `sdmc:/mkxp/LonaRPG.mkxpz`; entries 1 and 2 were
preserved. The old runtime and library have SD backups with suffix
`.before-mkxp-20261028-182900` and local backups. Fallout's settings, Wine's
quartz/dsound and the DLL manifest retain their pre-install hashes.
The existing native content is outside Autorun's FTP root and was not read
back in this installation. No game, input, sound, save/load or return to
Autorun was exercised through the new integration.
