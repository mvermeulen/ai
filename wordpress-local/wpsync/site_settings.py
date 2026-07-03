"""Offline site settings: title, tagline, and a few other core WordPress
settings, editable in ``content/site.yaml`` and synced via the core
``/wp/v2/settings`` REST endpoint (requires ``manage_options``, i.e. an
administrator's Application Password).

Only a safe, deliberately small allowlist of fields is synced -- things
like the site title and tagline that are painful to be without a fast
offline edit path for, not things like ``default_role`` or
``start_of_week`` where a stray edit has security or scheduling
consequences well outside what an "offline blog authoring tool" should be
touching.
"""
from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

import yaml

from .config import Config
from .wp_client import WPClient

MANIFEST_FILENAME = "site.yaml"
STATE_FILENAME = "settings_state.json"

# Deliberately narrow: fields safe for a single-author blog tool to manage.
ALLOWED_FIELDS = ("title", "description", "timezone", "date_format", "time_format")


def manifest_path(config: Config) -> Path:
    return config.content_dir / MANIFEST_FILENAME


def _state_path(config: Config) -> Path:
    return config.state_dir / STATE_FILENAME


def _hash(data: dict) -> str:
    return hashlib.sha256(json.dumps(data, sort_keys=True).encode("utf-8")).hexdigest()


@dataclass
class SiteSettings:
    values: Dict[str, str] = field(default_factory=dict)

    def dump(self) -> str:
        return yaml.safe_dump(self.values, sort_keys=False, allow_unicode=True)

    @classmethod
    def load(cls, path: Path) -> "SiteSettings":
        if not path.exists():
            return cls()
        data = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
        return cls(values={k: v for k, v in data.items() if k in ALLOWED_FIELDS})

    def save(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(self.dump(), encoding="utf-8")


def load(config: Config) -> SiteSettings:
    return SiteSettings.load(manifest_path(config))


def _load_last_synced(config: Config) -> Optional[dict]:
    p = _state_path(config)
    if not p.exists():
        return None
    return json.loads(p.read_text(encoding="utf-8"))


def _save_last_synced(config: Config, values: dict) -> None:
    p = _state_path(config)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(values, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def pull(config: Config, client: WPClient) -> SiteSettings:
    remote = client.get_settings()
    values = {k: remote.get(k, "") for k in ALLOWED_FIELDS if k in remote}
    settings = SiteSettings(values=values)
    settings.save(manifest_path(config))
    _save_last_synced(config, values)
    return settings


def status(config: Config) -> str:
    """'new' (never pulled), 'modified', or 'unchanged', purely offline."""
    last_synced = _load_last_synced(config)
    local = load(config)
    if last_synced is None:
        return "new" if local.values else "unsynced"
    return "modified" if local.values != last_synced else "unchanged"


@dataclass
class PushOutcome:
    action: str  # would-update | update | unchanged | error
    detail: str = ""
    changed_fields: Optional[List[str]] = None


def plan_push(config: Config) -> PushOutcome:
    last_synced = _load_last_synced(config) or {}
    local = load(config)
    changed = sorted(k for k in local.values if local.values.get(k) != last_synced.get(k))
    if not changed:
        return PushOutcome("unchanged")
    return PushOutcome("would-update", changed_fields=changed)


def apply_push(config: Config, client: WPClient) -> PushOutcome:
    plan = plan_push(config)
    if plan.action == "unchanged":
        return plan
    local = load(config)
    try:
        updated = client.update_settings(local.values)
    except Exception as exc:  # noqa: BLE001
        return PushOutcome("error", detail=str(exc))
    values = {k: updated.get(k, local.values.get(k)) for k in ALLOWED_FIELDS if k in local.values}
    _save_last_synced(config, values)
    return PushOutcome("update", changed_fields=plan.changed_fields)
