# 16x16 Giant Sudoku Solver

Solves the "16x16 Giant Sudoku" puzzles published daily at
[puzzlemadness.co.uk](https://puzzlemadness.co.uk/16by16giantsudoku/) (and,
more generally, any sudoku-family puzzle expressible as "cells" +
"regions that must each contain every symbol exactly once").

## How it works

puzzlemadness renders its puzzle client-side from a JSON literal embedded
directly in the page HTML:

```js
let puzzleData = {"type":11,"baseURI":"16by16giantsudoku","gridWidth":16,
                   "gridHeight":16,"startingGrid":[0,1,10,16,...],
                   "layout":[0,0,0,0,1,1,1,1,...],"extraRegions":[],
                   "source":{"year":2026,"month":9,"day":9,"variant":11}};
```

- `startingGrid`: 256 values, row-major, `0` = empty cell.
- `layout`: which of the 16 jigsaw/box regions each cell belongs to
  (for the standard variant this is just the usual 4x4 boxes).
- `extraRegions`: bonus regions for variants like hyper-sudoku (empty for
  the plain 16x16 puzzle).
- Values 1-9 and 10-16 are displayed as `1`-`9` and `A`-`G`.

`fetch_and_solve.py` downloads the page, regex-extracts that JSON, and
hands it to a generic solver.

## Files

- `puzzlemadness.py` - shared helpers: build a puzzle URL for a given
  date/difficulty, fetch a page, and extract its embedded `puzzleData`
  JSON. Used by both scripts below.
- `solver.py` - the solver itself. Bitmask candidate propagation
  (naked singles cascade automatically) plus MRV-ordered backtracking
  over rows/columns/boxes/extra-regions. Not specific to size 16 or to
  this site; it only needs a cell count and a list of regions.
- `fetch_and_solve.py` - CLI: fetch a puzzle URL (or a saved HTML/JSON
  file), solve it, print the starting grid and solution, and sanity-check
  that the solution is unique.
- `interactive_template.html` / `generate_interactive.py` - the
  interactive board (see below) as a template with `__PLACEHOLDER__`
  tokens for the date, difficulty, source URL, and the starting/solution
  arrays; `generate_interactive.py` fills those in for whatever
  date/difficulty you ask for and writes `interactive_solver.html`.
- `interactive_solver.html` - a self-contained, playable board generated
  from the template above (currently baked with the Wed 9 Sep 2026
  medium puzzle): fill it in yourself (click/tap a cell, type
  `1`-`9`/`A`-`G`, or use the on-screen keypad), and click **Hint** for
  the next logical step, explained in human terms - naked single, hidden
  single, pointing pair, box-line reduction, naked pair - falling back to
  a direct reveal only when no simple pattern applies. A "Check my grid"
  button flags entries that don't match the solution without giving them
  away, and progress is saved to the browser's local storage. Open the
  file directly in a browser (no server needed); it's also published as
  a Claude Artifact for sharing.

## Usage

```bash
# Solve today's medium puzzle directly from the site:
python3 fetch_and_solve.py https://puzzlemadness.co.uk/16by16giantsudoku/medium/2026/9/9

# Any difficulty/date the site publishes works the same way:
python3 fetch_and_solve.py https://puzzlemadness.co.uk/16by16giantsudoku/tough/2026/9/10

# Solve from a page you already saved (e.g. via curl), and dump the
# extracted puzzle JSON for reuse:
python3 fetch_and_solve.py --file page.html --save-json puzzle.json
python3 fetch_and_solve.py --file puzzle.json
```

### Updating the interactive board to a new date

```bash
# Today's medium puzzle (the defaults):
python3 generate_interactive.py

# A specific date and/or difficulty:
python3 generate_interactive.py --date 2026-09-10
python3 generate_interactive.py --date 2026-09-10 --difficulty tough

# From a file you already have (uses the date embedded in its JSON):
python3 generate_interactive.py --file page.html

# Write somewhere other than interactive_solver.html:
python3 generate_interactive.py --date 2026-09-10 --output sept10.html
```

This overwrites `interactive_solver.html` in place (unless `--output` says
otherwise) with a fresh title, date/difficulty labels, source link,
localStorage key, and starting/solution grids - re-publish that file as
the Artifact to share the new puzzle. Sparser puzzles (tough/hard, fewer
givens) can take longer to solve - a "hard" puzzle with 99 givens took
about 13 seconds in testing, versus well under a second for a "medium"
one with 115.

Example output:

```
Starting grid:
0 1 A G | 6 0 9 3 | E 0 D B | F 0 7 C
...

Solved in 469 backtracking guesses.

Solution:
5 1 A G | 6 2 9 3 | E 4 D B | F 8 7 C
...
```

## Notes / possible extensions

- The solver checks for a second solution (up to `limit=2`) and warns if
  the puzzle isn't uniquely solvable; pass `--no-verify` to skip that for
  speed on very sparse grids.
- Because regions are just cell-index lists, the same `solver.py` also
  solves puzzlemadness's other grid-based variants (12x12 Giant Sudoku,
  Jigsaw Sudoku, Hyper Sudoku) as long as `layout`/`extraRegions` are
  passed through - the site's other variant pages (Killer, Kropki,
  Arrow, Consecutive, Greater Than, ...) add clue types (cages, dot
  markers, inequality signs) this solver doesn't model yet.
- No browser automation is required to fetch the puzzle - the data is
  in the plain HTML response, no JS execution needed.
