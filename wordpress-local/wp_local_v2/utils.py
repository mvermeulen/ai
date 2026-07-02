from __future__ import annotations

import hashlib
import json
import re
from datetime import datetime, timezone

STRAVA_ROUTE_RE = re.compile(r"https?://www\.strava\.com/routes/\d+")
ASSET_TOKEN_RE = re.compile(r"asset://([A-Za-z0-9._/-]+)")


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def slugify_title(title: str) -> str:
    normalized = re.sub(r"[^a-zA-Z0-9]+", "-", title.strip().lower()).strip("-")
    return normalized or "untitled"


def digest_json(value: object) -> str:
    payload = json.dumps(value, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def digest_text(*parts: str) -> str:
    h = hashlib.sha256()
    for part in parts:
        h.update(part.encode("utf-8"))
    return h.hexdigest()


def markdown_to_html(markdown: str) -> str:
    paragraphs = [p.strip() for p in markdown.split("\n\n") if p.strip()]
    html_parts: list[str] = []
    for p in paragraphs:
        line = p
        line = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\\1</strong>", line)
        line = re.sub(r"\*([^*]+)\*", r"<em>\\1</em>", line)
        line = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r"<a href=\"\\2\">\\1</a>", line)
        line = line.replace("\n", "<br>")
        html_parts.append(f"<p>{line}</p>")
    return "\n".join(html_parts)


def collect_strava_links(markdown: str) -> list[str]:
    matches = STRAVA_ROUTE_RE.findall(markdown)
    unique: list[str] = []
    for item in matches:
        if item not in unique:
            unique.append(item)
    return unique


def replace_asset_tokens(markdown: str, mapping: dict[str, str]) -> str:
    def repl(match: re.Match[str]) -> str:
        token = match.group(1)
        return mapping.get(token, match.group(0))

    return ASSET_TOKEN_RE.sub(repl, markdown)
