import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { test } from "node:test";
import type { Config } from "../src/config.js";
import { createContent, getContentBySlug } from "../src/db.js";
import { openDb } from "../src/db.js";
import { SyncEngine } from "../src/sync.js";
import { WPClient } from "../src/wpClient.js";
import { FakeWordPress } from "./fakeWordPress.js";

function makeProject(): { config: Config; wp: FakeWordPress } {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "wpsync2-test-"));
  fs.mkdirSync(path.join(dir, "media"));
  const config: Config = {
    baseUrl: "https://example.test/site",
    username: "mike",
    appPassword: "dummy",
    dbPath: path.join(dir, "site.db"),
    mediaDir: path.join(dir, "media"),
    root: dir,
  };
  return { config, wp: new FakeWordPress() };
}

function makeEngine(config: Config, wp: FakeWordPress, withClient = true): SyncEngine {
  const db = openDb(config.dbPath);
  const client = withClient
    ? new WPClient({ baseUrl: config.baseUrl, username: config.username, appPassword: "dummy", fetchImpl: wp.fetch })
    : undefined;
  return new SyncEngine(db, config, client);
}

test("pull creates local rows from remote posts", async () => {
  const { config, wp } = makeProject();
  wp.seedPost({ slug: "hello-world", title: "Hello World", content: "<p>Hi there.</p>" });
  const engine = makeEngine(config, wp);

  const outcomes = await engine.pull();
  assert.equal(outcomes.length, 1);
  assert.equal(outcomes[0].action, "new");

  const row = engine.findLocal("hello-world");
  assert.ok(row);
  assert.equal(row!.title, "Hello World");
  assert.equal(row!.wp_id, 1);
});

test("pull skips a row with unsynced local edits", async () => {
  const { config, wp } = makeProject();
  const seeded = wp.seedPost({ slug: "hello-world", title: "Hello World", content: "<p>Hi there.</p>" });
  let engine = makeEngine(config, wp);
  await engine.pull();

  // Simulate an offline edit: local_content now differs from base_content.
  const db = openDb(config.dbPath);
  const { saveLocalContent } = await import("../src/db.js");
  saveLocalContent(db, "hello-world", {
    title: "Hello World", status: "publish", date: null, excerpt: "",
    categories: [], tags: [], local_content: "<p>Hi there, MY EDIT.</p>",
  } as any);

  wp.touch(seeded.id, new Date(Date.now() + 1000).toISOString());
  engine = makeEngine(config, wp);
  const outcomes = await engine.pull();
  assert.equal(outcomes[0].action, "skipped-local-changes");
  assert.equal(engine.findLocal("hello-world")!.local_content, "<p>Hi there, MY EDIT.</p>");
});

test("status reports new/modified/unchanged purely offline", async () => {
  const { config, wp } = makeProject();
  wp.seedPost({ slug: "hello-world", title: "Hello", content: "<p>Hi</p>" });
  const engine = makeEngine(config, wp);
  await engine.pull();
  assert.deepEqual(engine.status(), { "hello-world": "unchanged" });

  const db = openDb(config.dbPath);
  createContent(db, { kind: "post", slug: "brand-new", title: "Brand new", local_content: "<p>x</p>" });
  assert.deepEqual(engine.status(), { "hello-world": "unchanged", "brand-new": "new" });
});

test("push dry run performs no network mutations", async () => {
  const { config, wp } = makeProject();
  const db = openDb(config.dbPath);
  createContent(db, { kind: "post", slug: "new-post", title: "New Post", local_content: "<p>Hello from offline.</p>" });
  const engine = makeEngine(config, wp);

  const outcomes = await engine.push({ apply: false });
  assert.equal(outcomes[0].action, "would-create");
  assert.equal(wp.requestLog.filter((r) => r.method !== "GET").length, 0);
});

test("push creates a new post then reports unchanged on the next push", async () => {
  const { config, wp } = makeProject();
  const db = openDb(config.dbPath);
  createContent(db, { kind: "post", slug: "new-post", title: "New Post", local_content: "<p>Hello from offline.</p>" });
  const engine = makeEngine(config, wp);

  let outcomes = await engine.push({ apply: true });
  assert.equal(outcomes[0].action, "create");
  assert.equal(getContentBySlug(db, "new-post")!.wp_id, 1);

  outcomes = await engine.push({ apply: true });
  assert.equal(outcomes[0].action, "unchanged");
});

test("clean update: local edit pushes fine when remote hasn't drifted", async () => {
  const { config, wp } = makeProject();
  wp.seedPost({ slug: "hello-world", title: "Hello", content: "<p>original</p>" });
  const engine = makeEngine(config, wp);
  await engine.pull();

  const db = openDb(config.dbPath);
  const { saveLocalContent } = await import("../src/db.js");
  saveLocalContent(db, "hello-world", {
    title: "Hello", status: "publish", date: null, excerpt: "", categories: [], tags: [],
    local_content: "<p>original</p>\n\n<p>a new paragraph added offline</p>",
  } as any);

  const outcomes = await engine.push({ apply: true });
  assert.equal(outcomes[0].action, "update");
  assert.match(wp.posts.get(1)!.content, /a new paragraph added offline/);
});

