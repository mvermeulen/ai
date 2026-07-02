# wpsync2

A second, deliberately different take on offline-first authoring and sync
for **[Gone to Look for America](https://mvermeulen.org/gone2look4america/)**
(see the `wordpress-local-claude-v1` branch for the first one -- a Python
CLI over Markdown files). This version explores a different point in the
design space: a **local web dashboard** over a **SQLite mirror** of the
site, editing **raw Gutenberg HTML directly**, with a real **three-way
merge** for conflicts instead of an all-or-nothing block.

```bash
npm install
npm run dashboard        # http://localhost:4173 -- open a browser, click "Set up wpsync2"
```

Everything -- creating posts, editing, previewing, reviewing a push,
resolving a conflict, deleting -- can be done from the browser with zero
command-line use beyond starting the process. A CLI (`npx tsx src/cli.ts`)
covers the same ground for scripting.

## What's different from v1, and why

| | v1 (`wordpress-local-claude-v1`) | v2 (this branch) |
|---|---|---|
| Language | Python | TypeScript / Node |
| Storage | One Markdown file per post/page + a JSON state file | One SQLite database (`.wpsync2/site.db`) |
| Content format | Markdown, converted to Gutenberg HTML on push | Raw Gutenberg block HTML, edited directly |
| Primary interface | CLI | Browser dashboard (+ a thin CLI) |
| Conflict handling | Block the push if the remote changed at all | Real three-way merge; only overlapping edits are a conflict |
| Git-friendliness | Content *is* the git history | Content lives in a gitignored local DB (see below) |
| Extra capability | -- | GPX route file -> distance/elevation summary block |

Neither approach is strictly better -- they trade off differently:

**Markdown files (v1) are git-friendly and portable**: you can `git diff`
a post, review it in a PR, edit it in any editor anywhere. Raw Gutenberg
HTML (v2) is exactly what WordPress stores, so nothing is ever lost in
translation -- but it's less pleasant to hand-write and the SQLite file
itself is gitignored, so **git is not this version's durability story**.
That's a genuine tradeoff, not an oversight: if you want the local
database itself backed up or reviewable, see `wpsync2 export` below,
which was added specifically to borrow v1's git-friendly-files idea into
this architecture.

**All-or-nothing conflict blocking (v1) is simple and never surprises
you.** A real three-way merge (v2) is more capable -- editing your intro
locally while someone fixes a typo in your closing paragraph on the
dashboard merges automatically instead of stopping you -- but a merge
algorithm is more moving parts than a timestamp comparison, and a
successful *auto*-merge is still worth skimming before you trust it
blindly on a live site.

## How the three-way merge works

Every row keeps three states, on purpose:

* **local** -- what's in the editor right now.
* **base** -- what it looked like at the last successful sync (the common
  ancestor).
* **remote** -- fetched fresh from WordPress at push time, never cached.

If remote's `modified_gmt` hasn't moved since `base`, it's a plain update.
If it *has* moved, `push` runs [node-diff3](https://github.com/bhousel/node-diff3)
(the same algorithm behind `git merge`) over local vs. base vs. remote,
line by line:

* Non-overlapping changes on both sides merge automatically and go
  straight to WordPress.
* Overlapping changes come back as a `conflict` outcome with `<<<<<<<` /
  `=======` / `>>>>>>>` markers, shown in the dashboard for you to resolve
  by editing -- push is refused until you do, or you pass `--force` to
  take the local copy as-is.

## The safety model

Same non-negotiables as v1, adapted to this architecture:

* **Push always dry-runs first.** The dashboard's `/push` page and the
  CLI's `wpsync2 push` (no `--apply`) never write to WordPress -- they
  show you the plan (diffs, merges, conflicts) and nothing else.
* **Conflict detection via `modified_gmt`**, as above, refusing to
  overwrite an edit made directly on the live site unless forced.
* **An implicit backup on every write.** Before any update or delete, the
  live HTML is fetched and stored in the `outbox` table alongside the
  outcome -- see `/history` in the dashboard. There's no separate backups
  folder to lose track of; it's one `SELECT` away.
* **Delete requires the exact live title**, same as v1, trashing by
  default (`--permanent` to skip the trash).
* **Local edits are never silently discarded.** `pull` skips (and
  reports) any row whose `local_content` has diverged from `base_content`
  rather than overwriting it.
* **The application password never touches a browser form.** The
  dashboard reads `WPSYNC2_APP_PASSWORD` from the process environment
  only; there is deliberately no password input field anywhere in the UI,
  so it can't end up in a request log, browser autofill, or this
  project's own database.

## Setup

```bash
npm install
npm run dashboard
```

Open http://localhost:4173, fill in the site URL and username under
"Set up wpsync2". Then create a WordPress
[Application Password](https://make.wordpress.org/core/2020/11/05/application-passwords-integration-guide/)
(admin -> Users -> Profile -> Application Passwords) and restart the
dashboard with it set:

```bash
WPSYNC2_APP_PASSWORD='xxxx xxxx xxxx xxxx xxxx xxxx' npm run dashboard
```

Editing, previewing, and the GPX tool all work with zero credentials;
Pull/Push/Delete light up once the password is set.

## The `{{strava}}` -> GPX story

v1 added a `{{strava:route:ID}}` marker for Strava links. This version
keeps that spirit but adds something Strava-shaped tooling doesn't cover:
a day's ride before it's ever reached Strava. Drop the `.gpx` a bike
computer or phone app exported into the "GPX tool" page (or the per-post
"insert a route summary" box), and it computes distance, elevation gain,
and elapsed time straight off the track points, rendering a ready-to-use
content block -- fully offline, no upload required.

## Content model

Posts/pages live as rows in `.wpsync2/site.db`, each with:

- `title`, `slug`, `status`, `date`, `excerpt`, `categories`, `tags`
- `local_content` -- the Gutenberg block HTML you're editing, e.g.:

  ```html
  <!-- wp:paragraph -->
  <p class="wp-block-paragraph">We rolled into Austin after three days on the train.</p>
  <!-- /wp:paragraph -->

  <!-- wp:image {"sizeSlug":"large"} -->
  <figure class="wp-block-image size-large"><img src="media/austin.jpg" alt=""/></figure>
  <!-- /wp:image -->
  ```

  Reference local files as `media/filename.ext`; they're uploaded to
  WordPress automatically (deduplicated by content hash) the first time a
  push touches a post that references them, and the `<img src>` is
  rewritten to the real WordPress URL only in what gets sent -- your
  local file keeps its own relative path.

## Commands (CLI)

| Command | Network? | What it does |
|---|---|---|
| `wpsync2 init --base-url <url> --username <name>` | no | Write `.wpsync2/config.json` |
| `wpsync2 test-connection` | yes | Verify the configured credentials work |
| `wpsync2 new <post\|page> "Title"` | no | Create a local row |
| `wpsync2 status` | no | new / modified / unchanged, purely local |
| `wpsync2 pull` | yes | Bring remote content down (skips rows with unsynced local edits) |
| `wpsync2 push [slugs...] [--apply] [--force]` | yes | Dry-run (default) or apply, with three-way merge |
| `wpsync2 delete <slug> --confirm-title "..." [--permanent] [--yes]` | yes | Trash (or permanently delete) |
| `wpsync2 export` | no | Dump all local content to `export/<slug>.html` files, git-friendly |

Run any of these with `npx tsx src/cli.ts <command>` during development,
or `npm run build && node dist/cli.js <command>` after building.

## Known limitations (v1's README documented its own; here are this
version's)

* **Not git-friendly by default.** The SQLite file is gitignored, so
  unlike v1 there's no automatic version history of your writing itself
  -- only of this codebase. If that matters to you, mix in v1's approach
  for the content layer, or export rows to files periodically.
* **No Markdown.** You write (or the dashboard's "insert" helpers write)
  Gutenberg HTML directly. That's zero-conversion-loss but a rougher
  authoring experience than v1's Markdown for long-form prose.
* **A clean auto-merge is not proof against anything.** node-diff3
  correctly detects *textual* overlap, not *semantic* conflict -- two
  non-overlapping edits that don't make sense together will still merge
  silently. Skim the dry-run diff before applying.
* **Requires Node >= 22.5** (built-in `fetch`, and this was developed
  against Node 22.22).

## Tests

```bash
npm test
```

24 tests (`node:test`, no test framework dependency) cover the DB layer,
GPX parsing, and -- most importantly -- the sync engine: clean pulls,
pulls that correctly refuse to clobber local edits, dry-run no-mutation
guarantees, a clean update, a real non-overlapping auto-merge, an actual
overlapping conflict (checked to *not* write anything until resolved or
forced), delete's title-match safeguard, and media upload dedup. A small
in-memory fake of the WordPress REST API (`tests/fakeWordPress.ts`) drives
all of it -- like v1, no real credentials were used or needed to build
or test this.
