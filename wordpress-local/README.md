# wpsync

Offline-first authoring and sync tool for **[Gone to Look for America](https://mvermeulen.org/gone2look4america/)**,
a WordPress blog documenting a six-month bicycle ride visiting state capitols.

The problem it solves: writing a post, editing a page, or dropping in photos
today means being online in the WordPress dashboard the whole time. `wpsync`
moves that work to plain text files on your laptop -- write on a train, in a
tent, wherever -- and syncs everything to the live site in one deliberate
step once you have a connection.

```
content/
  posts/    one Markdown file per blog post
  pages/    one Markdown file per page
  media/    photos and other files referenced from posts/pages
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

That's it -- no WordPress-specific markup to learn beyond the one
`{{strava:route:ID}}` / `{{strava:activity:ID}}` marker (see below). Edit
these files in any text editor, commit them to git like anything else, and
run `wpsync` when you're ready to publish.

## Install

```bash
python3 -m venv .venv
.venv/bin/pip install -e ".[dev]"
```

This installs the `wpsync` command into `.venv/bin/`.

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

## The `{{strava:...}}` marker

Write `{{strava:route:12345678}}` or `{{strava:activity:12345678}}` on its
own line. It's rendered as the bare canonical Strava URL
(`https://www.strava.com/routes/12345678`) sitting alone in its own
paragraph -- which is exactly the shape WordPress's built-in oEmbed
support looks for to turn a plain link into a rich embed automatically,
no plugin required. Pulling a post back down recognizes a lone Strava URL
and turns it back into the same marker.

## Commands

| Command | Network? | What it does |
|---|---|---|
| `wpsync init` | no | Write `.wpsync/config.yml` for this site |
| `wpsync test-connection` | yes | Verify the configured credentials work |
| `wpsync new post\|page TITLE` | no | Scaffold a new Markdown file |
| `wpsync status` | no | Show new / modified / unchanged for local files |
| `wpsync preview SLUG` | no | Render to HTML and open in a browser |
| `wpsync diff SLUG` | no | Diff the last-synced remote HTML against the current local render |
| `wpsync pull [--force]` | yes | Bring remote posts/pages down into local Markdown |
| `wpsync push [--apply] [--force]` | yes | Dry-run (default) or apply local changes to WordPress |
| `wpsync delete SLUG --confirm-title "..."` | yes | Trash (or `--permanent`ly delete) a synced post/page |

## Known limitations (v1)

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

## Running the tests

```bash
.venv/bin/pip install -e ".[dev]"
.venv/bin/pytest
```

All WordPress API interaction in the test suite is mocked
(`requests_mock`) against a fake `https://example.test` site --
development so far has been done with no credentials to the real site at
all, by design.
