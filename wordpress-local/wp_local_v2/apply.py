from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from .config import AppConfig, assert_host_allowed, require_apply_credentials
from .paths import ASSETS
from .planner import PlanError, assert_plan_fresh
from .storage import append_ledger
from .types import LedgerItem, LocalEntry
from .utils import collect_strava_links, replace_asset_tokens, utc_now_iso
from .vault import load_entries
from .wordpress_api import WordPressApi


@dataclass
class ApplyOptions:
    execute: bool
    ack: str | None
    expected_host: str | None
    force_large: bool
    force_conflicts: bool


def apply_plan(plan: dict, config: AppConfig, options: ApplyOptions) -> dict:
    assert_plan_fresh(plan, config.max_plan_age_hours)
    _assert_ack(plan, options.ack)

    operations: list[dict] = plan.get("operations", [])
    if len(operations) > int(plan.get("max_operations", config.max_plan_operations)) and not options.force_large:
        raise PlanError(
            f"Plan has {len(operations)} operations which exceeds max_operations. Use --force-large to continue"
        )

    if not options.execute:
        return {"applied": 0, "conflicts": 0, "skipped": len(operations), "mode": "dry-run"}

    assert_host_allowed(config, options.expected_host)
    require_apply_credentials(config)

    api = WordPressApi(config.site_url, config.username or "", config.app_password or "")
    applied = 0
    conflicts = 0
    skipped = 0
    local_by_key = {f"{x.entry_type}:{x.slug}": x for x in load_entries()}

    for op in operations:
        key = op["key"]
        local_entry = local_by_key.get(key)
        if local_entry is None:
            skipped += 1
            continue

        entry = _materialize_entry_with_assets(api, local_entry)

        if op["action"] == "update" and op.get("remote_id") is not None:
            remote = api.get_item(entry.entry_type, int(op["remote_id"]))
            expected = op.get("expected_remote_modified_gmt")
            actual = str(remote.get("modified_gmt", ""))
            if expected and expected != actual and not options.force_conflicts:
                conflicts += 1
                continue
            result = api.create_or_update(entry, int(op["remote_id"]))
        else:
            result = api.create_or_update(entry, None)

        append_ledger(
            LedgerItem(
                key=key,
                entry_type=entry.entry_type,
                slug=entry.slug,
                wp_id=result.wp_id,
                last_remote_modified_gmt=result.modified_gmt,
                last_local_hash=entry.content_hash,
                applied_at=utc_now_iso(),
            )
        )
        applied += 1

    return {"applied": applied, "conflicts": conflicts, "skipped": skipped, "mode": "execute"}


def _assert_ack(plan: dict, ack: str | None) -> None:
    expected = str(plan.get("requires_ack", ""))
    if ack != expected:
        raise PlanError(
            "ACK phrase mismatch. Re-run with exact --ack value shown in plan summary."
        )


def _materialize_entry_with_assets(api: WordPressApi, entry: LocalEntry) -> LocalEntry:
    mapping: dict[str, str] = {}
    tokens = [t for t in _extract_asset_tokens(entry.body_markdown)]

    for token in tokens:
        asset_file = ASSETS / token
        if not asset_file.exists() or not asset_file.is_file():
            continue
        uploaded = api.upload_asset(asset_file)
        source_url = str(uploaded.get("source_url", "")).strip()
        if source_url:
            mapping[token] = source_url

    markdown = replace_asset_tokens(entry.body_markdown, mapping)
    strava_links = collect_strava_links(markdown)
    html = entry.body_html if not mapping else entry.body_html.replace(entry.body_markdown, markdown)
    if mapping:
        from .utils import markdown_to_html

        html = markdown_to_html(markdown)

    if strava_links:
        routes = ", ".join(strava_links)
        html += f"\n<!-- strava_routes: {routes} -->"

    return LocalEntry(
        entry_type=entry.entry_type,
        slug=entry.slug,
        title=entry.title,
        status=entry.status,
        excerpt=entry.excerpt,
        tags=entry.tags,
        categories=entry.categories,
        body_markdown=markdown,
        body_html=html,
        content_hash=entry.content_hash,
        entry_dir=entry.entry_dir,
    )


def _extract_asset_tokens(markdown: str) -> set[str]:
    from .utils import ASSET_TOKEN_RE

    out: set[str] = set()
    for match in ASSET_TOKEN_RE.finditer(markdown):
        out.add(match.group(1))
    return out
