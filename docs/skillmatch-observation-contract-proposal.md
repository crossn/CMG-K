# Skill Match start observations and rated eligibility (proposal)

This document is a design, not a production wire contract or an implemented
decision rule. `WatchObservation` stays a local, unverified diagnostic. No
existing or fixture-generated Web result can become verified evidence merely by
matching this proposal.

## Character investigation

The local `feat/skillmatch-observer` worktree at `9661a6ae` contains **uncommitted**
character diagnostics in `SkillMatchSmash64.cpp` and `Emulation.cpp`. It reads
logical RDRAM bytes `0x000A2CEB` and `0x000A2D5F` as P1/P2 candidates after the
Smash Bros. Japan v1.0 ROM gate. Its synthetic-memory tests demonstrate the reader
and intended mapping, not real-ROM correctness. No proven real-ROM character
diagnostic result was located in this investigation. Those changes are not part
of this PR and the other worktree must remain untouched.

The repository's existing `mupencheat.txt`, in the `67D20729-F696774C-C:4A`
VS-mode section, labels the corresponding byte addresses for P1/P2/P3/P4 as
`800A2CEB`, `800A2D5F`, `800A2DD3`, `800A2E47` and describes candidate IDs:

| Raw | Candidate label | Raw | Candidate label |
| --- | --- | --- | --- |
| 00 | Mario | 06 | Yoshi |
| 01 | Fox | 07 | Captain Falcon |
| 02 | Donkey Kong | 08 | Kirby |
| 03 | Samus | 09 | Pikachu |
| 04 | Luigi | 0A | Jigglypuff |
| 05 | Link | 0B | Ness |

This is a **hypothesis from a cheat database**, not validation of start-time
semantics, all ID mappings, player presence, or rollback stability. Do not enable
the cheats or write to RDRAM. Validate using read-only captures on the gated ROM,
known selections and multiple characters for each slot, menu-to-start and
subsequent games, both clients, CPU/unused slots and rollback. Treat out-of-range,
stale or unreadable data as unknown. Candidate character bytes cannot prove that
a slot is occupied or absent. The current #46 build does not read or emit them.

## Proposed observation model

Each **game-start observation** should have an observation ID, observed frame,
ROM signature, observer/session provenance and finality (`unconfirmed`,
`confirmed` only with independently validated evidence, or `unknown`). It
contains exactly four slot observations indexed 0..3, each with:

* `presence`: `present`, `absent`, or `unknown`;
* `controllerKind`: `human`, `cpu`, or `unknown`;
* `characterId`: optional raw ID plus namespace/ROM version and confidence;
* `teamId`: optional opaque value, or unknown;
* `observedFrame` and `source`: where and when this field was measured.

Each field needs independent validity and provenance. Omission, null, unreadable
memory, unknown value and unsupported measurement must not silently become
`absent`. Do not infer absence from controller connections or stock values.
Currently all four presence/controller/character/team fields are unknown; only
watcher frame, event type and some indices are available. The current wire
`WatchObservation` has no slot array and no finality field: no claim is made that
this proposed model has been implemented.

To consider a 1v1 result for rating, the server must independently verify human
P1/P2 presence, the absence of participating fighters in P3/P4, game/mode and
validity of all required observations. Any unknown, mismatch, stale attempt or
missing proof blocks automatic finalization. Two matching unverified client
claims are not by themselves proof. Team battle rating/adjudication is outside
scope. Source ownership, evidence standards and compatibility/versioning remain
subject to Web/CMG agreement.

## Character continuity

For each server-owned set and game attempt, keep immutable per-observer start
evidence and its finality/correction history, rather than a client-supplied set
score. Record the first game's confirmed, verified participant-to-slot/character
association as the baseline only after all required evidence is available. Compare
later confirmed **game-start** observations by participant, not by client index;
character-select cursor changes do not form a game-start observation. Missing or
conflicting evidence remains pending/disputed. No automatic character forfeit is
implemented or enabled; the rule in Web #197 awaits final owner approval.

## Authentication and binding dependencies

1. Each participant authenticates separately on the Web.
2. Each CMG-K installation pairs with its own account using a server-controlled,
   revocable device mechanism; the details are not implemented here.
3. The server validates distinct principals and current room membership generation.
4. Independent evidence establishes the mapping between each principal/device and
   the game's P1/P2 slots for the correct set and game attempt.
5. The server validates slot binding, ROM, game-start state, four-slot eligibility,
   rollback finality, both participants' evidence and result consistency before
   accepting a rated result. Failure or unknown holds the result.

`localPlayerIndex` is an emulator-local hint, not an authenticated principal or a
verified Web slot. Kaillera/Gekko two-client testing may proceed without login as
observation-only QA; it cannot satisfy the rated requirements. The Web #205 test
fixture supplies its own claims and is not real CMG-K evidence.

In the current frontend, `CoreStartEmulation` converts `player` values 1 and 2 to
indices 0 and 1; any other value becomes -1. `EmulationThread::resetState()`
uses -1 for ordinary local emulation, explaining why a standalone observation may
have an unknown local index. Whether each netplay path supplies the expected value
must still be checked on both real clients; this is not identity verification.

## Local Windows QA and dump triage

Owner-driven local ROM QA on 2026-11-19 received `MatchStarted` and
`NormalResultCandidate` over localhost; the winner candidate was game index 0.
OFF no-traffic remains unverified because the OFF game's time interval was not
recorded. The local two-client preparation keeps client configuration, receiver
ports and logs separate. Character and four-slot observations are not implemented.

An earlier local dump (2026-11-19 01:26:30 JST) has exception `0xc0000005`
(write near null) at `RMG-Input.dll` offset `0x2f480`. The dump's module timestamp
matches the build, and the local DLL used for disassembly matches the QA runtime
DLL by SHA-256. That offset maps to `Thread::HotkeysThread::SetState`; source calls
it from `RomOpen` and `RomClosed`, but the dump inspection did not establish the
caller or the lifecycle sequence. This is not proof that Skill Match transport
caused the crash. No fix or successful reproduction is claimed. Stop the next QA
and retain the new dump if it recurs; do not post dumps or private paths publicly.
