"""Command-line interface for wpsync."""
from __future__ import annotations

import re
from datetime import datetime
from pathlib import Path

import click

from . import comments as comments_mod
from . import insights as insights_mod
from . import media_library as media_lib
from . import safety
from . import site_settings as settings_mod
from . import taxonomy as tax
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


def _build_engine(need_client: bool, confirm_host_change: bool = False) -> SyncEngine:
    """Build a SyncEngine. When a WordPress client is needed, the host-pin
    safety check runs here, before the engine can make *any* network call
    (including a read-only `pull` or a dry-run `push`'s conflict-detection
    GETs) -- checking it later, only around --apply, would be too late."""
    config = _load_config()
    state = StateStore(config.state_dir)
    client = None
    if need_client:
        try:
            config.require_credentials()
        except ConfigError as exc:
            raise click.ClickException(str(exc))
        try:
            safety.check_host(config, confirm_host_change)
        except safety.SafetyError as exc:
            raise click.ClickException(str(exc))
        client = WPClient(
            WPConfig(base_url=config.base_url, username=config.username, app_password=config.app_password)
        )
    return SyncEngine(config, state, client)


def _post_id_to_slug(state: StateStore) -> dict:
    return {state.get(s).wp_id: s for s in state.slugs()}


def _slugify(text: str) -> str:
    text = text.lower().strip()
    text = re.sub(r"[^a-z0-9]+", "-", text)
    return text.strip("-")


def _apply_gate(*, yes: bool, yes_large_batch: bool = None, batch_count=None, summary: str):
    """Shared safety gate immediately before a plan is actually applied: an
    optional batch cap, then a typed confirmation. The host-pin check has
    already happened earlier, at engine-build time (see `_build_engine`) --
    by the time a plan exists, at least one network call already went out.
    Raises click.ClickException on a hard safety failure; returns False if
    the operator declines the confirmation prompt (caller should abort
    quietly)."""
    try:
        if batch_count is not None:
            safety.check_batch_size(batch_count, bool(yes_large_batch))
    except safety.SafetyError as exc:
        raise click.ClickException(str(exc))
    return safety.confirm(summary, assume_yes=yes)


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
@click.argument("slug")
@click.argument("when")
def schedule(slug, when):
    """Set a local post/page to publish automatically at a future date/time.

    WHEN must be an ISO 8601 date/time (e.g. 2026-08-01T09:00:00). Sets
    status to 'future' and the post's date -- WordPress itself performs the
    scheduled publish once this has been pushed, the same as scheduling a
    post from the dashboard.
    """
    engine = _build_engine(need_client=False)
    item = engine.find_local(slug)
    if item is None:
        raise click.ClickException(f"No local content found for slug '{slug}'")
    try:
        parsed = datetime.fromisoformat(when)
    except ValueError as exc:
        raise click.ClickException(f"Could not parse '{when}' as an ISO 8601 date/time: {exc}")
    item.status = "future"
    item.date = parsed.isoformat()
    item.path.write_text(item.dump(), encoding="utf-8")
    click.echo(f"Scheduled '{slug}' for {item.date} (status=future). Run `wpsync push --apply` when ready.")


