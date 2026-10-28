# Deployment, recovery and evidence

## Identify what the update actually changes

A native NRO, Windows DLLs, a game-fix profile, registry registration, game
scripts and assets are separate deliverables. Inventory the installed tree
and prepare only the components the change needs. Keep source revision plus
dirty diff, submodules, build image/toolchain, flags, ELF, NRO, file sizes and
SHA-256 together. A large NRO is not a RAM-use measurement.

Check NRO format and, when relevant, its matching AArch64 ELF/symbols. Inspect
PE architecture/imports separately. Bind source patches to their real base;
include local commits absent from the public upstream. Generated/vendor changes
and prerequisites must be discoverable from the build record.

Do not include a user's game, saves, credentials, private network details or
unreviewed raw logs in a public package. Preserve applicable engine and component
licenses and download provenance. Do not assume a game-specific adapter is
redistributable merely because the agent can read it locally.

## Use the server that is actually running

Autorun's built-in FTP `/` is `sdmc:/switch/wine`, **not** the SD root. A
module may live under `/engines`, while native content under `/mkxp` is outside
that share. Use an authorized SD-root service such as the owner's Sphaira
configuration or DBI/MTP when access there is required; do not attempt traversal.
Those tools expose different path namespaces. FTP is neither HTTP nor a shell,
and it does not provide remote console button presses.

Read connection details from the current session/private configuration; never
ship this skill's author's IP or password. Distinguish no TCP greeting,
authentication rejection, and data-channel timeout. Passive FTP has a separate
data connection. Do not reset credentials for a routing failure, disable a
working VPN, or ask for repeated reconnects without checking a changed factor.

## Stage before activation

For an authorized installation:

1. Preserve current runtime/configuration/registry as relevant, the catalog and
   saves. Identify old hashes and unrelated files that must remain unchanged.
   Keep backups outside script-autoload directories and on the PC where useful.
2. Upload into a unique temporary location on the same target storage. For an
   archive, inspect member paths before extraction. Read back bytes and check
   size/hash; an upload progress bar alone proves neither completeness nor identity.
3. Before activation, recheck targets that could have changed. Keep a journal
   of staged paths, original hashes, backups and each completed rename.
   Activate dependencies before enabling hooks/configuration that requires them.
4. Verify final paths by readback. Preserve old files for rollback and check
   the unrelated-file guards. A partial transfer should leave the installed
   runtime usable. If activation is partial, report exact state; restore only
   known originals, retaining unknown or subsequently changed files.

On FAT, rename-over-existing and multi-file replacement need care. Several
successful renames do not form a power-loss-atomic transaction. Validate edited
catalog/INI data before activation, preserving existing entries and unknown
settings. Our first mkxp install attempt was stopped before activation after
finding a newline error in a catalog edit; the corrected attempt checked that
all pre-existing records were unchanged.

Updating the NRO file does not change an already running process. Tell the
owner how to close/restart it and confirm the next session's BUILD before
attributing behavior to the new version. Do not launch the game as an implicit
side effect of installation when the owner retained gameplay testing.

## Registry and save recovery

Installing a DLL/filter and registering it are different steps. Use a narrow,
game-specific override when appropriate; keep shared registry backups. Retain
setup logs and confirm persisted state after an authorized registration run.
A consumed setup queue or exit code zero alone is weaker than the relevant
successful operation plus registry readback. Keep unregister/disable tools
available before removing the registered files.

Prefer reverting the exact keys/files added by the change. Restoring an entire
old registry can overwrite newer changes from another game. Identify files
using the receipt rather than deleting everything with a familiar extension.

For save changes, preserve real progress and use separate slots/copies for
tests. Serialize before opening the replacement, handle write/flush/close
failures, validate before rotation, and separate optional preview creation
from the successful state write. Validate loaded data before replacing the
live world. Respect intentional deletion and game rules. Review the actual
runtime's write-root and rename semantics before assuming desktop durability.

## Evidence to leave for the next agent

Keep a compact status record with:

- Source/build IDs and payload hashes, including the module/profile layers.
- What was actually installed and the backup/rollback locations.
- Dated logs or owner observations, the exact scenario reached and artifact
  identity when known. Preserve old failed experiments as historical results.
- Which checks ran, which did not, their coverage and the next useful experiment.

For exported diagnostics, retain the beginning/BUILD and relevant tail/events
of capped logs. An omitted tail is not evidence of no error; restarting the
launcher may create a different session. Review logs for private information
before posting them. Never publish credentials, raw private configuration or
save contents as part of a compatibility report.
