# Sonic 2 additive party experiment

Tracking: central Beads `beads-5dyp.1` (game) and `beads-3vb.4` (source baseline).
Engine and consumer branch: `experiment/sonic2-local-4p` in separate worktrees.

## M0 — source baseline (primary master)

- [x] Pin stock Sonic 1, Sonic 2, and Sonic 3 & Knuckles disassemblies as submodules.
- [x] Rebuild and byte-verify Sonic 1/2/3, S&K, and combined S3&K.
- [x] Regenerate recompiler symbols and durable Ghidra annotation exports.
- [x] Import and round-trip Ghidra projects without committing databases or ROMs.
- [x] Build all five native targets; 12 framework tests and five 3,600-frame smoke runs pass.
- [x] Commit primary engine master: `7a927e4`, `24a44ff`.
- [ ] Update consumer engine pins at integration (experimental code stays on its branch).

## M1 — roster and native Options route

- [x] Stable character IDs and registry; availability independent of slot assignment.
- [x] PLAYERS = available slots, 1–4; defaults Sonic, Tails, NONE, NONE.
- [x] D-pad cursor and left/right cycling; unique characters; P1 cannot be NONE.
- [x] Disabled slots clear to NONE; unavailable imports repaired safely on load.
- [x] Persistent roster; native title Options entry retained.
- [x] Automated model tests and real navigation screenshots.

This is the configuration layer only. Gameplay adapters below are not yet
wired: a changed roster does **not yet change the spawned native actors**.
Local validation: 16 CTests, strict-stack Options navigation, and a 3,600-frame
stock run with no dispatch misses. Three PNGs and two full-RAM captures match
primary master byte-for-byte. Historical frame-60 baseline is stale relative
to both builds; the experiment matches a fresh primary CPU/WRAM snapshot.

## M2 — resource-backed character mods

- [ ] Amy enabled by default, never selected automatically; can be disabled.
- [ ] Extract only gameplay art/logic from the supplied Rev 1.7.1 hack.
  - [x] SHA-256-verified gameplay art, mapping and palette decoder; all 253 Amy
    and 251 Knuckles frames decode, contact sheets visually inspected.
  - [ ] Portable gameplay controllers and their host collision adapters.
- [ ] S3&K default off; verified user-selected stock donor ROM required.
- [ ] Knuckles gameplay implementation (including glide/climb), not a Sonic reskin.
- [ ] Source/provenance and collision/animation adapters; no patched ROM or generated-C edits.
- [ ] Mods UI explains missing/wrong resources and gates selection accordingly.

## M3 — local party simulation

- [x] Four independent logical inputs, bindings, hotplug and script timelines.
  - [x] Virtual SDL P3/P4 button isolation and disconnect tests; shared launcher
    bindings round-trip through the engine's actual settings.ini parser.
- [ ] Native Genesis physical ports remain two; no netplay expansion.
- [ ] Independent P3/P4 actor state; tick the world only once.
- [ ] Companion CPU fallback when local controller unavailable.
- [ ] P1 owns camera, lives, progression and checkpoints; catchup/respawn for companions.
- [ ] Terrain, solid objects, enemies and representative boss interactions.

## M4 — mode compatibility

- [ ] VS remains native two-player; uses selected P1/P2 characters; rejects NONE.
- [ ] Special stages use selected P1/P2; restore companions after the round trip.
- [ ] Existing netplay rejects unsupported roster/mod combinations.
- [ ] Restart, death, level transition and donor-disable recovery.

## M5 — human validation gate

- [ ] Runnable build and reproducible keyboard/controller instructions.
- [ ] Automated evidence and honest known limitations, with representative playable coverage.
- [ ] Owner explicitly validates and approves four-player work.

## BLOCKED UNTIL M5 APPROVAL — campaign saves / zone selection

Do not implement these during the character spike. Later: opt-in S3&K-backed
Sonic-3-style save-slot and zone UI, Sonic-2 zone/act progression, unconditional
Sonic-and-Tails slot icon, roster still owned by Options, and unlocked zones
after completion. This is distinct from existing emulator quicksave support.
