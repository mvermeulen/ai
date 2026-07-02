from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import urlparse


@dataclass
class AppConfig:
    site_url: str
    username: str | None
    app_password: str | None
    allowed_hosts: list[str]
    max_plan_operations: int
    max_plan_age_hours: int


class ConfigError(RuntimeError):
    pass


def load_config() -> AppConfig:
    _load_dotenv(Path.cwd() / ".env")
    site_url = os.getenv("WP_SITE_URL")
    if not site_url:
        raise ConfigError("WP_SITE_URL is required. See .env.example")

    parsed = urlparse(site_url)
    if not parsed.scheme or not parsed.netloc:
        raise ConfigError("WP_SITE_URL must be a valid URL")

    username = os.getenv("WP_USERNAME")
    app_password = os.getenv("WP_APPLICATION_PASSWORD")
    allow_hosts_raw = os.getenv("WP_ALLOWED_HOSTS", parsed.hostname or "")
    allowed_hosts = [h.strip() for h in allow_hosts_raw.split(",") if h.strip()]

    max_ops = _parse_positive_int(os.getenv("WP_PLAN_MAX_OPERATIONS", "20"), 20)
    max_age = _parse_positive_int(os.getenv("WP_PLAN_MAX_AGE_HOURS", "72"), 72)

    return AppConfig(
        site_url=site_url.rstrip("/"),
        username=username,
        app_password=app_password,
        allowed_hosts=allowed_hosts,
        max_plan_operations=max_ops,
        max_plan_age_hours=max_age,
    )


def require_apply_credentials(config: AppConfig) -> None:
    if not config.username or not config.app_password:
        raise ConfigError("WP_USERNAME and WP_APPLICATION_PASSWORD are required for this command")


def assert_host_allowed(config: AppConfig, expected_host: str | None = None) -> None:
    host = urlparse(config.site_url).hostname or ""
    if host not in config.allowed_hosts:
        raise ConfigError(
            f"Refusing apply. Host '{host}' is not in WP_ALLOWED_HOSTS: {', '.join(config.allowed_hosts)}"
        )
    if expected_host and expected_host != host:
        raise ConfigError(f"Refusing apply. Expected host '{expected_host}' does not match '{host}'")


def _parse_positive_int(raw: str, fallback: int) -> int:
    try:
        value = int(raw)
    except ValueError:
        return fallback
    if value <= 0:
        return fallback
    return value


def _load_dotenv(dotenv_file: Path) -> None:
    if not dotenv_file.exists():
        return

    for line in dotenv_file.read_text(encoding="utf-8").splitlines():
        text = line.strip()
        if not text or text.startswith("#") or "=" not in text:
            continue
        key, value = text.split("=", 1)
        key = key.strip()
        value = value.strip().strip('"').strip("'")
        if key and key not in os.environ:
            os.environ[key] = value
