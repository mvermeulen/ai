from pathlib import Path

from wpsync.state import ItemState, StateStore, content_hash


def test_state_round_trip(tmp_path: Path):
    store = StateStore(tmp_path)
    store.set("hello-world", ItemState(wp_id=42, kind="post", content_hash="abc123", remote_modified="2026-01-01T00:00:00"))
    store.save()

    reloaded = StateStore(tmp_path)
    item = reloaded.get("hello-world")
    assert item is not None
    assert item.wp_id == 42
    assert item.kind == "post"
    assert item.content_hash == "abc123"
    assert item.remote_modified == "2026-01-01T00:00:00"


def test_state_remove(tmp_path: Path):
    store = StateStore(tmp_path)
    store.set("a", ItemState(wp_id=1, kind="post", content_hash="x"))
    store.remove("a")
    assert store.get("a") is None
    assert store.slugs() == []


def test_snapshot_read_write(tmp_path: Path):
    store = StateStore(tmp_path)
    assert store.read_snapshot("hello-world") is None
    store.save_snapshot("hello-world", "<p>Hi</p>")
    assert store.read_snapshot("hello-world") == "<p>Hi</p>"


def test_backup_creates_timestamped_file(tmp_path: Path):
    store = StateStore(tmp_path)
    path = store.backup("hello-world", "<p>Old content</p>")
    assert path.exists()
    assert path.read_text(encoding="utf-8") == "<p>Old content</p>"
    assert path.parent == store.backups_dir / "hello-world"


def test_content_hash_is_stable():
    assert content_hash("abc") == content_hash("abc")
    assert content_hash("abc") != content_hash("abd")
