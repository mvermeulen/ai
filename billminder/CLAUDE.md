# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

BillMinder is a personal, local-first bill-tracking system. Source code is public; operational data (SQLite DB, `.env` secrets) must never enter the repo. See `high-level-design.md` for the full requirements/design doc and `architecture.md` for the component diagram, DB schema, and REST API contract — read these before making structural changes.

## Components

- **`core/`** — C++20 service: SQLite persistence, projection/rollover logic, HTTP API (`cpp-httplib`), email notifications. This is the only component that touches the database.
- **`cli/`** — C++ CLI client. Talks to `core` exclusively via REST (`http://localhost:8080/api`), never touches SQLite directly.
- **`ui/`** — Vanilla JS/HTML/CSS single-page dashboard (`ui/js/app.js`). No frameworks. Talks to the same REST API.
- **`mobile/`** — React Native (Expo SDK 54) Android app with its own `CLAUDE.md`/`AGENTS.md` — read `mobile/AGENTS.md` before touching mobile code, and check the pinned Expo docs (versioned to SDK 54) rather than assuming latest-Expo behavior. Zero-persistence by design: no disk caching of API responses, and in-memory state is cleared when the app is backgrounded.

The CLI and Web UI are REST-only clients; all three (core, cli, ui) share the same API surface documented in `architecture.md`.

## Build & Run

```bash
./start.sh                  # builds core+cli via CMake, then launches core (port 8080) and serves ui/ (port 8000)
```

Manual build:
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```
Binaries land at `build/core/billminder_core` and `build/cli/billminder_cli`.

Requires `cmake`, `libcurl4-openssl-dev`, and a populated `third_party/` directory (gitignored, not fetched automatically) containing the SQLite amalgamation (`third_party/sqlite3/sqlite3.c`), `cpp-httplib` (`httplib.h`), and `nlohmann/json` (`json.hpp`) — these must be vendored locally before the build will succeed.

CLI usage once `core` is running: `./build/cli/billminder_cli list|add|pay|delete ...`.

## Tests

Tests use GoogleTest, fetched automatically by CMake (`FetchContent`) — no manual setup needed.

```bash
cd build
ctest                                    # run all tests
ctest -R ProjectionTest                  # run one test suite
./core/core_tests --gtest_filter=ProjectionTest.AddTimeToDate  # run a single test case
```

Test files: `core/tests/test_db.cpp` (schema/CRUD, encryption, pay/rollover persistence), `core/tests/test_projection.cpp` (recurrence-rule date math, projection generation).

## Architecture Notes

- **Bills vs. Bill Instances**: `Bill` is the recurring template (name, amount, recurrence rule, payee/url/account/password metadata). `BillInstance` is a single generated occurrence (due date, status, amount). All schedule math is done in terms of instances, not templates.
- **Rollover logic** (`core/src/server.cpp`, `Server::process_rollovers`) runs synchronously on every `GET /api/instances` call: for each bill, it finds the latest instance, and if it's past-due it marks it `overdue` (or leaves `paid` instances as-is) and spawns the next instance via `ProjectionEngine::add_time_to_date`. New instance IDs are slugified as `<bill-name>-<next-due-date>`.
- **Projection engine** (`core/src/projection.cpp`) is pure date/recurrence math — separate from rollover, used for forward-looking "what's due in N days" views.
- **Sensitive bill metadata** (`password` field) is XOR-"encrypted" at rest per `architecture.md` — not real cryptographic protection, just obfuscation against casual disclosure.
- **Notification engine** (`core/src/notification_engine.cpp`) runs on a background `std::thread` inside the same `core` process, polling and sending a weekly due-soon summary via `EmailService` (SMTP over libcurl) when `.env` (`SMTP_USER`/`SMTP_PASS`/`SMTP_TO`) is present. `core/src/main.cpp` does its own minimal `.env` line parsing — it is not a general-purpose env loader.
- Server routes register two CORS mechanisms per-route (`OPTIONS` preflight handler + per-response header) since the UI is served from a different port (8000) than the API (8080).

## Security & Data Boundaries

- `data/` (SQLite DB), `.env`, and any `*.sqlite`/credential files are gitignored — never commit real bill data, SMTP credentials, or the DB file. Use `.env.example` as the template for `.env`.
- `third_party/` is also gitignored (vendored deps, not repo-owned code).
- Treat bill amounts, account identifiers, and payment history as sensitive; avoid adding verbose logging that echoes these fields.
