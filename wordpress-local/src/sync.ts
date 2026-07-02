/**
 * Sync engine: pull, status, and push. The key departure from v1 is push's
 * conflict handling. v1 treated "remote changed since last sync" as a hard
 * stop -- correct, but coarse: editing the intro paragraph locally while
 * someone fixes a typo in the last paragraph on the live site would still
 * be flagged as a conflict requiring a human to pick a side.
 *
 * Here, `base_content` (recorded at the last successful sync) is kept
 * distinct from `local_content` specifically so a real three-way merge
 * (node-diff3, the same algorithm behind `git merge`) can be attempted:
 * non-overlapping edits on both sides merge automatically; only a genuine
 * overlapping edit surfaces as a `conflict` outcome, with the merge
 * tool's own conflict markers (<<<<<<< / ======= / >>>>>>>) ready to
 * resolve directly in the dashboard.
 */
import type BetterSqlite3 from "better-sqlite3";
import { diffComm, merge } from "node-diff3";
import fs from "node:fs";
import path from "node:path";
import type { Config } from "./config.js";
import {
  deleteContentRow,
  getContentBySlug,
  getMedia,
  listContent,
  logOutbox,
  markSynced,
  setMedia,
  upsertFromRemote,
} from "./db.js";
import { fileHash, guessMime } from "./media.js";
import type { ContentRow, Kind, PullOutcome, SyncOutcome } from "./types.js";
import { WPClient, WPNotFoundError } from "./wpClient.js";

function field(value: any, key = "raw", fallback = "rendered"): string {
  if (value && typeof value === "object") return value[key] ?? value[fallback] ?? "";
  return value ?? "";
}

function stripHtml(s: string): string {
  return (s || "").replace(/<[^>]+>/g, "").trim();
}

function extractLocalMediaRefs(html: string): string[] {
  const refs = new Set<string>();
  for (const m of html.matchAll(/<img[^>]*\bsrc="([^"]+)"/g)) {
    if (!/^https?:\/\//.test(m[1])) refs.add(m[1]);
  }
  return [...refs];
}

function resolveMediaRefs(html: string, resolve: (ref: string) => string): string {
  return html.replace(/(<img[^>]*\bsrc=")([^"]+)(")/g, (full, pre, src, post) => {
    if (/^https?:\/\//.test(src)) return full;
    return `${pre}${resolve(src)}${post}`;
  });
}

export function unifiedDiffPreview(oldText: string, newText: string): string {
  const regions = diffComm(oldText.split("\n"), newText.split("\n"));
  const out: string[] = [];
  for (const region of regions) {
    if (region.common) {
      for (const line of region.common) out.push(`  ${line}`);
    } else {
      for (const line of region.buffer1 ?? []) out.push(`- ${line}`);
      for (const line of region.buffer2 ?? []) out.push(`+ ${line}`);
    }
  }
  return out.join("\n");
}

export class SyncEngine {
  constructor(
    private db: BetterSqlite3.Database,
    private config: Config,
    private client?: WPClient
  ) {}

  // ------------------------------------------------------------- pull

  async pull(kinds: Kind[] = ["post", "page"]): Promise<PullOutcome[]> {
    if (!this.client) throw new Error("pull() requires a WPClient");
    const outcomes: PullOutcome[] = [];
    for (const kind of kinds) {
      const items = await this.client.listAll(kind);
      for (const remote of items) outcomes.push(this.pullOne(kind, remote));
    }
    return outcomes;
  }

  private pullOne(kind: Kind, remote: any): PullOutcome {
    const slug = remote.slug as string;
    const content = field(remote.content, "rendered", "rendered");
    const existing = getContentBySlug(this.db, slug);
    const common = {
      kind,
      slug,
      title: field(remote.title),
      status: remote.status ?? "draft",
      date: remote.date ?? null,
      excerpt: stripHtml(field(remote.excerpt)),
      local_content: content,
      wp_id: remote.id as number,
      base_modified_gmt: remote.modified_gmt as string,
    };

    if (!existing) {
      upsertFromRemote(this.db, common);
      return { slug, action: "new" };
    }
    if (existing.local_content !== existing.base_content) {
      return { slug, action: "skipped-local-changes" };
    }
    if (existing.base_content === content && existing.base_modified_gmt === remote.modified_gmt) {
      return { slug, action: "unchanged" };
    }
    upsertFromRemote(this.db, common);
    return { slug, action: "fast-forwarded" };
  }

  // ------------------------------------------------------------ status

