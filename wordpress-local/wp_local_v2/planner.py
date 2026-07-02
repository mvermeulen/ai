from __future__ import annotations

import json
from dataclasses import asdict
from datetime import datetime, timedelta, timezone
from pathlib import Path

from .config import AppConfig
from .paths import PLANS, ensure_layout
from .storage import load_ledger_index, load_snapshot
from .types import LocalEntry
from .utils import digest_json, utc_now_iso


class PlanError(RuntimeError):
    pass


def build_plan(entries: list[LocalEntry], config: AppConfig, plan_name: str | None = None) -> Path:
    ensure_layout()
    ledger = load_ledger_index()
    snapshot = load_snapshot()

    created_at = utc_now_iso()
    plan_id = plan_name or created_at.replace(":", "").replace("+00:00", "Z")
    operations: list[dict] = []

    for entry in entries:
        key = f"{entry.entry_type}:{entry.slug}"
        ledger_item = ledger.get(key)
        snapshot_item = snapshot.get(key)

        if ledger_item and ledger_item.last_local_hash == entry.content_hash:
            continue

        if ledger_item is None:
            op = {
                "action": "create",
                "key": key,
                "entry_type": entry.entry_type,
                "slug": entry.slug,
                "entry": asdict(entry),
                "expected_remote_modified_gmt": None,
                "remote_id": None,
                "snapshot_remote_id": snapshot_item.get("id") if snapshot_item else None,
            }
        else:
            op = {
                "action": "update",
                "key": key,
                "entry_type": entry.entry_type,
                "slug": entry.slug,
                "entry": asdict(entry),
                "expected_remote_modified_gmt": ledger_item.last_remote_modified_gmt,
                "remote_id": ledger_item.wp_id,
                "snapshot_remote_id": snapshot_item.get("id") if snapshot_item else ledger_item.wp_id,
            }
        operations.append(op)

    checksum = digest_json(operations)
    payload = {
        "version": 1,
        "plan_id": plan_id,
        "created_at": created_at,
        "target_site": config.site_url,
        "requires_ack": f"APPROVE-{plan_id}",
        "max_operations": config.max_plan_operations,
        "checksum": checksum,
        "operations": operations,
    }

    plan_file = PLANS / f"plan-{plan_id}.json"
    plan_file.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    return plan_file


def load_plan(plan_path: Path) -> dict:
    payload = json.loads(plan_path.read_text(encoding="utf-8"))
    if payload.get("version") != 1:
        raise PlanError("Unsupported plan version")

    operations = payload.get("operations", [])
    checksum = payload.get("checksum")
    if checksum != digest_json(operations):
        raise PlanError("Plan checksum mismatch. Refusing to apply potentially tampered plan")

    return payload


def assert_plan_fresh(plan: dict, max_age_hours: int) -> None:
    created_at = datetime.fromisoformat(plan["created_at"].replace("Z", "+00:00"))
    oldest_ok = datetime.now(timezone.utc) - timedelta(hours=max_age_hours)
    if created_at < oldest_ok:
        raise PlanError(
            f"Plan is older than {max_age_hours} hours. Rebuild a fresh plan before apply"
        )


def summarize_plan(plan: dict) -> str:
    ops = plan.get("operations", [])
    create_count = sum(1 for op in ops if op.get("action") == "create")
    update_count = sum(1 for op in ops if op.get("action") == "update")
    lines = [
        f"Plan: {plan['plan_id']}",
        f"Target: {plan['target_site']}",
        f"Created: {plan['created_at']}",
        f"Operations: {len(ops)} (create={create_count}, update={update_count})",
        f"Ack phrase: {plan['requires_ack']}",
    ]
    return "\n".join(lines)
