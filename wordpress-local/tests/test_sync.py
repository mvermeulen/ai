from pathlib import Path

import pytest

from tests.conftest import API_ROOT, make_remote
from wpsync.config import Config
from wpsync.content import ContentItem
from wpsync.state import ItemState, StateStore
from wpsync.sync import SyncEngine
from wpsync.wp_client import WPClient


def write_local_post(project: Config, slug: str, title: str, body: str, **kwargs) -> Path:
    item = ContentItem(kind="post", path=Path(), title=title, slug=slug, body=body, **kwargs)
    path = project.content_dir / "posts" / f"{slug}.md"
    path.write_text(item.dump(), encoding="utf-8")
    return path


def engine_for(project, state, client=None):
    return SyncEngine(project, state, client)


# --------------------------------------------------------------------- pull


def test_pull_creates_local_files(requests_mock, project: Config, state: StateStore, client: WPClient):
    requests_mock.get(
        f"{API_ROOT}/posts",
        json=[make_remote(id=1, slug="hello-world", title="Hello World")],
        headers={"X-WP-TotalPages": "1"},
    )
    requests_mock.get(f"{API_ROOT}/pages", json=[], headers={"X-WP-TotalPages": "1"})

    engine = engine_for(project, state, client)
    outcomes = engine.pull()

    assert [o.action for o in outcomes] == ["new"]
    path = project.content_dir / "posts" / "hello-world.md"
    assert path.exists()
    assert "Hello World" in path.read_text(encoding="utf-8")
    assert "Hi there." in path.read_text(encoding="utf-8")

    st = state.get("hello-world")
    assert st is not None
    assert st.wp_id == 1
    assert st.remote_modified == "2026-01-01T00:00:00"


def test_pull_skips_local_edits_unless_forced(requests_mock, project: Config, state: StateStore, client: WPClient):
    requests_mock.get(f"{API_ROOT}/posts", json=[make_remote(id=1, slug="hello-world")], headers={"X-WP-TotalPages": "1"})
    requests_mock.get(f"{API_ROOT}/pages", json=[], headers={"X-WP-TotalPages": "1"})
    engine = engine_for(project, state, client)
    engine.pull()

    path = project.content_dir / "posts" / "hello-world.md"
    original = path.read_text(encoding="utf-8")
    path.write_text(original.replace("Hi there.", "Hi there, MY OFFLINE EDIT."), encoding="utf-8")

    outcomes = engine.pull()
    assert outcomes[0].action == "skipped-local-changes"
    assert "MY OFFLINE EDIT" in path.read_text(encoding="utf-8")

    outcomes = engine.pull(force=True)
    assert outcomes[0].action == "updated"
    assert "MY OFFLINE EDIT" not in path.read_text(encoding="utf-8")


# ------------------------------------------------------------------- status


def test_status_unchanged_immediately_after_pull(requests_mock, project: Config, state: StateStore, client: WPClient):
    """Regression test: our own renderer's HTML never byte-matches WordPress's
    original markup exactly, so status must compare against our own canonical
    hash (recorded at pull time), not against the raw remote snapshot -- else
    every freshly pulled post would show as 'modified' despite zero edits."""
    requests_mock.get(f"{API_ROOT}/posts", json=[make_remote(id=1, slug="hello-world")], headers={"X-WP-TotalPages": "1"})
    requests_mock.get(f"{API_ROOT}/pages", json=[], headers={"X-WP-TotalPages": "1"})
    engine = engine_for(project, state, client)
    engine.pull()

    assert engine.status() == {"hello-world": "unchanged"}


def test_status_detects_new_and_modified(project: Config, state: StateStore):
    engine = engine_for(project, state)
    write_local_post(project, "brand-new", "Brand New", "Some text.")
    assert engine.status() == {"brand-new": "new"}

    state.set("brand-new", ItemState(wp_id=1, kind="post", content_hash="deadbeef"))
    assert engine.status() == {"brand-new": "modified"}


# --------------------------------------------------------------------- push


def test_push_dry_run_makes_no_network_mutations(requests_mock, project: Config, state: StateStore, client: WPClient):
    write_local_post(project, "new-post", "New Post", "Hello from offline.")
    engine = engine_for(project, state, client)
    outcomes = engine.push(dry_run=True)

    assert outcomes[0].action == "would-create"
    assert not any(h.method == "POST" for h in requests_mock.request_history)


def test_push_creates_new_post(requests_mock, project: Config, state: StateStore, client: WPClient):
    write_local_post(project, "new-post", "New Post", "Hello from offline.")
    requests_mock.post(f"{API_ROOT}/posts", json=make_remote(id=10, slug="new-post", title="New Post"))

    engine = engine_for(project, state, client)
    outcomes = engine.push(dry_run=False)

    assert outcomes[0].action == "create"
    st = state.get("new-post")
    assert st.wp_id == 10


