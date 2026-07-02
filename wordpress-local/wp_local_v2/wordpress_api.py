from __future__ import annotations

import base64
import json
from dataclasses import dataclass
from pathlib import Path
from urllib import request
from urllib.error import HTTPError

from .types import EntryType, LocalEntry


@dataclass
class WpResult:
    wp_id: int
    modified_gmt: str


class WordPressApi:
    def __init__(self, site_url: str, username: str, app_password: str) -> None:
        self.site_url = site_url.rstrip("/")
        token = base64.b64encode(f"{username}:{app_password}".encode("utf-8")).decode("ascii")
        self.auth_header = f"Basic {token}"

    def list_items(self, entry_type: EntryType, per_page: int = 20, page: int = 1) -> list[dict]:
        endpoint = "posts" if entry_type == "post" else "pages"
        url = f"{self.site_url}/wp-json/wp/v2/{endpoint}?per_page={per_page}&page={page}&context=edit"
        return self._request_json(url)

    def get_item(self, entry_type: EntryType, wp_id: int) -> dict:
        endpoint = "posts" if entry_type == "post" else "pages"
        url = f"{self.site_url}/wp-json/wp/v2/{endpoint}/{wp_id}?context=edit"
        return self._request_json(url)

    def create_or_update(self, entry: LocalEntry, wp_id: int | None) -> WpResult:
        endpoint = "posts" if entry.entry_type == "post" else "pages"
        target = f"{self.site_url}/wp-json/wp/v2/{endpoint}"
        if wp_id is not None:
            target = f"{target}/{wp_id}"

        body = {
            "title": entry.title,
            "slug": entry.slug,
            "status": entry.status,
            "excerpt": entry.excerpt,
            "content": entry.body_html,
        }

        result = self._request_json(target, method="POST", payload=body)
        return WpResult(wp_id=int(result["id"]), modified_gmt=str(result["modified_gmt"]))

    def upload_asset(self, asset_path: Path) -> dict:
        url = f"{self.site_url}/wp-json/wp/v2/media"
        data = asset_path.read_bytes()
        headers = {
            "Authorization": self.auth_header,
            "Content-Type": "application/octet-stream",
            "Content-Disposition": f'attachment; filename="{asset_path.name}"',
        }
        req = request.Request(url, method="POST", data=data, headers=headers)
        try:
            with request.urlopen(req) as resp:
                return json.loads(resp.read().decode("utf-8"))
        except HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            raise RuntimeError(f"Asset upload failed for {asset_path}: {exc.code} {detail}") from exc

    def _request_json(self, url: str, method: str = "GET", payload: dict | None = None):
        headers = {
            "Authorization": self.auth_header,
            "Content-Type": "application/json",
        }
        data = None
        if payload is not None:
            data = json.dumps(payload).encode("utf-8")

        req = request.Request(url, method=method, headers=headers, data=data)
        try:
            with request.urlopen(req) as resp:
                return json.loads(resp.read().decode("utf-8"))
        except HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            raise RuntimeError(f"WordPress API error {exc.code} for {url}: {detail}") from exc
