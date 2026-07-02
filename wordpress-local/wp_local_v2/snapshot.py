from __future__ import annotations

from .config import AppConfig, require_apply_credentials
from .storage import write_snapshot
from .types import EntryType
from .wordpress_api import WordPressApi


def pull_snapshot(config: AppConfig, limit_per_type: int = 40) -> dict[str, dict]:
    require_apply_credentials(config)
    api = WordPressApi(config.site_url, config.username or "", config.app_password or "")

    items: dict[str, dict] = {}
    for entry_type in ("post", "page"):
        _pull_type(api, entry_type, limit_per_type, items)

    write_snapshot(items)
    return items


def _pull_type(
    api: WordPressApi,
    entry_type: EntryType,
    limit_per_type: int,
    items: dict[str, dict],
) -> None:
    seen = 0
    page = 1
    while seen < limit_per_type:
        per_page = min(20, limit_per_type - seen)
        batch = api.list_items(entry_type, per_page=per_page, page=page)
        if not batch:
            break

        for item in batch:
            slug = str(item.get("slug", "")).strip()
            if not slug:
                continue
            key = f"{entry_type}:{slug}"
            items[key] = {
                "id": int(item["id"]),
                "modified_gmt": str(item.get("modified_gmt", "")),
                "status": str(item.get("status", "")),
                "title": str(item.get("title", {}).get("rendered", "")),
            }
            seen += 1
            if seen >= limit_per_type:
                break
        page += 1
