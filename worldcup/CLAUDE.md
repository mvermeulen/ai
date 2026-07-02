# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

`wc` is a C++20 CLI + embedded web server that tracks and simulates the 48-team FIFA World Cup 2026. It simulates match outcomes with an Elo-calibrated Poisson goal model (with host-nation advantage for USA/MEX/CAN), computes group standings with FIFA tiebreaker rules, allocates the Round of 32 bracket (including the 8-best-third-place-team backtracking search), and serves a dashboard over a hand-rolled HTTP server.

This codebase was adapted from an NFL tracker/simulator prototype (`nfl3`) — see `investigation.md` for the original design rationale if you need historical context on why certain modeling choices (Poisson vs. logistic, tiebreaker fallback to drawing of lots, etc.) were made.

## Build, test, run

```bash
./build.sh                 # cmake + make -j, then copies build/wc and build/wc_tests to repo root
```
or directly:
```bash
mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$(nproc)
```

Run all tests:
```bash
./wc_tests
```
Run a single test case (Catch2 tag or name filtering):
```bash
./wc_tests "[MonteCarlo]"
./wc_tests "Tiebreaker ranks third-place teams across groups"
./wc_tests --list-tests
```

CLI commands (`./wc <command>`), all reading `data/teams.csv` / `data/schedule.csv` by default:
```bash
./wc status                # group standings + 10k-iteration sim summary
./wc simulate [iterations] # Monte Carlo advancement probabilities (default 100000)
./wc impact [iterations]   # match-importance delta analysis for unplayed games (default 10000)
./wc bracket [--unplayed]  # Round of 32 bracket, allocated via Tiebreaker rules
./wc backfit-model         # calibrates base_rate/alpha/hostAdvantage from data/historical/, writes data/model_coefficients.csv
./wc fetch-live            # shells out to scripts/fetch_live_scores.py to sync scores from ESPN's scoreboard API into schedule.csv
./wc web [port]            # embedded HTTP server (default 8080) serving the dashboard
```

There is no separate lint step; `CMAKE_EXPORT_COMPILE_COMMANDS` is on and `.clangd` points at `build/compile_commands.json` for editor diagnostics.

## Architecture

- `src/model/` — domain logic, no I/O:
  - `Team` / `Match`: plain data + accumulated stats (wins/draws/losses/goals). `Match.status` is one of `"scheduled"`, `"in_progress"`, `"final"`.
  - `Tournament`: owns all `Team`s (keyed by abbreviation) and `Match`es; `computeStandings()` replays all matches to update team records. `wc::loadTournamentFromCsvFiles` builds one from `data/teams.csv` + `data/schedule.csv`.
  - `Tiebreaker`: static FIFA tiebreaker logic — `breakGroupTie` (within a group of 4), `rankThirdPlaces` (cross-group ranking of the 12 third-place finishers to pick the best 8), and `allocateRoundOf32Matchups` (fills in matches 73–88 using FIFA's official third-place-to-bracket-slot lookup, via backtracking constraint search). Fair-play points are intentionally skipped — ties fall straight through to "drawing of lots" per `investigation.md` §5.9.
  - `MonteCarlo`: the simulation engine. `simulate()` runs N iterations of `simulateIteration`, each of which simulates all unplayed matches (Poisson-distributed goals from Elo difference, `alpha`, `baseRate`, `hostAdvantage`; knockout draws go to extra time then a 50/50 penalty-shootout coin flip) and re-derives the whole bracket. `analyzeImpact()` computes each unplayed match's effect on R32 probability by forcing home-win/away-win and diffing. `fitPoissonModel()` (free function) calibrates the three model parameters against `data/historical/`.
- `src/util/CsvParser` — minimal CSV reader/writer used for all `data/*.csv` I/O.
- `src/output/`:
  - `AsciiPrinter`: renders standings/simulation/impact/bracket to stdout for the CLI.
  - `WebServer`: a multi-threaded socket-based HTTP server (no external library) — routes requests by path prefix in `handleRequest`, renders HTML dashboard views and JSON API responses, and supports a "what-if" sandbox that lets a client POST result overrides (`applyResultUpdate`) which mutate the in-memory `Tournament` and rewrite `schedule.csv` (`persistSchedule`). `handleForTests` exposes routing directly for unit/integration testing without opening a real socket.
- `scripts/fetch_live_scores.py` — standalone script invoked via `std::system` from `./wc fetch-live`; pulls ESPN's soccer scoreboard API and updates `schedule.csv` in place.
- `data/`: `teams.csv` (abbreviation, full_name, group, elo_rating, federation), `schedule.csv` (match_id, stage, group, date, home_team, away_team, scores, penalty scores, status, host_city), `historical/` (past World Cups for calibration), `model_coefficients.csv` (persisted fit output), `probability_history.csv` (time series written by the web server for dashboard sparklines).

## Notes

- `wc` and `wc_tests` binaries checked into the repo root are copies of `build/wc` / `build/wc_tests`, refreshed by `build.sh` — rebuild via `build.sh` rather than hand-copying after source changes.
- Tournament stage strings used across the codebase and CSVs: `"group"`, `"knockout_r32"`, and further knockout rounds — check `Tiebreaker.cpp`/`Tournament.cpp` for the exact stage-name set before adding new stage-dependent logic.
- Host advantage is hardcoded to USA/MEX/CAN (`MonteCarlo::isHost`) for the 2026 tournament; this is not read from data.