@main.command()
@click.option("--force", is_flag=True, help="Overwrite local files even if they have unsynced edits.")
@click.option("--type", "post_type", type=click.Choice(["post", "page", "both"]), default="both")
@click.option(
    "--confirm-host-change", is_flag=True,
    help="Acknowledge that base_url's host has changed since it was last pinned for this project.",
)
def pull(force, post_type, confirm_host_change):
    """Fetch posts/pages from WordPress into local Markdown files.

    Skips (and reports) any local file that already has unsynced edits,
    rather than overwriting your offline work -- pass --force to pull
    anyway and discard the local edit.
    """
    types = ("post", "page") if post_type == "both" else (post_type,)
    engine = _build_engine(need_client=True, confirm_host_change=confirm_host_change)
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
@click.option("--yes", is_flag=True, help="Skip the interactive confirmation prompt when applying.")
@click.option("--yes-large-batch", is_flag=True, help="Allow applying more than the safety cap of changes at once.")
@click.option(
    "--confirm-host-change", is_flag=True,
    help="Acknowledge that base_url's host has changed since it was last pinned for this project.",
)
def push(slugs, apply_, force, yes, yes_large_batch, confirm_host_change):
    """Preview (default) or apply local changes to WordPress.

    Push always dry-runs first -- and shows that plan -- even with --apply,
    so you can review exactly what would change (including any conflicts
    with edits made directly on the live site) before anything is written.
    Every network call this makes -- including the read-only conflict
    checks behind this dry-run plan -- is gated on a pinned-host check.
    Applying is additionally gated on a cap on how many items can change
    in one go, and a typed confirmation.
    """
    engine = _build_engine(need_client=True, confirm_host_change=confirm_host_change)
    outcomes = engine.push(slugs=list(slugs) or None, dry_run=True, force=force)

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

    if not apply_:
        click.echo("\nDry run only -- re-run with --apply to push these changes.")
        return

    changing = [o for o in noteworthy if o.action in ("would-create", "would-update")]
    proceed = _apply_gate(
        yes=yes,
        yes_large_batch=yes_large_batch,
        batch_count=len(changing),
        summary=f"About to apply {len(changing)} change(s) to {engine.config.base_url}.",
    )
    if not proceed:
        click.echo("Aborted -- no changes were made.")
        return

    outcomes = engine.push(slugs=list(slugs) or None, dry_run=False, force=force)
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
@click.option(
    "--confirm-host-change", is_flag=True,
    help="Acknowledge that base_url's host has changed since it was last pinned for this project.",
)
def delete(slug, confirm_title, permanent, yes, confirm_host_change):
    """Delete a post/page on WordPress that wpsync previously synced.

    Always requires --confirm-title to exactly match the *current live*
    title, as a guard against deleting the wrong thing because of a stale
    local slug. Defaults to trashing (recoverable from the WP dashboard);
    pass --permanent to skip the trash entirely.
    """
    engine = _build_engine(need_client=True, confirm_host_change=confirm_host_change)
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
    path = write_and_open(
        item, engine.offline_resolver(item), gpx_root=engine.config.content_dir, open_browser=not no_open
    )
    click.echo(f"Wrote {path}")


@main.command("test-connection")
@click.option(
    "--confirm-host-change", is_flag=True,
    help="Acknowledge that base_url's host has changed since it was last pinned for this project.",
)
def test_connection(confirm_host_change):
    """Verify the configured credentials can talk to WordPress."""
    engine = _build_engine(need_client=True, confirm_host_change=confirm_host_change)
    try:
        me = engine.client.whoami()
    except WPError as exc:
        raise click.ClickException(str(exc))
    caps = ", ".join(k for k, v in (me.get("capabilities") or {}).items() if v) or "none listed"
    click.echo(f"Connected as {me.get('name')} (capabilities: {caps})")


@main.command()
def insights():
    """Offline content analytics: word counts, publishing cadence, taxonomy
    coverage, orphaned media, and accessibility warnings. No network."""
    engine = _build_engine(need_client=False)
    report = insights_mod.build_report(engine.config, engine.local_items())
    click.echo(insights_mod.format_report(report))


@main.command()
@click.option("--host", default="127.0.0.1", help="Bind address. Non-localhost exposes local editing to your network.")
@click.option("--port", default=8642, type=int)
def serve(host, port):
    """Launch the local-only web GUI for editing & previewing content.

    The GUI only ever reads/writes files inside this project -- it never
    talks to WordPress. Anything that would sync to the live site is
    surfaced as a CLI command to run yourself, the same deliberate,
    dry-run-first gate as everywhere else in wpsync.
    """
    engine = _build_engine(need_client=False)
    try:
        from .webapp import create_app
    except ImportError as exc:
        raise click.ClickException(
            'The web GUI requires Flask. Install it with: pip install -e ".[gui]"\n'
            f"(import error: {exc})"
        )
    if host not in ("127.0.0.1", "localhost"):
        click.echo(
            "WARNING: binding to a non-localhost address exposes local content "
            "editing to your network. Only do this on a trusted network.",
            err=True,
        )
    app = create_app(engine.config)
    click.echo(f"Serving wpsync editor at http://{host}:{port} (Ctrl+C to stop)")
    app.run(host=host, port=port, debug=False)


