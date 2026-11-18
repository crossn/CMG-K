# Skill Match Phase 2: transport foundation (incomplete)

The outbox is a contract-neutral, in-memory queue. It accepts bytes produced by a
future agreed encoder and calls an injected sender on a worker thread. Retries use
the same event ID and bytes. Duplicate IDs with different bytes are rejected. The
bounded capacity includes acknowledged IDs; exhaustion requires an explicit
fallback rather than silently forgetting idempotency history. The receiver must
also enforce idempotency, since a response can be lost after acceptance.

This is not wired to the watcher or a network endpoint. No shared wire format is
defined by this change. In particular, the watcher exposes a frame and local result
candidate but does not supply Web room/attempt/membership identifiers or proof of
confirmed rollback frames. The Web contract, identity binding and finality remain
open. A future integration must fail closed on missing context, slot, pause,
NoContest, unknown state and unconfirmed rollback, and instantiate the sender only
when Skill Match is enabled. A bounded localhost-only sender must prevent redirects
and DNS rebinding and cancel or bound in-flight requests on disable. Neither secrets
nor personal data should be stored in the queue. The current queue is process-local
and loses pending entries on shutdown; it does not provide restart recovery.

No production endpoint, receiver, automatic submission, or rating confirmation is
implemented here. Do not treat this foundation as Phase 2 completion.
