/**
 * Local SQLite mirror of the WordPress site -- the v2 "source of truth"
 * for offline work. Where v1 kept one Markdown file per post/page plus a
 * small JSON state file, v2 keeps everything in a single `.wpsync2/site.db`
 * and stores three content states per row deliberately: `local_content`
 * (what you're editing), `base_content` (the content as of the last
 * successful sync -- the three-way-merge common ancestor), and whatever
 * WordPress currently has (fetched live at push time, never cached here).
 * Keeping `base` distinct from `local` is what makes a real three-way
 * merge possible in sync.ts instead of the coarser "did anything change"
 * check v1 uses.
 */
import Database from "better-sqlite3";
import { randomUUID } from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import type { ContentRow, Kind, MediaRow, OutboxEntry, OutboxOp, OutboxResult, Status } from "./types.js";

export function openDb(dbPath: string): Database.Database {
  fs.mkdirSync(path.dirname(dbPath), { recursive: true });
  const db = new Database(dbPath);
  db.pragma("journal_mode = WAL");
  initSchema(db);
  return db;
}

function initSchema(db: Database.Database): void {
  db.exec(`
    CREATE TABLE IF NOT EXISTS content (
      local_id TEXT PRIMARY KEY,
      wp_id INTEGER,
      kind TEXT NOT NULL,
      slug TEXT NOT NULL UNIQUE,
      title TEXT NOT NULL,
      status TEXT NOT NULL DEFAULT 'draft',
      date TEXT,
      excerpt TEXT NOT NULL DEFAULT '',
      categories TEXT NOT NULL DEFAULT '[]',
      tags TEXT NOT NULL DEFAULT '[]',
      local_content TEXT NOT NULL DEFAULT '',
      base_content TEXT,
      base_modified_gmt TEXT,
      created_at TEXT NOT NULL,
      updated_at TEXT NOT NULL
    );

    CREATE TABLE IF NOT EXISTS media (
      local_path TEXT PRIMARY KEY,
      file_hash TEXT NOT NULL,
      wp_id INTEGER,
      source_url TEXT,
      uploaded_at TEXT
    );

    CREATE TABLE IF NOT EXISTS outbox (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      local_id TEXT NOT NULL,
      slug TEXT NOT NULL,
      op TEXT NOT NULL,
      created_at TEXT NOT NULL,
      result TEXT NOT NULL,
      detail TEXT NOT NULL DEFAULT '',
      remote_backup TEXT
    );
  `);
}

interface ContentRowRaw {
  local_id: string;
  wp_id: number | null;
  kind: string;
  slug: string;
  title: string;
  status: string;
  date: string | null;
  excerpt: string;
  categories: string;
  tags: string;
  local_content: string;
  base_content: string | null;
  base_modified_gmt: string | null;
  created_at: string;
  updated_at: string;
}

function fromRaw(row: ContentRowRaw): ContentRow {
  return {
    ...row,
    kind: row.kind as Kind,
    status: row.status as Status,
    categories: JSON.parse(row.categories),
    tags: JSON.parse(row.tags),
  };
}

export function listContent(db: Database.Database): ContentRow[] {
  const rows = db.prepare(`SELECT * FROM content ORDER BY slug`).all() as ContentRowRaw[];
  return rows.map(fromRaw);
}

export function getContentBySlug(db: Database.Database, slug: string): ContentRow | undefined {
  const row = db.prepare(`SELECT * FROM content WHERE slug = ?`).get(slug) as ContentRowRaw | undefined;
  return row ? fromRaw(row) : undefined;
}

export interface NewContentInput {
  kind: Kind;
  slug: string;
  title: string;
  status?: Status;
  date?: string | null;
  excerpt?: string;
  categories?: string[];
  tags?: string[];
  local_content: string;
}

export function createContent(db: Database.Database, input: NewContentInput): ContentRow {
  const now = new Date().toISOString();
  const local_id = randomUUID();
  db.prepare(
    `INSERT INTO content
      (local_id, wp_id, kind, slug, title, status, date, excerpt, categories, tags, local_content, base_content, base_modified_gmt, created_at, updated_at)
     VALUES (@local_id, NULL, @kind, @slug, @title, @status, @date, @excerpt, @categories, @tags, @local_content, NULL, NULL, @created_at, @updated_at)`
  ).run({
    local_id,
    kind: input.kind,
    slug: input.slug,
    title: input.title,
    status: input.status ?? "draft",
    date: input.date ?? null,
    excerpt: input.excerpt ?? "",
    categories: JSON.stringify(input.categories ?? []),
    tags: JSON.stringify(input.tags ?? []),
    local_content: input.local_content,
    created_at: now,
    updated_at: now,
  });
  return getContentBySlug(db, input.slug)!;
}

export function saveLocalContent(db: Database.Database, slug: string, fields: Partial<ContentRow>): void {
  const current = getContentBySlug(db, slug);
  if (!current) throw new Error(`No content row for slug '${slug}'`);
  const next = { ...current, ...fields, updated_at: new Date().toISOString() };
  db.prepare(
    `UPDATE content SET
      title=@title, status=@status, date=@date, excerpt=@excerpt,
      categories=@categories, tags=@tags, local_content=@local_content,
      updated_at=@updated_at
     WHERE slug=@slug`
  ).run({
    title: next.title,
    status: next.status,
    date: next.date,
    excerpt: next.excerpt,
    categories: JSON.stringify(next.categories),
    tags: JSON.stringify(next.tags),
    local_content: next.local_content,
    updated_at: next.updated_at,
    slug,
  });
}

