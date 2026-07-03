import pytest

from tests.conftest import API_ROOT, make_remote
from wpsync.wp_client import WPAuthError, WPClient, WPNotFoundError


def test_pagination_walks_all_pages(requests_mock, client: WPClient):
    page1 = [make_remote(id=1, slug="post-1")]
    page2 = [make_remote(id=2, slug="post-2")]
    requests_mock.get(
        f"{API_ROOT}/posts",
        [
            {"json": page1, "headers": {"X-WP-TotalPages": "2"}},
            {"json": page2, "headers": {"X-WP-TotalPages": "2"}},
        ],
    )
    results = client.list_all("post")
    assert [r["slug"] for r in results] == ["post-1", "post-2"]


def test_401_raises_auth_error(requests_mock, client: WPClient):
    requests_mock.get(f"{API_ROOT}/users/me", status_code=401, json={"message": "bad creds"})
    with pytest.raises(WPAuthError):
        client.whoami()


def test_404_raises_not_found(requests_mock, client: WPClient):
    requests_mock.get(f"{API_ROOT}/posts/999", status_code=404, json={"message": "not found"})
    with pytest.raises(WPNotFoundError):
        client.get("post", 999)


def test_create_post(requests_mock, client: WPClient):
    requests_mock.post(f"{API_ROOT}/posts", json=make_remote(id=7, slug="new-post"))
    result = client.create("post", {"title": "New Post", "content": "<p>hi</p>", "status": "draft"})
    assert result["id"] == 7


def test_ensure_terms_creates_missing_and_reuses_existing(requests_mock, client: WPClient):
    requests_mock.get(
        f"{API_ROOT}/categories",
        [
            {"json": [{"id": 3, "name": "Cycling"}]},
            {"json": []},
        ],
    )
    requests_mock.post(f"{API_ROOT}/categories", json={"id": 9, "name": "Texas"})
    ids = client.ensure_terms("category", ["cycling", "Texas"])
    assert ids == [3, 9]


def test_upload_media_sends_content_disposition(requests_mock, client: WPClient):
    requests_mock.post(f"{API_ROOT}/media", json={"id": 5, "source_url": "https://example.test/img.jpg"})
    result = client.upload_media("img.jpg", b"fakebytes", "image/jpeg")
    assert result["source_url"] == "https://example.test/img.jpg"
    sent_headers = requests_mock.last_request.headers
    assert "attachment" in sent_headers["Content-Disposition"]


def test_list_media_all_paginates(requests_mock, client: WPClient):
    requests_mock.get(
        f"{API_ROOT}/media",
        [
            {"json": [{"id": 1}], "headers": {"X-WP-TotalPages": "2"}},
            {"json": [{"id": 2}], "headers": {"X-WP-TotalPages": "2"}},
        ],
    )
    result = client.list_media_all()
    assert [r["id"] for r in result] == [1, 2]


def test_update_media_posts_to_media_id(requests_mock, client: WPClient):
    requests_mock.post(f"{API_ROOT}/media/5", json={"id": 5, "alt_text": "new alt"})
    result = client.update_media(5, {"alt_text": "new alt"})
    assert result["alt_text"] == "new alt"


def test_list_comments_filters_by_post_and_status(requests_mock, client: WPClient):
    requests_mock.get(f"{API_ROOT}/comments", json=[{"id": 1, "post": 10, "status": "hold"}])
    result = client.list_comments(post_id=10, status="hold")
    assert result[0]["id"] == 1
    sent = requests_mock.last_request.qs
    assert sent["post"] == ["10"]
    assert sent["status"] == ["hold"]


def test_update_comment_status(requests_mock, client: WPClient):
    requests_mock.post(f"{API_ROOT}/comments/9", json={"id": 9, "status": "approve"})
    result = client.update_comment_status(9, "approve")
    assert result["status"] == "approve"


def test_create_comment_reply_sets_parent(requests_mock, client: WPClient):
    requests_mock.post(f"{API_ROOT}/comments", json={"id": 20, "parent": 9})
    result = client.create_comment_reply(post_id=10, parent_id=9, content="Thanks!")
    assert result["parent"] == 9
    import json as _json

    body = _json.loads(requests_mock.last_request.body)
    assert body == {"post": 10, "parent": 9, "content": "Thanks!"}


def test_list_terms_and_create_and_update(requests_mock, client: WPClient):
    requests_mock.get(f"{API_ROOT}/categories", json=[{"id": 3, "name": "Texas", "slug": "texas"}])
    assert client.list_terms("category")[0]["name"] == "Texas"

    requests_mock.post(f"{API_ROOT}/categories", json={"id": 4, "name": "Trains"})
    created = client.create_term("category", {"name": "Trains"})
    assert created["id"] == 4

    requests_mock.post(f"{API_ROOT}/categories/4", json={"id": 4, "description": "choo choo"})
    updated = client.update_term("category", 4, {"description": "choo choo"})
    assert updated["description"] == "choo choo"


def test_get_and_update_settings(requests_mock, client: WPClient):
    requests_mock.get(f"{API_ROOT}/settings", json={"title": "Old Title"})
    assert client.get_settings()["title"] == "Old Title"

    requests_mock.post(f"{API_ROOT}/settings", json={"title": "New Title"})
    updated = client.update_settings({"title": "New Title"})
    assert updated["title"] == "New Title"
