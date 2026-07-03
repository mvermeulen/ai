# wpsync

Offline-first authoring and sync tool for **[Gone to Look for America](https://mvermeulen.org/gone2look4america/)**,
a WordPress blog documenting a six-month bicycle ride visiting state capitols.

The problem it solves: doing everything the WordPress mobile app or
dashboard can do -- drafting posts, moderating comments, tidying up media
alt text, checking site stats -- today means being online the whole time.
`wpsync` moves that work to plain text/YAML files on your laptop -- write
on a train, in a tent, wherever -- and syncs everything to the live site in
one deliberate step once you have a connection.

```
content/
  posts/                one Markdown file per blog post
  pages/                one Markdown file per page
  media/                photos and other files referenced from posts/pages
    routes/             GPX track logs for the {{route:...}} marker
  comments/             one YAML file per post/page's comment thread
  media-library.yaml    offline manifest of the remote media library (alt text, captions, titles)
  taxonomy.yaml         categories/tags, curated offline
  site.yaml             site title/tagline/timezone
```

Each file is Markdown with a small YAML frontmatter header:

```markdown
---
title: Austin, Back in Texas
slug: austin-back-in-texas
type: post
status: draft
date: 2026-07-01T09:00:00
categories: [texas]
tags: [trains, austin]
excerpt: Three days of train travel and I am back in Austin.
featured_image: media/austin-skyline.jpg
---

Three days of train travel and I am back in Austin. The first two days
were from Seattle to Chicago, then on to Austin.

![The skyline from the train](media/austin-skyline.jpg "Rolling into Austin")

Here's the route I rode out of town on before catching the train:

{{strava:route:12345678}}

- Slept in a real bed for the first time in weeks
- Ate entirely too much brisket
```

That's it -- no WordPress-specific markup to learn beyond the
`{{strava:route:ID}}` / `{{strava:activity:ID}}` and `{{route:...}}`
markers (see below). Edit these files in any text editor, commit them to
git like anything else, and run `wpsync` when you're ready to publish.

## What's new in v3

v1 covered drafting/editing posts and pages offline. v3 extends the same
plain-text-files-plus-deliberate-sync model to the rest of what the
[WordPress mobile app](https://apps.wordpress.com/mobile/) can do, while
keeping every new capability just as offline-first and dry-run-first as
the original:

* **Comment moderation & replies**, offline (`wpsync comments ...`) --
  mirror comment threads to YAML, draft replies and approve/hold/spam/trash
  decisions on a train, apply them in one deliberate step later.
* **Media library metadata** (`wpsync media ...`) -- review and fix alt
  text/captions/titles across your whole media library offline, without
  re-uploading any image bytes.
* **Taxonomy curation** (`wpsync taxonomy ...`) -- draft new
  categories/tags and edit descriptions offline before they exist on WordPress.
* **Site settings** (`wpsync settings ...`) -- edit the site title/tagline/timezone
  in `content/site.yaml` and sync it like everything else.
* **GPX route stats** -- drop a `.gpx` track log in `content/media/routes/`
  and reference it with `{{route:media/routes/day1.gpx}}` to get a
  computed distance/elevation summary and an inline SVG elevation profile,
  entirely offline (see below).
* **`wpsync insights`** -- offline content analytics: word counts, reading
  time, publishing cadence, taxonomy coverage, orphaned media files, and
  accessibility warnings (missing alt text/excerpts). No network, ever.
* **`wpsync serve`** -- an optional local web GUI for editing content with
  a live preview pane, browsing the media library, and moderating
  comments in a browser instead of a text editor. It **never** talks to
  WordPress -- see [The local web GUI](#the-local-web-gui).
* **Extra safety hardening** -- a pinned-host check, a cap on how many
  items can be applied in one batch, and a typed confirmation before any
  `--apply` actually runs. See [The safety model](#the-safety-model).

## Install

```bash
python3 -m venv .venv
.venv/bin/pip install -e ".[dev]"
```

This installs the `wpsync` command into `.venv/bin/`. `.venv/bin/pip install -e ".[gui]"`
additionally installs Flask, needed only for `wpsync serve` (the optional
web GUI) -- the CLI itself doesn't need it.

## First-time setup

```bash
wpsync init --base-url https://mvermeulen.org/gone2look4america --username <your-username>
```

This writes `.wpsync/config.yml` (gitignored -- it's local machine config,
not something to publish). It does **not** ask for a password: create a
WordPress [Application Password](https://make.wordpress.org/core/2020/11/05/application-passwords-integration-guide/)
instead (WordPress admin -> Users -> Profile -> Application Passwords).
Application Passwords are scoped, independently revocable credentials --
unlike your real login password, you can invalidate one from your profile
page at any time without affecting the account itself, which is exactly
what you want handed to a script.

Put the generated password in your shell, never in a file that gets
committed:

```bash
export WPSYNC_APP_PASSWORD='xxxx xxxx xxxx xxxx xxxx xxxx'
wpsync test-connection
```

## Everyday workflow

```bash
# Start a new post or page
wpsync new post "Crossing into New Mexico"
wpsync new page "Gear List" --status draft

# Edit content/posts/crossing-into-new-mexico.md in your editor...
# ...drop photos into content/media/ and reference them with ![]()...

# See what you've changed locally -- entirely offline, no network
wpsync status

# Read it back like it'll actually look, in a browser, offline
wpsync preview crossing-into-new-mexico

# Once you're back online: pull down anything edited on the live
# dashboard since your last sync (skips files you've changed locally)
wpsync pull

# Review exactly what would be sent -- this is always a dry run
wpsync push

# Satisfied? Actually send it
wpsync push --apply
```

`wpsync push` on its own **never changes anything on the server** -- it
always shows a plan first (new posts/pages that would be created, diffs
for anything that would be updated, and any conflicts) and only touches
WordPress when you re-run it with `--apply`. Think `terraform plan` /
`terraform apply`.

## The safety model

This tool has API access to a real, already-populated WordPress site, so
its defaults are built around not stepping on existing content:

* **Dry run by default.** `push` without `--apply` performs zero writes.
  Every mutating command tells you exactly what it's about to do first.
* **Conflict detection.** Before overwriting a post or page, `wpsync`
  fetches its current `modified_gmt` timestamp and compares it against
  what was recorded at the last successful sync. If someone (or
  something) edited it directly in the dashboard in the meantime, push
  stops with a `conflict` instead of silently clobbering that edit --
  you either `wpsync pull` to bring the remote edit down first, or pass
  `--force` to knowingly overwrite it.
* **Automatic backups before every overwrite or delete.** The live
  HTML is saved to `.wpsync/backups/<slug>/<timestamp>.html` immediately
  before any update or delete call goes out, so a mistake is recoverable
  even outside of WordPress's own revision history.
* **Deletion is a distinct, deliberate command**, never a side effect of
  push. `wpsync delete <slug> --confirm-title "Exact Current Title"`
  requires the title argument to match the *live* title byte-for-byte,
  which stops a stale local slug from deleting the wrong thing. It trashes
  (recoverable from the dashboard) unless you also pass `--permanent`.
* **Unknown markup is preserved, never mangled.** This site uses NextGen
  Gallery and WP Google Maps plugins, whose output `wpsync` doesn't
  understand. When pulling a post that contains something it can't
  faithfully turn into Markdown, it's kept byte-for-byte in a
  `<!--raw--> ... <!--/raw-->` fence in the Markdown file instead of being
  dropped or guessed at, and is passed straight back through untouched
  on the next push.
* **Local edits are never silently discarded.** `pull` checks each local
  file against what it looked like at last sync; if you've edited it
  offline, pull skips that file (and tells you) rather than overwriting
  your draft, unless you explicitly pass `--force`.
* **Host pinning (v3).** The first network call to a site pins its host
  into `.wpsync/pinned_host.json`. Every later command that talks to
  WordPress -- including a read-only `pull` or a dry-run `push`'s
  conflict-detection checks -- verifies the configured `base_url` still
  resolves to that same host *before making any network call at all*.
  This guards against a stray edit to `config.yml`, or running `wpsync`
  from the wrong project checkout, silently pointing it at an unintended
  site. A genuine host change requires `--confirm-host-change`.
* **Batch cap (v3).** Applying more than 20 changes in one `--apply` (a
  post/page push, or a comments/media/taxonomy push) requires
  `--yes-large-batch`. This guards against a corrupted or wiped state file
  suddenly making everything look new/modified and pushing far more than
  intended.
* **Typed confirmation (v3).** Immediately before `--apply` actually
  mutates WordPress, the plan is shown again and (only when running in a
  real terminal) you must type `yes`. Pass `--yes` to skip this in scripts
  or CI, where a prompt nobody can answer would just hang.
* **Comments and taxonomy are additive-only.** Comment moderation never
  deletes a comment (only approve/hold/spam/trash, same as the dashboard),
  and taxonomy sync only ever creates new terms or edits descriptions --
  never renames or deletes a category/tag, since either is easy to get
  wrong from a stale local copy.

## The `{{strava:...}}` and `{{route:...}}` markers

Write `{{strava:route:12345678}}` or `{{strava:activity:12345678}}` on its
own line to link an already-published Strava route. It's rendered as the
bare canonical Strava URL sitting alone in its own paragraph -- which is
exactly the shape WordPress's built-in oEmbed support looks for to turn a
plain link into a rich embed automatically, no plugin required. Pulling a
post back down recognizes a lone Strava URL and turns it back into the
same marker.

**`{{route:media/routes/day1.gpx}}` (v3)** is for a GPX track log you're
carrying around that isn't (or isn't yet) on Strava. Drop the `.gpx` file
under `content/media/routes/`, reference it with the marker (path relative
to `content/`, same convention as image references), and `wpsync` computes
distance, elevation gain/loss, and renders an inline SVG elevation profile
-- entirely offline, using only the standard library (no mapping service,
no upload). A missing or unparseable GPX file renders a clearly-labeled
warning block instead of failing the whole render, so one broken
reference doesn't break `status`/`push`/`preview` for everything else.
Pulling a post back down recognizes wpsync's own rendered route block and
turns it back into the same marker.

## The local web GUI

`wpsync serve` starts a local web server (`http://127.0.0.1:8642` by
default) with a dashboard, a post/page editor with a live preview pane,
and browsers for the media library, comments, taxonomy, and settings.

**It never makes a network call to WordPress.** Every route in
`wpsync/webapp.py` only reads or writes files inside the project; anything
that would actually sync to the live site (`push --apply`, `pull`,
`delete`, `comments/media/taxonomy/settings push --apply`, ...) is shown
as a copyable CLI command to run yourself instead of a button the page can
click for you -- the same dry-run-first, deliberate-apply-step boundary as
everywhere else in wpsync, just made visible in the browser. This also
means the GUI has a much smaller safety surface to get right: there's no
POST endpoint anywhere in it that can reach the internet.

```bash
.venv/bin/pip install -e ".[gui]"   # Flask, only needed for serve
wpsync serve
```

Binds to `127.0.0.1` only by default; `--host` accepts other addresses but
prints a loud warning, since that would expose local content editing to
your network. Every form submission requires a per-process CSRF token
(rejected with 403 if missing/wrong), as defense-in-depth given this
process does perform local filesystem writes.

## Commands

| Command | Network? | What it does |
|---|---|---|
| `wpsync init` | no | Write `.wpsync/config.yml` for this site |
| `wpsync test-connection` | yes | Verify the configured credentials work |
| `wpsync new post\|page TITLE` | no | Scaffold a new Markdown file |
| `wpsync schedule SLUG WHEN` | no | Set a post/page to `status: future` for a scheduled publish |
| `wpsync status` | no | Show new / modified / unchanged for local files |
| `wpsync preview SLUG` | no | Render to HTML and open in a browser |
| `wpsync diff SLUG` | no | Diff the last-synced remote HTML against the current local render |
| `wpsync pull [--force]` | yes | Bring remote posts/pages down into local Markdown |
| `wpsync push [--apply] [--force]` | yes | Dry-run (default) or apply local changes to WordPress |
| `wpsync delete SLUG --confirm-title "..."` | yes | Trash (or `--permanent`ly delete) a synced post/page |
| `wpsync insights` | no | Offline content analytics: word counts, cadence, accessibility warnings |
| `wpsync serve [--host] [--port]` | no | Local web GUI for editing/preview (never talks to WordPress) |
| `wpsync comments pull\|status\|set\|push` | pull/push only | Offline comment moderation and replies |
| `wpsync media pull\|status\|set\|push` | pull/push only | Offline media alt text/caption/title editing |
| `wpsync taxonomy pull\|add\|describe\|push` | pull/push only | Offline category/tag curation |
| `wpsync settings pull\|status\|push` | pull/push only | Offline site title/tagline/timezone editing |

Every `push` subcommand (post/page, comments, media, taxonomy, settings)
follows the same pattern: it always shows a dry-run plan first, and
`--apply` is additionally gated by the pinned-host check, the batch cap,
and a typed confirmation described in [The safety model](#the-safety-model).

## Known limitations

* Images pushed by `wpsync` become plain `<img>` tags. WordPress will
  display them fine, but they won't carry the `srcset`/`sizes` responsive
  attributes the block editor normally generates until you re-save the
  post in the dashboard. A future version could rebuild these from the
  media API's registered sizes.
* The very first `push` of a post that already existed before `wpsync`
  touched it will show a full-file diff even with zero intended edits --
  `wpsync`'s renderer doesn't byte-for-byte reproduce whatever originally
  generated that post's HTML (classic editor, a different plugin, etc.).
  This is cosmetic: the `unchanged` vs. `modified` decision in `status`
  and `push` is based on comparing wpsync's own renderer against itself
  across runs, not against WordPress's copy, so it doesn't cause spurious
  "changed" states on every later push -- just the first one.
* Galleries, maps, and other plugin-rendered embeds round-trip as opaque
  raw HTML (see above) -- they're preserved perfectly but aren't editable
  as Markdown; edit those directly in the dashboard if needed.
* **`wpsync insights` is not visitor analytics.** WordPress.com/Jetpack's
  Stats tab (views, visitors, traffic sources) needs Jetpack's own
  pipeline and a different auth flow than the core REST API this tool
  otherwise sticks to; faking that offline would be misleading. Insights
  is honest, purely local content analytics instead -- useful, but a
  different thing.
* Taxonomy sync never renames or deletes a category/tag, and comment
  moderation never permanently deletes a comment -- both are deliberate
  scope limits, not oversights (see the safety model above).
* The comment-reply feature posts as whatever WordPress user the
  Application Password belongs to; there's no support for replying as a
  different author.

## Running the tests

```bash
.venv/bin/pip install -e ".[dev]"   # includes Flask, for the webapp tests
.venv/bin/pytest
```

136 tests. All WordPress API interaction is mocked (`requests_mock`)
against a fake `https://example.test` site, and the web GUI is tested via
Flask's test client (no real server binding) -- development has been done
with no credentials to the real site at all, by design, on both v1 and v3.
Tests specifically cover: content round-tripping (including the new
`{{route:...}}` marker), GPX distance/elevation math against a known
value, conflict detection, dry-run no-mutation guarantees, the
comments/media/taxonomy/settings pull-preserves-local-edits invariant, and
the v3 safety hardening end-to-end through the CLI (host-pin blocking a
push with zero network calls made, and the batch cap blocking an
oversized apply).
