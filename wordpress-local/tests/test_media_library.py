import pytest

from tests.conftest import API_ROOT
from wpsync import media_library as ml
from wpsync.config import Config
from wpsync.wp_client import WPClient


def make_remote_media(id=1, filename="photo.jpg", alt_text="", caption="", title="photo"):
    return {
        "id": id,
        "source_url": f"https://example.test/uploads/{filename}",
        "mime_type": "image/jpeg",
        "alt_text": alt_text,
        "caption": {"raw": caption, "rendered": caption},
        "title": {"raw": title, "rendered": title},
    }


def test_pull_creates_manifest(requests_mock, project: Config, client: WPClient):
    requests_mock.get(f"{API_ROOT}/media", json=[make_remote_media()], headers={"X-WP-TotalPages": "1"})
    result = ml.pull(project, client)
    assert result.new == 1

    manifest = ml.load_manifest(project)
    assert manifest.items[0].filename == "photo.jpg"
    assert manifest.items[0].local_edit is False


def test_pull_preserves_local_edits(requests_mock, project: Config, client: WPClient):
    requests_mock.get(f"{API_ROOT}/media", json=[make_remote_media()], headers={"X-WP-TotalPages": "1"})
    ml.pull(project, client)
    ml.set_metadata(project, 1, alt_text="A skyline at dusk")

    requests_mock.get(f"{API_ROOT}/media", json=[make_remote_media()], headers={"X-WP-TotalPages": "1"})
    ml.pull(project, client)

    manifest = ml.load_manifest(project)
    assert manifest.items[0].alt_text == "A skyline at dusk"
    assert manifest.items[0].local_edit is True


def test_set_metadata_requires_pulled_item(project: Config):
    with pytest.raises(ValueError):
        ml.set_metadata(project, 999, alt_text="nope")


def test_plan_push_lists_only_edited_items(requests_mock, project: Config, client: WPClient):
    requests_mock.get(
        f"{API_ROOT}/media",
        json=[make_remote_media(id=1), make_remote_media(id=2)],
        headers={"X-WP-TotalPages": "1"},
    )
    ml.pull(project, client)
    ml.set_metadata(project, 1, alt_text="edited")

    outcomes = ml.plan_push(project)
    assert [o.wp_id for o in outcomes] == [1]
    assert not any(h.method == "POST" for h in requests_mock.request_history)


def test_apply_push_updates_remote_and_clears_local_edit_flag(requests_mock, project: Config, client: WPClient):
    requests_mock.get(f"{API_ROOT}/media", json=[make_remote_media(id=1)], headers={"X-WP-TotalPages": "1"})
    ml.pull(project, client)
    ml.set_metadata(project, 1, alt_text="edited alt")

    requests_mock.post(f"{API_ROOT}/media/1", json={"id": 1, "alt_text": "edited alt"})
    outcomes = ml.apply_push(project, client)
    assert outcomes[0].action == "update"

    manifest = ml.load_manifest(project)
    assert manifest.items[0].local_edit is False

    import json as _json

    body = _json.loads(requests_mock.last_request.body)
    assert body["alt_text"] == "edited alt"


def test_apply_push_records_error_and_keeps_local_edit_flag(requests_mock, project: Config, client: WPClient):
    requests_mock.get(f"{API_ROOT}/media", json=[make_remote_media(id=1)], headers={"X-WP-TotalPages": "1"})
    ml.pull(project, client)
    ml.set_metadata(project, 1, alt_text="edited alt")

    requests_mock.post(f"{API_ROOT}/media/1", status_code=500, text="boom")
    outcomes = ml.apply_push(project, client)
    assert outcomes[0].action == "error"

    manifest = ml.load_manifest(project)
    assert manifest.items[0].local_edit is True
