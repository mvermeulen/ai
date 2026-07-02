/** Tiny server-rendered HTML helpers -- deliberately no frontend framework or
 * build step for the UI itself, to keep v2's "different choices" focused on
 * the sync engine rather than piling on frontend tooling too. */

export function escapeHtml(s: string): string {
  return String(s ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

export function layout(title: string, body: string): string {
  return `<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>${escapeHtml(title)} -- wpsync2</title>
<style>
  :root { color-scheme: light dark; }
  body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; max-width: 960px;
         margin: 0 auto; padding: 1.5rem; line-height: 1.5; }
  nav { display: flex; gap: 1rem; margin-bottom: 1.5rem; padding-bottom: 1rem; border-bottom: 1px solid #8884; flex-wrap: wrap; }
  nav a { text-decoration: none; font-weight: 600; opacity: 0.8; }
  nav a:hover { opacity: 1; }
  table { width: 100%; border-collapse: collapse; margin: 1rem 0; }
  th, td { text-align: left; padding: 0.5rem; border-bottom: 1px solid #8884; vertical-align: top; }
  .badge { display: inline-block; padding: 0.15rem 0.55rem; border-radius: 999px; font-size: 0.78rem; font-weight: 600; }
  .badge-new { background: #2563eb33; color: #2563eb; }
  .badge-modified { background: #d9770633; color: #d97706; }
  .badge-unchanged { background: #16a34a33; color: #16a34a; }
  .badge-conflict { background: #dc262633; color: #dc2626; }
  textarea { width: 100%; box-sizing: border-box; font-family: ui-monospace, monospace; font-size: 0.85rem; }
  input[type=text], input[type=date], select { padding: 0.4rem; box-sizing: border-box; }
  .field { margin-bottom: 1rem; }
  .field label { display: block; font-weight: 600; margin-bottom: 0.25rem; font-size: 0.9rem; }
  .row { display: flex; gap: 1.5rem; }
  .row > div { flex: 1; }
  button, .btn { cursor: pointer; border: none; background: #2563eb; color: white; padding: 0.5rem 1rem;
         border-radius: 6px; font-size: 0.9rem; font-weight: 600; }
  button.secondary, .btn.secondary { background: #6b7280; }
  button.danger, .btn.danger { background: #dc2626; }
  pre.diff { background: #0001; padding: 1rem; overflow-x: auto; border-radius: 6px; font-size: 0.82rem; }
  .diff .add { color: #16a34a; }
  .diff .del { color: #dc2626; }
  iframe.preview { width: 100%; height: 420px; border: 1px solid #8884; border-radius: 6px; background: white; }
  .banner { padding: 0.8rem 1rem; border-radius: 6px; margin-bottom: 1rem; }
  .banner.error { background: #dc262622; border: 1px solid #dc262666; }
  .banner.info { background: #2563eb22; border: 1px solid #2563eb66; }
  .banner.conflict { background: #dc262622; border: 1px solid #dc262666; white-space: pre-wrap; font-family: ui-monospace, monospace; font-size: 0.82rem; }
  form.inline { display: inline; }
</style>
</head>
<body>
<nav>
  <a href="/">Dashboard</a>
  <a href="/new/post">New post</a>
  <a href="/new/page">New page</a>
  <a href="/media">Media</a>
  <a href="/gpx">GPX tool</a>
  <a href="/push">Review &amp; push</a>
  <a href="/history">History</a>
</nav>
${body}
</body>
</html>`;
}

export function statusBadge(status: string): string {
  const cls = { new: "new", modified: "modified", unchanged: "unchanged", conflict: "conflict" }[status] ?? "unchanged";
  return `<span class="badge badge-${cls}">${escapeHtml(status)}</span>`;
}

export function diffHtml(diffText: string): string {
  if (!diffText.trim()) return "<p><em>No differences.</em></p>";
  const lines = diffText.split("\n").map((line) => {
    const cls = line.startsWith("+") ? "add" : line.startsWith("-") ? "del" : "";
    return `<span class="${cls}">${escapeHtml(line)}</span>`;
  });
  return `<pre class="diff">${lines.join("\n")}</pre>`;
}
