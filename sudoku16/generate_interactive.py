#!/usr/bin/env python3
"""
Generate interactive_solver.html for a specific puzzlemadness.co.uk
16x16 Giant Sudoku date/difficulty, from interactive_template.html.

Usage:
    python3 generate_interactive.py                        # today, medium
    python3 generate_interactive.py --date 2026-09-10
    python3 generate_interactive.py --date 2026-09-10 --difficulty tough
    python3 generate_interactive.py --file page.html        # from a saved page
    python3 generate_interactive.py --output my_puzzle.html
"""

from __future__ import annotations

import argparse
import datetime
import sys
from pathlib import Path

import solver
from puzzlemadness import DIFFICULTIES, build_url, extract_puzzle_data, fetch_html, puzzle_date

HERE = Path(__file__).resolve().parent
DEFAULT_TEMPLATE = HERE / "interactive_template.html"
DEFAULT_OUTPUT = HERE / "interactive_solver.html"


def _compact_array(values: list[int]) -> str:
    return "[" + ",".join(str(v) for v in values) + "]"


def solve_puzzle_data(data: dict) -> solver.SolveResult:
    size = data["gridWidth"]
    if data["gridHeight"] != size:
        raise ValueError("Only square grids are supported")
    regions = solver.standard_regions(
        size, layout=data.get("layout"), extra_regions=data.get("extraRegions") or None,
    )
    results = solver.solve(size, data["startingGrid"], regions, limit=2)
    if not results:
        raise solver.Unsolvable("no solution found")
    if len(results) > 1:
        print("warning: puzzle has multiple solutions (using the first)", file=sys.stderr)
    return results[0]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--date", help="YYYY-MM-DD (default: today). Ignored if --file's "
                                    "puzzle JSON carries its own date.")
    ap.add_argument("--difficulty", default="medium", choices=DIFFICULTIES,
                     help="default: medium")
    ap.add_argument("--file", help="use a saved HTML/JSON file instead of fetching")
    ap.add_argument("--output", default=str(DEFAULT_OUTPUT),
                     help=f"default: {DEFAULT_OUTPUT.name}")
    ap.add_argument("--template", default=str(DEFAULT_TEMPLATE))
    args = ap.parse_args()

    difficulty = args.difficulty

    if args.file:
        with open(args.file, encoding="utf-8") as f:
            raw = f.read()
        data = extract_puzzle_data(raw)
        date = puzzle_date(data)
        if date is None:
            if not args.date:
                ap.error("--file's puzzle data has no source date; pass --date explicitly")
            date = datetime.date.fromisoformat(args.date)
        source_url = build_url(date, difficulty)
    else:
        date = datetime.date.fromisoformat(args.date) if args.date else datetime.date.today()
        source_url = build_url(date, difficulty)
        raw = fetch_html(source_url)
        data = extract_puzzle_data(raw)

    result = solve_puzzle_data(data)
    start = data["startingGrid"]
    solution = result.grid
    givens = sum(1 for v in start if v)

    weekday = date.strftime("%a")
    date_label = f"{date.day} {date.strftime('%b')} {date.year}"
    date_key = date.isoformat()
    title = f"The {date.strftime('%B')} {date.day} Giant Sudoku"

    template = Path(args.template).read_text(encoding="utf-8")
    out = (template
           .replace("__TITLE__", title)
           .replace("__SOURCE_URL__", source_url)
           .replace("__DIFFICULTY_LABEL__", difficulty)
           .replace("__DIFFICULTY_TITLE__", difficulty.capitalize())
           .replace("__WEEKDAY__", weekday)
           .replace("__DATE_LABEL__", date_label)
           .replace("__GIVENS__", str(givens))
           .replace("__DATE_KEY__", date_key)
           .replace("__START_ARRAY__", _compact_array(start))
           .replace("__SOLUTION_ARRAY__", _compact_array(solution)))

    Path(args.output).write_text(out, encoding="utf-8")
    print(f"Wrote {args.output}: {date_key} ({weekday}) {difficulty}, "
          f"{givens}/{len(start)} givens, {result.guesses} backtracking guesses.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
