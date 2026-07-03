"""Extra guardrails layered on top of every command that writes to
WordPress, on top of (never instead of) the existing dry-run-by-default
and conflict-detection safety model in ``sync.py``.

* **Host pinning.** The first mutating run against a site pins its host
  into ``.wpsync/pinned_host.json``. Every later mutating run verifies the
  configured ``base_url`` still resolves to that same host -- a guard
  against a stray edit to ``config.yml`` (or running wpsync from the
  wrong project checkout) silently redirecting a push at an unintended
  site. A genuine host change requires ``--confirm-host-change``.

* **Batch cap.** Applying more than ``max_batch`` changes in one go
  requires ``--yes-large-batch``, a guard against a corrupted or
  wiped state file suddenly making everything look "new" or "modified"
  and pushing far more than intended.

* **Typed confirmation.** Immediately before a plan is actually applied,
  the plan summary is shown and (only when attached to a real terminal)
  the operator must type "yes". Skippable with ``--yes`` for scripted or
  CI use, where a prompt nobody can answer would just hang.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path
from urllib.parse import urlparse

from .config import Config

PINNED_HOST_FILENAME = "pinned_host.json"
DEFAULT_MAX_BATCH = 20


class SafetyError(RuntimeError):
    pass


def _pinned_host_path(config: Config) -> Path:
    return config.state_dir / PINNED_HOST_FILENAME


def _host_of(base_url: str) -> str:
    return urlparse(base_url).netloc.lower()


def check_host(config: Config, confirm_host_change: bool = False) -> None:
    path = _pinned_host_path(config)
    current_host = _host_of(config.base_url)
    if not path.exists():
        _pin(path, current_host)
        return
    pinned = json.loads(path.read_text(encoding="utf-8")).get("host")
    if pinned == current_host:
        return
    if not confirm_host_change:
        raise SafetyError(
            f"Configured host '{current_host}' does not match the host this project "
            f"was previously pinned to ('{pinned}', recorded in .wpsync/pinned_host.json). "
            "This check exists to catch wpsync accidentally being pointed at the wrong "
            "site. If this change is intentional, re-run with --confirm-host-change."
        )
    _pin(path, current_host)


def _pin(path: Path, host: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"host": host}, indent=2) + "\n", encoding="utf-8")


def check_batch_size(count: int, yes_large_batch: bool, max_batch: int = DEFAULT_MAX_BATCH) -> None:
    if count > max_batch and not yes_large_batch:
        raise SafetyError(
            f"This action would change {count} item(s) at once, more than the safety "
            f"cap of {max_batch}. If this is really what you intend, re-run with "
            "--yes-large-batch."
        )


def confirm(summary: str, assume_yes: bool = False, stream=None) -> bool:
    """Interactive apply gate. Always returns True without prompting when
    ``assume_yes`` is set, or when stdin isn't a real terminal (scripted/CI
    use), so automation is never blocked on an unanswerable prompt."""
    if assume_yes:
        return True
    stream = stream if stream is not None else sys.stdin
    if not stream.isatty():
        return True
    print(summary)
    answer = input("Type 'yes' to apply these changes: ")
    return answer.strip().lower() == "yes"
