"""Local-only web GUI for editing and previewing content.

Deliberately narrow in what it's allowed to do: every route in this module
reads or writes files inside the local project only. **Nothing here ever
makes a network call to WordPress.** Anything that would actually sync to
the live site (push, pull, delete, taxonomy/media/comments push, ...) is
surfaced as a copyable CLI command instead -- the same dry-run-first,
deliberate-apply-step safety boundary the rest of wpsync uses, just made
visible in the browser rather than executed by it.

Binds to 127.0.0.1 by default (see ``wpsync serve``). A lightweight
per-process CSRF token is required on every POST as defense-in-depth,
since this process does perform local filesystem writes.
"""
from __future__ import annotations

import secrets
from html import escape
from pathlib import Path
from typing import Optional

from flask import Flask, abort, redirect, request, url_for

from . import comments as comments_mod
from . import insights as insights_mod
from . import media_library as media_lib
from . import site_settings as settings_mod
from . import taxonomy as tax
from .config import Config
from .content import ContentItem, VALID_STATUSES
from .preview import render_preview_html
from .state import StateStore
from .sync import SyncEngine

_STYLE = """
body { font-family: -apple-system, BlinkMacSystemFont, sans-serif; max-width: 960px;
       margin: 0 auto; padding: 1.5rem; line-height: 1.5; color: #1e1e1e; background: #fafafa; }
nav { margin-bottom: 1.5rem; padding-bottom: 0.75rem; border-bottom: 2px solid #2c3338; }
nav a { margin-right: 1rem; color: #2c3338; text-decoration: none; font-weight: 600; }
nav a:hover { text-decoration: underline; }
h1, h2 { color: #2c3338; }
table { border-collapse: collapse; width: 100%; margin: 1rem 0; }
th, td { text-align: left; padding: 0.4rem 0.6rem; border-bottom: 1px solid #ddd; }
.badge { display: inline-block; padding: 0.1rem 0.5rem; border-radius: 4px; font-size: 0.8rem; }
.badge-new { background: #d4edda; color: #155724; }
.badge-modified { background: #fff3cd; color: #856404; }
.badge-unchanged { background: #e2e3e5; color: #383d41; }
textarea { width: 100%; box-sizing: border-box; font-family: ui-monospace, monospace; font-size: 0.9rem; }
input[type=text], select { padding: 0.3rem; box-sizing: border-box; }
label { display: block; margin-top: 0.75rem; font-weight: 600; }
.row { display: flex; gap: 2rem; }
.col { flex: 1; min-width: 0; }
.preview-pane { border: 1px solid #ccc; background: #fff; padding: 1rem; border-radius: 4px;
                max-height: 70vh; overflow-y: auto; }
.cli-hint { background: #2c3338; color: #fff; padding: 0.6rem 1rem; border-radius: 6px;
            font-family: ui-monospace, monospace; font-size: 0.85rem; margin: 1rem 0; }
button { background: #2c3338; color: #fff; border: none; padding: 0.5rem 1.2rem; border-radius: 4px;
         cursor: pointer; font-size: 0.9rem; }
button:hover { background: #444; }
.warn { color: #a94442; }
pre { background: #fff; border: 1px solid #ddd; padding: 1rem; overflow-x: auto; border-radius: 4px; }
"""

_NAV = (
    '<nav><a href="{home}">Dashboard</a><a href="{media}">Media</a>'
    '<a href="{comments}">Comments</a><a href="{tax}">Taxonomy</a>'
    '<a href="{settings}">Settings</a><a href="{insights}">Insights</a></nav>'
)


def _page(title: str, body: str) -> str:
    nav = _NAV.format(
        home=url_for("dashboard"),
        media=url_for("media_page"),
        comments=url_for("comments_page"),
        tax=url_for("taxonomy_page"),
        settings=url_for("settings_page"),
        insights=url_for("insights_page"),
    )
    return (
        f"<!doctype html><html><head><meta charset='utf-8'>"
        f"<title>{escape(title)} &middot; wpsync</title><style>{_STYLE}</style></head>"
        f"<body>{nav}<h1>{escape(title)}</h1>{body}</body></html>"
    )


def _cli_hint(command: str) -> str:
    return f'<div class="cli-hint">To sync this to WordPress, run in your terminal:<br>$ {escape(command)}</div>'


