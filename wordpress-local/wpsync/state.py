"""Local sync-state tracking: what each local file last looked like when it
matched the server, so `push` can detect edits made on the live site in the
meantime instead of silently clobbering them.

``state.json`` is intentionally small and text-only (slug -> WordPress id,
a content hash, and the last-known remote modified timestamp) so it's safe
and useful to commit to git: it's a record of "what this repo believes is
in sync," and reviewing it in a diff tells you exactly what a push changed.
Snapshots (the full rendered HTML at last sync, used to build diffs and
detect conflicts) and backups (a timestamped copy of whatever was live on
the server immediately before an overwrite) are kept alongside it but are
gitignored -- they're recovery aids, not history worth versioning twice
over since git already versions the source markdown.
"""
from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List, Optional

STATE_FILENAME = "state.json"


def content_hash(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


@dataclass
class MediaRef:
    hash: str
    wp_id: int
    source_url: str


@dataclass
class ItemState:
    wp_id: int
    kind: str
    content_hash: str
    remote_modified: Optional[str] = None
    media: Dict[str, dict] = field(default_factory=dict)

    def to_dict(self) -> dict:
        return {
            "wp_id": self.wp_id,
            "kind": self.kind,
            "content_hash": self.content_hash,
            "remote_modified": self.remote_modified,
            "media": self.media,
        }

    @classmethod
    def from_dict(cls, data: dict) -> "ItemState":
        return cls(
            wp_id=data["wp_id"],
            kind=data["kind"],
            content_hash=data["content_hash"],
            remote_modified=data.get("remote_modified"),
            media=data.get("media", {}),
        )


class StateStore:
    def __init__(self, state_dir: Path):
        self.state_dir = Path(state_dir)
        self.path = self.state_dir / STATE_FILENAME
        self.snapshots_dir = self.state_dir / "snapshots"
        self.backups_dir = self.state_dir / "backups"
        self._items: Dict[str, ItemState] = {}
        self._load()

    def _load(self) -> None:
        if self.path.exists():
            raw = json.loads(self.path.read_text(encoding="utf-8"))
            self._items = {
                slug: ItemState.from_dict(item) for slug, item in raw.get("items", {}).items()
            }

    def save(self) -> None:
        self.state_dir.mkdir(parents=True, exist_ok=True)
        payload = {"items": {slug: item.to_dict() for slug, item in self._items.items()}}
        self.path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    def get(self, slug: str) -> Optional[ItemState]:
        return self._items.get(slug)

    def set(self, slug: str, item: ItemState) -> None:
        self._items[slug] = item

    def remove(self, slug: str) -> None:
        self._items.pop(slug, None)

    def slugs(self) -> List[str]:
        return list(self._items.keys())

    # -- snapshots (used for diff + conflict detection) --------------------

    def snapshot_path(self, slug: str) -> Path:
        return self.snapshots_dir / f"{slug}.html"

    def save_snapshot(self, slug: str, html_content: str) -> None:
        self.snapshots_dir.mkdir(parents=True, exist_ok=True)
        self.snapshot_path(slug).write_text(html_content, encoding="utf-8")

    def read_snapshot(self, slug: str) -> Optional[str]:
        p = self.snapshot_path(slug)
        return p.read_text(encoding="utf-8") if p.exists() else None

    # -- backups (safety net before any overwrite/delete) -------------------

    def backup(self, slug: str, html_content: str) -> Path:
        d = self.backups_dir / slug
        d.mkdir(parents=True, exist_ok=True)
        ts = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        backup_path = d / f"{ts}.html"
        backup_path.write_text(html_content, encoding="utf-8")
        return backup_path
