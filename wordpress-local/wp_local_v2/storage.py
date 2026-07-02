from __future__ import annotations

import json
from pathlib import Path

from .paths import LEDGER, SNAPSHOT, ensure_layout
from .types import LedgerItem


def load_snapshot() -> dict[str, dict]:
    ensure_layout()
    if not SNAPSHOT.exists():
        return {}
    data = json.loads(SNAPSHOT.read_text(encoding="utf-8"))
    return data.get("items", {}) if isinstance(data, dict) else {}


def write_snapshot(items: dict[str, dict]) -> None:
    ensure_layout()
    payload = {"items": items}
    SNAPSHOT.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def append_ledger(item: LedgerItem) -> None:
    ensure_layout()
    line = json.dumps(item.__dict__, sort_keys=True)
    with LEDGER.open("a", encoding="utf-8") as fh:
        fh.write(line + "\n")


def load_ledger_index() -> dict[str, LedgerItem]:
    ensure_layout()
    if not LEDGER.exists():
        return {}

    index: dict[str, LedgerItem] = {}
    for line in LEDGER.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        raw = json.loads(line)
        index[raw["key"]] = LedgerItem(
            key=raw["key"],
            entry_type=raw["entry_type"],
            slug=raw["slug"],
            wp_id=int(raw["wp_id"]),
            last_remote_modified_gmt=str(raw["last_remote_modified_gmt"]),
            last_local_hash=str(raw["last_local_hash"]),
            applied_at=str(raw["applied_at"]),
        )
    return index
