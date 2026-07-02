/**
 * Local-first browser dashboard -- v2's primary interface. Where v1 is a
 * CLI you point at Markdown files, this is a small Express app you point a
 * browser at (`npm run dashboard`, then http://localhost:4173). Content is
 * edited as raw Gutenberg block HTML directly (no Markdown conversion
 * layer, so nothing is ever lossy), with a live preview pane, a GPX-to-
 * route-summary tool, and a review screen before anything reaches
 * WordPress.
 *
 * Deliberately never accepts the WordPress application password through
 * an HTML form: it's read only from the WPSYNC2_APP_PASSWORD environment
 * variable the dashboard process was started with, so it never touches a
 * browser form, request log, or this process's own persisted state.
 */
import express from "express";
import multer from "multer";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { Config, ConfigError, loadConfig, requireCredentials, writeConfigTemplate } from "./config.js";
import { createContent, getContentBySlug, getMedia, listOutbox, openDb, saveLocalContent } from "./db.js";
import { parseGpx, renderRouteSummaryHtml, summarizeRoute } from "./gpx.js";
import { fileHash } from "./media.js";
import { SyncEngine, unifiedDiffPreview } from "./sync.js";
import type { ContentRow, Kind } from "./types.js";
import { diffHtml, escapeHtml, layout, statusBadge } from "./views.js";
import { WPClient } from "./wpClient.js";

const PORT = Number(process.env.WPSYNC2_PORT ?? 4173);

function tryLoadConfig(root: string): Config | null {
  try {
    return loadConfig(root);
  } catch (err) {
    if (err instanceof ConfigError) return null;
    throw err;
  }
}

function buildEngine(config: Config, withClient: boolean): SyncEngine {
  const db = openDb(config.dbPath);
  let client: WPClient | undefined;
  if (withClient) {
    const appPassword = requireCredentials(config);
    client = new WPClient({ baseUrl: config.baseUrl, username: config.username, appPassword });
  }
  return new SyncEngine(db, config, client);
}

function renderPreviewDoc(row: Pick<ContentRow, "title" | "status" | "date" | "categories" | "tags" | "local_content">): string {
  const metaBits = [`status: ${row.status}`];
  if (row.date) metaBits.push(row.date);
  if (row.categories.length) metaBits.push("categories: " + row.categories.join(", "));
  if (row.tags.length) metaBits.push("tags: " + row.tags.join(", "));
  const body = row.local_content.replace(/<!--\s*\/?wp:[^>]*-->\n?/g, "");
  return `<!doctype html><html><head><meta charset="utf-8">
  <style>
    body { font-family: Georgia, 'Times New Roman', serif; max-width: 700px; margin: 2rem auto; padding: 0 1.2rem; line-height: 1.65; }
    .meta { font-family: -apple-system, sans-serif; color: #777; font-size: 0.85rem; margin-bottom: 1.5rem; }
    img { max-width: 100%; height: auto; border-radius: 4px; }
    blockquote { border-left: 4px solid #ccc; margin: 1.2rem 0; padding: 0.2rem 1rem; font-style: italic; color: #444; }
  </style></head><body>
  <h1>${escapeHtml(row.title)}</h1>
  <div class="meta">${escapeHtml(metaBits.join(" · "))}</div>
  ${body}
  </body></html>`;
}

