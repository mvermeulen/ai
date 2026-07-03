"""End-to-end tests (through the CLI, not just the underlying modules) for
the v3 safety hardening: host pinning, the large-batch cap, and that a
blocked safety check makes zero mutating network calls."""
import os

from click.testing import CliRunner

from wpsync.cli import main

BASE_URL = "https://example.test/site"
API_ROOT = f"{BASE_URL}/wp-json/wp/v2"


def _env():
    env = dict(os.environ)
    env["WPSYNC_APP_PASSWORD"] = "dummy-app-password"
    return env


def _init(runner):
    return runner.invoke(main, ["init", "--base-url", BASE_URL, "--username", "mike"], env=_env())


def test_push_apply_pins_host_on_first_run(requests_mock):
    runner = CliRunner()
    with runner.isolated_filesystem():
        _init(runner)
        runner.invoke(main, ["new", "post", "First Post"], env=_env())

        requests_mock.post(f"{API_ROOT}/posts", json={
            "id": 1, "slug": "first-post", "status": "draft", "type": "post",
            "modified_gmt": "2026-01-01T00:00:00",
            "title": {"raw": "First Post", "rendered": "First Post"},
            "content": {"raw": "", "rendered": "", "protected": False},
            "excerpt": {"raw": "", "rendered": ""},
        })
        result = runner.invoke(main, ["push", "--apply", "--yes"], env=_env())
        assert result.exit_code == 0, result.output
        assert os.path.exists(".wpsync/pinned_host.json")
        assert "example.test" in open(".wpsync/pinned_host.json").read()


def test_push_apply_blocked_after_host_change_no_network_call(requests_mock):
    runner = CliRunner()
    with runner.isolated_filesystem():
        _init(runner)
        runner.invoke(main, ["new", "post", "First Post"], env=_env())
        requests_mock.post(f"{API_ROOT}/posts", json={
            "id": 1, "slug": "first-post", "status": "draft", "type": "post",
            "modified_gmt": "2026-01-01T00:00:00",
            "title": {"raw": "First Post", "rendered": "First Post"},
            "content": {"raw": "", "rendered": "", "protected": False},
            "excerpt": {"raw": "", "rendered": ""},
        })
        result = runner.invoke(main, ["push", "--apply", "--yes"], env=_env())
        assert result.exit_code == 0, result.output

        # Simulate config.yml being pointed at a different site (a copy-paste
        # mistake, or running wpsync from the wrong checkout).
        config_text = open(".wpsync/config.yml").read()
        config_text = config_text.replace(BASE_URL, "https://a-totally-different-site.example/blog")
        open(".wpsync/config.yml", "w").write(config_text)

        runner.invoke(main, ["new", "post", "Second Post"], env=_env())
        calls_before = len(requests_mock.request_history)
        result = runner.invoke(main, ["push", "--apply", "--yes"], env=_env())
        assert result.exit_code != 0
        assert "pinned" in result.output.lower() or "confirm-host-change" in result.output.lower()
        # The safety check must fail before any mutating (or any) network call is made.
        assert len(requests_mock.request_history) == calls_before


def test_push_apply_blocked_by_batch_cap_no_network_call(requests_mock):
    runner = CliRunner()
    with runner.isolated_filesystem():
        _init(runner)
        for i in range(25):
            runner.invoke(main, ["new", "post", f"Post Number {i}"], env=_env())

        create_mock = requests_mock.post(f"{API_ROOT}/posts", json={
            "id": 1, "slug": "x", "status": "draft", "type": "post",
            "modified_gmt": "2026-01-01T00:00:00",
            "title": {"raw": "x", "rendered": "x"},
            "content": {"raw": "", "rendered": "", "protected": False},
            "excerpt": {"raw": "", "rendered": ""},
        })
        result = runner.invoke(main, ["push", "--apply", "--yes"], env=_env())
        assert result.exit_code != 0
        assert "cap" in result.output.lower() or "batch" in result.output.lower()
        assert not create_mock.called


def test_push_apply_succeeds_over_batch_cap_with_explicit_flag(requests_mock):
    runner = CliRunner()
    with runner.isolated_filesystem():
        _init(runner)
        for i in range(25):
            runner.invoke(main, ["new", "post", f"Post Number {i}"], env=_env())

        def make_response(request, context):
            import json as _json

            body = _json.loads(request.body)
            return {
                "id": abs(hash(body["slug"])) % 100000, "slug": body["slug"], "status": "draft", "type": "post",
                "modified_gmt": "2026-01-01T00:00:00",
                "title": {"raw": body["title"], "rendered": body["title"]},
                "content": {"raw": "", "rendered": "", "protected": False},
                "excerpt": {"raw": "", "rendered": ""},
            }

        requests_mock.post(f"{API_ROOT}/posts", json=make_response)
        result = runner.invoke(main, ["push", "--apply", "--yes", "--yes-large-batch"], env=_env())
        assert result.exit_code == 0, result.output
        assert "25 created" in result.output


def test_taxonomy_push_apply_respects_batch_cap(requests_mock):
    runner = CliRunner()
    with runner.isolated_filesystem():
        _init(runner)
        for i in range(25):
            runner.invoke(main, ["taxonomy", "add", "tag", f"tag-{i}"], env=_env())
        create_mock = requests_mock.post(f"{API_ROOT}/tags", json={"id": 1, "name": "x", "slug": "x"})
        result = runner.invoke(main, ["taxonomy", "push", "--apply", "--yes"], env=_env())
        assert result.exit_code != 0
        assert not create_mock.called
