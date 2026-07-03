"""Offline comment moderation and replies.

Comments are mirrored into one YAML file per post/page at
``content/comments/<slug>.yaml``. Each comment entry has two kinds of
fields, kept deliberately separate:

* **remote fields** (``author``, ``date``, ``content``, ``status``, ...) --
  refreshed from WordPress on every ``pull``. These are a read-only mirror;
  wpsync never rewrites someone else's comment text.
* **local intent fields** (``desired_status``, ``reply``) -- set offline by
  you, in the file or via ``wpsync comments set``. ``pull`` never touches
  these, so a moderation decision or a drafted reply made while offline
  always survives a later pull.

Nothing is deleted through this module. The only actions available are
changing a comment's moderation status (approve/hold/spam/trash -- trash is
recoverable from the dashboard) and posting a reply, which mirrors the
bounded, low-blast-radius philosophy the rest of wpsync uses for anything
that touches the live site.
"""
from __future__ import annotations

import html
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

import yaml

from .config import Config
from .wp_client import WPClient

VALID_STATUSES = ("approve", "hold", "spam", "trash")


def _strip_html(text: str) -> str:
    return html.unescape(re.sub(r"<[^>]+>", "", text or "")).strip()


def _field(value, key: str = "raw", fallback: str = "rendered") -> str:
    if isinstance(value, dict):
        return value.get(key) or value.get(fallback) or ""
    return value or ""


@dataclass
class Comment:
    wp_id: int
    post_id: int
    post_slug: str
    parent: int = 0
    author_name: str = ""
    date: Optional[str] = None
    status: str = "hold"
    content: str = ""
    desired_status: Optional[str] = None
    reply: Optional[str] = None
    reply_sent: bool = False

    @property
    def has_pending_action(self) -> bool:
        status_change = self.desired_status is not None and self.desired_status != self.status
        pending_reply = bool(self.reply) and not self.reply_sent
        return status_change or pending_reply

    def to_dict(self) -> dict:
        return {
            "wp_id": self.wp_id,
            "post_id": self.post_id,
            "parent": self.parent,
            "author_name": self.author_name,
            "date": self.date,
            "status": self.status,
            "content": self.content,
            "desired_status": self.desired_status,
            "reply": self.reply,
            "reply_sent": self.reply_sent,
        }

    @classmethod
    def from_dict(cls, post_slug: str, data: dict) -> "Comment":
        return cls(
            wp_id=data["wp_id"],
            post_id=data["post_id"],
            post_slug=post_slug,
            parent=data.get("parent", 0),
            author_name=data.get("author_name", ""),
            date=data.get("date"),
            status=data.get("status", "hold"),
            content=data.get("content", ""),
            desired_status=data.get("desired_status"),
            reply=data.get("reply"),
            reply_sent=bool(data.get("reply_sent", False)),
        )

    @classmethod
    def from_remote(cls, post_slug: str, remote: dict) -> "Comment":
        return cls(
            wp_id=remote["id"],
            post_id=remote["post"],
            post_slug=post_slug,
            parent=remote.get("parent", 0) or 0,
            author_name=_field(remote.get("author_name")) or remote.get("author_name", ""),
            date=remote.get("date"),
            status=remote.get("status", "hold"),
            content=_strip_html(_field(remote.get("content"))),
        )


@dataclass
class CommentThread:
    post_slug: str
    path: Path
    comments: List[Comment] = field(default_factory=list)

    def dump(self) -> str:
        payload = {
            "post_slug": self.post_slug,
            "comments": [c.to_dict() for c in self.comments],
        }
        return yaml.safe_dump(payload, sort_keys=False, allow_unicode=True)

    @classmethod
    def load(cls, path: Path) -> "CommentThread":
        data = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
        slug = data.get("post_slug", path.stem)
        comments = [Comment.from_dict(slug, c) for c in (data.get("comments") or [])]
        return cls(post_slug=slug, path=path, comments=comments)

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(self.dump(), encoding="utf-8")

    def get(self, wp_id: int) -> Optional[Comment]:
        return next((c for c in self.comments if c.wp_id == wp_id), None)


class CommentsStore:
    """Reads/writes the ``content/comments/*.yaml`` threads for a project."""

    def __init__(self, config: Config):
        self.config = config
        self.dir = config.content_dir / "comments"

    def path_for(self, slug: str) -> Path:
        return self.dir / f"{slug}.yaml"

    def load(self, slug: str) -> Optional[CommentThread]:
        path = self.path_for(slug)
        if not path.exists():
            return None
        return CommentThread.load(path)

    def all_threads(self) -> List[CommentThread]:
        if not self.dir.exists():
            return []
        return [CommentThread.load(p) for p in sorted(self.dir.glob("*.yaml"))]

    def pending(self) -> List[Comment]:
        return [c for thread in self.all_threads() for c in thread.comments if c.has_pending_action]


