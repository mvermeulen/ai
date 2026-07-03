import re

import pytest

from wpsync import comments as comments_mod
from wpsync import media_library as media_lib
from wpsync import taxonomy as tax
from wpsync.config import Config
from wpsync.content import ContentItem
from wpsync.webapp import create_app


def _token(html: str) -> str:
    m = re.search(r'name="_token" value="([^"]+)"', html)
    assert m, "CSRF token field not found in page"
    return m.group(1)


@pytest.fixture
def app(project: Config):
    return create_app(project)


@pytest.fixture
def client(app):
    return app.test_client()


def write_post(project: Config, slug: str, **kwargs):
    defaults = dict(
        kind="post", path=project.content_dir / "posts" / f"{slug}.md",
        title=slug.title(), slug=slug, status="draft", body="Hello there.",
    )
    defaults.update(kwargs)
    item = ContentItem(**defaults)
    item.path.parent.mkdir(parents=True, exist_ok=True)
    item.path.write_text(item.dump(), encoding="utf-8")
    return item


def test_dashboard_renders_with_no_content(client):
    resp = client.get("/")
    assert resp.status_code == 200
    assert b"No local content yet" in resp.data


def test_dashboard_lists_local_posts(project: Config, client):
    write_post(project, "hello-world")
    resp = client.get("/")
    assert b"hello-world" in resp.data
    assert b"badge-new" in resp.data


def test_new_content_creates_file_and_redirects(project: Config, client):
    resp = client.get("/")
    token = _token(resp.text)
    resp = client.post("/new", data={"kind": "post", "title": "My New Post", "_token": token})
    assert resp.status_code == 302
    assert (project.content_dir / "posts" / "my-new-post.md").exists()


def test_new_content_rejects_missing_csrf_token(project: Config, client):
    resp = client.post("/new", data={"kind": "post", "title": "No Token"})
    assert resp.status_code == 403
    assert not (project.content_dir / "posts" / "no-token.md").exists()


def test_new_content_rejects_duplicate_slug(project: Config, client):
    write_post(project, "dup-post")
    resp = client.get("/")
    token = _token(resp.text)
    resp = client.post("/new", data={"kind": "post", "title": "Dup Post", "_token": token})
    assert resp.status_code == 400


def test_edit_page_shows_form_and_preview(project: Config, client):
    write_post(project, "hello-world", title="Hello World", body="Some **bold** text.")
    resp = client.get("/edit/hello-world")
    assert resp.status_code == 200
    assert b"Hello World" in resp.data
    assert b"<strong>bold</strong>" in resp.data


def test_edit_page_404_for_unknown_slug(client):
    resp = client.get("/edit/does-not-exist")
    assert resp.status_code == 404


def test_edit_page_save_persists_changes(project: Config, client):
    write_post(project, "hello-world", title="Original Title")
    resp = client.get("/edit/hello-world")
    token = _token(resp.text)
    resp = client.post(
        "/edit/hello-world",
        data={
            "_token": token,
            "title": "Updated Title",
            "status": "publish",
            "categories": "texas, cycling",
            "tags": "",
            "excerpt": "",
            "featured_image": "",
            "body": "New body text.",
        },
    )
    assert resp.status_code == 302
    reloaded = ContentItem.load(project.content_dir / "posts" / "hello-world.md")
    assert reloaded.title == "Updated Title"
    assert reloaded.status == "publish"
    assert reloaded.categories == ["texas", "cycling"]
    assert "New body text." in reloaded.body


def test_edit_page_save_rejects_missing_csrf_token(project: Config, client):
    write_post(project, "hello-world", title="Original Title")
    resp = client.post("/edit/hello-world", data={"title": "Hacked Title", "body": "x", "status": "draft"})
    assert resp.status_code == 403
    reloaded = ContentItem.load(project.content_dir / "posts" / "hello-world.md")
    assert reloaded.title == "Original Title"


def test_media_page_lists_pulled_items_and_edit_persists(project: Config, client):
    manifest = media_lib.load_manifest(project)
    manifest.items.append(media_lib.MediaItem(wp_id=1, filename="photo.jpg", source_url="https://x/photo.jpg"))
    manifest.save()

    resp = client.get("/media")
    assert b"photo.jpg" in resp.data
    token = _token(resp.text)

    resp = client.post("/media/1/edit", data={"_token": token, "alt_text": "A nice photo"})
    assert resp.status_code == 302
    updated = media_lib.load_manifest(project)
    assert updated.items[0].alt_text == "A nice photo"
    assert updated.items[0].local_edit is True


def test_media_edit_unknown_id_404s(project: Config, client):
    manifest = media_lib.load_manifest(project)
    manifest.items.append(media_lib.MediaItem(wp_id=1, filename="photo.jpg", source_url="https://x/photo.jpg"))
    manifest.save()

    resp = client.get("/media")
    token = _token(resp.text)
    resp = client.post("/media/999/edit", data={"_token": token, "alt_text": "x"})
    assert resp.status_code == 404


def test_comments_page_lists_and_edit_persists(project: Config, client):
    thread = comments_mod.CommentThread(
        post_slug="hello-world",
        path=comments_mod.CommentsStore(project).path_for("hello-world"),
        comments=[
            comments_mod.Comment(wp_id=1, post_id=10, post_slug="hello-world", author_name="Jane", content="Nice!")
        ],
    )
    thread.save()

    resp = client.get("/comments")
    assert b"Jane" in resp.data
    token = _token(resp.text)

    resp = client.post(
        "/comments/hello-world/1/edit",
        data={"_token": token, "desired_status": "approve", "reply": "Thanks!"},
    )
    assert resp.status_code == 302
    reloaded = comments_mod.CommentsStore(project).load("hello-world")
    c = reloaded.get(1)
    assert c.desired_status == "approve"
    assert c.reply == "Thanks!"


def test_taxonomy_page_add_draft_term(project: Config, client):
    resp = client.get("/taxonomy")
    token = _token(resp.text)
    resp = client.post(
        "/taxonomy/add", data={"_token": token, "kind": "category", "name": "New Mexico", "description": "desc"}
    )
    assert resp.status_code == 302
    manifest = tax.load_manifest(project)
    assert manifest.categories[0].name == "New Mexico"


def test_settings_page_save_persists(project: Config, client):
    resp = client.get("/settings")
    token = _token(resp.text)
    resp = client.post("/settings", data={"_token": token, "title": "New Title", "description": "New tagline"})
    assert resp.status_code == 302
    from wpsync import site_settings as settings_mod

    reloaded = settings_mod.load(project)
    assert reloaded.values["title"] == "New Title"


def test_insights_page_renders(project: Config, client):
    write_post(project, "hello-world")
    resp = client.get("/insights")
    assert resp.status_code == 200
    assert b"Posts: 1" in resp.data


def test_no_route_ever_imports_wp_client(project: Config):
    """The web GUI must never be able to reach WordPress -- guard against a
    future route accidentally importing the network client."""
    import wpsync.webapp as webapp_module

    assert "wp_client" not in webapp_module.__dict__
    assert "WPClient" not in dir(webapp_module)