export function createApp(root: string): express.Express {
const app = express();
app.use(express.urlencoded({ extended: true }));
const upload = multer({ storage: multer.memoryStorage() });

// -- setup / config --------------------------------------------------

app.get("/setup", (_req, res) => {
  res.send(
    layout(
      "Setup",
      `<h1>Set up wpsync2</h1>
      <form method="post" action="/setup">
        <div class="field"><label>WordPress base URL</label>
          <input type="text" name="baseUrl" style="width:100%" placeholder="https://mvermeulen.org/gone2look4america" required></div>
        <div class="field"><label>Username</label>
          <input type="text" name="username" style="width:100%" required></div>
        <button type="submit">Save</button>
      </form>
      <p>After saving, start the dashboard with <code>WPSYNC2_APP_PASSWORD='...' npm run dashboard</code>
      to enable pull/push/delete. An Application Password is created under WordPress admin -&gt;
      Users -&gt; Profile -&gt; Application Passwords. The dashboard never asks for it in a form.</p>`
    )
  );
});

app.post("/setup", (req, res) => {
  writeConfigTemplate(root, String(req.body.baseUrl).replace(/\/$/, ""), String(req.body.username));
  res.redirect("/");
});

app.use((req, res, next) => {
  if (req.path === "/setup") return next();
  const config = tryLoadConfig(root);
  if (!config) return res.redirect("/setup");
  (req as any).config = config;
  next();
});

// -- dashboard home ----------------------------------------------------

app.get("/", (req, res) => {
  const config: Config = (req as any).config;
  const engine = buildEngine(config, false);
  const rows = engine.listLocal();
  const status = engine.status();
  const hasCreds = Boolean(config.appPassword);

  const tableRows = rows
    .map((r) => {
      return `<tr>
        <td><a href="/content/${encodeURIComponent(r.slug)}">${escapeHtml(r.title)}</a><br>
            <small>${escapeHtml(r.kind)} &middot; ${escapeHtml(r.slug)}</small></td>
        <td>${statusBadge(status[r.slug])}</td>
        <td>${r.wp_id != null ? `#${r.wp_id}` : "<em>not yet synced</em>"}</td>
        <td>${escapeHtml(r.status)}</td>
      </tr>`;
    })
    .join("\n");

  res.send(
    layout(
      "Dashboard",
      `<h1>Gone2Look4America -- local mirror</h1>
      ${
        hasCreds
          ? `<form method="post" action="/pull" class="inline"><button type="submit">Pull from WordPress</button></form>`
          : `<div class="banner info">Set <code>WPSYNC2_APP_PASSWORD</code> and restart the dashboard to enable Pull/Push/Delete. Editing and previewing offline still works fully.</div>`
      }
      <table>
        <thead><tr><th>Content</th><th>Local status</th><th>WordPress id</th><th>Publish status</th></tr></thead>
        <tbody>${tableRows || `<tr><td colspan="4"><em>No local content yet -- create one above.</em></td></tr>`}</tbody>
      </table>`
    )
  );
});

// -- create ---------------------------------------------------------

app.get("/new/:kind", (req, res) => {
  const kind = req.params.kind as Kind;
  if (kind !== "post" && kind !== "page") return res.status(404).send("unknown kind");
  res.send(
    layout(
      `New ${kind}`,
      `<h1>New ${kind}</h1>
      <form method="post" action="/new/${kind}">
        <div class="field"><label>Title</label><input type="text" name="title" style="width:100%" required></div>
        <button type="submit">Create</button>
      </form>`
    )
  );
});

app.post("/new/:kind", (req, res) => {
  const kind = req.params.kind as Kind;
  const config: Config = (req as any).config;
  const db = openDb(config.dbPath);
  const title = String(req.body.title || "").trim();
  const slug = slugify(title);
  if (!title || !slug) return res.status(400).send("Title is required");
  if (getContentBySlug(db, slug)) return res.status(400).send(`Slug '${slug}' already exists`);
  createContent(db, {
    kind,
    slug,
    title,
    local_content: '<!-- wp:paragraph -->\n<p class="wp-block-paragraph">Start writing here.</p>\n<!-- /wp:paragraph -->',
  });
  res.redirect(`/content/${encodeURIComponent(slug)}`);
});

// -- edit -------------------------------------------------------------

app.get("/content/:slug", (req, res) => {
  const config: Config = (req as any).config;
  const engine = buildEngine(config, false);
  const row = engine.findLocal(req.params.slug);
  if (!row) return res.status(404).send("Not found");

  const db = openDb(config.dbPath);
  const conflict = pendingConflictPreview(row, listOutbox(db, row.slug));

  res.send(
    layout(
      row.title,
      `<h1>Edit: ${escapeHtml(row.title)}</h1>
      <p><a href="/">&larr; back to dashboard</a> &middot; <a href="/content/${encodeURIComponent(row.slug)}/preview" target="_blank">open full preview</a></p>
      ${
        conflict
          ? `<div class="banner conflict"><strong>Unresolved conflict from the last push attempt.</strong>
             This post changed both locally and on the live site in overlapping regions. Edit the content
             below to resolve it (a suggested merge with &lt;&lt;&lt;&lt;&lt;&lt;&lt; / ======= / &gt;&gt;&gt;&gt;&gt;&gt;&gt; markers
             is shown here for reference -- copy whichever parts you want into the editor, then save and push again).\n\n${escapeHtml(
               conflict
             )}</div>`
          : ""
      }
      <form method="post" action="/content/${encodeURIComponent(row.slug)}">
        <div class="row">
          <div class="field"><label>Title</label><input type="text" name="title" value="${escapeHtml(row.title)}" style="width:100%"></div>
          <div class="field"><label>Status</label>
            <select name="status">
              ${["draft", "publish", "pending", "private"]
                .map((s) => `<option value="${s}" ${s === row.status ? "selected" : ""}>${s}</option>`)
                .join("")}
            </select>
          </div>
          <div class="field"><label>Date</label><input type="text" name="date" value="${escapeHtml(row.date ?? "")}" placeholder="2026-07-01T09:00:00"></div>
        </div>
        <div class="row">
          <div class="field"><label>Categories (comma-separated)</label><input type="text" name="categories" value="${escapeHtml(row.categories.join(", "))}" style="width:100%"></div>
          <div class="field"><label>Tags (comma-separated)</label><input type="text" name="tags" value="${escapeHtml(row.tags.join(", "))}" style="width:100%"></div>
        </div>
        <div class="field"><label>Excerpt</label><input type="text" name="excerpt" value="${escapeHtml(row.excerpt)}" style="width:100%"></div>
        <div class="row">
          <div class="field" style="flex:1.3">
            <label>Content (Gutenberg block HTML -- edited directly, no conversion layer)</label>
            <textarea name="local_content" id="local_content" rows="20" oninput="updatePreview()">${escapeHtml(row.local_content)}</textarea>
          </div>
          <div class="field">
            <label>Live preview (offline, local media shown as placeholders until synced)</label>
            <iframe class="preview" id="preview" src="/content/${encodeURIComponent(row.slug)}/preview"></iframe>
          </div>
        </div>
        <button type="submit">Save locally</button>
      </form>

      <h3>Insert a route summary from a .gpx file</h3>
      <form method="post" action="/content/${encodeURIComponent(row.slug)}/gpx" enctype="multipart/form-data">
        <input type="file" name="gpx" accept=".gpx" required>
        <button type="submit" class="secondary">Parse and append to content</button>
      </form>

      <h3>Delete on WordPress</h3>
      ${
        row.wp_id == null
          ? "<p><em>Not yet synced -- nothing to delete remotely. Removing the local row isn't wired into this UI on purpose; edit the database directly if you truly need to discard a draft.</em></p>"
          : `<p>Type the <strong>exact current title</strong> shown on the live site to confirm.</p>
      <form method="post" action="/content/${encodeURIComponent(row.slug)}/delete" onsubmit="return confirm('This will delete on WordPress. Continue?')">
        <input type="text" name="confirmTitle" placeholder="Exact live title" required>
        <label><input type="checkbox" name="permanent" value="1"> permanently (skip trash)</label>
        <button type="submit" class="danger">Delete on WordPress</button>
      </form>`
      }

      <script>
        function updatePreview() {
          // Debounced client-side re-render isn't wired to the server preview
          // route to keep this dependency-free; reload the preview frame with
          // the saved copy after saving instead.
        }
      </script>`
    )
  );
});

app.get("/content/:slug/preview", (req, res) => {
  const config: Config = (req as any).config;
  const engine = buildEngine(config, false);
  const row = engine.findLocal(req.params.slug);
  if (!row) return res.status(404).send("Not found");
  res.send(renderPreviewDoc(row));
});

app.post("/content/:slug", (req, res) => {
  const config: Config = (req as any).config;
  const db = openDb(config.dbPath);
  const categories = String(req.body.categories || "")
    .split(",")
    .map((s) => s.trim())
    .filter(Boolean);
  const tags = String(req.body.tags || "")
    .split(",")
    .map((s) => s.trim())
    .filter(Boolean);
  saveLocalContent(db, req.params.slug, {
    title: req.body.title,
    status: req.body.status,
    date: req.body.date || null,
    excerpt: req.body.excerpt || "",
    categories,
    tags,
    local_content: req.body.local_content,
  });
  res.redirect(`/content/${encodeURIComponent(req.params.slug)}`);
});

app.post("/content/:slug/gpx", upload.single("gpx"), (req, res) => {
  const config: Config = (req as any).config;
  const engine = buildEngine(config, false);
  const row = engine.findLocal(req.params.slug);
  if (!row) return res.status(404).send("Not found");
  if (!req.file) return res.status(400).send("No file uploaded");
  const points = parseGpx(req.file.buffer.toString("utf-8"));
  if (points.length === 0) return res.status(400).send("Could not find any track points in that .gpx file");
  const summary = summarizeRoute(points);
  const block = renderRouteSummaryHtml(summary, "Today's route");
  const db = openDb(config.dbPath);
  saveLocalContent(db, row.slug, { local_content: `${row.local_content}\n\n${block}` } as any);
  res.redirect(`/content/${encodeURIComponent(row.slug)}`);
});

app.post("/content/:slug/delete", async (req, res) => {
  const config: Config = (req as any).config;
  try {
    const engine = buildEngine(config, true);
    const outcome = await engine.delete(req.params.slug, String(req.body.confirmTitle), Boolean(req.body.permanent));
    res.send(layout("Deleted", `<div class="banner info">${escapeHtml(outcome.detail ?? "")}</div><p><a href="/">Back to dashboard</a></p>`));
  } catch (err) {
    res.status(400).send(layout("Delete failed", `<div class="banner error">${escapeHtml(String((err as Error).message))}</div><p><a href="/content/${encodeURIComponent(req.params.slug)}">Back</a></p>`));
  }
});

// -- pull ---------------------------------------------------------------

app.post("/pull", async (req, res) => {
  const config: Config = (req as any).config;
  try {
    const engine = buildEngine(config, true);
    const outcomes = await engine.pull();
    const items = outcomes.map((o) => `<li><strong>${escapeHtml(o.action)}</strong> -- ${escapeHtml(o.slug)}</li>`).join("");
    res.send(layout("Pull complete", `<h1>Pull complete</h1><ul>${items || "<li>Nothing to pull.</li>"}</ul><p><a href="/">Back to dashboard</a></p>`));
  } catch (err) {
    res.status(500).send(layout("Pull failed", `<div class="banner error">${escapeHtml(String((err as Error).message))}</div><p><a href="/">Back</a></p>`));
  }
});

// -- push (review then apply) -------------------------------------------

app.get("/push", async (req, res) => {
  const config: Config = (req as any).config;
  try {
    const engine = buildEngine(config, true);
    const outcomes = await engine.push({ apply: false });
    const rows = outcomes
      .filter((o) => o.action !== "unchanged")
      .map((o) => {
        const applicable = ["would-create", "would-update", "would-merge"].includes(o.action);
        return `<div class="banner ${o.action === "conflict" ? "conflict" : "info"}">
          <strong>${escapeHtml(o.slug)}</strong> -- ${escapeHtml(o.action)}
          ${o.detail ? `<p>${escapeHtml(o.detail)}</p>` : ""}
          ${o.diff ? diffHtml(o.diff) : ""}
          ${
            applicable
              ? `<form method="post" action="/push/apply" class="inline"><input type="hidden" name="slug" value="${escapeHtml(o.slug)}"><button type="submit">Apply this change</button></form>`
              : ""
          }
        </div>`;
      })
      .join("\n");
    res.send(
      layout(
        "Review push",
        `<h1>Review &amp; push</h1>
        <p>This is always a dry run first. Nothing above has touched WordPress yet.</p>
        ${rows || '<p><em>Nothing to push -- everything matches the last sync.</em></p>'}
        ${rows ? `<form method="post" action="/push/apply"><button type="submit">Apply all safe changes (skips conflicts)</button></form>` : ""}`
      )
    );
  } catch (err) {
    res.status(500).send(layout("Push failed", `<div class="banner error">${escapeHtml(String((err as Error).message))}</div><p><a href="/">Back</a></p>`));
  }
});

app.post("/push/apply", async (req, res) => {
  const config: Config = (req as any).config;
  try {
    const engine = buildEngine(config, true);
    const slugs = req.body.slug ? [String(req.body.slug)] : undefined;
    const outcomes = await engine.push({ apply: true, slugs });
    const items = outcomes
      .filter((o) => o.action !== "unchanged")
      .map((o) => `<li><strong>${escapeHtml(o.action)}</strong> -- ${escapeHtml(o.slug)} ${o.detail ? `(${escapeHtml(o.detail)})` : ""}</li>`)
      .join("");
    res.send(layout("Push applied", `<h1>Push applied</h1><ul>${items || "<li>Nothing changed.</li>"}</ul><p><a href="/push">Review again</a> &middot; <a href="/">Dashboard</a></p>`));
  } catch (err) {
    res.status(500).send(layout("Push failed", `<div class="banner error">${escapeHtml(String((err as Error).message))}</div><p><a href="/push">Back</a></p>`));
  }
});

// -- media ----------------------------------------------------------

app.get("/media", (req, res) => {
  const config: Config = (req as any).config;
  fs.mkdirSync(config.mediaDir, { recursive: true });
  const db = openDb(config.dbPath);
  const files = fs.readdirSync(config.mediaDir).filter((f) => !f.startsWith("."));
  const rows = files
    .map((f) => {
      const full = path.join(config.mediaDir, f);
      const hash = fileHash(full);
      const media = getMedia(db, `media/${f}`);
      const synced = media && media.file_hash === hash;
      return `<tr><td>${escapeHtml(f)}</td><td>${synced ? `uploaded (wp id ${media!.wp_id})` : "not yet uploaded"}</td></tr>`;
    })
    .join("");
  res.send(
    layout(
      "Media",
      `<h1>Media</h1>
      <form method="post" action="/media" enctype="multipart/form-data">
        <input type="file" name="file" required>
        <button type="submit">Add to media/</button>
      </form>
      <p>Reference these from a post as <code>&lt;img src="media/filename.jpg" alt="..."/&gt;</code>.
      Files are uploaded to WordPress automatically the first time a push references them.</p>
      <table><thead><tr><th>File</th><th>Status</th></tr></thead><tbody>${rows || '<tr><td colspan="2"><em>No media yet.</em></td></tr>'}</tbody></table>`
    )
  );
});

app.post("/media", upload.single("file"), (req, res) => {
  const config: Config = (req as any).config;
  fs.mkdirSync(config.mediaDir, { recursive: true });
  if (!req.file) return res.status(400).send("No file uploaded");
  fs.writeFileSync(path.join(config.mediaDir, req.file.originalname), req.file.buffer);
  res.redirect("/media");
});

// -- gpx converter utility -------------------------------------------

app.get("/gpx", (_req, res) => {
  res.send(
    layout(
      "GPX tool",
      `<h1>GPX route summary</h1>
      <p>Drop in a .gpx file exported from a bike computer or phone app to get distance/elevation
      stats and a ready-to-paste content block, before the ride ever reaches Strava.</p>
      <form method="post" action="/gpx" enctype="multipart/form-data">
        <input type="file" name="gpx" accept=".gpx" required>
        <button type="submit">Summarize</button>
      </form>`
    )
  );
});

app.post("/gpx", upload.single("gpx"), (req, res) => {
  if (!req.file) return res.status(400).send("No file uploaded");
  const points = parseGpx(req.file.buffer.toString("utf-8"));
  if (points.length === 0) {
    return res.send(layout("GPX tool", `<div class="banner error">Could not find any track points in that file.</div><p><a href="/gpx">Try again</a></p>`));
  }
  const summary = summarizeRoute(points);
  const block = renderRouteSummaryHtml(summary);
  res.send(
    layout(
      "GPX tool",
      `<h1>Route summary</h1>
      <p>${summary.points} points &middot; ${summary.distanceKm} km &middot; ${summary.elevationGainM} m climbing
      ${summary.movingTimeMinutes != null ? `&middot; ${summary.movingTimeMinutes} min elapsed` : ""}</p>
      <label>Paste this into a post's content:</label>
      <textarea rows="8" readonly>${escapeHtml(block)}</textarea>
      <p><a href="/gpx">Convert another</a></p>`
    )
  );
});

// -- history / outbox -------------------------------------------------

app.get("/history", (req, res) => {
  const config: Config = (req as any).config;
  const db = openDb(config.dbPath);
  const entries = listOutbox(db);
  const rows = entries
    .map(
      (e) =>
        `<tr><td>${escapeHtml(e.created_at)}</td><td>${escapeHtml(e.slug)}</td><td>${escapeHtml(e.op)}</td><td>${escapeHtml(
          e.result
        )}</td><td>${escapeHtml(e.detail)}</td></tr>`
    )
    .join("");
  res.send(
    layout(
      "History",
      `<h1>Sync history</h1>
      <p>Every push/delete attempt is logged here, along with a backup copy of whatever was live
      immediately before an update or delete -- kept in the local database, not a separate backups folder.</p>
      <table><thead><tr><th>When</th><th>Slug</th><th>Op</th><th>Result</th><th>Detail</th></tr></thead>
      <tbody>${rows || '<tr><td colspan="5"><em>Nothing yet.</em></td></tr>'}</tbody></table>`
    )
  );
});

  return app;
}

function pendingConflictPreview(row: ContentRow, outboxEntries: ReturnType<typeof listOutbox>): string | null {
  const last = outboxEntries[0];
  if (!last || last.result !== "conflict" || !last.remote_backup) return null;
  if (row.local_content === row.base_content) return null; // already resolved/reverted
  return unifiedDiffPreview(last.remote_backup, row.local_content);
}

function slugify(text: string): string {
  return text
    .toLowerCase()
    .trim()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-+|-+$/g, "");
}

function isMainModule(): boolean {
  return process.argv[1] === fileURLToPath(import.meta.url);
}

if (isMainModule()) {
  const app = createApp(process.cwd());
  app.listen(PORT, () => {
    console.log(`wpsync2 dashboard running at http://localhost:${PORT}`);
  });
}