  status(): Record<string, string> {
    const result: Record<string, string> = {};
    for (const row of listContent(this.db)) {
      if (row.wp_id == null) result[row.slug] = "new";
      else if (row.local_content !== row.base_content) result[row.slug] = "modified";
      else result[row.slug] = "unchanged";
    }
    return result;
  }

  listLocal(): ContentRow[] {
    return listContent(this.db);
  }

  async whoami(): Promise<any> {
    if (!this.client) throw new Error("whoami() requires a WPClient");
    return this.client.whoami();
  }

  findLocal(slug: string): ContentRow | undefined {
    return getContentBySlug(this.db, slug);
  }

  // -------------------------------------------------------------- push

  async push(opts: { slugs?: string[]; apply?: boolean; force?: boolean } = {}): Promise<SyncOutcome[]> {
    if (!this.client) throw new Error("push() requires a WPClient");
    const apply = opts.apply ?? false;
    const force = opts.force ?? false;
    let rows = listContent(this.db);
    if (opts.slugs?.length) {
      const wanted = new Set(opts.slugs);
      rows = rows.filter((r) => wanted.has(r.slug));
    }
    const outcomes: SyncOutcome[] = [];
    for (const row of rows) outcomes.push(await this.pushOne(row, apply, force));
    return outcomes;
  }

  private async uploadPendingMedia(html: string): Promise<void> {
    const client = this.client!;
    for (const ref of extractLocalMediaRefs(html)) {
      const filePath = path.join(this.config.mediaDir, path.basename(ref));
      if (!fs.existsSync(filePath)) continue;
      const hash = fileHash(filePath);
      const existingMedia = getMedia(this.db, ref);
      if (existingMedia && existingMedia.file_hash === hash) continue;
      const uploaded = await client.uploadMedia(path.basename(filePath), fs.readFileSync(filePath), guessMime(filePath));
      setMedia(this.db, {
        local_path: ref,
        file_hash: hash,
        wp_id: uploaded.id,
        source_url: uploaded.source_url,
        uploaded_at: new Date().toISOString(),
      });
    }
  }

  private async pushOne(row: ContentRow, apply: boolean, force: boolean): Promise<SyncOutcome> {
    const client = this.client!;
    if (apply) await this.uploadPendingMedia(row.local_content);
    const resolvedContent = resolveMediaRefs(row.local_content, (ref) => getMedia(this.db, ref)?.source_url ?? ref);

    const payload: Record<string, unknown> = { title: row.title, content: resolvedContent, status: row.status, slug: row.slug };
    if (row.excerpt) payload.excerpt = row.excerpt;
    if (row.date) payload.date = row.date;

    if (row.wp_id == null) {
      return this.createNew(row, payload, resolvedContent, apply);
    }
    if (row.local_content === row.base_content) {
      return { slug: row.slug, action: "unchanged" };
    }

    let remote: any;
    try {
      remote = await client.get(row.kind, row.wp_id);
    } catch (err) {
      if (err instanceof WPNotFoundError) {
        return {
          slug: row.slug,
          action: "error",
          detail: "the post/page this row was synced to no longer exists on the server (deleted outside wpsync2?).",
        };
      }
      throw err;
    }
    const remoteContent = field(remote.content, "rendered", "rendered");
    const remoteDrifted = remote.modified_gmt !== row.base_modified_gmt;

    if (!remoteDrifted) {
      return this.cleanUpdate(row, payload, resolvedContent, apply, remoteContent);
    }
    return this.mergeUpdate(row, payload, resolvedContent, remoteContent, apply, force);
  }

  private async createNew(row: ContentRow, payload: Record<string, unknown>, resolvedContent: string, apply: boolean): Promise<SyncOutcome> {
    if (!apply) return { slug: row.slug, action: "would-create", detail: `new ${row.kind} '${row.title}'` };
    const client = this.client!;
    if (row.categories.length) payload.categories = await client.ensureTerms("category", row.categories);
    if (row.tags.length) payload.tags = await client.ensureTerms("tag", row.tags);
    const created = await client.create(row.kind, payload);
    markSynced(this.db, row.slug, {
      wp_id: created.id,
      base_content: field(created.content, "rendered", "rendered"),
      base_modified_gmt: created.modified_gmt,
      local_content: resolvedContent,
    });
    logOutbox(this.db, { local_id: row.local_id, slug: row.slug, op: "create", result: "applied", detail: `created ${row.kind} id=${created.id}` });
    return { slug: row.slug, action: "create", detail: `created ${row.kind} id=${created.id}` };
  }

