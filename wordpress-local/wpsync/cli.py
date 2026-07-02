"""Command-line interface for wpsync."""
from __future__ import annotations

import re
from pathlib import Path

import click

from .config import Config, ConfigError
from .content import ContentItem, VALID_STATUSES
from .preview import write_and_open
from .state import StateStore
from .sync import SyncEngine
from .wp_client import WPClient, WPConfig, WPError


def _load_config() -> Config:
    try:
        return Config.load()
    except ConfigError as exc:
        raise click.ClickException(str(exc))


def _build_engine(need_client: bool) -> SyncEngine:
    config = _load_config()
    state = StateStore(config.state_dir)
    client = None
    if need_client:
        config.require_credentials()
        client = WPClient(
            WPConfig(base_url=config.base_url, username=config.username, app_password=config.app_password)
        )
    return SyncEngine(config, state, client)


def _slugify(text: str) -> str:
    text = text.lower().strip()
    text = re.sub(r"[^a-z0-9]+", "-", text)
    return text.strip("-")


@click.group()
@click.version_option()
def main():
    """wpsync -- offline-first authoring and sync for a WordPress site."""


@main.command()
@click.option("--base-url", prompt="WordPress site base URL (e.g. https://mvermeulen.org/gone2look4america)")
@click.option("--username", prompt="WordPress username")
def init(base_url, username):
    """Create .wpsync/config.yml for this site."""
    path = Config.write_template(Path("."), base_url.rstrip("/"), username)
    click.echo(f"Wrote {path}")
    click.echo(
        "\nNext: create an Application Password under WordPress admin -> Users -> "
        "Profile -> Application Passwords, then set it in your shell before "
        "running pull/push/delete:\n\n"
        "    export WPSYNC_APP_PASSWORD='xxxx xxxx xxxx xxxx xxxx xxxx'\n\n"
        "Run `wpsync test-connection` afterwards to confirm it works."
    )


