"""Offline media library manifest: browse what's on WordPress and edit
alt text / captions / titles offline, without re-uploading anything.

This is separate from ``wpsync/media.py`` (which hashes and lightly
optimizes files *before* upload during a post/page push). This module
mirrors the *existing* remote media library into
``content/media-library.yaml`` so accessibility metadata (alt text
especially) can be reviewed and edited in bulk, offline, then pushed back
with a single PATCH per changed item -- no file re-upload involved, so
there's no risk of this module touching image bytes at all.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional

import yaml

from .config import Config
from .wp_client import WPClient

MANIFEST_FILENAME = "media-library.yaml"


def _field(value, key: str = "raw", fallback: str = "rendered") -> str:
    if isinstance(value, dict):
        return value.get(key) or value.get(fallback) or ""
    return value or ""


@dataclass
class MediaItem:
    wp_id: int
    filename: str
    source_url: str
    mime_type: str = ""
    alt_text: str = ""
    caption: str = ""
    title: str = ""
    local_edit: bool = False

    def to_dict(self) -> dict:
        return {
            "wp_id": self.wp_id,
            "filename": self.filename,
            "source_url": self.source_url,
            "mime_type": self.mime_type,
            "alt_text": self.alt_text,
            "caption": self.caption,
            "title": self.title,
            "local_edit": self.local_edit,
        }

    @classmethod
    def from_dict(cls, data: dict) -> "MediaItem":
        return cls(
            wp_id=data["wp_id"],
            filename=data.get("filename", ""),
            source_url=data.get("source_url", ""),
            mime_type=data.get("mime_type", ""),
            alt_text=data.get("alt_text", ""),
            caption=data.get("caption", ""),
            title=data.get("title", ""),
            local_edit=bool(data.get("local_edit", False)),
        )

    @classmethod
    def from_remote(cls, remote: dict) -> "MediaItem":
        return cls(
            wp_id=remote["id"],
            filename=Path(remote.get("source_url", "")).name,
            source_url=remote.get("source_url", ""),
            mime_type=remote.get("mime_type", ""),
            alt_text=remote.get("alt_text", "") or "",
            caption=_field(remote.get("caption")),
            title=_field(remote.get("title")),
        )


@dataclass
class MediaManifest:
    path: Path
    items: List[MediaItem] = field(default_factory=list)

    def get(self, wp_id: int) -> Optional[MediaItem]:
        return next((i for i in self.items if i.wp_id == wp_id), None)

    def dump(self) -> str:
        return yaml.safe_dump({"items": [i.to_dict() for i in self.items]}, sort_keys=False, allow_unicode=True)

    @classmethod
    def load(cls, path: Path) -> "MediaManifest":
        if not path.exists():
            return cls(path=path, items=[])
        data = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
        return cls(path=path, items=[MediaItem.from_dict(i) for i in (data.get("items") or [])])

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(self.dump(), encoding="utf-8")


def manifest_path(config: Config) -> Path:
    return config.content_dir / MANIFEST_FILENAME


def load_manifest(config: Config) -> MediaManifest:
    return MediaManifest.load(manifest_path(config))


@dataclass
class PullResult:
    new: int = 0
    refreshed: int = 0


def pull(config: Config, client: WPClient) -> PullResult:
    """Refresh read-only fields for every remote media item. Never touches
    ``alt_text``/``caption``/``title`` for an item flagged ``local_edit``,
    so an offline accessibility-metadata edit always survives a pull."""
    manifest = load_manifest(config)
    by_id = {i.wp_id: i for i in manifest.items}
    result = PullResult()
    merged: List[MediaItem] = []
    for remote in client.list_media_all():
        incoming = MediaItem.from_remote(remote)
        local = by_id.get(incoming.wp_id)
        if local is None:
            result.new += 1
            merged.append(incoming)
        else:
            result.refreshed += 1
            if local.local_edit:
                incoming.alt_text = local.alt_text
                incoming.caption = local.caption
                incoming.title = local.title
                incoming.local_edit = True
            merged.append(incoming)
    manifest.items = merged
    manifest.save()
    return result


def set_metadata(
    config: Config,
    wp_id: int,
    alt_text: Optional[str] = None,
    caption: Optional[str] = None,
    title: Optional[str] = None,
) -> MediaItem:
    manifest = load_manifest(config)
    item = manifest.get(wp_id)
    if item is None:
        raise ValueError(f"No media item with id={wp_id} in the local manifest. Run `wpsync media pull` first.")
    if alt_text is not None:
        item.alt_text = alt_text
    if caption is not None:
        item.caption = caption
    if title is not None:
        item.title = title
    item.local_edit = True
    manifest.save()
    return item


@dataclass
class PushOutcome:
    wp_id: int
    filename: str
    action: str  # would-update | update | error
    detail: str = ""


def plan_push(config: Config) -> List[PushOutcome]:
    manifest = load_manifest(config)
    return [
        PushOutcome(i.wp_id, i.filename, "would-update", detail="alt/caption/title changed offline")
        for i in manifest.items
        if i.local_edit
    ]


def apply_push(config: Config, client: WPClient) -> List[PushOutcome]:
    manifest = load_manifest(config)
    outcomes: List[PushOutcome] = []
    changed = False
    for item in manifest.items:
        if not item.local_edit:
            continue
        try:
            client.update_media(
                item.wp_id, {"alt_text": item.alt_text, "caption": item.caption, "title": item.title}
            )
            item.local_edit = False
            changed = True
            outcomes.append(PushOutcome(item.wp_id, item.filename, "update"))
        except Exception as exc:  # noqa: BLE001
            outcomes.append(PushOutcome(item.wp_id, item.filename, "error", detail=str(exc)))
    if changed:
        manifest.save()
    return outcomes
