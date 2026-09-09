"""
Shared helpers for pulling puzzle data out of puzzlemadness.co.uk pages.

puzzlemadness embeds each puzzle as a JS literal directly in the page HTML:

    let puzzleData = {"type":11,"baseURI":"16by16giantsudoku","gridWidth":16,
                       "gridHeight":16,"startingGrid":[...],"layout":[...],
                       "extraRegions":[...],"source":{...}};

No JS execution or browser automation is needed to read it - it's plain
text in the HTTP response.
"""

from __future__ import annotations

import datetime
import json
import re
import urllib.request

PUZZLE_DATA_RE = re.compile(r"let puzzleData\s*=\s*(\{.*?\});", re.DOTALL)

USER_AGENT = (
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/120.0 Safari/537.36"
)

DIFFICULTIES = ["easy", "medium", "hard", "tough", "mixed"]


def build_url(date: datetime.date, difficulty: str,
              base: str = "16by16giantsudoku") -> str:
    """Build a puzzlemadness puzzle URL, e.g.
    https://puzzlemadness.co.uk/16by16giantsudoku/medium/2026/9/9
    (the site does not zero-pad month/day)."""
    return f"https://puzzlemadness.co.uk/{base}/{difficulty}/{date.year}/{date.month}/{date.day}"


def fetch_html(url: str) -> str:
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=30) as resp:
        return resp.read().decode("utf-8", errors="replace")


def extract_puzzle_data(html_or_json: str) -> dict:
    """Accepts either a full HTML page or a bare puzzleData JSON blob
    (e.g. from a file previously saved with --save-json)."""
    text = html_or_json.strip()
    if text.startswith("{"):
        return json.loads(text)
    m = PUZZLE_DATA_RE.search(text)
    if not m:
        raise ValueError("Could not find `let puzzleData = {...};` in the page")
    return json.loads(m.group(1))


def puzzle_date(data: dict) -> datetime.date | None:
    src = data.get("source") or {}
    if "year" in src and "month" in src and "day" in src:
        return datetime.date(src["year"], src["month"], src["day"])
    return None
