# wordpress-local-sync

A local-first workflow for offline WordPress editing with cautious, explicit synchronization.

## Why this exists

You can draft and edit content offline in plain markdown files, organize media locally, and only push to WordPress when internet is available. Sync is intentionally conservative:

- Dry-run by default
- No delete operations implemented
- Explicit create/update switches required
- Conflict detection when remote content changed since last sync
- Host allowlist and optional exact-host confirmation
- Apply operation cap guardrails

## Features

- Offline drafts for posts and pages in `content/posts` and `content/pages`
- Markdown with YAML frontmatter
- Local media references (for example: `![alt](media/image.jpg)`)
- Optional Strava route link discovery from markdown body
- Pull existing content from WordPress into local files
- Stateful manifest in `.state/manifest.json` to track mappings and conflict baseline

## Project layout

- `content/posts/*.md`: Local post drafts and edits
- `content/pages/*.md`: Local pages
- `media/*`: Local media to upload on sync apply
- `.state/manifest.json`: Mapping from local content to remote IDs and last sync hashes

## Setup

1. Install Node.js 20+.
2. Install dependencies:

```bash
npm install
```

3. Create env file:

```bash
cp .env.example .env
```

4. Fill credentials in `.env` when available:
- `WP_SITE_URL`
- `WP_USERNAME`
- `WP_APPLICATION_PASSWORD`
- `WP_ALLOWED_HOSTS`
- `WP_MAX_APPLY_OPERATIONS`

5. Initialize workspace folders:

```bash
npm run dev -- init
```

## Usage

Create new local draft:

```bash
npm run dev -- new --type post --title "Ride to the Canyon"
```

Plan sync (dry run):

```bash
npm run dev -- plan --allow-create --allow-update
```

Pull remote content for offline editing:

```bash
npm run dev -- pull --type all --limit 30
```

Apply sync safely:

```bash
npm run dev -- sync --apply --allow-create --allow-update --confirm-host mvermeulen.org
```

If operation count exceeds your cap, add:

```bash
npm run dev -- sync --apply --allow-create --allow-update --approve-large-sync --confirm-host mvermeulen.org
```

## Safety model

Sync apply is blocked unless all of these pass:

- Target host is in `WP_ALLOWED_HOSTS`
- Optional `--confirm-host` matches configured host
- Credentials are present for apply
- Operation count is under cap, or `--approve-large-sync` is set
- No remote conflicts unless `--allow-conflicts` is set

No deletion endpoint is called in this implementation.

## Markdown frontmatter example

```yaml
---
title: "Great Divide Day 3"
slug: "great-divide-day-3"
status: draft
tags: [bikepacking, colorado]
categories: [adventure]
strava_routes:
  - https://www.strava.com/routes/1234567890
---

Today I rode through...
```

## Notes

- WordPress REST API can differ per plugin/theme setup; test in a staging site first.
- HTML conversion is intentionally simple in this version.
- Existing WordPress taxonomies are not auto-created in v1.

## Tests

```bash
npm test
```
