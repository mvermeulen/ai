from __future__ import annotations

import json
from pathlib import Path

from .paths import PAGES, POSTS, ensure_layout
from .types import EntryType, LocalEntry
from .utils import digest_text, markdown_to_html, slugify_title


def create_entry(entry_type: EntryType, title: str) -> Path:
    ensure_layout()
    slug = slugify_title(title)
    base = POSTS if entry_type == "post" else PAGES
    entry_dir = base / slug
    entry_dir.mkdir(parents=True, exist_ok=False)

    meta = {
        "type": entry_type,
        "title": title,
        "slug": slug,
        "status": "draft",
        "excerpt": "",
        "tags": [],
        "categories": [],
    }

    (entry_dir / "meta.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")
    (entry_dir / "body.md").write_text(
        "Write your content here.\n\n"
        "Use local assets with asset://photo.jpg\n\n"
        "Add Strava links directly in markdown.\n",
        encoding="utf-8",
    )
    return entry_dir


def load_entries() -> list[LocalEntry]:
    ensure_layout()
    entries: list[LocalEntry] = []

    for root, entry_type in ((POSTS, "post"), (PAGES, "page")):
        for entry_dir in sorted([p for p in root.iterdir() if p.is_dir()]):
            meta_file = entry_dir / "meta.json"
            body_file = entry_dir / "body.md"
            if not meta_file.exists() or not body_file.exists():
                continue

            meta = json.loads(meta_file.read_text(encoding="utf-8"))
            body = body_file.read_text(encoding="utf-8")

            slug = str(meta.get("slug") or entry_dir.name)
            title = str(meta.get("title") or slug)
            status = str(meta.get("status") or "draft")
            excerpt = str(meta.get("excerpt") or "")
            tags = [str(x) for x in meta.get("tags", [])]
            categories = [str(x) for x in meta.get("categories", [])]
            html = markdown_to_html(body)
            content_hash = digest_text(json.dumps(meta, sort_keys=True), body)

            entries.append(
                LocalEntry(
                    entry_type=entry_type,
                    slug=slug,
                    title=title,
                    status=status,
                    excerpt=excerpt,
                    tags=tags,
                    categories=categories,
                    body_markdown=body,
                    body_html=html,
                    content_hash=content_hash,
                    entry_dir=str(entry_dir),
                )
            )

    entries.sort(key=lambda x: f"{x.entry_type}:{x.slug}")
    return entries