test("non-overlapping remote + local edits merge automatically", async () => {
  const { config, wp } = makeProject();
  const seeded = wp.seedPost({ slug: "hello-world", title: "Hello", content: "First line.\nSecond line.\nThird line." });
  const engine = makeEngine(config, wp);
  await engine.pull();

  // Local edit: change the first line.
  const db = openDb(config.dbPath);
  const { saveLocalContent } = await import("../src/db.js");
  saveLocalContent(db, "hello-world", {
    title: "Hello", status: "publish", date: null, excerpt: "", categories: [], tags: [],
    local_content: "First line EDITED LOCALLY.\nSecond line.\nThird line.",
  } as any);

  // Remote edit (simulating a dashboard edit): change the third line, and advance modified_gmt.
  seeded.content = "First line.\nSecond line.\nThird line EDITED REMOTELY.";
  wp.touch(seeded.id, new Date(Date.now() + 5000).toISOString());

  const outcomes = await engine.push({ apply: true });
  assert.equal(outcomes[0].action, "merge");
  const finalContent = wp.posts.get(seeded.id)!.content;
  assert.match(finalContent, /First line EDITED LOCALLY/);
  assert.match(finalContent, /Third line EDITED REMOTELY/);
});

test("overlapping remote + local edits are a real conflict, not silently resolved", async () => {
  const { config, wp } = makeProject();
  const seeded = wp.seedPost({ slug: "hello-world", title: "Hello", content: "Original opening line." });
  const engine = makeEngine(config, wp);
  await engine.pull();

  const db = openDb(config.dbPath);
  const { saveLocalContent } = await import("../src/db.js");
  saveLocalContent(db, "hello-world", {
    title: "Hello", status: "publish", date: null, excerpt: "", categories: [], tags: [],
    local_content: "Local edit of the opening line.",
  } as any);

  seeded.content = "Remote edit of the opening line.";
  wp.touch(seeded.id, new Date(Date.now() + 5000).toISOString());

  let outcomes = await engine.push({ apply: true });
  assert.equal(outcomes[0].action, "conflict");
  assert.match(outcomes[0].mergedPreview ?? "", /<<<<<<</);
  // The conflicted push must not have written anything to the server.
  assert.equal(wp.posts.get(seeded.id)!.content, "Remote edit of the opening line.");

  // Forcing takes the local copy as-is.
  outcomes = await engine.push({ apply: true, force: true });
  assert.equal(outcomes[0].action, "merge");
  assert.equal(wp.posts.get(seeded.id)!.content, "Local edit of the opening line.");
});

test("delete requires the exact live title and backs up content first", async () => {
  const { config, wp } = makeProject();
  const seeded = wp.seedPost({ slug: "hello-world", title: "The Real Title", content: "<p>x</p>" });
  const engine = makeEngine(config, wp);
  await engine.pull();

  await assert.rejects(() => engine.delete("hello-world", "Wrong Title"), /Title confirmation did not match/);
  assert.ok(wp.posts.has(seeded.id));

  const outcome = await engine.delete("hello-world", "The Real Title");
  assert.equal(outcome.action, "delete");
  assert.ok(!wp.posts.has(seeded.id));
  assert.equal(engine.findLocal("hello-world"), undefined);
});

test("media referenced from content is uploaded once and reused on later pushes", async () => {
  const { config, wp } = makeProject();
  fs.writeFileSync(path.join(config.mediaDir, "photo.jpg"), "not-a-real-jpeg-but-fine-for-hashing");
  const db = openDb(config.dbPath);
  createContent(db, {
    kind: "post", slug: "with-photo", title: "With Photo",
    local_content: '<!-- wp:image --><figure class="wp-block-image"><img src="media/photo.jpg" alt=""/></figure><!-- /wp:image -->',
  });
  const engine = makeEngine(config, wp);

  await engine.push({ apply: true });
  assert.equal(wp.media.length, 1);
  const wpId = getContentBySlug(db, "with-photo")!.wp_id!;
  assert.match(wp.posts.get(wpId)!.content, /https:\/\/example\.test\/uploads\//);

  const { saveLocalContent } = await import("../src/db.js");
  saveLocalContent(db, "with-photo", {
    title: "With Photo", status: "draft", date: null, excerpt: "", categories: [], tags: [],
    local_content:
      '<!-- wp:image --><figure class="wp-block-image"><img src="media/photo.jpg" alt=""/></figure><!-- /wp:image -->' +
      "\n\n<!-- wp:paragraph --><p>one more line</p><!-- /wp:paragraph -->",
  } as any);
  await engine.push({ apply: true });
  assert.equal(wp.media.length, 1, "same file must not be re-uploaded");
});
