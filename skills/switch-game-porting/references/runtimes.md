# Runtime selection and source boundaries

## Select by the game's actual format

| Observed game | Candidate route | Establish before promising support |
|---|---|---|
| Existing dedicated Horizon engine/port for the game | Its maintained Switch target, using the owner's required assets | Actual target branch, build artifact, asset versions, extensions/mods, saves and controls; a desktop engine build alone is insufficient |
| RPG Maker 2000/2003, typically `RPG_RT.ldb` | [EasyRPG Player](https://easyrpg.org/player/) | Supported patches and RTP. EasyRPG does not replace RGSS for XP/VX/VX Ace |
| RPG Maker XP/VX/VX Ace, RGSS data and `Game.ini` | [mkxp-z libretro](https://docs.libretro.com/library/mkxp-z/), our native module where appropriate | Ruby version, Win32API/native extensions, private Bitmap ABI, fonts/RTP, preload order and writable overlay |
| Other Windows x86/x64 PE game | Autorun/Wine with a matching CPU and graphics backend | Imported APIs, runtime DLLs, graphics/audio path, launch model and memory requirements |
| Android native APK, only if this alternative is in scope | [Viridite](https://github.com/Viridite/Viridite) or a game-specific port | Exact APK ABI, engine and JNI/Android dependencies. This does not convert a PC copy and does not install Android OS |

For another format, research an existing compatible engine and its Horizon
target before proposing a new adapter. Lack of a ready engine is a finding,
not a reason to silently replace the user's OS. Installing Linux/Android,
changing firmware or altering a boot chain is a separate scope decision.

## Autorun path

The native Horizon NRO hosts Wine and translates supported guest code. Its
process/address space is distinct from the guest's PE architecture: an x86
game does not automatically require the legacy 32-bit forwarder. Inspect PE
relocations/base address and the current low-address support. Do not label a
39-bit forwarder as proof that all fixed-base games work. This fork's current
launcher requires a 39-bit address space for Windows games and a verified low
window for PE files classified as fixed-low; inspect `address_space_fits` when
comparing that behavior with an older 32-bit-forwarder recipe.

This fork's current path is under `horizon-wine/`; old notes referencing
`wine-nx-probe/` or stock Mesa 20 belong to earlier builds. Start with the
actual checkout's `horizon-wine/README.md` and `documentation/technical.md`,
then inspect the relevant layer:

| Boundary | Files to locate in the fork |
|---|---|
| Startup, effective settings, component queue, lifecycle | `horizon-wine/source/runtime.c`, `launcher*.c`, `launcher_settings.h`, `game_settings.c`, `fallout_startup.h` |
| Guest translation and SMC | `horizon-wine/source/wow64_box64_dynarec.c`, `fex_options.h`, `horizon-wine/fex/`, generated Box64 sources and build flags |
| Mapping, exceptions and in-process Wine server | `dlls/ntdll/unix/horizon*.c`, `virtual.c` |
| Display ownership and actual presentation | `dlls/win32u/winnx_opengl.c`, `dlls/win32u/vulkan.c`, `horizon-wine/source/compositor*` |
| Music/video graph versus audio output | `dlls/quartz/avisplit.c`, `dsoundrender.c`, `dlls/dsound/buffer.c`, `mixer.c`, `dlls/winegstreamer/`, Horizon audio backend |
| Controllers and focus | `dlls/xinput1_3/nx_pad.h`, launcher input profiles, keyboard/hooks and game-specific bindings |
| Evidence and deployment | `diagnostics.c`, `ftp_server*.c`, `autorun_update.c`, package tools, DLL manifest and `game-fixes/` submodule |

The Windows DLL payload lives separately from the NRO, with its own manifests
and runtime interface expectations. Replacing an NRO does not install a Quartz
fix; updating the DLL set can overwrite a locally rebuilt DLL. Keep local
provenance separate from the upstream manifest and verify PE architecture and
imports. Host regression checks and package inspection answer different questions.

Autorun supports one running Windows program in its host process. A preparatory
launcher may be replaceable by its settings and arguments; a required parallel
client is a different incompatibility. Keep changes scoped to the user's copy
and the runtime's supported launch contract. Game fixes are not general evidence
that every launcher, online dependency or plugin can run.

### Configuration is code-version dependent

Inspect parsing, writing and precedence before prescribing a sidecar. In this
fork's `launcher_settings_read`, legacy `d3d=dxvk` selects **DXVK+VKD3D** and
can reset Sarek to official DXVK. The New Vegas trial deliberately omits `d3d`
and `d3d9`, retaining the current DXVK-only default and setting Sarek explicitly.
Do not copy that omission to a different runtime without reading its parser.

Per-game settings override globals; a visible launcher selection does not prove
the effective guest backend. Confirm versions and settings in the game session.
Xbox positional mapping exchanges Switch A/B and X/Y labels in this fork.
Preserve or change it according to the user's preference, at the appropriate
mapping layer; avoid swapping twice through a game mod and a runtime profile.

## Native mkxp module in this fork

Public implementation: `horizon-wine/engines/mkxp-z/`; native recognition and
handoff: `horizon-wine/source/native_mkxp.h`, `runtime.c` and `launcher.c`.
The module is a separate NRO with a statically linked core. The ELF entry uses
`--wrap=main` to bypass the old standalone bootstrap that ignored arguments
and always selected Lona. It calls the existing RetroArch implementation.

Content is currently an SD `Game.ini` with RGSS script data, or a `.mkxpz`
archive. `Game.exe` still takes the Windows route. Sidecars append to the
native filename, avoiding a collision between `Game.ini` and `Game.exe`.
The native menu excludes Windows CPU/DXVK/control settings. USB content and
native per-game HOME forwarders are not enabled by this integration.

The protocol is:

```text
"sdmc:/switch/wine/engines/mkxp-z/mkxp-z.nro" --autorun "sdmc:/mkxp/Game.mkxpz"
```

Both ends check content/path constraints; Autorun checks the NRO header.
The entry strips `sdmc:` for this frontend's slash-based `realpath`, initializes
RomFS, and requests return to the fixed Autorun NRO after ordinary completion.
HOME closure ends the process directly. Bounds/quotes, content existence,
RomFS resources and the actual on-device handoff all need their own evidence.

The existing native save root is `/mkxp/saves`, including title-derived paths.
Different games with the same internal title can collide. Preserve the original
save namespace when migrating an existing game; do not relocate saves just
because a new launcher is used. Game-specific Ruby adapters are not supplied
by the generic module, nor are they automatically portable to another game.

For reconstruction, read the module README's upstream base and cumulative
patches. Its packaging script currently requires an already prepared native
tree/core and local build image. It is not a clean-clone, one-command rebuild
of every native dependency or a distributor of Lona's game scripts/assets.

## Native adaptation boundaries learned from Lona

Win32API stubs must implement the behavior the game actually needs or report
an unsupported dependency; returning zero everywhere hides broken state.
Ruby objects and native pointers have different ABI/lifetime rules. Replace
private x86 Bitmap pointer arithmetic with checked native operations, preserving
channel order, alpha, rectangles and filter order.

Load compatibility code early enough for the intended Ruby environment; verify
removed/changed Ruby APIs and exception behavior. Precompute stable data where
useful, not running Fiber/Bitmap/native-pointer state. Keep font bytes, requested
family names, fallback and rasterization as separate failure points.

Review the actual filesystem adapter: case lookup, cache misses, overlay
precedence, write roots, rename/replace, flush and persistence can differ from
desktop behavior. A valid save followed by a failed PNG preview is not a lost
save. Do not restore a backup over an intentional in-game deletion/permadeath.
