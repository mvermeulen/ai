"""Sync engine: pull, status, and push, built around one safety invariant --
we only ever overwrite or delete something on the live site if we can prove
our last-known copy of it still matches what's actually there right now.
If it doesn't, that's a conflict to surface, never something to guess at.

Two independent signals drive that check, deliberately kept separate:

* ``ItemState.remote_modified`` -- WordPress's own ``modified_gmt`` timestamp
  at the time of the last successful sync. If the live post's timestamp has
  moved on since, someone (or something) touched it in the dashboard and we
  stop and ask before pushing over it.
* ``ItemState.content_hash`` -- the hash of *our own* rendered HTML the last
  time this file was in sync. Comparing a fresh render against this (rather
  than against WordPress's own copy) is what lets `status`/`push` tell "you
  edited this file" apart from "wpsync's renderer just formats things
  slightly differently than the block editor did," which is unavoidable
  for content that existed before wpsync ever touched it.
"""
from __future__ import annotations

import difflib
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Dict, List, Optional

from .config import Config
from .content import ContentItem, html_to_markdown
from .media import file_hash, prepare_upload
from .state import ItemState, StateStore, content_hash
from .wp_client import WPClient, WPNotFoundError


def _field(value, key: str = "raw", fallback: str = "rendered") -> str:
    if isinstance(value, dict):
        return value.get(key) or value.get(fallback) or ""
    return value or ""


@dataclass
class PushOutcome:
    slug: str
    action: str  # create | update | unchanged | conflict | would-create | would-update | error | delete
    detail: str = ""
    diff: str = ""


@dataclass
class PullOutcome:
    slug: str
    action: str  # new | updated | skipped-local-changes
    path: Optional[Path] = None


