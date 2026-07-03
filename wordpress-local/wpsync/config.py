"""Site configuration for wpsync.

Non-secret settings (site URL, username, content directory) live in
``.wpsync/config.yml``, which is safe to read but is still gitignored so a
clone of this repo never silently points at someone else's site. The
Application Password itself is read from the ``WPSYNC_APP_PASSWORD``
environment variable by preference -- it never needs to touch disk at all.
As a convenience for local-only use it may also be set in config.yml, but
the CLI's ``init`` command recommends the environment variable.
"""
from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

import yaml

DEFAULT_CONFIG_DIR = Path(".wpsync")
DEFAULT_CONFIG_FILE = "config.yml"
APP_PASSWORD_ENV_VAR = "WPSYNC_APP_PASSWORD"


class ConfigError(RuntimeError):
    pass


@dataclass
class Config:
    base_url: str
    username: str
    app_password: Optional[str]
    content_dir: Path
    state_dir: Path

    @classmethod
    def load(cls, root: Path = Path(".")) -> "Config":
        config_path = root / DEFAULT_CONFIG_DIR / DEFAULT_CONFIG_FILE
        if not config_path.exists():
            raise ConfigError(
                f"No config found at {config_path}. Run `wpsync init` first."
            )
        data = yaml.safe_load(config_path.read_text(encoding="utf-8")) or {}
        base_url = data.get("base_url")
        username = data.get("username")
        if not base_url or not username:
            raise ConfigError(f"{config_path} must set both base_url and username")
        app_password = os.environ.get(APP_PASSWORD_ENV_VAR) or data.get("app_password")
        return cls(
            base_url=base_url,
            username=username,
            app_password=app_password,
            content_dir=root / data.get("content_dir", "content"),
            state_dir=root / DEFAULT_CONFIG_DIR,
        )

    def require_credentials(self) -> None:
        if not self.app_password:
            raise ConfigError(
                "No application password available. Set the "
                f"{APP_PASSWORD_ENV_VAR} environment variable (recommended), "
                "or add app_password to .wpsync/config.yml. Create one under "
                "WordPress admin -> Users -> Profile -> Application Passwords."
            )

    @staticmethod
    def write_template(root: Path, base_url: str, username: str) -> Path:
        config_dir = root / DEFAULT_CONFIG_DIR
        config_dir.mkdir(parents=True, exist_ok=True)
        config_path = config_dir / DEFAULT_CONFIG_FILE
        template = (
            f"base_url: {base_url}\n"
            f"username: {username}\n"
            "content_dir: content\n"
            "\n"
            "# Application password: prefer setting the WPSYNC_APP_PASSWORD\n"
            "# environment variable instead of storing it here. This file is\n"
            "# gitignored either way, but the env var never touches disk.\n"
            "# app_password: xxxx xxxx xxxx xxxx xxxx xxxx\n"
        )
        config_path.write_text(template, encoding="utf-8")
        return config_path
