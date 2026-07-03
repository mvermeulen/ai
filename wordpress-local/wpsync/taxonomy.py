"""Offline taxonomy (category/tag) curation.

``wpsync/wp_client.py``'s existing ``ensure_terms`` already creates
categories/tags on the fly by name when a post/page references one that
doesn't exist yet -- that's unchanged and still how posts/pages get their
terms. This module is for the slower-moving, separate task of curating the
taxonomy itself: writing descriptions, reviewing what exists, and adding
new terms deliberately before you reference them, all offline, in
``content/taxonomy.yaml``.

Renames and deletions are intentionally not supported here -- both are
easy to get wrong from a stale local copy (a rename-by-slug could silently
create a duplicate term, a delete could strip terms off live posts that
were added to WordPress directly). Curate freely offline; if you truly
need to rename or delete a term, do that in the dashboard.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional

import yaml

from .config import Config
from .wp_client import WPClient

MANIFEST_FILENAME = "taxonomy.yaml"
TAXONOMIES = ("category", "tag")


@dataclass
class Term:
    taxonomy: str
    wp_id: Optional[int]
    name: str
    slug: str = ""
    description: str = ""
    parent: Optional[str] = None  # parent category *name*, categories only

    def to_dict(self) -> dict:
        d = {"wp_id": self.wp_id, "name": self.name, "slug": self.slug, "description": self.description}
        if self.taxonomy == "category":
            d["parent"] = self.parent
        return d

    @classmethod
    def from_dict(cls, taxonomy: str, data: dict) -> "Term":
        return cls(
            taxonomy=taxonomy,
            wp_id=data.get("wp_id"),
            name=data["name"],
            slug=data.get("slug", ""),
            description=data.get("description", ""),
            parent=data.get("parent"),
        )

    @classmethod
    def from_remote(cls, taxonomy: str, remote: dict, id_to_name: Optional[dict] = None) -> "Term":
        parent_name = None
        if taxonomy == "category" and remote.get("parent") and id_to_name:
            parent_name = id_to_name.get(remote["parent"])
        return cls(
            taxonomy=taxonomy,
            wp_id=remote["id"],
            name=remote["name"],
            slug=remote.get("slug", ""),
            description=remote.get("description", ""),
            parent=parent_name,
        )


@dataclass
class TaxonomyManifest:
    path: Path
    categories: List[Term] = field(default_factory=list)
    tags: List[Term] = field(default_factory=list)

    def all_terms(self) -> List[Term]:
        return self.categories + self.tags

    def dump(self) -> str:
        payload = {
            "categories": [t.to_dict() for t in self.categories],
            "tags": [t.to_dict() for t in self.tags],
        }
        return yaml.safe_dump(payload, sort_keys=False, allow_unicode=True)

    @classmethod
    def load(cls, path: Path) -> "TaxonomyManifest":
        if not path.exists():
            return cls(path=path)
        data = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
        return cls(
            path=path,
            categories=[Term.from_dict("category", t) for t in (data.get("categories") or [])],
            tags=[Term.from_dict("tag", t) for t in (data.get("tags") or [])],
        )

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(self.dump(), encoding="utf-8")


def manifest_path(config: Config) -> Path:
    return config.content_dir / MANIFEST_FILENAME


def load_manifest(config: Config) -> TaxonomyManifest:
    return TaxonomyManifest.load(manifest_path(config))


def pull(config: Config, client: WPClient) -> TaxonomyManifest:
    """Fetch categories/tags from WordPress, replacing the local read-only
    mirror. New locally-drafted terms (``wp_id: null``) are preserved."""
    manifest = load_manifest(config)
    draft_categories = [t for t in manifest.categories if t.wp_id is None]
    draft_tags = [t for t in manifest.tags if t.wp_id is None]

    remote_cats = client.list_terms("category")
    id_to_name = {c["id"]: c["name"] for c in remote_cats}
    manifest.categories = [Term.from_remote("category", c, id_to_name) for c in remote_cats] + draft_categories
    manifest.tags = [Term.from_remote("tag", t) for t in client.list_terms("tag")] + draft_tags
    manifest.save()
    return manifest


def add_term(config: Config, taxonomy: str, name: str, description: str = "", parent: Optional[str] = None) -> Term:
    if taxonomy not in TAXONOMIES:
        raise ValueError(f"taxonomy must be one of {TAXONOMIES}")
    manifest = load_manifest(config)
    bucket = manifest.categories if taxonomy == "category" else manifest.tags
    if any(t.name.lower() == name.lower() for t in bucket):
        raise ValueError(f"A {taxonomy} named '{name}' already exists locally.")
    term = Term(taxonomy=taxonomy, wp_id=None, name=name, description=description, parent=parent)
    bucket.append(term)
    manifest.save()
    return term


def set_description(config: Config, taxonomy: str, name: str, description: str) -> Term:
    manifest = load_manifest(config)
    bucket = manifest.categories if taxonomy == "category" else manifest.tags
    term = next((t for t in bucket if t.name.lower() == name.lower()), None)
    if term is None:
        raise ValueError(f"No local {taxonomy} named '{name}'. Run `wpsync taxonomy pull` or add it first.")
    term.description = description
    manifest.save()
    return term


@dataclass
class PushOutcome:
    taxonomy: str
    name: str
    action: str  # would-create | would-update | create | update | error
    detail: str = ""


def _needs_create(term: Term) -> bool:
    return term.wp_id is None


def _needs_update(term: Term, remote_by_id: dict) -> bool:
    if term.wp_id is None:
        return False
    remote = remote_by_id.get(term.wp_id)
    return remote is not None and remote.get("description", "") != term.description


def plan_push(config: Config, client: Optional[WPClient] = None) -> List[PushOutcome]:
    manifest = load_manifest(config)
    outcomes = []
    for term in manifest.all_terms():
        if _needs_create(term):
            outcomes.append(PushOutcome(term.taxonomy, term.name, "would-create"))
    return outcomes


def apply_push(config: Config, client: WPClient) -> List[PushOutcome]:
    manifest = load_manifest(config)
    outcomes: List[PushOutcome] = []
    changed = False
    for term in manifest.all_terms():
        if not _needs_create(term):
            continue
        try:
            payload = {"name": term.name, "description": term.description}
            created = client.create_term(term.taxonomy, payload)
            term.wp_id = created["id"]
            term.slug = created.get("slug", term.slug)
            changed = True
            outcomes.append(PushOutcome(term.taxonomy, term.name, "create", detail=f"id={term.wp_id}"))
        except Exception as exc:  # noqa: BLE001
            outcomes.append(PushOutcome(term.taxonomy, term.name, "error", detail=str(exc)))
    if changed:
        manifest.save()
    return outcomes
