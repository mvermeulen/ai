# wordpress-local-plan-sync (v2 alternative)

A second, intentionally different offline workflow for WordPress publishing.

This v2 approach uses immutable sync plans you can build and review offline, then apply later with explicit approval. It is designed to contrast with direct stateful sync tools.

## Big differences from a direct sync model

- Uses a local vault with entry folders (`meta.json` + `body.md`) instead of frontmatter markdown files.
- Generates explicit, immutable plan files in `.plans/` before any publish step.
- Requires an exact ACK phrase from the plan (`APPROVE-<plan_id>`) before apply.
- Enforces plan freshness and operation count caps before execution.
- Stores append-only apply history in `.cache/ledger.jsonl` rather than mutable manifest state.
- Uses `asset://filename.jpg` tokens that are resolved and uploaded at apply time.

## Directory layout

- `vault/posts/<slug>/meta.json`
- `vault/posts/<slug>/body.md`
- `vault/pages/<slug>/meta.json`
- `vault/pages/<slug>/body.md`
- `vault/assets/*`
- `.plans/plan-*.json`
- `.cache/snapshot.json`
- `.cache/ledger.jsonl`

## Setup

1. Ensure Python 3.11+ is installed.
2. Create `.env` from example:

```bash
cp .env.example .env
```

3. Fill your WordPress values in `.env`.
4. Initialize local folders:

```bash
python -m wp_local_v2.cli init
```

## Workflow

Create new offline content:

```bash
python -m wp_local_v2.cli new --type post --title "Bikepacking Day 1"
```

Optional: pull a remote index snapshot (helps planning context):

```bash
python -m wp_local_v2.cli snapshot --limit 60
```

Build immutable plan from local changes:

```bash
python -m wp_local_v2.cli plan-build
```

Inspect plan details:

```bash
python -m wp_local_v2.cli plan-show .plans/plan-<id>.json
```

Dry-run apply with explicit ACK phrase:

```bash
python -m wp_local_v2.cli plan-apply .plans/plan-<id>.json --ack APPROVE-<id>
```

Execute real apply when online:

```bash
python -m wp_local_v2.cli plan-apply .plans/plan-<id>.json --ack APPROVE-<id> --execute --confirm-host mvermeulen.org
```

## Safety checks

- No deletion operation is implemented.
- Exact ACK phrase required for every apply.
- Host must be in `WP_ALLOWED_HOSTS`.
- Optional host confirmation can be enforced with `--confirm-host`.
- Plan is rejected if too old (`WP_PLAN_MAX_AGE_HOURS`).
- Plan rejected if operation count exceeds cap unless `--force-large`.
- Update conflicts blocked when remote was modified since last applied state unless `--force-conflicts`.

## Asset and Strava support

- Put local files in `vault/assets/`.
- Reference in markdown as `asset://filename.jpg`.
- During apply, assets are uploaded and token URLs are replaced.
- Strava links (`https://www.strava.com/routes/<id>`) are detected and attached as HTML comments in published content.

## Tests

```bash
python -m unittest discover -s tests -p "test_*.py"
```
