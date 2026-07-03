"""Offline content analytics: word counts, publishing cadence, taxonomy
coverage, orphaned media, and a lightweight accessibility check (missing
alt text), computed entirely from the local ``content/`` directory.

This is deliberately *not* trying to reproduce WordPress.com/Jetpack's
visitor-traffic Stats tab -- that requires Jetpack's own analytics
pipeline and a different auth flow than the core REST API this tool
otherwise sticks to, and faking it would be misleading. What's here
instead is something a pure offline tool can honestly promise: read-only,
zero-network insight into the content itself, useful on a train with no
signal at all.
"""
from __future__ import annotations

import re
from collections import Counter
from dataclasses import dataclass, field
from typing import Dict, List, Tuple

from .config import Config
from .content import ContentItem

_WORD_RE = re.compile(r"[A-Za-z0-9''-]+")
_IMAGE_ALT_RE = re.compile(r'!\[(?P<alt>[^\]]*)\]\((?P<src>\S+?)(?:\s+"[^"]*")?\)')
READING_WPM = 200


def word_count(body: str) -> int:
    return len(_WORD_RE.findall(body))


def reading_minutes(words: int) -> int:
    return max(1, round(words / READING_WPM))


@dataclass
class ItemInsight:
    slug: str
    kind: str
    status: str
    words: int
    reading_minutes: int
    categories: List[str]
    tags: List[str]
    date: str = ""
    has_excerpt: bool = True
    has_featured_image: bool = True
    images_missing_alt: int = 0
    missing_media: List[str] = field(default_factory=list)


@dataclass
class InsightsReport:
    items: List[ItemInsight] = field(default_factory=list)
    orphaned_media: List[str] = field(default_factory=list)
    media_count: int = 0
    media_bytes: int = 0

    @property
    def total_posts(self) -> int:
        return sum(1 for i in self.items if i.kind == "post")

    @property
    def total_pages(self) -> int:
        return sum(1 for i in self.items if i.kind == "page")

    @property
    def status_counts(self) -> Dict[str, int]:
        return dict(Counter(i.status for i in self.items))

    @property
    def total_words(self) -> int:
        return sum(i.words for i in self.items)

    @property
    def average_words(self) -> float:
        return self.total_words / len(self.items) if self.items else 0.0

    @property
    def category_counts(self) -> Dict[str, int]:
        c: Counter = Counter()
        for i in self.items:
            c.update(i.categories)
        return dict(c.most_common())

    @property
    def tag_counts(self) -> Dict[str, int]:
        c: Counter = Counter()
        for i in self.items:
            c.update(i.tags)
        return dict(c.most_common())

    @property
    def posts_per_month(self) -> Dict[str, int]:
        c: Counter = Counter()
        for i in self.items:
            if not i.date:
                continue
            month = i.date[:7]  # YYYY-MM
            if len(month) == 7:
                c[month] += 1
        return dict(sorted(c.items()))

    @property
    def accessibility_warnings(self) -> List[Tuple[str, str]]:
        warnings = []
        for i in self.items:
            if i.images_missing_alt:
                warnings.append((i.slug, f"{i.images_missing_alt} image(s) missing alt text"))
            if not i.has_excerpt and i.kind == "post":
                warnings.append((i.slug, "missing excerpt"))
            for m in i.missing_media:
                warnings.append((i.slug, f"references missing media file: {m}"))
        return warnings


def _referenced_media_paths(engine_items: List[ContentItem]) -> set:
    found = set()
    for item in engine_items:
        for ref in item.referenced_media():
            found.add(ref)
    return found


def build_report(config: Config, items: List[ContentItem]) -> InsightsReport:
    report = InsightsReport()

    media_dir = config.content_dir / "media"
    on_disk = set()
    if media_dir.exists():
        for p in media_dir.rglob("*"):
            if p.is_file():
                rel = f"media/{p.relative_to(media_dir).as_posix()}"
                on_disk.add(rel)
                report.media_count += 1
                report.media_bytes += p.stat().st_size

    referenced = _referenced_media_paths(items)

    for item in items:
        words = word_count(item.body)
        missing_media = sorted(
            ref for ref in item.referenced_media() if not (config.content_dir / ref).exists()
        )
        images_missing_alt = sum(
            1 for m in _IMAGE_ALT_RE.finditer(item.body) if not m.group("alt").strip()
        )
        report.items.append(
            ItemInsight(
                slug=item.slug,
                kind=item.kind,
                status=item.status,
                words=words,
                reading_minutes=reading_minutes(words),
                categories=list(item.categories),
                tags=list(item.tags),
                date=item.date or "",
                has_excerpt=bool(item.excerpt.strip()),
                has_featured_image=bool(item.featured_image),
                images_missing_alt=images_missing_alt,
                missing_media=missing_media,
            )
        )

    report.orphaned_media = sorted(on_disk - referenced)
    return report


def format_report(report: InsightsReport) -> str:
    lines = []
    lines.append(f"Posts: {report.total_posts}   Pages: {report.total_pages}")
    if report.status_counts:
        status_str = ", ".join(f"{k}={v}" for k, v in sorted(report.status_counts.items()))
        lines.append(f"By status: {status_str}")
    lines.append(f"Total words: {report.total_words:,}   Average per item: {report.average_words:,.0f}")
    lines.append(f"Media library: {report.media_count} file(s), {report.media_bytes / 1024:,.0f} KB")

    if report.posts_per_month:
        lines.append("")
        lines.append("Posts per month:")
        max_count = max(report.posts_per_month.values())
        for month, count in report.posts_per_month.items():
            bar = "#" * max(1, round(count / max_count * 20))
            lines.append(f"  {month}  {bar} {count}")

    if report.category_counts:
        lines.append("")
        lines.append("Categories: " + ", ".join(f"{k} ({v})" for k, v in report.category_counts.items()))
    if report.tag_counts:
        lines.append("Tags: " + ", ".join(f"{k} ({v})" for k, v in report.tag_counts.items()))

    if report.orphaned_media:
        lines.append("")
        lines.append(f"Orphaned media ({len(report.orphaned_media)}, not referenced by any post/page):")
        for m in report.orphaned_media:
            lines.append(f"  - {m}")

    warnings = report.accessibility_warnings
    if warnings:
        lines.append("")
        lines.append(f"Warnings ({len(warnings)}):")
        for slug, msg in warnings:
            lines.append(f"  - {slug}: {msg}")

    return "\n".join(lines)
