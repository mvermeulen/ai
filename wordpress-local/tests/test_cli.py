import os

from click.testing import CliRunner

from wpsync.cli import main


def test_init_new_status_preview_offline_workflow():
    runner = CliRunner()
    with runner.isolated_filesystem():
        result = runner.invoke(
            main, ["init", "--base-url", "https://example.test/site", "--username", "mike"]
        )
        assert result.exit_code == 0, result.output
        assert "Wrote .wpsync/config.yml" in result.output

        result = runner.invoke(main, ["new", "post", "My First Post"])
        assert result.exit_code == 0, result.output
        assert "content/posts/my-first-post.md" in result.output

        result = runner.invoke(main, ["status"])
        assert result.exit_code == 0, result.output
        assert "new" in result.output
        assert "my-first-post" in result.output

        result = runner.invoke(main, ["preview", "my-first-post", "--no-open"])
        assert result.exit_code == 0, result.output
        assert "Wrote" in result.output

        # creating the same slug twice must fail rather than silently overwrite
        result = runner.invoke(main, ["new", "post", "My First Post"])
        assert result.exit_code != 0
        assert "already exists" in result.output


def test_push_requires_config():
    runner = CliRunner()
    with runner.isolated_filesystem():
        result = runner.invoke(main, ["push"])
        assert result.exit_code != 0
        assert "wpsync init" in result.output


def test_push_without_credentials_fails_cleanly_not_a_traceback():
    """Regression test: a config.yml that exists but has no application
    password must produce a clean CLI error, not an unhandled traceback --
    this is the exact "no credentials yet" scenario the tool is designed
    to support offline."""
    runner = CliRunner()
    with runner.isolated_filesystem():
        env = dict(os.environ)
        env.pop("WPSYNC_APP_PASSWORD", None)
        result = runner.invoke(
            main, ["init", "--base-url", "https://example.test/site", "--username", "mike"], env=env
        )
        assert result.exit_code == 0, result.output

        result = runner.invoke(main, ["push"], env=env)
        assert result.exit_code != 0
        assert result.exc_info[0] is SystemExit  # a clean click.ClickException, not an unhandled traceback
        assert "application password" in result.output.lower()


def test_insights_offline_and_empty_project():
    runner = CliRunner()
    with runner.isolated_filesystem():
        runner.invoke(main, ["init", "--base-url", "https://example.test/site", "--username", "mike"])
        result = runner.invoke(main, ["insights"])
        assert result.exit_code == 0, result.output
        assert "Posts: 0" in result.output


def test_schedule_sets_future_status():
    runner = CliRunner()
    with runner.isolated_filesystem():
        runner.invoke(main, ["init", "--base-url", "https://example.test/site", "--username", "mike"])
        runner.invoke(main, ["new", "post", "Later Post"])
        result = runner.invoke(main, ["schedule", "later-post", "2026-08-01T09:00:00"])
        assert result.exit_code == 0, result.output
        text = (open("content/posts/later-post.md").read())
        assert "status: future" in text
        assert "2026-08-01" in text


def test_schedule_rejects_unparseable_date():
    runner = CliRunner()
    with runner.isolated_filesystem():
        runner.invoke(main, ["init", "--base-url", "https://example.test/site", "--username", "mike"])
        runner.invoke(main, ["new", "post", "Later Post"])
        result = runner.invoke(main, ["schedule", "later-post", "not-a-date"])
        assert result.exit_code != 0


def test_comments_status_and_taxonomy_add_are_offline_and_safe():
    runner = CliRunner()
    with runner.isolated_filesystem():
        runner.invoke(main, ["init", "--base-url", "https://example.test/site", "--username", "mike"])
        result = runner.invoke(main, ["comments", "status"])
        assert result.exit_code == 0, result.output
        assert "No pending" in result.output

        result = runner.invoke(main, ["taxonomy", "add", "category", "New Mexico"])
        assert result.exit_code == 0, result.output
        assert "Added draft category" in result.output

        # duplicate add must fail, not silently create a second draft
        result = runner.invoke(main, ["taxonomy", "add", "category", "New Mexico"])
        assert result.exit_code != 0


def test_media_set_requires_prior_pull():
    runner = CliRunner()
    with runner.isolated_filesystem():
        runner.invoke(main, ["init", "--base-url", "https://example.test/site", "--username", "mike"])
        result = runner.invoke(main, ["media", "set", "1", "--alt", "test"])
        assert result.exit_code != 0
        assert "media pull" in result.output


def test_serve_without_flask_installed_fails_cleanly(monkeypatch):
    import builtins
    import sys

    real_import = builtins.__import__

    def fake_import(name, *args, **kwargs):
        if name == "flask" or name.startswith("flask."):
            raise ImportError("No module named 'flask'")
        return real_import(name, *args, **kwargs)

    # An earlier test may have already imported (and cached) wpsync.webapp,
    # which would short-circuit its `from flask import ...` line and never
    # exercise the patched __import__ below. Evict it so the import is
    # attempted fresh.
    for mod_name in [n for n in sys.modules if n == "wpsync.webapp" or n.startswith("wpsync.webapp.")]:
        monkeypatch.delitem(sys.modules, mod_name, raising=False)

    runner = CliRunner()
    with runner.isolated_filesystem():
        runner.invoke(main, ["init", "--base-url", "https://example.test/site", "--username", "mike"])
        monkeypatch.setattr(builtins, "__import__", fake_import)
        result = runner.invoke(main, ["serve"])
        assert result.exit_code != 0
        assert "gui" in result.output.lower()