export function markSynced(
  db: Database.Database,
  slug: string,
  fields: { wp_id: number; base_content: string; base_modified_gmt: string; local_content?: string }
): void {
  const current = getContentBySlug(db, slug);
  if (!current) throw new Error(`No content row for slug '${slug}'`);
  db.prepare(
    `UPDATE content SET wp_id=@wp_id, base_content=@base_content, base_modified_gmt=@base_modified_gmt,
       local_content=@local_content, updated_at=@updated_at WHERE slug=@slug`
  ).run({
    wp_id: fields.wp_id,
    base_content: fields.base_content,
    base_modified_gmt: fields.base_modified_gmt,
    local_content: fields.local_content ?? current.local_content,
    updated_at: new Date().toISOString(),
    slug,
  });
}

export function deleteContentRow(db: Database.Database, slug: string): void {
  db.prepare(`DELETE FROM content WHERE slug = ?`).run(slug);
}

export function upsertFromRemote(
  db: Database.Database,
  input: NewContentInput & { wp_id: number; base_modified_gmt: string }
): void {
  const existing = getContentBySlug(db, input.slug);
  const now = new Date().toISOString();
  if (existing) {
    db.prepare(
      `UPDATE content SET wp_id=@wp_id, title=@title, status=@status, date=@date, excerpt=@excerpt,
         categories=@categories, tags=@tags, local_content=@local_content, base_content=@base_content,
         base_modified_gmt=@base_modified_gmt, updated_at=@updated_at WHERE slug=@slug`
    ).run({
      wp_id: input.wp_id,
      title: input.title,
      status: input.status ?? "draft",
      date: input.date ?? null,
      excerpt: input.excerpt ?? "",
      categories: JSON.stringify(input.categories ?? []),
      tags: JSON.stringify(input.tags ?? []),
      local_content: input.local_content,
      base_content: input.local_content,
      base_modified_gmt: input.base_modified_gmt,
      updated_at: now,
      slug: input.slug,
    });
  } else {
    db.prepare(
      `INSERT INTO content
        (local_id, wp_id, kind, slug, title, status, date, excerpt, categories, tags, local_content, base_content, base_modified_gmt, created_at, updated_at)
       VALUES (@local_id, @wp_id, @kind, @slug, @title, @status, @date, @excerpt, @categories, @tags, @local_content, @base_content, @base_modified_gmt, @created_at, @updated_at)`
    ).run({
      local_id: randomUUID(),
      wp_id: input.wp_id,
      kind: input.kind,
      slug: input.slug,
      title: input.title,
      status: input.status ?? "draft",
      date: input.date ?? null,
      excerpt: input.excerpt ?? "",
      categories: JSON.stringify(input.categories ?? []),
      tags: JSON.stringify(input.tags ?? []),
      local_content: input.local_content,
      base_content: input.local_content,
      base_modified_gmt: input.base_modified_gmt,
      created_at: now,
      updated_at: now,
    });
  }
}

// -- media --------------------------------------------------------------

export function getMedia(db: Database.Database, localPath: string): MediaRow | undefined {
  return db.prepare(`SELECT * FROM media WHERE local_path = ?`).get(localPath) as MediaRow | undefined;
}

export function setMedia(db: Database.Database, row: MediaRow): void {
  db.prepare(
    `INSERT INTO media (local_path, file_hash, wp_id, source_url, uploaded_at)
     VALUES (@local_path, @file_hash, @wp_id, @source_url, @uploaded_at)
     ON CONFLICT(local_path) DO UPDATE SET
       file_hash=excluded.file_hash, wp_id=excluded.wp_id,
       source_url=excluded.source_url, uploaded_at=excluded.uploaded_at`
  ).run(row);
}

// -- outbox (audit log + implicit backup) --------------------------------

export function logOutbox(
  db: Database.Database,
  entry: { local_id: string; slug: string; op: OutboxOp; result: OutboxResult; detail: string; remote_backup?: string | null }
): void {
  db.prepare(
    `INSERT INTO outbox (local_id, slug, op, created_at, result, detail, remote_backup)
     VALUES (@local_id, @slug, @op, @created_at, @result, @detail, @remote_backup)`
  ).run({
    local_id: entry.local_id,
    slug: entry.slug,
    op: entry.op,
    created_at: new Date().toISOString(),
    result: entry.result,
    detail: entry.detail,
    remote_backup: entry.remote_backup ?? null,
  });
}

export function listOutbox(db: Database.Database, slug?: string): OutboxEntry[] {
  if (slug) {
    return db.prepare(`SELECT * FROM outbox WHERE slug = ? ORDER BY id DESC`).all(slug) as OutboxEntry[];
  }
  return db.prepare(`SELECT * FROM outbox ORDER BY id DESC LIMIT 200`).all() as OutboxEntry[];
}