@dataclass
class PullResult:
    post_slug: str
    new: int = 0
    updated: int = 0


def pull(config: Config, client: WPClient, slug_by_post_id: Dict[int, str]) -> List[PullResult]:
    """Refresh the local comment mirror for every known post/page.

    ``slug_by_post_id`` maps WordPress post id -> local slug, normally built
    from the sync state store (only posts/pages wpsync already knows about
    are pulled, since a bare comment thread with no local content file has
    nowhere sensible to live).
    """
    store = CommentsStore(config)
    results: List[PullResult] = []
    for post_id, slug in slug_by_post_id.items():
        remote_comments = client.list_comments(post_id=post_id, status="any")
        existing = store.load(slug)
        by_id = {c.wp_id: c for c in existing.comments} if existing else {}

        new_count = 0
        updated_count = 0
        merged: List[Comment] = []
        for remote in remote_comments:
            incoming = Comment.from_remote(slug, remote)
            local = by_id.get(incoming.wp_id)
            if local is None:
                new_count += 1
                merged.append(incoming)
            else:
                if local.status != incoming.status or local.content != incoming.content:
                    updated_count += 1
                # Preserve local intent fields untouched.
                incoming.desired_status = local.desired_status
                incoming.reply = local.reply
                incoming.reply_sent = local.reply_sent
                # A status change we already pushed is now reflected remotely;
                # clear a stale desired_status that matches reality.
                if incoming.desired_status == incoming.status:
                    incoming.desired_status = None
                merged.append(incoming)

        if not remote_comments and existing is None:
            continue

        thread = CommentThread(post_slug=slug, path=store.path_for(slug), comments=merged)
        thread.save()
        results.append(PullResult(post_slug=slug, new=new_count, updated=updated_count))
    return results


@dataclass
class PushOutcome:
    post_slug: str
    wp_id: int
    action: str  # would-approve | would-spam | ... | would-reply | status | reply | error
    detail: str = ""


def plan_push(config: Config) -> List[PushOutcome]:
    """Dry-run: describe every pending local moderation/reply action."""
    outcomes = []
    for thread in CommentsStore(config).all_threads():
        for c in thread.comments:
            if c.desired_status is not None and c.desired_status != c.status:
                outcomes.append(
                    PushOutcome(thread.post_slug, c.wp_id, f"would-set-status:{c.desired_status}")
                )
            if c.reply and not c.reply_sent:
                outcomes.append(PushOutcome(thread.post_slug, c.wp_id, "would-reply"))
    return outcomes


def apply_push(config: Config, client: WPClient) -> List[PushOutcome]:
    """Apply every pending local moderation/reply action to WordPress."""
    outcomes: List[PushOutcome] = []
    store = CommentsStore(config)
    for thread in store.all_threads():
        changed = False
        for c in thread.comments:
            if c.desired_status is not None and c.desired_status != c.status:
                try:
                    client.update_comment_status(c.wp_id, c.desired_status)
                    c.status = c.desired_status
                    c.desired_status = None
                    changed = True
                    outcomes.append(PushOutcome(thread.post_slug, c.wp_id, "status", detail=f"set to {c.status}"))
                except Exception as exc:  # noqa: BLE001 - surfaced to the caller as an outcome
                    outcomes.append(PushOutcome(thread.post_slug, c.wp_id, "error", detail=str(exc)))
            if c.reply and not c.reply_sent:
                try:
                    client.create_comment_reply(c.post_id, c.wp_id, c.reply)
                    c.reply_sent = True
                    changed = True
                    outcomes.append(PushOutcome(thread.post_slug, c.wp_id, "reply", detail="reply posted"))
                except Exception as exc:  # noqa: BLE001
                    outcomes.append(PushOutcome(thread.post_slug, c.wp_id, "error", detail=str(exc)))
        if changed:
            thread.save()
    return outcomes


def set_intent(
    config: Config, slug: str, wp_id: int, desired_status: Optional[str] = None, reply: Optional[str] = None
) -> Comment:
    """Offline-only: record a moderation decision and/or draft reply."""
    store = CommentsStore(config)
    thread = store.load(slug)
    if thread is None:
        raise ValueError(f"No local comment thread for slug '{slug}'. Run `wpsync comments pull` first.")
    comment = thread.get(wp_id)
    if comment is None:
        raise ValueError(f"No comment id={wp_id} found in thread for '{slug}'.")
    if desired_status is not None:
        if desired_status not in VALID_STATUSES:
            raise ValueError(f"Invalid status '{desired_status}'. Must be one of {VALID_STATUSES}.")
        comment.desired_status = desired_status
    if reply is not None:
        comment.reply = reply
        comment.reply_sent = False
    thread.save()
    return comment