# --------------------------------------------------------------------- comments


@main.group()
def comments():
    """Offline comment moderation and replies."""


@comments.command("pull")
@click.option("--confirm-host-change", is_flag=True)
def comments_pull(confirm_host_change):
    """Refresh the local comment mirror for every synced post/page."""
    engine = _build_engine(need_client=True, confirm_host_change=confirm_host_change)
    mapping = _post_id_to_slug(engine.state)
    if not mapping:
        click.echo("No synced posts/pages yet -- run `wpsync pull` first.")
        return
    try:
        results = comments_mod.pull(engine.config, engine.client, mapping)
    except WPError as exc:
        raise click.ClickException(str(exc))
    if not results:
        click.echo("No comments found.")
        return
    for r in results:
        click.echo(f"{r.post_slug}: {r.new} new, {r.updated} updated")


@comments.command("status")
def comments_status():
    """List comments with a pending offline moderation decision or reply. Offline."""
    engine = _build_engine(need_client=False)
    pending = comments_mod.CommentsStore(engine.config).pending()
    if not pending:
        click.echo("No pending comment actions.")
        return
    for c in pending:
        bits = []
        if c.desired_status and c.desired_status != c.status:
            bits.append(f"set status -> {c.desired_status}")
        if c.reply and not c.reply_sent:
            bits.append("reply drafted")
        click.echo(f"{c.post_slug}  #{c.wp_id}  {c.author_name}: {', '.join(bits)}")


@comments.command("set")
@click.argument("slug")
@click.argument("comment_id", type=int)
@click.option("--status", "status_", type=click.Choice(comments_mod.VALID_STATUSES), default=None)
@click.option("--reply", default=None, help="Draft text for a reply to this comment.")
def comments_set(slug, comment_id, status_, reply):
    """Offline: record a moderation decision and/or draft a reply for one comment."""
    engine = _build_engine(need_client=False)
    if status_ is None and reply is None:
        raise click.ClickException("Provide --status and/or --reply.")
    try:
        comment = comments_mod.set_intent(engine.config, slug, comment_id, desired_status=status_, reply=reply)
    except ValueError as exc:
        raise click.ClickException(str(exc))
    click.echo(f"Updated local intent for comment #{comment.wp_id} on '{slug}'.")


@comments.command("push")
@click.option("--apply", "apply_", is_flag=True)
@click.option("--yes", is_flag=True)
@click.option("--yes-large-batch", is_flag=True)
@click.option("--confirm-host-change", is_flag=True)
def comments_push(apply_, yes, yes_large_batch, confirm_host_change):
    """Preview (default) or apply pending comment moderation/replies."""
    engine = _build_engine(need_client=True, confirm_host_change=confirm_host_change)
    plan = comments_mod.plan_push(engine.config)
    if not plan:
        click.echo("Nothing to push -- no pending comment actions.")
        return
    for o in plan:
        click.echo(f"{o.post_slug}  #{o.wp_id}  {o.action}")
    if not apply_:
        click.echo("\nDry run only -- re-run with --apply to push these changes.")
        return
    proceed = _apply_gate(
        yes=yes,
        yes_large_batch=yes_large_batch,
        batch_count=len(plan),
        summary=f"About to apply {len(plan)} comment action(s).",
    )
    if not proceed:
        click.echo("Aborted -- no changes were made.")
        return
    outcomes = comments_mod.apply_push(engine.config, engine.client)
    for o in outcomes:
        click.echo(f"{o.post_slug}  #{o.wp_id}  {o.action}  {o.detail}")


# ------------------------------------------------------------------------ media