class SyncEngine:
    def __init__(self, config: Config, state: StateStore, client: Optional[WPClient] = None):
        self.config = config
        self.state = state
        self.client = client

    # ---------------------------------------------------------------- pull

    def pull(self, post_types=("post", "page"), force: bool = False) -> List[PullOutcome]:
        assert self.client is not None, "pull() requires a WPClient"
        outcomes = [
            self._pull_one(post_type, remote, force=force)
            for post_type in post_types
            for remote in self.client.list_all(post_type)
        ]
        self.state.save()
        return outcomes

    def _pull_one(self, kind: str, remote: dict, force: bool) -> PullOutcome:
        slug = remote["slug"]
        path = self._path_for(kind, slug)
        rendered_content = _field(remote.get("content"), fallback="rendered")

        existing_state = self.state.get(slug)
        if path.exists() and existing_state and not force:
            local_item = ContentItem.load(path)
            local_hash = content_hash(local_item.render_html(self.offline_resolver(local_item)))
            if local_hash != existing_state.content_hash:
                return PullOutcome(slug=slug, action="skipped-local-changes", path=path)

        item = ContentItem(
            kind=kind,
            path=path,
            title=_field(remote.get("title")),
            slug=slug,
            status=remote.get("status", "draft"),
            date=remote.get("date"),
            excerpt=_strip_html(_field(remote.get("excerpt"))),
            body=html_to_markdown(rendered_content),
        )

        is_new = not path.exists()
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(item.dump(), encoding="utf-8")

        canonical_hash = content_hash(item.render_html(self.offline_resolver(item)))
        self.state.set(
            slug,
            ItemState(
                wp_id=remote["id"],
                kind=kind,
                content_hash=canonical_hash,
                remote_modified=remote.get("modified_gmt"),
                media=(existing_state.media if existing_state else {}),
            ),
        )
        self.state.save_snapshot(slug, rendered_content)
        return PullOutcome(slug=slug, action="new" if is_new else "updated", path=path)

    def _path_for(self, kind: str, slug: str) -> Path:
        sub = "posts" if kind == "post" else "pages"
        return self.config.content_dir / sub / f"{slug}.md"

    # -------------------------------------------------------------- status

    def local_items(self) -> List[ContentItem]:
        items = []
        for sub in ("posts", "pages"):
            d = self.config.content_dir / sub
            if not d.exists():
                continue
            for path in sorted(d.glob("*.md")):
                items.append(ContentItem.load(path))
        return items

    def status(self) -> Dict[str, str]:
        result = {}
        for item in self.local_items():
            st = self.state.get(item.slug)
            rendered = item.render_html(self.offline_resolver(item))
            h = content_hash(rendered)
            if st is None:
                result[item.slug] = "new"
            elif h != st.content_hash:
                result[item.slug] = "modified"
            else:
                result[item.slug] = "unchanged"
        return result

    def offline_resolver(self, item: ContentItem) -> Callable[[str], str]:
        st = self.state.get(item.slug)
        media = st.media if st else {}

        def resolve(src: str) -> str:
            if src.startswith("http://") or src.startswith("https://"):
                return src
            entry = media.get(src)
            return entry["source_url"] if entry else f"(unsynced-media:{src})"

        return resolve

    def diff(self, slug: str) -> str:
        item = self.find_local(slug)
        if item is None:
            raise ValueError(f"No local content found for slug '{slug}'")
        new_html = item.render_html(self.offline_resolver(item))
        old_html = self.state.read_snapshot(slug) or ""
        return "\n".join(
            difflib.unified_diff(
                old_html.splitlines(),
                new_html.splitlines(),
                fromfile=f"remote/{slug}",
                tofile=f"local/{slug}",
                lineterm="",
            )
        )

    def find_local(self, slug: str) -> Optional[ContentItem]:
        return next((i for i in self.local_items() if i.slug == slug), None)

    # ---------------------------------------------------------------- push

    def push(self, slugs: Optional[List[str]] = None, dry_run: bool = True, force: bool = False) -> List[PushOutcome]:
        items = self.local_items()
        if slugs:
            wanted = set(slugs)
            items = [i for i in items if i.slug in wanted]
        outcomes = [self._push_one(item, dry_run=dry_run, force=force) for item in items]
        if not dry_run:
            self.state.save()
        return outcomes

    def _push_one(self, item: ContentItem, dry_run: bool, force: bool) -> PushOutcome:
        assert self.client is not None, "push() requires a WPClient"
        st = self.state.get(item.slug)
        media_map = dict(st.media) if st else {}

        if not dry_run:
            for local_ref in item.referenced_media():
                if local_ref not in media_map:
                    media_map[local_ref] = self._upload_media(local_ref)

        def resolve(src: str) -> str:
            if src.startswith("http://") or src.startswith("https://"):
                return src
            entry = media_map.get(src)
            return entry["source_url"] if entry else src

        new_html = item.render_html(resolve)
        payload = {"title": item.title, "content": new_html, "status": item.status, "slug": item.slug}
        if item.excerpt:
            payload["excerpt"] = item.excerpt
        if item.date:
            payload["date"] = item.date

        if st is None:
            return self._create(item, payload, new_html, media_map, dry_run)
        return self._update(item, st, payload, new_html, media_map, dry_run, force)

    def _create(self, item, payload, new_html, media_map, dry_run) -> PushOutcome:
        if dry_run:
            return PushOutcome(item.slug, "would-create", detail=f"new {item.kind} '{item.title}'")
        if item.categories:
            payload["categories"] = self.client.ensure_terms("category", item.categories)
        if item.tags:
            payload["tags"] = self.client.ensure_terms("tag", item.tags)
        created = self.client.create(item.kind, payload)
        self.state.set(
            item.slug,
            ItemState(
                wp_id=created["id"],
                kind=item.kind,
                content_hash=content_hash(new_html),
                remote_modified=created.get("modified_gmt"),
                media=media_map,
            ),
        )
        self.state.save_snapshot(item.slug, _field(created.get("content"), fallback="rendered"))
        return PushOutcome(item.slug, "create", detail=f"created {item.kind} id={created['id']}")

    def _update(self, item, st: ItemState, payload, new_html, media_map, dry_run, force) -> PushOutcome:
        try:
            remote = self.client.get(item.kind, st.wp_id)
        except WPNotFoundError:
            return PushOutcome(
                item.slug,
                "error",
                detail=(
                    "the post/page this file was synced to no longer exists on the server "
                    "(deleted outside wpsync?). Remove its entry from .wpsync/state.json "
                    "to push it again as new content."
                ),
            )

        remote_modified = remote.get("modified_gmt")
        conflicted = remote_modified != st.remote_modified
        if conflicted and not force:
            return PushOutcome(
                item.slug,
                "conflict",
                detail=(
                    f"remote copy of '{item.title}' changed since the last sync "
                    f"(remote modified {remote_modified}, expected {st.remote_modified}). "
                    "Run `wpsync pull` to review the remote edit before overwriting it, "
                    "or re-run push with --force to overwrite it anyway."
                ),
            )

        if not conflicted and content_hash(new_html) == st.content_hash:
            return PushOutcome(item.slug, "unchanged")

        old_html = self.state.read_snapshot(item.slug) or _field(remote.get("content"), fallback="rendered")
        diff_text = "\n".join(
            difflib.unified_diff(
                old_html.splitlines(),
                new_html.splitlines(),
                fromfile=f"remote/{item.slug}",
                tofile=f"local/{item.slug}",
                lineterm="",
            )
        )
        if dry_run:
            return PushOutcome(item.slug, "would-update", diff=diff_text)

        self.state.backup(item.slug, _field(remote.get("content"), fallback="rendered"))

        if item.categories:
            payload["categories"] = self.client.ensure_terms("category", item.categories)
        if item.tags:
            payload["tags"] = self.client.ensure_terms("tag", item.tags)
        updated = self.client.update(item.kind, st.wp_id, payload)
        self.state.set(
            item.slug,
            ItemState(
                wp_id=st.wp_id,
                kind=item.kind,
                content_hash=content_hash(new_html),
                remote_modified=updated.get("modified_gmt"),
                media=media_map,
            ),
        )
        self.state.save_snapshot(item.slug, _field(updated.get("content"), fallback="rendered"))
        return PushOutcome(item.slug, "update", diff=diff_text)

    def _upload_media(self, local_ref: str) -> dict:
        candidate = self.config.content_dir / "media" / Path(local_ref).name
        if not candidate.exists():
            candidate = self.config.content_dir / local_ref
        if not candidate.exists():
            raise FileNotFoundError(f"Referenced media file not found: {local_ref}")
        h = file_hash(candidate)
        data, mime = prepare_upload(candidate)
        uploaded = self.client.upload_media(candidate.name, data, mime)
        return {"hash": h, "wp_id": uploaded["id"], "source_url": uploaded["source_url"]}

    # --------------------------------------------------------------- delete

    def delete(self, slug: str, confirm_title: str, permanent: bool = False) -> PushOutcome:
        assert self.client is not None, "delete() requires a WPClient"
        st = self.state.get(slug)
        if st is None:
            raise ValueError(f"No sync record for slug '{slug}' -- nothing to delete remotely.")
        remote = self.client.get(st.kind, st.wp_id)
        actual_title = _field(remote.get("title"))
        if actual_title != confirm_title:
            raise ValueError(
                "Title confirmation did not match, refusing to delete. "
                f"The current remote title is: {actual_title!r}"
            )
        self.state.backup(slug, _field(remote.get("content"), fallback="rendered"))
        self.client.delete(st.kind, st.wp_id, force=permanent)
        self.state.remove(slug)
        self.state.save()
        verb = "permanently deleted" if permanent else "moved to trash"
        return PushOutcome(slug, "delete", detail=f"{verb} {st.kind} id={st.wp_id}")


def _strip_html(text: str) -> str:
    return re.sub(r"<[^>]+>", "", text or "").strip()
