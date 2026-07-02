from pathlib import Path

from wpsync.content import ContentItem, html_to_markdown, render_body


def identity(src: str) -> str:
    return src


def test_frontmatter_round_trip(tmp_path: Path):
    item = ContentItem(
        kind="post",
        path=tmp_path / "posts" / "hello.md",
        title="Hello World",
        slug="hello-world",
        status="draft",
        date="2026-01-01T00:00:00",
        categories=["cycling"],
        tags=["texas", "capitols"],
        excerpt="A short teaser.",
        featured_image="media/hello.jpg",
        body="Some **bold** text.\n",
    )
    dumped = item.dump()
    (tmp_path / "posts").mkdir(parents=True)
    path = tmp_path / "posts" / "hello.md"
    path.write_text(dumped, encoding="utf-8")

    loaded = ContentItem.load(path)
    assert loaded.title == "Hello World"
    assert loaded.slug == "hello-world"
    assert loaded.categories == ["cycling"]
    assert loaded.tags == ["texas", "capitols"]
    assert loaded.featured_image == "media/hello.jpg"
    assert "bold" in loaded.body


def test_render_paragraph_and_heading():
    body = "# Trip Update\n\nWe rode **80 miles** today."
    html_out = render_body(body, identity)
    assert '<!-- wp:heading {"level":1} -->' in html_out
    assert '<h1 class="wp-block-heading">Trip Update</h1>' in html_out
    assert '<p class="wp-block-paragraph">We rode <strong>80 miles</strong> today.</p>' in html_out


def test_render_image_with_caption():
    body = '![A sunrise](media/sunrise.jpg "Morning in Texas")'

    def resolve(src):
        return "https://cdn.example/sunrise.jpg" if src == "media/sunrise.jpg" else src

    html_out = render_body(body, resolve)
    assert 'src="https://cdn.example/sunrise.jpg"' in html_out
    assert 'wp-block-image' in html_out
    assert "Morning in Texas" in html_out


def test_render_strava_marker_becomes_bare_url_for_autoembed():
    html_out = render_body("{{strava:route:12345678}}", identity)
    assert "<p class=\"wp-block-paragraph\">https://www.strava.com/routes/12345678</p>" in html_out

    html_out2 = render_body("{{strava:activity:987}}", identity)
    assert "https://www.strava.com/activities/987" in html_out2


def test_render_list_quote_hr_code():
    body = "- one\n- two\n\n> a wise quote\n\n---\n\n```\nprint('hi')\n```"
    html_out = render_body(body, identity)
    assert '<ul class="wp-block-list">' in html_out
    assert "<li>one</li>" in html_out
    assert 'wp-block-quote' in html_out
    assert 'wp-block-separator' in html_out
    assert '<pre class="wp-block-code">' in html_out
    assert "print(&#x27;hi&#x27;)" in html_out or "print('hi')" in html_out


def test_raw_passthrough_preserved_verbatim():
    raw_snippet = '<div class="ngg-gallery-wrap" data-id="3"><script>weirdJsThatWeDontUnderstand();</script></div>'
    body = f"Some intro text.\n\n<!--raw-->\n{raw_snippet}\n<!--/raw-->"
    html_out = render_body(body, identity)
    assert raw_snippet in html_out
    assert "<!--raw-->" not in html_out


def test_html_to_markdown_basic_roundtrip():
    html_in = (
        '<p class="wp-block-paragraph">We rolled into <strong>Austin</strong> today.</p>'
        '<h2 class="wp-block-heading">Next stop</h2>'
        '<ul class="wp-block-list"><li>Item one</li><li>Item two</li></ul>'
    )
    md = html_to_markdown(html_in)
    assert "We rolled into **Austin** today." in md
    assert "## Next stop" in md
    assert "- Item one" in md
    assert "- Item two" in md


def test_html_to_markdown_preserves_unknown_embeds_as_raw():
    html_in = '<div class="wpgmza_map" id="wpgmza_map_1"><script>initMap();</script></div>'
    md = html_to_markdown(html_in)
    assert "<!--raw-->" in md
    assert "wpgmza_map" in md


def test_html_to_markdown_recognizes_strava_link():
    html_in = '<p class="wp-block-paragraph">https://www.strava.com/routes/555</p>'
    md = html_to_markdown(html_in)
    assert md.strip() == "{{strava:route:555}}"


def test_referenced_media_extraction():
    item = ContentItem(
        kind="post",
        path=Path("content/posts/x.md"),
        title="X",
        slug="x",
        featured_image="media/cover.jpg",
        body="![alt](media/inline.jpg)\n\nAnd a remote one: ![alt2](https://example.com/already-hosted.jpg)",
    )
    refs = item.referenced_media()
    assert refs == ["media/cover.jpg", "media/inline.jpg"]