def create_app(config: Config) -> Flask:
    app = Flask("wpsync")
    app.config["WPSYNC_TOKEN"] = secrets.token_hex(16)
    state = StateStore(config.state_dir)
    engine = SyncEngine(config, state, client=None)

    def token_field() -> str:
        return f'<input type="hidden" name="_token" value="{app.config["WPSYNC_TOKEN"]}">'

    def require_token():
        if request.form.get("_token") != app.config["WPSYNC_TOKEN"]:
            abort(403)

    # ------------------------------------------------------------- dashboard

    @app.get("/")
    def dashboard():
        status = engine.status()
        rows = []
        for slug, st in sorted(status.items()):
            item = engine.find_local(slug)
            rows.append(
                f"<tr><td><a href='{url_for('edit_page', slug=slug)}'>{escape(slug)}</a></td>"
                f"<td>{escape(item.kind if item else '')}</td>"
                f"<td><span class='badge badge-{st}'>{escape(st)}</span></td></tr>"
            )
        table = (
            "<table><tr><th>Slug</th><th>Kind</th><th>Status</th></tr>" + "".join(rows) + "</table>"
            if rows
            else "<p>No local content yet.</p>"
        )
        new_form = (
            f'<h2>New</h2><form method="post" action="{url_for("new_content")}">{token_field()}'
            '<select name="kind"><option value="post">Post</option><option value="page">Page</option></select> '
            '<input type="text" name="title" placeholder="Title" required> '
            "<button type='submit'>Create</button></form>"
        )
        hint = _cli_hint("wpsync push          # review pending changes\nwpsync push --apply  # publish them")
        return _page("Dashboard", table + new_form + hint)

    @app.post("/new")
    def new_content():
        require_token()
        kind = request.form.get("kind", "post")
        title = request.form.get("title", "").strip()
        if kind not in ("post", "page") or not title:
            abort(400)
        import re as _re

        slug = _re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")
        sub = "posts" if kind == "post" else "pages"
        path = config.content_dir / sub / f"{slug}.md"
        if path.exists():
            abort(400, "A post/page with that slug already exists.")
        item = ContentItem(kind=kind, path=path, title=title, slug=slug, status="draft", body="Start writing here.\n")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(item.dump(), encoding="utf-8")
        return redirect(url_for("edit_page", slug=slug))

    # ------------------------------------------------------------------ edit

    @app.get("/edit/<slug>")
    def edit_page(slug):
        item = engine.find_local(slug)
        if item is None:
            abort(404)
        status_options = "".join(
            f'<option value="{s}"{" selected" if s == item.status else ""}>{s}</option>' for s in VALID_STATUSES
        )
        preview_html = render_preview_html(item, engine.offline_resolver(item), gpx_root=config.content_dir)
        form = f"""
        <form method="post" action="{url_for('edit_page', slug=slug)}">
        {token_field()}
        <div class="row">
          <div class="col">
            <label>Title</label><input type="text" name="title" value="{escape(item.title)}" style="width:100%">
            <label>Status</label><select name="status">{status_options}</select>
            <label>Categories (comma-separated)</label>
            <input type="text" name="categories" value="{escape(', '.join(item.categories))}" style="width:100%">
            <label>Tags (comma-separated)</label>
            <input type="text" name="tags" value="{escape(', '.join(item.tags))}" style="width:100%">
            <label>Excerpt</label><input type="text" name="excerpt" value="{escape(item.excerpt)}" style="width:100%">
            <label>Featured image (path relative to content/)</label>
            <input type="text" name="featured_image" value="{escape(item.featured_image or '')}" style="width:100%">
            <label>Body (Markdown)</label>
            <textarea name="body" rows="20">{escape(item.body)}</textarea>
            <p><button type="submit">Save</button></p>
          </div>
          <div class="col">
            <label>Preview</label>
            <div class="preview-pane">{preview_html}</div>
          </div>
        </div>
        </form>
        """
        hint = _cli_hint(f"wpsync diff {slug}    # see what would change on WordPress\nwpsync push --apply")
        return _page(f"Edit: {item.title}", form + hint)

    @app.post("/edit/<slug>")
    def edit_page_save(slug):
        require_token()
        item = engine.find_local(slug)
        if item is None:
            abort(404)
        item.title = request.form.get("title", item.title)
        item.status = request.form.get("status", item.status)
        item.categories = [c.strip() for c in request.form.get("categories", "").split(",") if c.strip()]
        item.tags = [t.strip() for t in request.form.get("tags", "").split(",") if t.strip()]
        item.excerpt = request.form.get("excerpt", "")
        item.featured_image = request.form.get("featured_image", "").strip() or None
        item.body = request.form.get("body", item.body)
        item.path.write_text(item.dump(), encoding="utf-8")
        return redirect(url_for("edit_page", slug=slug))

    # ----------------------------------------------------------------- media

    @app.get("/media")
    def media_page():
        manifest = media_lib.load_manifest(config)
        rows = []
        for m in manifest.items:
            rows.append(
                f"<tr><td>{m.wp_id}</td><td>{escape(m.filename)}</td>"
                f"<td><form method='post' action='{url_for('media_edit', wp_id=m.wp_id)}'>{token_field()}"
                f"<input type='text' name='alt_text' value='{escape(m.alt_text)}' placeholder='alt text'>"
                f"<button type='submit'>Save</button></form></td>"
                f"<td>{'edited (unsynced)' if m.local_edit else ''}</td></tr>"
            )
        table = (
            "<table><tr><th>ID</th><th>Filename</th><th>Alt text</th><th></th></tr>" + "".join(rows) + "</table>"
            if rows
            else "<p>No media pulled yet. Run <code>wpsync media pull</code> in your terminal.</p>"
        )
        hint = _cli_hint("wpsync media pull\nwpsync media push --apply")
        return _page("Media library", table + hint)

    @app.post("/media/<int:wp_id>/edit")
    def media_edit(wp_id):
        require_token()
        alt = request.form.get("alt_text")
        try:
            media_lib.set_metadata(config, wp_id, alt_text=alt)
        except ValueError:
            abort(404)
        return redirect(url_for("media_page"))

    # -------------------------------------------------------------- comments

    @app.get("/comments")
    def comments_page():
        threads = comments_mod.CommentsStore(config).all_threads()
        rows = []
        for thread in threads:
            for c in thread.comments:
                rows.append(
                    f"<tr><td>{escape(thread.post_slug)}</td><td>{escape(c.author_name)}</td>"
                    f"<td>{escape(c.content[:120])}</td><td>{escape(c.status)}</td>"
                    f"<td><form method='post' action='{url_for('comments_edit', slug=thread.post_slug, comment_id=c.wp_id)}'>"
                    f"{token_field()}"
                    f"<select name='desired_status'><option value=''>-- no change --</option>"
                    + "".join(
                        f"<option value='{s}'{' selected' if c.desired_status == s else ''}>{s}</option>"
                        for s in comments_mod.VALID_STATUSES
                    )
                    + "</select><br>"
                    f"<input type='text' name='reply' value='{escape(c.reply or '')}' placeholder='draft reply'>"
                    f"<button type='submit'>Save</button></form></td></tr>"
                )
        table = (
            "<table><tr><th>Post</th><th>Author</th><th>Comment</th><th>Status</th><th>Action</th></tr>"
            + "".join(rows) + "</table>"
            if rows
            else "<p>No comments pulled yet. Run <code>wpsync comments pull</code> in your terminal.</p>"
        )
        hint = _cli_hint("wpsync comments pull\nwpsync comments push --apply")
        return _page("Comments", table + hint)

    @app.post("/comments/<slug>/<int:comment_id>/edit")
    def comments_edit(slug, comment_id):
        require_token()
        desired_status = request.form.get("desired_status") or None
        reply = request.form.get("reply") or None
        try:
            comments_mod.set_intent(config, slug, comment_id, desired_status=desired_status, reply=reply)
        except ValueError:
            abort(404)
        return redirect(url_for("comments_page"))

    # -------------------------------------------------------------- taxonomy

    @app.get("/taxonomy")
    def taxonomy_page():
        manifest = tax.load_manifest(config)

        def term_rows(terms):
            return "".join(
                f"<tr><td>{escape(t.name)}</td><td>{escape(t.description)}</td>"
                f"<td>{'draft (unsynced)' if t.wp_id is None else t.wp_id}</td></tr>"
                for t in terms
            )

        body = (
            "<h2>Categories</h2><table><tr><th>Name</th><th>Description</th><th>ID</th></tr>"
            + term_rows(manifest.categories)
            + "</table><h2>Tags</h2><table><tr><th>Name</th><th>Description</th><th>ID</th></tr>"
            + term_rows(manifest.tags)
            + "</table>"
        )
        add_form = f"""
        <h2>Add draft term</h2>
        <form method="post" action="{url_for('taxonomy_add')}">{token_field()}
        <select name="kind"><option value="category">Category</option><option value="tag">Tag</option></select>
        <input type="text" name="name" placeholder="Name" required>
        <input type="text" name="description" placeholder="Description">
        <button type="submit">Add</button></form>
        """
        hint = _cli_hint("wpsync taxonomy pull\nwpsync taxonomy push --apply")
        return _page("Taxonomy", body + add_form + hint)

    @app.post("/taxonomy/add")
    def taxonomy_add():
        require_token()
        kind = request.form.get("kind", "category")
        name = request.form.get("name", "").strip()
        description = request.form.get("description", "")
        if kind not in tax.TAXONOMIES or not name:
            abort(400)
        try:
            tax.add_term(config, kind, name, description=description)
        except ValueError as exc:
            abort(400, str(exc))
        return redirect(url_for("taxonomy_page"))

    # -------------------------------------------------------------- settings

    @app.get("/settings")
    def settings_page():
        settings = settings_mod.load(config)
        fields = "".join(
            f'<label>{escape(f)}</label><input type="text" name="{f}" value="{escape(settings.values.get(f, ""))}" style="width:100%">'
            for f in settings_mod.ALLOWED_FIELDS
        )
        form = f"""
        <form method="post" action="{url_for('settings_save')}">{token_field()}
        {fields}
        <p><button type="submit">Save</button></p>
        </form>
        """
        hint = _cli_hint("wpsync settings pull\nwpsync settings push --apply")
        return _page("Site settings", form + hint)

    @app.post("/settings")
    def settings_save():
        require_token()
        values = {f: request.form.get(f, "") for f in settings_mod.ALLOWED_FIELDS if request.form.get(f, "") != ""}
        settings = settings_mod.SiteSettings(values=values)
        settings.save(settings_mod.manifest_path(config))
        return redirect(url_for("settings_page"))

    # -------------------------------------------------------------- insights

    @app.get("/insights")
    def insights_page():
        report = insights_mod.build_report(config, engine.local_items())
        text = insights_mod.format_report(report)
        return _page("Insights", f"<pre>{escape(text)}</pre>")

    return app
