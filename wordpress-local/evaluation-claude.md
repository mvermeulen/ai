# Evaluating the Six WordPress-Local Implementations

## Context

The original brief (`prompt.txt`, given identically to three tools — Antigravity, Claude, and Copilot — each asked for two independent attempts) was:

> A better solution for offline editing of https://mvermeulen.org/gone2look4america/ (a bike-touring blog on WordPress). Draft new posts/pages, edit existing ones, upload media, and link Strava routes — all offline — then "sync" when internet is available. No WordPress credentials were available during development. Explicit ask: add safety checks against accidentally destroying existing live content.

Six branches resulted:

| Branch | Language | Interface | Content format | State tracking |
|---|---|---|---|---|
| `wordpress-local-antigravity-v1` | Node/Express | Web dashboard | Markdown + frontmatter | SQLite |
| `wordpress-local-antigravity-v2` | Node/Express | Web dashboard | Markdown + frontmatter | none (re-pushes everything) |
| `wordpress-local-claude-v1` | Python | CLI | Markdown + frontmatter | flat `state.json` |
| `wordpress-local-claude-v2` | TypeScript | CLI + web dashboard | Raw Gutenberg HTML | SQLite |
| `wordpress-local-copilot-v1` | TypeScript | CLI | Markdown + frontmatter | flat `manifest.json` |
| `wordpress-local-copilot-v2` | Python (stdlib only) | CLI | Markdown + JSON "vault" | append-only ledger + immutable plan files |

All six are single-session prototypes (1–2 commits, ~750–2,500 LOC) built against WordPress's REST API with Application Password auth. None talk to WP-CLI, SSH, or the database directly, and none were ever run against a live site — this is worth remembering when judging "maturity."

## Comparing the approaches

### Two genuinely different architectures

Four of the six (`claude-v1`, `copilot-v1`, `copilot-v2`, and both `antigravity` branches) converge on the same shape: **Markdown files with YAML/JSON frontmatter, hashed and diffed against a small state file, pushed to WP as rendered HTML.** This is the "obvious" design and it shows — they differ mostly in polish, not concept.

`claude-v2` breaks from this: it authors content as **raw Gutenberg HTML directly in a local SQLite mirror**, and does real three-way merges (`node-diff3`) between local edits, the last-synced baseline, and the live remote copy. This is a materially more ambitious (and more complex) design — it's the only one that can *auto-resolve* non-overlapping concurrent edits rather than just detecting and blocking on conflicts. The tradeoff is a steeper authoring UX (no Markdown) and a gitignored DB that loses content history unless explicitly exported.

`antigravity-v2` is the outlier in the other direction: it has no pull, no diffing, and just re-pushes every post on every sync. Given the prompt's explicit safety requirement, this is the weakest response to the actual brief.

### Conflict detection, ranked by sophistication

This is the axis that most separates a toy from a tool, since the entire point of the brief is "don't destroy live content while I've been editing offline."

