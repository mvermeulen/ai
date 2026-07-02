import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { test } from "node:test";
import {
  createContent,
  deleteContentRow,
  getContentBySlug,
  getMedia,
  listOutbox,
  logOutbox,
  markSynced,
  openDb,
  saveLocalContent,
  setMedia,
  upsertFromRemote,
} from "../src/db.js";

function tmpDbPath(): string {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "wpsync2-test-"));
  return path.join(dir, "site.db");
}

test("createContent + getContentBySlug round trip", () => {
  const db = openDb(tmpDbPath());
  createContent(db, { kind: "post", slug: "hello", title: "Hello", local_content: "<p>hi</p>" });
  const row = getContentBySlug(db, "hello");
  assert.ok(row);
  assert.equal(row!.kind, "post");
  assert.equal(row!.wp_id, null);
  assert.equal(row!.status, "draft");
  assert.deepEqual(row!.categories, []);
});

test("saveLocalContent updates fields without touching base_content", () => {
  const db = openDb(tmpDbPath());
  createContent(db, { kind: "post", slug: "hello", title: "Hello", local_content: "<p>v1</p>" });
  markSynced(db, "hello", { wp_id: 1, base_content: "<p>v1</p>", base_modified_gmt: "2026-01-01T00:00:00" });
  saveLocalContent(db, "hello", {
    title: "Hello",
    status: "draft",
    date: null,
    excerpt: "",
    categories: [],
    tags: [],
    local_content: "<p>v2 edited offline</p>",
  } as any);
  const row = getContentBySlug(db, "hello");
  assert.equal(row!.local_content, "<p>v2 edited offline</p>");
  assert.equal(row!.base_content, "<p>v1</p>"); // unchanged -- this is what makes 3-way merge possible
});

test("upsertFromRemote inserts new rows and fast-forwards existing ones", () => {
  const db = openDb(tmpDbPath());
  upsertFromRemote(db, {
    kind: "post", slug: "hello", title: "Hello", local_content: "<p>from remote</p>",
    wp_id: 5, base_modified_gmt: "2026-01-01T00:00:00",
  });
  let row = getContentBySlug(db, "hello");
  assert.equal(row!.wp_id, 5);
  assert.equal(row!.base_content, "<p>from remote</p>");

  upsertFromRemote(db, {
    kind: "post", slug: "hello", title: "Hello", local_content: "<p>updated remotely</p>",
    wp_id: 5, base_modified_gmt: "2026-02-01T00:00:00",
  });
  row = getContentBySlug(db, "hello");
  assert.equal(row!.local_content, "<p>updated remotely</p>");
  assert.equal(row!.base_modified_gmt, "2026-02-01T00:00:00");
});

test("deleteContentRow removes the row", () => {
  const db = openDb(tmpDbPath());
  createContent(db, { kind: "page", slug: "about", title: "About", local_content: "<p>x</p>" });
  deleteContentRow(db, "about");
  assert.equal(getContentBySlug(db, "about"), undefined);
});

test("media get/set round trip", () => {
  const db = openDb(tmpDbPath());
  assert.equal(getMedia(db, "media/a.jpg"), undefined);
  setMedia(db, { local_path: "media/a.jpg", file_hash: "abc", wp_id: 9, source_url: "https://x/a.jpg", uploaded_at: "now" });
  const row = getMedia(db, "media/a.jpg");
  assert.equal(row!.wp_id, 9);
  assert.equal(row!.source_url, "https://x/a.jpg");
});

test("outbox logs entries and lists them most-recent first", () => {
  const db = openDb(tmpDbPath());
  logOutbox(db, { local_id: "1", slug: "hello", op: "create", result: "applied", detail: "first" });
  logOutbox(db, { local_id: "1", slug: "hello", op: "update", result: "conflict", detail: "second", remote_backup: "<p>old</p>" });
  const entries = listOutbox(db, "hello");
  assert.equal(entries.length, 2);
  assert.equal(entries[0].detail, "second");
  assert.equal(entries[0].remote_backup, "<p>old</p>");
});
