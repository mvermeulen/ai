"""Markdown <-> WordPress content conversion.

Local posts and pages are plain Markdown files with a YAML frontmatter
header. The body is parsed block-by-block (paragraphs, headings, lists,
quotes, images, horizontal rules, fenced code, and a ``{{strava:...}}``
embed marker) and rendered into the same Gutenberg block HTML WordPress
itself produces, so posts stay fully editable in the block editor after
a push.

Anything WordPress renders that this module doesn't understand -- NextGen
Gallery output, WP Google Maps embeds, raw <script>/<iframe> blocks, and
so on -- is preserved byte-for-byte in a ``<!--raw--> ... <!--/raw-->``
fence rather than being dropped or mangled, both when pulling content
down and when pushing a file back up that still contains one of these
fences. This is deliberate: the tool should never be the reason a hand
crafted embed on the live site disappears.
"""
from __future__ import annotations

import html
import re
from dataclasses import dataclass, field
from html.parser import HTMLParser
from pathlib import Path
from typing import Callable, List, Optional, Tuple

import markdown
import yaml

from . import routes as _routes

RAW_OPEN = "<!--raw-->"
RAW_CLOSE = "<!--/raw-->"

VALID_KINDS = ("post", "page")
VALID_STATUSES = ("draft", "publish", "pending", "private", "future")


# --------------------------------------------------------------------------
# Frontmatter model
# --------------------------------------------------------------------------


@dataclass
class ContentItem:
    kind: str
    path: Path
    title: str
    slug: str
    status: str = "draft"
    date: Optional[str] = None
    categories: List[str] = field(default_factory=list)
    tags: List[str] = field(default_factory=list)
    excerpt: str = ""
    featured_image: Optional[str] = None
    parent: Optional[str] = None
    body: str = ""

    @classmethod
    def load(cls, path: Path) -> "ContentItem":
        text = path.read_text(encoding="utf-8")
        meta, body = _split_frontmatter(text)
        kind = "page" if "/pages/" in path.as_posix() or "\\pages\\" in str(path) else "post"
        return cls(
            kind=meta.get("type", kind),
            path=path,
            title=meta.get("title", path.stem),
            slug=meta.get("slug", path.stem),
            status=meta.get("status", "draft"),
            date=meta.get("date"),
            categories=list(meta.get("categories", []) or []),
            tags=list(meta.get("tags", []) or []),
            excerpt=meta.get("excerpt", "") or "",
            featured_image=meta.get("featured_image"),
            parent=meta.get("parent"),
            body=body,
        )

    def dump(self) -> str:
        meta = {
            "title": self.title,
            "slug": self.slug,
            "type": self.kind,
            "status": self.status,
        }
        if self.date:
            meta["date"] = self.date
        if self.categories:
            meta["categories"] = self.categories
        if self.tags:
            meta["tags"] = self.tags
        if self.excerpt:
            meta["excerpt"] = self.excerpt
        if self.featured_image:
            meta["featured_image"] = self.featured_image
        if self.parent:
            meta["parent"] = self.parent
        front = yaml.safe_dump(meta, sort_keys=False, allow_unicode=True).strip()
        return f"---\n{front}\n---\n\n{self.body.strip()}\n"

    def referenced_media(self) -> List[str]:
        """Local (non-http) media paths referenced by this item, featured image first."""
        found: List[str] = []
        if self.featured_image and not _is_remote(self.featured_image):
            found.append(self.featured_image)
        for m in re.finditer(r"!\[[^\]]*\]\((\S+?)(?:\s+\"[^\"]*\")?\)", self.body):
            src = m.group(1)
            if not _is_remote(src) and src not in found:
                found.append(src)
        return found

    def render_html(self, resolve_image: Callable[[str], str], gpx_root: Optional[Path] = None) -> str:
        return render_body(self.body, resolve_image, gpx_root)


def _is_remote(src: str) -> bool:
    return src.startswith("http://") or src.startswith("https://")


def _split_frontmatter(text: str) -> Tuple[dict, str]:
    if not text.startswith("---"):
        return {}, text
    parts = text.split("\n---", 1)
    if len(parts) != 2:
        return {}, text
    front_raw = parts[0][3:]
    body = parts[1]
    if body.startswith("\n"):
        body = body[1:]
    meta = yaml.safe_load(front_raw) or {}
    return meta, body


