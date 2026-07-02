from __future__ import annotations

from dataclasses import dataclass
from typing import Literal

EntryType = Literal["post", "page"]


@dataclass
class LocalEntry:
    entry_type: EntryType
    slug: str
    title: str
    status: str
    excerpt: str
    tags: list[str]
    categories: list[str]
    body_markdown: str
    body_html: str
    content_hash: str
    entry_dir: str


@dataclass
class LedgerItem:
    key: str
    entry_type: EntryType
    slug: str
    wp_id: int
    last_remote_modified_gmt: str
    last_local_hash: str
    applied_at: str
