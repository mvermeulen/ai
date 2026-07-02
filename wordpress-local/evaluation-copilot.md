# Offline WordPress Sync Evaluation (6 Branches)

## Scope

This document compares six implementations for offline-first WordPress authoring and synchronization:

- `wordpress-local-antigravity-v1`
- `wordpress-local-antigravity-v2`
- `wordpress-local-claude-v1`
- `wordpress-local-claude-v2`
- `wordpress-local-copilot-v1`
- `wordpress-local-copilot-v2`

The comparison focuses on architecture, safety model, offline ergonomics, test maturity, and operational fit.

## Executive Summary

- The strongest safety and operational maturity appear in `claude-v1` and `claude-v2`.
- The clearest deliberate-approval workflow appears in `copilot-v2` (immutable plan + checksum + ACK phrase + plan freshness).
- The clearest guardrails against accidental production targeting appear in `copilot-v1` (host allowlist + explicit host confirmation + max-ops cap).
- `antigravity-v1` and `antigravity-v2` are useful prototypes with quick local UX, but they currently lack testing depth and robust conflict/safety controls.

If choosing a starting point for production-hardening today, start with `claude-v1` (or `claude-v2` if browser-first and merge-heavy workflows are top priority), then adopt selected guardrails from `copilot-v2` and `copilot-v1`.

## Compare and Contrast

## 1) Core Architecture

| Branch | Primary Interface | Local Storage | Content Representation | Sync Shape |
|---|---|---|---|---|
| antigravity-v1 | Browser UI + Express API | SQLite (`posts`, `media`) | Markdown stored in DB, converted on push | Immediate push/pull API |
| antigravity-v2 | Browser UI + Express API | Filesystem (`content/drafts`, `content/published`) | Markdown files + frontmatter | Immediate push API |
| claude-v1 | Python CLI | Files + state store (`.wpsync`) | Markdown + YAML frontmatter | Pull/Push CLI with dry-run-first |
| claude-v2 | Browser dashboard + CLI | SQLite mirror + outbox/history | Raw Gutenberg HTML | Pull/Push with three-way merge |
| copilot-v1 | TypeScript CLI | Files + manifest (`.state/manifest.json`) | Markdown + frontmatter | Plan + apply style sync command |
| copilot-v2 | Python CLI | Vault folders + immutable plans + append-only ledger | `meta.json` + `body.md` + `asset://` tokens | Two-stage `plan-build` then `plan-apply` |

Observations:

- There are two major camps:
  - File-based authoring (`antigravity-v2`, `claude-v1`, `copilot-v1`, `copilot-v2`)
  - DB-backed local mirror (`antigravity-v1`, `claude-v2`)
- `copilot-v2` is the only approach that makes immutable plans a first-class artifact.
- `claude-v2` is the only implementation with explicit three-way text merge for overlapping concurrent edits.

## 2) Safety and Data Protection

| Branch | Dry-Run Default | Conflict Handling | Delete Safeguards | Target/Blast-Radius Guards | Backup Strategy |
|---|---|---|---|---|---|
| antigravity-v1 | Supported (UI can run dry run) | Basic skip on pull if local modified | Local delete confirmation only | Limited | None explicit |
| antigravity-v2 | Effectively dry-run when creds missing; UI mostly mock sync | Minimal | Local file delete | Limited | None explicit |
| claude-v1 | Strong dry-run-first push | `modified_gmt` conflict block, force override option | Confirm live title, trash/permanent options | CLI intention gates | Remote HTML backup before overwrite/delete |
| claude-v2 | Strong dry-run-first push | `modified_gmt` + three-way merge + conflict markers | Confirm live title, trash/permanent options | CLI/UI intention gates | Remote backups in outbox/history |
| copilot-v1 | Dry-run default (`--apply` required) | `modified_gmt` conflict block with explicit override | No delete operation implemented | Host allowlist, optional exact host, max operation cap | Manifest history only |
| copilot-v2 | Dry-run default (`--execute` required) | Remote modified conflict block with explicit override | No delete operation implemented | ACK phrase, plan checksum, plan freshness, host allowlist, max operations | Append-only ledger + plan artifacts |

Observations:

- Best safety primitives across all branches are fragmented and complementary.
- `claude-v1/v2` are strongest on recoverability (pre-write backups) and explicit delete safeguards.
- `copilot-v2` is strongest on preflight integrity and operator intent confirmation.
- `copilot-v1` is strongest on destination validation and blast-radius caps.

## 3) Offline Authoring Experience

| Branch | Offline Authoring Strength | Notable Limitations |
|---|---|---|
| antigravity-v1 | Simple local browser editor; quick to start | Less explicit review pipeline; limited merge/conflict sophistication |
| antigravity-v2 | File-based markdown plus browser management | Sync logic is basic; mostly prototype-grade safeguards |
| claude-v1 | Git-friendly markdown workflow, strong CLI ergonomics | CLI-centric; less visual editing unless using external editor/preview |
| claude-v2 | Browser-native editing, preview/history, rich conflict workflow | Raw Gutenberg HTML is less pleasant than markdown for long prose |
| copilot-v1 | Straightforward markdown workflow with explicit sync knobs | Simpler renderer/taxonomy handling in current form |
| copilot-v2 | Deliberate plan/review/apply loop, clean audit trail | More steps; not as immediate for quick edits |

## 4) Testing and Delivery Maturity