# --------------------------------------------------------------------------
# Markdown body -> Gutenberg-flavoured HTML (for push)
# --------------------------------------------------------------------------

_IMAGE_RE = re.compile(r'^!\[(?P<alt>.*?)\]\((?P<src>\S+?)(?:\s+"(?P<caption>.*?)")?\)$')
_STRAVA_RE = re.compile(r"^\{\{strava:(?:(?P<kind>route|activity):)?(?P<id>\d+)\}\}$")
_ROUTE_RE = re.compile(r"^\{\{route:(?P<path>\S+)\}\}$")
_HEADING_RE = re.compile(r"^(#{1,6})\s+(.*)$")
_HR_RE = re.compile(r"^(-{3,}|\*{3,}|_{3,})$")
_LIST_ITEM_RE = re.compile(r"^([-*+]|\d+\.)\s+")

_inline_md = markdown.Markdown(extensions=["extra"])


def _inline_html(text: str) -> str:
    _inline_md.reset()
    out = _inline_md.convert(text)
    if out.startswith("<p>") and out.endswith("</p>"):
        out = out[3:-4]
    return out


def _split_markdown_blocks(text: str) -> List[str]:
    lines = text.replace("\r\n", "\n").split("\n")
    blocks: List[str] = []
    current: List[str] = []
    in_fence = False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("```"):
            current.append(line)
            in_fence = not in_fence
            continue
        if in_fence:
            current.append(line)
            continue
        if stripped == "":
            if current:
                blocks.append("\n".join(current))
                current = []
            continue
        current.append(line)
    if current:
        blocks.append("\n".join(current))
    return blocks


def render_body(body: str, resolve_image: Callable[[str], str], gpx_root: Optional[Path] = None) -> str:
    return "\n\n".join(_render_block(b, resolve_image, gpx_root) for b in _split_markdown_blocks(body))


def _render_block(block: str, resolve_image: Callable[[str], str], gpx_root: Optional[Path] = None) -> str:
    stripped = block.strip()

    if stripped.startswith(RAW_OPEN):
        inner = stripped[len(RAW_OPEN):]
        if inner.rstrip().endswith(RAW_CLOSE):
            inner = inner.rstrip()[: -len(RAW_CLOSE)]
        return inner.strip("\n")

    m = _STRAVA_RE.match(stripped)
    if m:
        kind = m.group("kind") or "route"
        path = "routes" if kind == "route" else "activities"
        url = f"https://www.strava.com/{path}/{m.group('id')}"
        return f'<!-- wp:paragraph -->\n<p class="wp-block-paragraph">{url}</p>\n<!-- /wp:paragraph -->'

    m = _ROUTE_RE.match(stripped)
    if m:
        marker_path = m.group("path")
        base = gpx_root if gpx_root is not None else Path(".")
        return _routes.render_route_html(base / marker_path, marker_path)

    if _HR_RE.match(stripped):
        return '<!-- wp:separator -->\n<hr class="wp-block-separator has-alpha-channel-opacity"/>\n<!-- /wp:separator -->'

    m = _HEADING_RE.match(stripped)
    if m:
        level = len(m.group(1))
        text = _inline_html(m.group(2).strip())
        return (
            f'<!-- wp:heading {{"level":{level}}} -->\n'
            f'<h{level} class="wp-block-heading">{text}</h{level}>\n'
            f"<!-- /wp:heading -->"
        )

    if stripped.startswith("```"):
        lines = stripped.split("\n")
        code_lines = lines[1:]
        if code_lines and code_lines[-1].strip().startswith("```"):
            code_lines = code_lines[:-1]
        code = html.escape("\n".join(code_lines))
        return f'<!-- wp:code -->\n<pre class="wp-block-code"><code>{code}</code></pre>\n<!-- /wp:code -->'

    if "\n" not in stripped:
        m = _IMAGE_RE.match(stripped)
        if m:
            return _render_image(m.group("alt"), m.group("src"), m.group("caption"), resolve_image)

    if stripped.startswith(">"):
        frag = markdown.markdown(block, extensions=["extra"])
        frag = frag.replace("<blockquote>", '<blockquote class="wp-block-quote">', 1)
        return f"<!-- wp:quote -->\n{frag}\n<!-- /wp:quote -->"

    first_line = stripped.split("\n", 1)[0].lstrip()
    if _LIST_ITEM_RE.match(first_line):
        frag = markdown.markdown(block, extensions=["extra", "sane_lists"])
        tag = "ol" if re.match(r"^\d+\.", first_line) else "ul"
        frag = frag.replace(f"<{tag}>", f'<{tag} class="wp-block-list">', 1)
        return f"<!-- wp:list -->\n{frag}\n<!-- /wp:list -->"

    frag = markdown.markdown(block, extensions=["extra"])
    frag = frag.replace("<p>", '<p class="wp-block-paragraph">', 1)
    return f"<!-- wp:paragraph -->\n{frag}\n<!-- /wp:paragraph -->"


