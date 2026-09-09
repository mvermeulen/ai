#!/usr/bin/env python3
"""
Fetch a puzzlemadness.co.uk sudoku page (any of its grid-based sudoku
variants, though this was built for /16by16giantsudoku/...) and solve it.

puzzlemadness embeds the puzzle as a JS literal in the page:

    let puzzleData = {"type":11,"baseURI":"16by16giantsudoku","gridWidth":16,
                       "gridHeight":16,"startingGrid":[...],"layout":[...],
                       "extraRegions":[...],"source":{...}};

We extract that JSON, hand it to solver.solve(), and print the solution
in the same 1-9,A-G notation the site's UI uses.

Usage:
    python3 fetch_and_solve.py <url>
    python3 fetch_and_solve.py --file puzzle.json     # already-saved JSON
    python3 fetch_and_solve.py --file page.html       # already-saved HTML
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import urllib.request

import solver


PUZZLE_DATA_RE = re.compile(r"let puzzleData\s*=\s*(\{.*?\});", re.DOTALL)

USER_AGENT = (
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0 Safari/537.36"
)


def fetch_html(url: str) -> str:
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=30) as resp:
        return resp.read().decode("utf-8", errors="replace")


def extract_puzzle_data(html_or_json: str) -> dict:
    text = html_or_json.strip()
    if text.startswith("{"):
        return json.loads(text)
    m = PUZZLE_DATA_RE.search(text)
    if not m:
        raise ValueError("Could not find `let puzzleData = {...};` in the page")
    return json.loads(m.group(1))


def solve_puzzle_data(data: dict, verify_unique: bool = True) -> solver.SolveResult:
    size = data["gridWidth"]
    if data["gridHeight"] != size:
        raise ValueError("Only square grids are supported")

    regions = solver.standard_regions(
        size,
        layout=data.get("layout"),
        extra_regions=data.get("extraRegions") or None,
    )

    results = solver.solve(size, data["startingGrid"], regions,
                            limit=2 if verify_unique else 1)
    if not results:
        raise solver.Unsolvable("no solution found")
    if verify_unique and len(results) > 1:
        print("warning: puzzle has multiple solutions (showing the first)",
              file=sys.stderr)
    return results[0]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("url", nargs="?", help="puzzlemadness puzzle page URL")
    ap.add_argument("--file", help="read HTML or puzzleData JSON from a local file "
                                   "instead of fetching a URL")
    ap.add_argument("--save-json", help="write the extracted puzzleData JSON here")
    ap.add_argument("--no-verify", action="store_true",
                     help="skip the (cheap) uniqueness check")
    args = ap.parse_args()

    if not args.url and not args.file:
        ap.error("provide a URL or --file")

    if args.file:
        with open(args.file, encoding="utf-8") as f:
            raw = f.read()
    else:
        raw = fetch_html(args.url)

    data = extract_puzzle_data(raw)

    if args.save_json:
        with open(args.save_json, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=2)

    size = data["gridWidth"]
    givens = sum(1 for v in data["startingGrid"] if v)
    src = data.get("source", {})
    print(f"{size}x{size} sudoku (variant type {data.get('type')}), "
          f"{givens} givens, source={src}", file=sys.stderr)

    print("Starting grid:")
    print(solver.format_grid(size, data["startingGrid"]))
    print()

    result = solve_puzzle_data(data, verify_unique=not args.no_verify)

    print(f"Solved in {result.guesses} backtracking guesses.")
    print()
    print("Solution:")
    print(solver.format_grid(size, result.grid))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
