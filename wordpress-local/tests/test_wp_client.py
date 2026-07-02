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
