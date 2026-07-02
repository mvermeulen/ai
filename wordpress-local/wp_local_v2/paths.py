from __future__ import annotations

from pathlib import Path

ROOT = Path.cwd()
VAULT = ROOT / "vault"
POSTS = VAULT / "posts"
PAGES = VAULT / "pages"
ASSETS = VAULT / "assets"
PLANS = ROOT / ".plans"
CACHE = ROOT / ".cache"
LEDGER = CACHE / "ledger.jsonl"
SNAPSHOT = CACHE / "snapshot.json"


def ensure_layout() -> None:
    for path in (POSTS, PAGES, ASSETS, PLANS, CACHE):
        path.mkdir(parents=True, exist_ok=True)