- antigravity-v1: no tests in branch.
- antigravity-v2: no tests in branch.
- claude-v1: substantial pytest suite (CLI, content, state, sync, WP client).
- claude-v2: meaningful Node test suite including sync and fake WP server.
- copilot-v1: focused but small unit tests (config/hash/strava).
- copilot-v2: focused but small unit tests (planner/apply/utils).

Implication:

- `claude-v1` and `claude-v2` are currently strongest foundations for reliability-sensitive production use.
- `copilot-v1`/`copilot-v2` have good safety ideas that should be expanded with more integration tests.

## Best Practices to Share Across Implementations

## A) Safety Controls Worth Standardizing

1. Make every mutating sync path dry-run by default (`claude-v1`, `claude-v2`, `copilot-v1`, `copilot-v2`).
2. Require explicit intent for apply (`--apply` or `--execute`) and additionally require human confirmation tokens for high-risk operations (`copilot-v2` ACK phrase).
3. Add host allowlisting + optional exact host confirmation before any remote write (`copilot-v1`, `copilot-v2`).
4. Enforce operation-count caps to prevent accidental bulk changes (`copilot-v1`, `copilot-v2`).
5. Record recoverable backups before update/delete (`claude-v1`, `claude-v2`).
6. Keep delete as a separate, high-friction command with remote-title confirmation (`claude-v1`, `claude-v2`).

## B) Conflict Strategy Improvements

1. Minimum baseline: `modified_gmt` drift detection (`claude-v1`, `copilot-v1`, `copilot-v2`).
2. Advanced mode: three-way merge with clear conflict markers and forced overwrite option (`claude-v2`).
3. Hybrid recommendation: keep timestamp guard always, then optionally attempt three-way merge for update operations if drift detected.

## C) Auditability and Operability

1. Immutable plan artifacts with checksums (`copilot-v2`) improve change review and compliance.
2. Append-only event/ledger records (`copilot-v2`, `claude-v2` outbox/history pattern) aid debugging and incident review.
3. File-based content export from DB-centric systems (`claude-v2` export concept) improves portability and backup strategy.

## D) Content and Media Handling

1. Preserve unsupported/unknown plugin markup without lossy conversion (`claude-v1` raw preservation concept).
2. Support local media references and hash-based deduplicated uploads (`claude-v2`; also present in copilot variants).
3. Keep author-facing syntax simple (`claude-v1` markdown + lightweight route markers; `copilot-v2` asset tokens).

## Key Questions That Should Determine the Choice

1. Authoring mode preference:
- Do editors prefer markdown files in Git, or browser-native dashboard editing?

2. Conflict profile:
- How often are posts edited both offline and in wp-admin concurrently?
- If frequent, is three-way merge worth the complexity?

3. Governance and audit needs:
- Do you need immutable plan artifacts and explicit approvals for every publish window?

4. Recovery requirements:
- Is guaranteed pre-write backup and easy rollback mandatory?

5. Team size and skill mix:
- CLI-heavy workflow acceptable, or should non-technical users operate via browser UI only?

6. Operational risk tolerance:
- Is destination-host verification and max-operations guard mandatory before writes?

7. Durability strategy:
- Should content itself be Git-tracked source of truth, or can a local DB be primary with periodic export?

8. Plugin/content fidelity needs:
- Do you need exact Gutenberg/raw HTML fidelity or markdown-first authoring with selective passthrough?

## Recommended Direction

## Recommendation in one line

Start with `claude-v1` as the baseline, then selectively adopt high-value controls and capabilities from `copilot-v2`, `copilot-v1`, and `claude-v2`.

## Why this baseline

`claude-v1` currently offers the best balance of:

- mature safety posture,
- git-friendly offline writing,
- explicit pull/push semantics,
- and stronger automated test depth.

## Add these ideas immediately

1. From `copilot-v2`:
- Add immutable plan files for every push candidate set.
- Add checksum validation and plan-age expiration.
- Add explicit ACK phrase requirement before apply.

2. From `copilot-v1`:
- Add strict host allowlist + exact host confirmation option.
- Add max operation cap + explicit large-sync override.

3. From `claude-v2`:
- Add optional three-way merge mode for timestamp conflicts.
- Add integrated operation history/outbox view.

4. From `claude-v1`/`claude-v2`:
- Keep pre-write backups and high-friction delete safeguards as non-negotiable defaults.

## Suggested Hybrid Target Architecture

- Authoring layer: markdown files in Git (`claude-v1` style).
- Sync planning layer: immutable plan artifact (`copilot-v2` style).
- Apply gate: host checks + op cap + ACK (`copilot-v1` + `copilot-v2`).
- Conflict layer: timestamp check first, optional three-way merge fallback (`claude-v2`).
- Recovery layer: automatic pre-write backup + append-only apply log (`claude-v1/v2` + `copilot-v2`).
- Optional UX layer: browser dashboard over the same engine for non-CLI users (`claude-v2` style).

## Practical Next Step Plan

1. Select baseline branch (`claude-v1`) and freeze behavior with its current tests.
2. Introduce plan artifact generation and verification before apply.
3. Add host allowlist and max-op guard in apply path.
4. Add append-only operation ledger and backup indexing.
5. Implement optional three-way merge for update conflicts only.
6. Add integration tests against a fake WordPress API covering all safety gates.

## Final Note

The implementations are not mutually exclusive; they form a useful design palette. The best production result is likely a hybrid: conservative by default, auditable by design, and flexible enough to support both markdown-centric and browser-centric contributors.