def test_push_update_then_unchanged_on_second_run(requests_mock, project: Config, state: StateStore, client: WPClient):
    write_local_post(project, "hello-world", "Hello World", "Original body.")
    engine = engine_for(project, state, client)

    # First push: no prior state, so this is a create.
    requests_mock.post(f"{API_ROOT}/posts", json=make_remote(id=1, slug="hello-world", content="<p>irrelevant</p>"))
    outcomes = engine.push(dry_run=False)
    assert outcomes[0].action == "create"

    # Edit locally, then push again: should be a conflict-free update.
    path = project.content_dir / "posts" / "hello-world.md"
    path.write_text(path.read_text(encoding="utf-8").replace("Original body.", "Edited body."), encoding="utf-8")

    requests_mock.get(
        f"{API_ROOT}/posts/1",
        json=make_remote(id=1, slug="hello-world", modified_gmt=state.get("hello-world").remote_modified),
    )
    requests_mock.post(f"{API_ROOT}/posts/1", json=make_remote(id=1, slug="hello-world", modified_gmt="2026-03-01T00:00:00"))
    outcomes = engine.push(dry_run=False)
    assert outcomes[0].action == "update"

    # Push again with no further local edits: nothing to do, no extra network calls.
    update_calls_before = sum(1 for h in requests_mock.request_history if h.method == "POST" and h.path.endswith("/posts/1"))
    requests_mock.get(f"{API_ROOT}/posts/1", json=make_remote(id=1, slug="hello-world", modified_gmt="2026-03-01T00:00:00"))
    outcomes = engine.push(dry_run=False)
    assert outcomes[0].action == "unchanged"
    update_calls_after = sum(1 for h in requests_mock.request_history if h.method == "POST" and h.path.endswith("/posts/1"))
    assert update_calls_after == update_calls_before


def test_push_conflict_is_blocked_without_force(requests_mock, project: Config, state: StateStore, client: WPClient):
    write_local_post(project, "hello-world", "Hello World", "Original body.")
    state.set("hello-world", ItemState(wp_id=1, kind="post", content_hash="whatever", remote_modified="2026-01-01T00:00:00"))
    state.save()

    requests_mock.get(f"{API_ROOT}/posts/1", json=make_remote(id=1, slug="hello-world", modified_gmt="2026-05-05T00:00:00"))
    update_mock = requests_mock.post(f"{API_ROOT}/posts/1", json=make_remote(id=1, slug="hello-world"))

    engine = engine_for(project, state, client)
    outcomes = engine.push(dry_run=False)
    assert outcomes[0].action == "conflict"
    assert not update_mock.called

    outcomes = engine.push(dry_run=False, force=True)
    assert outcomes[0].action == "update"
    assert update_mock.called


# ------------------------------------------------------------------- delete


def test_delete_requires_exact_title_match(requests_mock, project: Config, state: StateStore, client: WPClient):
    state.set("hello-world", ItemState(wp_id=1, kind="post", content_hash="x", remote_modified="2026-01-01T00:00:00"))
    requests_mock.get(f"{API_ROOT}/posts/1", json=make_remote(id=1, slug="hello-world", title="The Real Title"))
    delete_mock = requests_mock.delete(f"{API_ROOT}/posts/1", json={"deleted": True})

    engine = engine_for(project, state, client)
    with pytest.raises(ValueError):
        engine.delete("hello-world", confirm_title="Wrong Title")
    assert not delete_mock.called

    outcome = engine.delete("hello-world", confirm_title="The Real Title")
    assert delete_mock.called
    assert state.get("hello-world") is None
    assert outcome.action == "delete"
    # a backup of the live content is kept before deleting
    assert (project.state_dir / "backups" / "hello-world").exists()


# --------------------------------------------------------------------- media


def test_media_upload_is_deduplicated_across_pushes(requests_mock, project: Config, state: StateStore, client: WPClient):
    media_path = project.content_dir / "media" / "photo.jpg"
    media_path.write_bytes(b"not-a-real-jpeg-but-good-enough-for-hashing")

    write_local_post(project, "with-photo", "With Photo", "![alt](media/photo.jpg)")
    media_mock = requests_mock.post(
        f"{API_ROOT}/media", json={"id": 50, "source_url": "https://example.test/uploads/photo.jpg"}
    )
    requests_mock.post(f"{API_ROOT}/posts", json=make_remote(id=20, slug="with-photo"))
    requests_mock.get(f"{API_ROOT}/posts/20", json=make_remote(id=20, slug="with-photo"))

    engine = engine_for(project, state, client)
    engine.push(dry_run=False)
    assert media_mock.call_count == 1

    # touch the post (no media change) and push again -- media must not re-upload
    path = project.content_dir / "posts" / "with-photo.md"
    path.write_text(path.read_text(encoding="utf-8") + "\n\nOne more line.\n", encoding="utf-8")
    requests_mock.post(f"{API_ROOT}/posts/20", json=make_remote(id=20, slug="with-photo", modified_gmt="2026-04-01T00:00:00"))
    engine.push(dry_run=False)
    assert media_mock.call_count == 1
