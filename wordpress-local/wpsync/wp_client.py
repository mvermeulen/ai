"""Thin wrapper around the WordPress core REST API (wp/v2).

Authentication uses WordPress `Application Passwords
<https://make.wordpress.org/core/2020/11/05/application-passwords-integration-guide/>`_,
which is the safe way to give a script API access without handing over the
real account password: it can be revoked independently from the user's
profile screen at any time without changing the login password.
"""
from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Any, Dict, Iterator, List, Optional

import requests


class WPError(Exception):
    """Base class for anything that goes wrong talking to WordPress."""


class WPAuthError(WPError):
    """Credentials are missing, wrong, or lack the needed capability."""


class WPNotFoundError(WPError):
    """The requested post/page/media id does not exist on the server."""


@dataclass
class WPConfig:
    base_url: str
    username: Optional[str] = None
    app_password: Optional[str] = None
    timeout: int = 20
    verify_ssl: bool = True


class WPClient:
    def __init__(self, config: WPConfig, session: Optional[requests.Session] = None):
        self.config = config
        self.api_root = f"{config.base_url.rstrip('/')}/wp-json/wp/v2"
        self.session = session or requests.Session()
        if config.username and config.app_password:
            self.session.auth = (config.username, config.app_password)
        self.session.headers.setdefault("User-Agent", "wpsync/0.1 (offline authoring tool)")

    # -- low level -----------------------------------------------------

    def _request(self, method: str, path: str, **kwargs) -> requests.Response:
        url = path if path.startswith("http") else f"{self.api_root}{path}"
        last_exc: Optional[Exception] = None
        for attempt in range(3):
            try:
                resp = self.session.request(
                    method,
                    url,
                    timeout=self.config.timeout,
                    verify=self.config.verify_ssl,
                    **kwargs,
                )
            except requests.RequestException as exc:
                last_exc = exc
                time.sleep(0.5 * (attempt + 1))
                continue

            if resp.status_code == 401:
                raise WPAuthError(
                    "WordPress rejected the credentials (401 Unauthorized). "
                    "Check the application password in your wpsync config."
                )
            if resp.status_code == 403:
                raise WPAuthError(
                    f"WordPress refused access (403 Forbidden) to {path}. "
                    "The account may lack permission for this action."
                )
            if resp.status_code == 404:
                raise WPNotFoundError(f"Not found: {path}")
            if resp.status_code >= 500 and attempt < 2:
                time.sleep(0.5 * (attempt + 1))
                continue
            if not resp.ok:
                raise WPError(f"{method} {path} failed: {resp.status_code} {resp.text[:400]}")
            return resp
        raise WPError(f"Could not reach {url} after 3 attempts: {last_exc}")

    def _paginated_get(self, path: str, params: Optional[Dict[str, Any]] = None) -> Iterator[dict]:
        params = dict(params or {})
        params.setdefault("per_page", 50)
        page = 1
        while True:
            params["page"] = page
            resp = self._request("GET", path, params=params)
            items = resp.json()
            for item in items:
                yield item
            total_pages = int(resp.headers.get("X-WP-TotalPages", "1") or "1")
            if page >= total_pages:
                break
            page += 1

    # -- identity / connectivity ----------------------------------------

    def whoami(self) -> dict:
        return self._request("GET", "/users/me", params={"context": "edit"}).json()

    # -- posts / pages ----------------------------------------------------

    def list_all(self, post_type: str, status: str = "any") -> List[dict]:
        path = "/posts" if post_type == "post" else "/pages"
        return list(self._paginated_get(path, {"status": status, "context": "edit"}))

    def get(self, post_type: str, wp_id: int) -> dict:
        path = f"/posts/{wp_id}" if post_type == "post" else f"/pages/{wp_id}"
        return self._request("GET", path, params={"context": "edit"}).json()

    def create(self, post_type: str, data: dict) -> dict:
        path = "/posts" if post_type == "post" else "/pages"
        return self._request("POST", path, json=data).json()

    def update(self, post_type: str, wp_id: int, data: dict) -> dict:
        path = f"/posts/{wp_id}" if post_type == "post" else f"/pages/{wp_id}"
        return self._request("POST", path, json=data).json()

    def delete(self, post_type: str, wp_id: int, force: bool = False) -> dict:
        path = f"/posts/{wp_id}" if post_type == "post" else f"/pages/{wp_id}"
        return self._request("DELETE", path, params={"force": force}).json()

    # -- media ------------------------------------------------------------

    def upload_media(self, filename: str, content: bytes, mime_type: str) -> dict:
        headers = {
            "Content-Disposition": f'attachment; filename="{filename}"',
            "Content-Type": mime_type,
        }
        return self._request("POST", "/media", data=content, headers=headers).json()

    def list_media(self, search: Optional[str] = None) -> List[dict]:
        params = {"context": "edit"}
        if search:
            params["search"] = search
        return list(self._paginated_get("/media", params))

    # -- taxonomy ---------------------------------------------------------

    def ensure_terms(self, taxonomy: str, names: List[str]) -> List[int]:
        """Return term ids for the given names, creating any that don't exist yet."""
        path = "/categories" if taxonomy == "category" else "/tags"
        ids: List[int] = []
        for name in names:
            existing = list(self._paginated_get(path, {"search": name}))
            match = next((t for t in existing if t["name"].lower() == name.lower()), None)
            if match:
                ids.append(match["id"])
            else:
                created = self._request("POST", path, json={"name": name}).json()
                ids.append(created["id"])
        return ids
