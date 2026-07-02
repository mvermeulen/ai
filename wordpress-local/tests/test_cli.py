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
