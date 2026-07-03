from __future__ import annotations

from pathlib import Path

import pytest

from wpsync.config import Config
from wpsync.state import StateStore
from wpsync.wp_client import WPClient, WPConfig

BASE_URL = "https://example.test/gone2look4america"
API_ROOT = f"{BASE_URL}/wp-json/wp/v2"


@pytest.fixture
def project(tmp_path: Path) -> Config:
    content_dir = tmp_path / "content"
    (content_dir / "posts").mkdir(parents=True)
    (content_dir / "pages").mkdir(parents=True)
    (content_dir / "media").mkdir(parents=True)
    state_dir = tmp_path / ".wpsync"
    state_dir.mkdir()
    return Config(
        base_url=BASE_URL,
        username="mike",
        app_password="dummy-app-password",
        content_dir=content_dir,
        state_dir=state_dir,
    )


@pytest.fixture
def state(project: Config) -> StateStore:
    return StateStore(project.state_dir)


@pytest.fixture
def client(project: Config) -> WPClient:
    return WPClient(WPConfig(base_url=project.base_url, username=project.username, app_password=project.app_password))


def make_remote(
    id=1,
    slug="hello-world",
    title="Hello World",
    content="<!-- wp:paragraph -->\n<p class=\"wp-block-paragraph\">Hi there.</p>\n<!-- /wp:paragraph -->",
    status="publish",
    modified_gmt="2026-01-01T00:00:00",
    kind="post",
):
    return {
        "id": id,
        "slug": slug,
        "status": status,
        "type": kind,
        "date": "2026-01-01T00:00:00",
        "modified_gmt": modified_gmt,
        "title": {"raw": title, "rendered": title},
        "content": {"raw": content, "rendered": content, "protected": False},
        "excerpt": {"raw": "", "rendered": ""},
        "categories": [],
        "tags": [],
    }