def _render_image(alt: str, src: str, caption: Optional[str], resolve_image: Callable[[str], str]) -> str:
    resolved = resolve_image(src)
    img = f'<img src="{resolved}" alt="{html.escape(alt)}"/>'
    if caption:
        fig = (
            f'<figure class="wp-block-image size-large">{img}'
            f'<figcaption class="wp-element-caption">{html.escape(caption)}</figcaption></figure>'
        )
    else:
        fig = f'<figure class="wp-block-image size-large">{img}</figure>'
    return f'<!-- wp:image {{"sizeSlug":"large"}} -->\n{fig}\n<!-- /wp:image -->'


# --------------------------------------------------------------------------
# WordPress HTML -> Markdown body (for pull)
# --------------------------------------------------------------------------

_VOID_TAGS = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr",
}
_STRAVA_URL_RE = re.compile(r"strava\.com/(routes|activities)/(\d+)")
_TAG_RE = re.compile(r"^\s*<([a-zA-Z0-9]+)")


class _TopLevelSplitter(HTMLParser):
    """Splits a fragment of HTML into its top-level (depth-0) elements."""

    def __init__(self, source: str):
        super().__init__(convert_charrefs=False)
        self.source = source
        self._line_starts = self._compute_line_starts(source)
        self.depth = 0
        self.blocks: List[Tuple[int, int]] = []
        self._current_start: Optional[int] = None
        self.feed(source)
        self.close()

    @staticmethod
    def _compute_line_starts(text: str) -> List[int]:
        starts = [0]
        for i, ch in enumerate(text):
            if ch == "\n":
                starts.append(i + 1)
        return starts

    def _offset(self) -> int:
        line, col = self.getpos()
        return self._line_starts[line - 1] + col

    def handle_starttag(self, tag, attrs):
        offset = self._offset()
        if self.depth == 0:
            self._current_start = offset
        if tag.lower() in _VOID_TAGS:
            if self.depth == 0:
                end = self.source.find(">", offset) + 1
                self.blocks.append((offset, end))
                self._current_start = None
        else:
            self.depth += 1

    def handle_startendtag(self, tag, attrs):
        offset = self._offset()
        if self.depth == 0:
            end = self.source.find(">", offset) + 1
            self.blocks.append((offset, end))

    def handle_endtag(self, tag):
        if tag.lower() in _VOID_TAGS:
            return
        if self.depth > 0:
            self.depth -= 1
        if self.depth == 0 and self._current_start is not None:
            offset = self._offset()
            end = self.source.find(">", offset) + 1
            self.blocks.append((self._current_start, end))
            self._current_start = None

    def handle_data(self, data):
        if self.depth == 0 and data.strip():
            offset = self._offset()
            self.blocks.append((offset, offset + len(data)))


def html_to_markdown(html_content: str) -> str:
    html_content = (html_content or "").strip()
    if not html_content:
        return ""
    splitter = _TopLevelSplitter(html_content)
    parts = [_block_to_markdown(html_content[start:end]) for start, end in splitter.blocks]
    return "\n\n".join(p for p in parts if p)


def _inner_html(raw: str, tag: str) -> str:
    m = re.match(rf"^\s*<{tag}[^>]*>(.*)</{tag}>\s*$", raw, re.DOTALL)
    return m.group(1) if m else raw