@main.group()
def media():
    """Offline media library metadata (alt text, captions, titles)."""


@media.command("pull")
@click.option("--confirm-host-change", is_flag=True)
def media_pull(confirm_host_change):
    """Refresh the local media library manifest from WordPress."""
    engine = _build_engine(need_client=True, confirm_host_change=confirm_host_change)
    try:
        result = media_lib.pull(engine.config, engine.client)
    except WPError as exc:
        raise click.ClickException(str(exc))
    click.echo(f"{result.new} new, {result.refreshed} refreshed.")


@media.command("status")
def media_status():
    """List media items with pending offline metadata edits. Offline."""
    engine = _build_engine(need_client=False)
    outcomes = media_lib.plan_push(engine.config)
    if not outcomes:
        click.echo("No pending media edits.")
        return
    for o in outcomes:
        click.echo(f"{o.wp_id}  {o.filename}  {o.detail}")


@media.command("set")
@click.argument("wp_id", type=int)
@click.option("--alt", default=None, help="New alt text.")
@click.option("--caption", default=None, help="New caption.")
@click.option("--title", default=None, help="New title.")
def media_set(wp_id, alt, caption, title):
    """Offline: edit a media item's alt text/caption/title."""
    engine = _build_engine(need_client=False)
    if alt is None and caption is None and title is None:
        raise click.ClickException("Provide at least one of --alt, --caption, --title.")
    try:
        item = media_lib.set_metadata(engine.config, wp_id, alt_text=alt, caption=caption, title=title)
    except ValueError as exc:
        raise click.ClickException(str(exc))
    click.echo(f"Updated local metadata for media #{item.wp_id} ({item.filename}).")


@media.command("push")
@click.option("--apply", "apply_", is_flag=True)
@click.option("--yes", is_flag=True)
@click.option("--yes-large-batch", is_flag=True)
@click.option("--confirm-host-change", is_flag=True)
def media_push(apply_, yes, yes_large_batch, confirm_host_change):
    """Preview (default) or apply pending media metadata edits."""
    engine = _build_engine(need_client=True, confirm_host_change=confirm_host_change)
    plan = media_lib.plan_push(engine.config)
    if not plan:
        click.echo("Nothing to push -- no pending media edits.")
        return
    for o in plan:
        click.echo(f"{o.wp_id}  {o.filename}  {o.detail}")
    if not apply_:
        click.echo("\nDry run only -- re-run with --apply to push these changes.")
        return
    proceed = _apply_gate(
        yes=yes,
        yes_large_batch=yes_large_batch,
        batch_count=len(plan),
        summary=f"About to update metadata for {len(plan)} media item(s).",
    )
    if not proceed:
        click.echo("Aborted -- no changes were made.")
        return
    outcomes = media_lib.apply_push(engine.config, engine.client)
    for o in outcomes:
        click.echo(f"{o.wp_id}  {o.filename}  {o.action}  {o.detail}")


# --------------------------------------------------------------------- taxonomy


@main.group()
def taxonomy():
    """Offline category/tag curation."""


@taxonomy.command("pull")
@click.option("--confirm-host-change", is_flag=True)
def taxonomy_pull(confirm_host_change):
    """Refresh the local taxonomy manifest from WordPress."""
    engine = _build_engine(need_client=True, confirm_host_change=confirm_host_change)
    try:
        manifest = tax.pull(engine.config, engine.client)
    except WPError as exc:
        raise click.ClickException(str(exc))
    click.echo(f"{len(manifest.categories)} categories, {len(manifest.tags)} tags.")


@taxonomy.command("add")
@click.argument("kind", type=click.Choice(list(tax.TAXONOMIES)))
@click.argument("name")
@click.option("--description", default="")
@click.option("--parent", default=None, help="Parent category name (categories only).")
def taxonomy_add(kind, name, description, parent):
    """Offline: draft a new category or tag, created on the next taxonomy push."""
    engine = _build_engine(need_client=False)
    try:
        term = tax.add_term(engine.config, kind, name, description=description, parent=parent)
    except ValueError as exc:
        raise click.ClickException(str(exc))
    click.echo(f"Added draft {kind} '{term.name}' (not yet synced).")


