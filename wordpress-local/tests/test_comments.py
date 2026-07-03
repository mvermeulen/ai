from pathlib import Path

import pytest

from tests.conftest import API_ROOT
from wpsync import comments as comments_mod
from wpsync.config import Config
from wpsync.wp_client import WPClient


def make_remote_comment(id=1, post=10, parent=0, author_name="Jane", status="hold", content="<p>Nice post!</p>"):
    return {
        "id": id,
        "post": post,
        "parent": parent,
        "author_name": author_name,
        "date": "2026-06-01T12:00:00",
        "status": status,
        "content": {"raw": content, "rendered": content},
    }


def test_pull_creates_thread_file(requests_mock, project: Config, client: WPClient):
    requests_mock.get(f"{API_ROOT}/comments", json=[make_remote_comment()])
    results = comments_mod.pull(project, client, {10: "hello-world"})

    assert results[0].new == 1
    thread = comments_mod.CommentsStore(project).load("hello-world")
    assert thread is not None
    assert thread.comments[0].author_name == "Jane"
    assert thread.comments[0].content == "Nice post!"
    assert thread.comments[0].status == "hold"


def test_pull_preserves_local_intent_fields(requests_mock, project: Config, client: WPClient):
    requests_mock.get(f"{API_ROOT}/comments", json=[make_remote_comment(status="hold")])
    comments_mod.pull(project, client, {10: "hello-world"})

    comments_mod.set_intent(project, "hello-world", 1, desired_status="approve", reply="Thanks for reading!")

    # A second pull (remote status still "hold") must not clobber the draft reply
    # or the pending status change.
    requests_mock.get(f"{API_ROOT}/comments", json=[make_remote_comment(status="hold")])
    comments_mod.pull(project, client, {10: "hello-world"})

    thread = comments_mod.CommentsStore(project).load("hello-world")
    c = thread.get(1)
    assert c.desired_status == "approve"
    assert c.reply == "Thanks for reading!"
    assert not c.reply_sent


def test_pull_clears_desired_status_once_remote_matches(requests_mock, project: Config, client: WPClient):
    requests_mock.get(f"{API_ROOT}/comments", json=[make_remote_comment(status="hold")])
    comments_mod.pull(project, client, {10: "hello-world"})
    comments_mod.set_intent(project, "hello-world", 1, desired_status="approve")

    # Remote now shows approve (e.g. pushed, or approved via dashboard directly).
    requests_mock.get(f"{API_ROOT}/comments", json=[make_remote_comment(status="approve")])
    comments_mod.pull(project, client, {10: "hello-world"})

    thread = comments_mod.CommentsStore(project).load("hello-world")
    assert thread.get(1).desired_status is None
    assert thread.get(1).status == "approve"


def test_set_intent_rejects_invalid_status(requests_mock, project: Config, client: WPClient):
    requests_mock.get(f"{API_ROOT}/comments", json=[make_remote_comment()])
    comments_mod.pull(project, client, {10: "hello-world"})
    with pytest.raises(ValueError):
        comments_mod.set_intent(project, "hello-world", 1, desired_status="not-a-real-status")


def test_set_intent_requires_existing_thread(project: Config):
    with pytest.raises(ValueError):
        comments_mod.set_intent(project, "nope", 1, desired_status="approve")


def test_plan_push_is_read_only(requests_mock, project: Config, client: WPClient):
    requests_mock.get(f"{API_ROOT}/comments", json=[make_remote_comment()])
    comments_mod.pull(project, client, {10: "hello-world"})
    comments_mod.set_intent(project, "hello-world", 1, desired_status="approve", reply="Thanks!")

    outcomes = comments_mod.plan_push(project)
    kinds = sorted(o.action for o in outcomes)
    assert kinds == ["would-reply", "would-set-status:approve"]
    assert not any(h.method == "POST" for h in requests_mock.request_history)


def test_apply_push_updates_status_and_posts_reply(requests_mock, project: Config, client: WPClient):
    requests_mock.get(f"{API_ROOT}/comments", json=[make_remote_comment()])
    comments_mod.pull(project, client, {10: "hello-world"})
    comments_mod.set_intent(project, "hello-world", 1, desired_status="approve", reply="Thanks for reading!")

    requests_mock.post(f"{API_ROOT}/comments/1", json={"id": 1, "status": "approve"})
    requests_mock.post(f"{API_ROOT}/comments", json={"id": 2, "parent": 1})

    outcomes = comments_mod.apply_push(project, client)
    actions = sorted(o.action for o in outcomes)
    assert actions == ["reply", "status"]

    thread = comments_mod.CommentsStore(project).load("hello-world")
    c = thread.get(1)
    assert c.status == "approve"
    assert c.desired_status is None
    assert c.reply_sent is True


def test_apply_push_records_error_without_raising(requests_mock, project: Config, client: WPClient):
    requests_mock.get(f"{API_ROOT}/comments", json=[make_remote_comment()])
    comments_mod.pull(project, client, {10: "hello-world"})
    comments_mod.set_intent(project, "hello-world", 1, desired_status="approve")

    requests_mock.post(f"{API_ROOT}/comments/1", status_code=500, text="boom")

    outcomes = comments_mod.apply_push(project, client)
    assert outcomes[0].action == "error"
    # Local state should remain "pending" since the push failed.
    thread = comments_mod.CommentsStore(project).load("hello-world")
    assert thread.get(1).desired_status == "approve"


def test_pending_lists_only_comments_needing_action(requests_mock, project: Config, client: WPClient):
    requests_mock.get(
        f"{API_ROOT}/comments",
        json=[make_remote_comment(id=1), make_remote_comment(id=2)],
    )
    comments_mod.pull(project, client, {10: "hello-world"})
    comments_mod.set_intent(project, "hello-world", 1, desired_status="spam")

    pending = comments_mod.CommentsStore(project).pending()
    assert [c.wp_id for c in pending] == [1]
