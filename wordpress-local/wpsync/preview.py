"""Offline preview: render a local post/page to a standalone styled HTML
file and open it in the default browser. No WordPress connection needed --
this is meant for reviewing how a draft reads while genuinely offline.
"""
from __future__ import annotations

import re
import tempfile
import webbrowser
from pathlib import Path
from typing import Callable

from .content import ContentItem

_TEMPLATE = """<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>{title} (wpsync preview)</title>
<style>
  body {{ font-family: Georgia, 'Times New Roman', serif; max-width: 720px; margin: 3rem auto;
          padding: 0 1.5rem; line-height: 1.65; color: #222; }}
  .wpsync-banner {{ background: #2c3338; color: #fff; font-family: -apple-system, sans-serif;
                     font-size: 0.85rem; padding: 0.6rem 1rem; border-radius: 6px; margin-bottom: 2rem; }}
  h1, h2, h3, h4, h5, h6 {{ font-family: -apple-system, sans-serif; }}
  img {{ max-width: 100%; height: auto; border-radius: 4px; }}
  figure {{ margin: 1.5rem 0; }}
  figcaption {{ font-size: 0.85rem; color: #666; margin-top: 0.4rem; }}
  blockquote {{ border-left: 4px solid #ccc; margin: 1.5rem 0; padding: 0.2rem 1.2rem; color: #444; font-style: italic; }}
  pre {{ background: #f5f5f5; padding: 1rem; overflow-x: auto; border-radius: 4px; }}
  .wpsync-meta {{ font-family: -apple-system, sans-serif; color: #777; font-size: 0.9rem; margin-bottom: 2rem; }}
</style>
</head>
<body>
<div class="wpsync-banner">wpsync offline preview &mdash; not yet synced to WordPress.
Unsynced local images are shown as broken-image placeholders.</div>
<h1>{title}</h1>
<div class="wpsync-meta">{meta}</div>
{body}
</body>
</html>
"""


def render_preview_html(item: ContentItem, resolve_image: Callable[[str], str]) -> str:
    meta_bits = [f"status: {item.status}"]
    if item.date:
        meta_bits.append(item.date)
    if item.categories:
        meta_bits.append("categories: " + ", ".join(item.categories))
    if item.tags:
        meta_bits.append("tags: " + ", ".join(item.tags))
    body_html = item.render_html(resolve_image)
    body_html = re.sub(r"<!--\s*/?wp:[^>]*-->\n?", "", body_html)
    return _TEMPLATE.format(title=item.title, meta=" &middot; ".join(meta_bits), body=body_html)


def write_and_open(item: ContentItem, resolve_image: Callable[[str], str], open_browser: bool = True) -> Path:
    html = render_preview_html(item, resolve_image)
    tmp_dir = Path(tempfile.gettempdir()) / "wpsync-preview"
    tmp_dir.mkdir(parents=True, exist_ok=True)
    out_path = tmp_dir / f"{item.slug}.html"
    out_path.write_text(html, encoding="utf-8")
    if open_browser:
        webbrowser.open(out_path.as_uri())
    return out_path