  private async cleanUpdate(
    row: ContentRow,
    payload: Record<string, unknown>,
    resolvedContent: string,
    apply: boolean,
    remoteContent: string
  ): Promise<SyncOutcome> {
    const diff = unifiedDiffPreview(row.base_content ?? "", resolvedContent);
    if (!apply) return { slug: row.slug, action: "would-update", diff };
    const client = this.client!;
    if (row.categories.length) payload.categories = await client.ensureTerms("category", row.categories);
    if (row.tags.length) payload.tags = await client.ensureTerms("tag", row.tags);
    const updated = await client.update(row.kind, row.wp_id!, payload);
    logOutbox(this.db, {
      local_id: row.local_id, slug: row.slug, op: "update", result: "applied",
      detail: "clean update (remote unchanged since last sync)", remote_backup: remoteContent,
    });
    markSynced(this.db, row.slug, {
      wp_id: row.wp_id!,
      base_content: field(updated.content, "rendered", "rendered"),
      base_modified_gmt: updated.modified_gmt,
      local_content: resolvedContent,
    });
    return { slug: row.slug, action: "update", diff };
  }

  private async mergeUpdate(
    row: ContentRow,
    payload: Record<string, unknown>,
    resolvedContent: string,
    remoteContent: string,
    apply: boolean,
    force: boolean
  ): Promise<SyncOutcome> {
    const localLines = resolvedContent.split("\n");
    const baseLines = (row.base_content ?? "").split("\n");
    const remoteLines = remoteContent.split("\n");
    const result = merge<string>(localLines, baseLines, remoteLines);

    if (result.conflict && !force) {
      logOutbox(this.db, {
        local_id: row.local_id, slug: row.slug, op: "update", result: "conflict",
        detail: "remote and local both changed in overlapping regions", remote_backup: remoteContent,
      });
      return {
        slug: row.slug,
        action: "conflict",
        mergedPreview: result.result.join("\n"),
        detail:
          `'${row.title}' was edited both locally and on the live site in overlapping regions. ` +
          "Resolve the <<<<<<< / ======= / >>>>>>> markers in the dashboard, or re-run push with force " +
          "to take the local copy as-is.",
      };
    }

    const mergedContent = force && result.conflict ? resolvedContent : result.result.join("\n");
    if (!apply) {
      return {
        slug: row.slug,
        action: "would-merge",
        diff: unifiedDiffPreview(remoteContent, mergedContent),
        detail: result.conflict ? "would resolve via --force (local copy wins)" : "auto-merges cleanly against the remote edit",
      };
    }

    const client = this.client!;
    payload.content = mergedContent;
    if (row.categories.length) payload.categories = await client.ensureTerms("category", row.categories);
    if (row.tags.length) payload.tags = await client.ensureTerms("tag", row.tags);
    const updated = await client.update(row.kind, row.wp_id!, payload);
    logOutbox(this.db, {
      local_id: row.local_id, slug: row.slug, op: "update", result: "applied",
      detail: result.conflict ? "forced (local wins)" : "auto-merged", remote_backup: remoteContent,
    });
    markSynced(this.db, row.slug, {
      wp_id: row.wp_id!,
      base_content: field(updated.content, "rendered", "rendered"),
      base_modified_gmt: updated.modified_gmt,
      local_content: mergedContent,
    });
    return { slug: row.slug, action: "merge", detail: result.conflict ? "forced (local wins)" : "auto-merged cleanly" };
  }

  // ------------------------------------------------------------- delete

  async delete(slug: string, confirmTitle: string, permanent = false): Promise<SyncOutcome> {
    if (!this.client) throw new Error("delete() requires a WPClient");
    const row = getContentBySlug(this.db, slug);
    if (!row || row.wp_id == null) {
      throw new Error(`No sync record for slug '${slug}' -- nothing to delete remotely.`);
    }
    const remote = await this.client.get(row.kind, row.wp_id);
    const actualTitle = field(remote.title);
    if (actualTitle !== confirmTitle) {
      throw new Error(`Title confirmation did not match, refusing to delete. The current remote title is: ${JSON.stringify(actualTitle)}`);
    }
    const remoteContent = field(remote.content, "rendered", "rendered");
    await this.client.del(row.kind, row.wp_id, permanent);
    logOutbox(this.db, {
      local_id: row.local_id, slug, op: "delete", result: "applied",
      detail: permanent ? "permanently deleted" : "moved to trash", remote_backup: remoteContent,
    });
    deleteContentRow(this.db, slug);
    return { slug, action: "delete", detail: permanent ? "permanently deleted" : "moved to trash" };
  }
}