def _html_inline_to_md(text: str) -> str:
    text = re.sub(r"<(strong|b)>(.*?)</\1>", r"**\2**", text, flags=re.DOTALL)
    text = re.sub(r"<(em|i)>(.*?)</\1>", r"*\2*", text, flags=re.DOTALL)
    text = re.sub(r"<code>(.*?)</code>", r"`\1`", text, flags=re.DOTALL)
    text = re.sub(r'<a[^>]*href="([^"]*)"[^>]*>(.*?)</a>', r"[\2](\1)", text, flags=re.DOTALL)
    text = re.sub(r"<br\s*/?>", "  \n", text)
    text = re.sub(r"<[^>]+>", "", text)
    return html.unescape(text).strip()


def _block_to_markdown(raw: str) -> Optional[str]:
    raw_stripped = raw.strip()
    if not raw_stripped:
        return None
    tag_match = _TAG_RE.match(raw_stripped)
    if not tag_match:
        m = _STRAVA_URL_RE.search(raw_stripped)
        if m:
            kind = "route" if m.group(1) == "routes" else "activity"
            return f"{{{{strava:{kind}:{m.group(2)}}}}}"
        return raw_stripped

    tag = tag_match.group(1).lower()

    if tag == "p":
        inner = _inner_html(raw_stripped, "p")
        plain = re.sub(r"<[^>]+>", "", inner).strip()
        m = re.fullmatch(r"(?:https?://)?(?:www\.)?strava\.com/(routes|activities)/(\d+)/?", plain)
        if m:
            kind = "route" if m.group(1) == "routes" else "activity"
            return f"{{{{strava:{kind}:{m.group(2)}}}}}"
        return _html_inline_to_md(inner)

    if tag in {"h1", "h2", "h3", "h4", "h5", "h6"}:
        level = int(tag[1])
        inner = _inner_html(raw_stripped, tag)
        return f'{"#" * level} {_html_inline_to_md(inner)}'

    if tag == "hr":
        return "---"

    if tag == "figure":
        img_match = re.search(r'<img[^>]*\bsrc="([^"]+)"', raw_stripped)
        safe = img_match and raw_stripped.count("<img") == 1 and "<script" not in raw_stripped
        if safe:
            src = img_match.group(1)
            alt_match = re.search(r'<img[^>]*\balt="([^"]*)"', raw_stripped)
            alt = alt_match.group(1) if alt_match else ""
            cap_match = re.search(r"<figcaption[^>]*>(.*?)</figcaption>", raw_stripped, re.DOTALL)
            if cap_match:
                caption = _html_inline_to_md(cap_match.group(1)).replace('"', "'")
                return f'![{alt}]({src} "{caption}")'
            return f"![{alt}]({src})"
        return _raw_or_strava(raw_stripped)

    if tag in {"ul", "ol"}:
        items = re.findall(r"<li[^>]*>(.*?)</li>", raw_stripped, re.DOTALL)
        if not items:
            return _raw_or_strava(raw_stripped)
        lines = []
        for i, item in enumerate(items):
            bullet = f"{i + 1}." if tag == "ol" else "-"
            lines.append(f"{bullet} {_html_inline_to_md(item)}")
        return "\n".join(lines)

    if tag == "blockquote":
        inner = _inner_html(raw_stripped, "blockquote")
        inner = re.sub(r"</?p[^>]*>", "", inner).strip()
        text = _html_inline_to_md(inner)
        return "\n".join(f"> {line}" for line in text.splitlines()) if text else None

    if tag == "div":
        m = re.search(r'\bdata-wpsync-route="([^"]*)"', raw_stripped)
        if m:
            return f"{{{{route:{html.unescape(m.group(1))}}}}}"

    return _raw_or_strava(raw_stripped)


def _raw_or_strava(raw_stripped: str) -> str:
    if len(raw_stripped) < 2000:
        m = _STRAVA_URL_RE.search(raw_stripped)
        if m:
            kind = "route" if m.group(1) == "routes" else "activity"
            return f"{{{{strava:{kind}:{m.group(2)}}}}}"
    return f"{RAW_OPEN}\n{raw_stripped}\n{RAW_CLOSE}"