@main.command("new")
@click.argument("kind", type=click.Choice(["post", "page"]))
@click.argument("title")
@click.option("--slug", default=None, help="Defaults to a slugified title.")
@click.option("--status", "status_", default="draft", type=click.Choice(VALID_STATUSES))
def new_content(kind, title, slug, status_):
    """Scaffold a new local post or page as a Markdown file with frontmatter."""
    config = _load_config()
    slug = slug or _slugify(title)
    sub = "posts" if kind == "post" else "pages"
    path = config.content_dir / sub / f"{slug}.md"
    if path.exists():
        raise click.ClickException(f"{path} already exists.")
    item = ContentItem(
        kind=kind, path=path, title=title, slug=slug, status=status_,
        body="Start writing here.\n",
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(item.dump(), encoding="utf-8")
    click.echo(f"Created {path}")


@main.command()
@click.option("--force", is_flag=True, help="Overwrite local files even if they have unsynced edits.")
@click.option("--type", "post_type", type=click.Choice(["post", "page", "both"]), default="both")
def pull(force, post_type):
    """Fetch posts/pages from WordPress into local Markdown files.

    Skips (and reports) any local file that already has unsynced edits,
    rather than overwriting your offline work -- pass --force to pull
    anyway and discard the local edit.
    """
    types = ("post", "page") if post_type == "both" else (post_type,)
    engine = _build_engine(need_client=True)
    try:
        outcomes = engine.pull(post_types=types, force=force)
    except WPError as exc:
        raise click.ClickException(str(exc))
    if not outcomes:
        click.echo("Nothing to pull.")
        return
    for o in outcomes:
        click.echo(f"{o.action:>22}  {o.slug}")
    skipped = [o for o in outcomes if o.action == "skipped-local-changes"]
    if skipped:
        click.echo(
            f"\n{len(skipped)} file(s) skipped because they have unsynced local edits. "
            "Use `wpsync diff <slug>` to review, or `wpsync pull --force` to overwrite."
        )


@main.command()
def status():
    """Show new/modified/unchanged status for every local post & page. Offline, no network."""
    engine = _build_engine(need_client=False)
    result = engine.status()
    if not result:
        click.echo('No local content yet. Try: wpsync new post "My First Post"')
        return
    for slug, state in sorted(result.items()):
        click.echo(f"{state:>10}  {slug}")


@main.command()
@click.argument("slug")
def diff(slug):
    """Show a diff between the last-synced remote HTML and the current local render."""
    engine = _build_engine(need_client=False)
    try:
        text = engine.diff(slug)
    except ValueError as exc:
        raise click.ClickException(str(exc))
    click.echo(text or "No differences (or nothing has been synced yet).")


@main.command()
@click.argument("slugs", nargs=-1)
@click.option("--apply", "apply_", is_flag=True, help="Actually push changes. Without this, push always dry-runs.")
@click.option("--force", is_flag=True, help="Override a detected remote conflict and push anyway.")
def push(slugs, apply_, force):
    """Preview (default) or apply local changes to WordPress.

    Push always dry-runs unless --apply is given, so you can review exactly
    what would change -- including any conflicts with edits made directly
    on the live site -- before anything is written.
    """
    engine = _build_engine(need_client=True)
    dry_run = not apply_
    outcomes = engine.push(slugs=list(slugs) or None, dry_run=dry_run, force=force)

    noteworthy = [o for o in outcomes if o.action != "unchanged"]
    if not noteworthy:
        click.echo("Nothing to push -- everything matches the last sync.")
        return

    for o in noteworthy:
        click.echo(f"\n=== {o.slug}: {o.action} ===")
        if o.detail:
            click.echo(o.detail)
        if o.diff:
            click.echo(o.diff)

    if dry_run:
        click.echo("\nDry run only -- re-run with --apply to push these changes.")
        return

    counts = {a: sum(1 for o in outcomes if o.action == a) for a in ("create", "update", "conflict", "error")}
    click.echo(
        f"\n{counts['create']} created, {counts['update']} updated, "
        f"{counts['conflict']} conflicts, {counts['error']} errors."
    )


@main.command()
@click.argument("slug")
@click.option("--confirm-title", required=True, help="Must exactly match the post/page's current title on WordPress.")
@click.option("--permanent", is_flag=True, help="Bypass the trash and delete permanently.")
@click.option("--yes", is_flag=True, help="Skip the interactive yes/no prompt.")
def delete(slug, confirm_title, permanent, yes):
    """Delete a post/page on WordPress that wpsync previously synced.

    Always requires --confirm-title to exactly match the *current live*
    title, as a guard against deleting the wrong thing because of a stale
    local slug. Defaults to trashing (recoverable from the WP dashboard);
    pass --permanent to skip the trash entirely.
    """
    engine = _build_engine(need_client=True)
    verb = "PERMANENTLY DELETE" if permanent else "move to trash"
    if not yes and not click.confirm(f"About to {verb} '{slug}' on WordPress. Continue?"):
        click.echo("Aborted.")
        return
    try:
        outcome = engine.delete(slug, confirm_title, permanent=permanent)
    except (ValueError, WPError) as exc:
        raise click.ClickException(str(exc))
    click.echo(outcome.detail)


@main.command()
@click.argument("slug")
@click.option("--no-open", "no_open", is_flag=True, help="Write the preview file without opening a browser.")
def preview(slug, no_open):
    """Render a local post/page to HTML and open it for offline review."""
    engine = _build_engine(need_client=False)
    item = engine.find_local(slug)
    if item is None:
        raise click.ClickException(f"No local content found for slug '{slug}'")
    path = write_and_open(item, engine.offline_resolver(item), open_browser=not no_open)
    click.echo(f"Wrote {path}")


@main.command("test-connection")
def test_connection():
    """Verify the configured credentials can talk to WordPress."""
    engine = _build_engine(need_client=True)
    try:
        me = engine.client.whoami()
    except WPError as exc:
        raise click.ClickException(str(exc))
    caps = ", ".join(k for k, v in (me.get("capabilities") or {}).items() if v) or "none listed"
    click.echo(f"Connected as {me.get('name')} (capabilities: {caps})")


if __name__ == "__main__":
    main()