@taxonomy.command("describe")
@click.argument("kind", type=click.Choice(list(tax.TAXONOMIES)))
@click.argument("name")
@click.argument("description")
def taxonomy_describe(kind, name, description):
    """Offline: edit an existing category/tag's description."""
    engine = _build_engine(need_client=False)
    try:
        term = tax.set_description(engine.config, kind, name, description)
    except ValueError as exc:
        raise click.ClickException(str(exc))
    click.echo(f"Updated description for {kind} '{term.name}'.")


@taxonomy.command("push")
@click.option("--apply", "apply_", is_flag=True)
@click.option("--yes", is_flag=True)
@click.option("--yes-large-batch", is_flag=True)
@click.option("--confirm-host-change", is_flag=True)
def taxonomy_push(apply_, yes, yes_large_batch, confirm_host_change):
    """Preview (default) or apply newly-drafted categories/tags."""
    engine = _build_engine(need_client=True, confirm_host_change=confirm_host_change)
    plan = tax.plan_push(engine.config)
    if not plan:
        click.echo("Nothing to push -- no new local terms.")
        return
    for o in plan:
        click.echo(f"{o.taxonomy}  {o.name}  {o.action}")
    if not apply_:
        click.echo("\nDry run only -- re-run with --apply to push these changes.")
        return
    proceed = _apply_gate(
        yes=yes,
        yes_large_batch=yes_large_batch,
        batch_count=len(plan),
        summary=f"About to create {len(plan)} term(s).",
    )
    if not proceed:
        click.echo("Aborted -- no changes were made.")
        return
    outcomes = tax.apply_push(engine.config, engine.client)
    for o in outcomes:
        click.echo(f"{o.taxonomy}  {o.name}  {o.action}  {o.detail}")


# --------------------------------------------------------------------- settings


@main.group("settings")
def settings_group():
    """Offline site title/tagline/timezone settings."""


@settings_group.command("pull")
@click.option("--confirm-host-change", is_flag=True)
def settings_pull(confirm_host_change):
    """Fetch site settings from WordPress into content/site.yaml."""
    engine = _build_engine(need_client=True, confirm_host_change=confirm_host_change)
    try:
        s = settings_mod.pull(engine.config, engine.client)
    except WPError as exc:
        raise click.ClickException(str(exc))
    click.echo(f"Pulled {len(s.values)} setting(s) into {settings_mod.manifest_path(engine.config)}")


@settings_group.command("status")
def settings_status():
    """Show whether content/site.yaml has unsynced local edits. Offline."""
    engine = _build_engine(need_client=False)
    click.echo(settings_mod.status(engine.config))


@settings_group.command("push")
@click.option("--apply", "apply_", is_flag=True)
@click.option("--yes", is_flag=True)
@click.option("--confirm-host-change", is_flag=True)
def settings_push(apply_, yes, confirm_host_change):
    """Preview (default) or apply local site settings changes."""
    engine = _build_engine(need_client=True, confirm_host_change=confirm_host_change)
    plan = settings_mod.plan_push(engine.config)
    if plan.action == "unchanged":
        click.echo("Nothing to push -- settings match the last sync.")
        return
    click.echo(f"Would update: {', '.join(plan.changed_fields or [])}")
    if not apply_:
        click.echo("\nDry run only -- re-run with --apply to push these changes.")
        return
    proceed = _apply_gate(
        yes=yes,
        summary=f"About to update site settings: {', '.join(plan.changed_fields or [])}.",
    )
    if not proceed:
        click.echo("Aborted -- no changes were made.")
        return
    outcome = settings_mod.apply_push(engine.config, engine.client)
    if outcome.action == "error":
        raise click.ClickException(outcome.detail)
    click.echo(f"Updated: {', '.join(outcome.changed_fields or [])}")


if __name__ == "__main__":
    main()