1. **`claude-v2`** — real three-way merge via `diff3`; only genuinely-overlapping edits are blocked, everything else auto-merges. Most sophisticated, but also least tested against WordPress's actual HTML quirks.
2. **`claude-v1`** — two independent signals (remote `modified_gmt` timestamp *and* a hash of wpsync's own last-rendered output) before allowing an overwrite; forces `--force` to proceed.
3. **`copilot-v1` / `copilot-v2`** — single-signal `modified_gmt` comparison; blocks the operation unless overridden. Simpler, and only guards updates, not create-time slug collisions.
4. **`antigravity-v1`** — protects pulls (won't clobber locally-modified rows) but has no equivalent guard on push beyond a client-side confirmation modal.
5. **`antigravity-v2`** — none. Blind overwrite, every time.

### Safety rails beyond conflict detection

The prompt explicitly asked for protection against destroying content, and this produced some of the most interesting divergence:

- **`copilot-v2`'s plan/apply/ACK model** is the standout idea here and appears nowhere else: `plan-build` emits an immutable, checksummed JSON plan; `plan-apply` re-verifies the checksum (tamper detection) and requires the caller to type back an exact `APPROVE-<plan_id>` phrase before anything executes. This is a meaningfully stronger human-in-the-loop gate than "pass `--apply`."
- **`copilot-v1` and `copilot-v2`** both add a **host allowlist** (`WP_ALLOWED_HOSTS`, optional `--confirm-host`) and an **apply-operation cap** requiring an explicit override flag for large batches — guards against pointing the tool at the wrong site or a runaway sync.
- **`claude-v1` and `claude-v2`** both take automatic timestamped backups/audit snapshots of the live content immediately before any overwrite or delete, and both require the caller to retype the exact live title before a delete proceeds (defaulting to trash, not permanent deletion).
- **`copilot-v1` and `copilot-v2`** deliberately implement no delete operation at all — the simplest possible way to bound blast radius, at the cost of a real feature gap.
- All six default to dry-run and require an explicit flag to write — this convergence suggests it's simply the correct default for this problem.

No branch validates against a real WordPress instance, so all of this safety machinery is unverified against actual REST API quirks (redirects, nonces, permalink structures, security plugins).

### Testing and documentation

Sorted roughly by rigor:

1. **`claude-v2`** — 24 tests against a hand-rolled in-memory fake WordPress server, covering the hard paths (auto-merge, forced-merge, conflict block, dedup). Best README: includes an explicit comparison table against its own v1 sibling and a documented "known limitations" section.
2. **`claude-v1`** — 5 test files (~535 lines) mocked via `requests_mock`; thorough README with an honest limitations section.
3. **`copilot-v1`** — 3 thin test files (~13 assertions), core sync engine and WP client untested; solid README.
4. **`copilot-v2`** — tests cover only pure logic (ACK mismatch, checksum tampering, slugify); no HTTP mocking at all; solid, concise README.
5. **`antigravity-v1` / `antigravity-v2`** — zero tests, no README at all (only the copied prompt file). `antigravity-v1` additionally ships a media-upload UI wired to a real API endpoint with **no JavaScript behind it at all** — dead UI. `antigravity-v2` lists `chokidar` and `turndown` as dependencies that are never imported anywhere — vestiges of unbuilt features.

### Nice touches worth stealing regardless of which base you pick

- **Tailoring to the actual blog**: `claude-v1`'s `{{strava:route|activity:ID}}` marker, `copilot-v1`/`copilot-v2`'s auto-detection of bare Strava links, `antigravity-v1`'s Strava embed panel, and `claude-v2`'s full GPX-file parser (computing distance/elevation gain and emitting a ready Gutenberg block) are all direct responses to "creating links to strava routes" in the prompt. `claude-v2`'s GPX parser is the most substantial of these — it's the only one that does something a human would otherwise do by hand in a mapping tool.
- **`claude-v1`'s raw-HTML passthrough fencing** — content WP renders that the tool doesn't understand (galleries, embeds, shortcodes) gets preserved byte-for-byte inside an HTML comment fence rather than mangled by the Markdown round-trip. This directly addresses a real weakness present in every other Markdown-based branch (`copilot-v1`'s pull command "crudely strips HTML" and will mangle lists/headings/images).
- **`claude-v1`'s "terraform plan/apply" framing** and **`copilot-v2`'s immutable/checksummed plan file** are two different maturity levels of the same good idea: make the pending change set inspectable before it's executed. Worth combining — a diffable plan file *and* a re-verified checksum before apply.
- **`claude-v2`'s secret handling**: the WP application password is readable only from an environment variable, deliberately never accepted through the dashboard UI, so it can't land in logs, browser autofill, or the SQLite DB.
- **Media dedup by content hash** (`claude-v2`) avoids re-uploading identical images on every push — a real cost/clutter saver missing from every other branch (`claude-v1` matches by filename only, which can collide).

## Key questions that should drive the choice

1. **Do you want to author in Markdown, or is WordPress's native Gutenberg HTML acceptable?** Markdown is more portable and diffs cleanly in git, but every Markdown-based branch has a lossy round-trip for anything beyond paragraphs/headings/links (galleries, embeds, columns). `claude-v2`'s HTML-native approach sidesteps this entirely at the cost of a less pleasant authoring experience.
2. **How likely are concurrent edits to the live site while you're offline?** If you're the only editor and offline sessions are short, "detect conflict and block" (claude-v1, copilot-v1/v2) is simpler and sufficient. If the site might get edited from elsewhere (or you want to feel safe merging after a long offline stretch), `claude-v2`'s three-way auto-merge is worth the extra complexity.
3. **CLI or dashboard?** A local web UI (both `antigravity` branches, `claude-v2`) is friendlier for casual drafting/preview and matches the "logging into the dashboard" muscle memory the prompt describes moving away from. A CLI (`claude-v1`, `copilot-v1/v2`) is more scriptable and pairs naturally with git for the content itself.
4. **Do you want content version history for free?** Markdown files in a git repo (claude-v1, copilot-v1/v2, antigravity) give you history automatically. `claude-v2`'s SQLite-backed model explicitly gitignores the DB and requires a separate `export` step to get git-friendly history — a real tradeoff, openly documented in its own README.
5. **How much ceremony do you want before a write actually happens?** Everything defaults to dry-run, but `copilot-v2`'s checksum + typed-ACK-phrase gate is meaningfully more conservative than a `--apply` flag. Decide whether that's reassuring or just friction for a single-user tool.
6. **Do you need delete/unpublish support at all?** If not, adopting `copilot-v1/v2`'s decision to simply not implement it is the cheapest possible safety win.
7. **Is category/tag (taxonomy) sync a real need?** None of the six branches did this well — `copilot-v1` parses taxonomy from frontmatter but never sends it; `claude-v1` implements it without caching. Whichever base you pick, budget separate work here.

## Recommendation

**Start from `wordpress-local-claude-v1`.**

It's the strongest all-around response to the actual brief: Markdown authoring (portable, diffs cleanly in git, lowest-friction for someone used to writing text), the best-documented safety model of the CLI-based branches, a working test suite with no live credentials required, and — unlike every other Markdown branch — a real answer (fenced raw-HTML passthrough) to the lossy-round-trip problem that Markdown-based sync otherwise inherits. It's also the only branch that's explicitly self-aware about its own limitations in its README rather than hiding them.

From there, deliberately backport a few ideas from its siblings rather than treating it as finished:

- **Take `copilot-v2`'s plan/ACK/checksum gate** and layer it on top of `claude-v1`'s existing dry-run diff output — you already get a printed diff before `--apply`; adding a tamper-checked plan file plus a typed confirmation phrase costs little and meaningfully raises the bar before anything touches the live site.
- **Take `copilot-v1`/`copilot-v2`'s host allowlist and max-operations cap.** These are cheap, orthogonal safety nets `claude-v1` doesn't have, and guard against a different failure mode (wrong site / runaway batch) than conflict detection does.
- **Consider `claude-v2`'s three-way merge** as an upgrade path if, in practice, you find yourself frequently blocked by conflicts that turn out to be non-overlapping. Don't build it up front — `claude-v1`'s simpler block-and-retry model is easier to reason about and matches a single-author workflow; only add merge complexity if it's actually earning its keep.
- **Take `claude-v2`'s GPX-parsing idea** (or extend `claude-v1`'s existing Strava marker) if richer route data — distance, elevation gain — is more valuable than a bare embed link for the bike-touring content this tool exists to support.
- **Fix the taxonomy gap** that every branch left incomplete: implement category/tag creation with simple in-memory caching per sync run, borrowing `claude-v1`'s `ensure_terms` as the starting point.

Treat both `antigravity` branches as reference material only, not a base — they're the least tested, least documented, and (in `v2`'s case) don't actually fulfill the "safety checks against destroying content" requirement the prompt explicitly asked for.
