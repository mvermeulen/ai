import path from "node:path";
import { assertCanApply } from "../config.js";
import { readManifest, writeManifest } from "../lib/fs-utils.js";
import { loadLocalContent } from "../lib/markdown.js";
import { collectStravaLinks } from "../lib/strava.js";
import { WordPressClient } from "../wp/client.js";
import type { AppConfig } from "../config.js";
import type { LocalDocument, SyncManifestItem, SyncOperation, SyncOptions } from "../types.js";

function manifestKey(doc: LocalDocument): string {
  return `${doc.type}:${doc.slug}`;
}

function replaceLocalMediaInMarkdown(markdown: string, replacements: Record<string, string>): string {
  let out = markdown;
  for (const [source, remote] of Object.entries(replacements)) {
    const escaped = source.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
    const pattern = new RegExp(`\\((?:\\./)?${escaped}\\)`, "g");
    out = out.replace(pattern, `(${remote})`);
  }
  return out;
}

async function uploadMediaAndPatchDoc(
  client: WordPressClient,
  doc: LocalDocument,
  mediaManifest: Record<string, { hash: string; sourceUrl: string; wpId: number; relPath: string; uploadedAt: string }>,
  apply: boolean
): Promise<LocalDocument> {
  if (doc.mediaRefs.length === 0) {
    return doc;
  }

  const replacements: Record<string, string> = {};

  for (const mediaRef of doc.mediaRefs) {
    const cleanRef = mediaRef.replace(/^\.\//, "");
    const existing = mediaManifest[cleanRef];

    if (existing) {
      replacements[mediaRef] = existing.sourceUrl;
      continue;
    }

    if (!apply) {
      continue;
    }

    const uploaded = await client.uploadMedia(cleanRef);
    mediaManifest[cleanRef] = uploaded;
    replacements[mediaRef] = uploaded.sourceUrl;
  }

  if (Object.keys(replacements).length === 0) {
    return doc;
  }

  const markdownPatched = replaceLocalMediaInMarkdown(doc.markdownBody, replacements);

  const dynamicStrava = collectStravaLinks(markdownPatched);
  const mergedRoutes = [...new Set([...(doc.frontmatter.strava_routes ?? []), ...dynamicStrava])];

  const htmlBody = markdownPatched
    .replace(/\n\n/g, "</p><p>")
    .replace(/^/, "<p>")
    .replace(/$/, "</p>");

  return {
    ...doc,
    markdownBody: markdownPatched,
    htmlBody,
    frontmatter: {
      ...doc.frontmatter,
      strava_routes: mergedRoutes
    }
  };
}

export async function planSync(
  config: AppConfig,
  options: SyncOptions
): Promise<{ operations: SyncOperation[]; applyCandidates: number }> {
  const manifest = await readManifest();
  const docs = await loadLocalContent();
  const operations: SyncOperation[] = [];

  for (const doc of docs) {
    const key = manifestKey(doc);
    const item = manifest.items[key];

    if (!item) {
      operations.push({
        kind: options.allowCreate ? "create" : "skip",
        type: doc.type,
        slug: doc.slug,
        reason: options.allowCreate ? "new local content" : "create disabled",
        filePath: doc.relativePath
      });
      continue;
    }

    if (item.lastSyncedLocalHash === doc.localHash) {
      operations.push({
        kind: "skip",
        type: doc.type,
        slug: doc.slug,
        reason: "unchanged locally",
        filePath: doc.relativePath,
        wpId: item.wpId
      });
      continue;
    }

    if (!options.allowUpdate) {
      operations.push({
        kind: "skip",
        type: doc.type,
        slug: doc.slug,
        reason: "update disabled",
        filePath: doc.relativePath,
        wpId: item.wpId
      });
      continue;
    }

    operations.push({
      kind: "update",
      type: doc.type,
      slug: doc.slug,
      reason: "local changes detected",
      filePath: doc.relativePath,
      wpId: item.wpId
    });
  }

  const applyCandidates = operations.filter((o) => o.kind === "create" || o.kind === "update").length;
  return { operations, applyCandidates };
}

export async function runSync(
  config: AppConfig,
  options: SyncOptions
): Promise<{ operations: SyncOperation[]; applied: number; conflicts: number }> {
  const manifest = await readManifest();
  const docs = await loadLocalContent();
  const operations: SyncOperation[] = [];

  const client = new WordPressClient(
    config.siteUrl,
    config.username ?? "",
    config.applicationPassword ?? ""
  );

  if (options.apply) {
    assertCanApply(config.siteUrl.hostname, config.allowedHosts, options.expectedHost);
  }

  const applyDocs = docs.filter((doc) => {
    const key = manifestKey(doc);
    const item = manifest.items[key];

    if (!item) {
      return options.allowCreate;
    }

    if (item.lastSyncedLocalHash === doc.localHash) {
      return false;
    }

    return options.allowUpdate;
  });

  if (options.apply && applyDocs.length > options.maxApplyOperations && !options.approveLargeSync) {
    throw new Error(
      `Refusing apply: ${applyDocs.length} operations exceeds cap of ${options.maxApplyOperations}. Use --approve-large-sync if intentional.`
    );
  }

  let applied = 0;
  let conflicts = 0;

  for (const originalDoc of docs) {
    const key = manifestKey(originalDoc);
    const entry = manifest.items[key];

    if (!entry && !options.allowCreate) {
      operations.push({
        kind: "skip",
        type: originalDoc.type,
        slug: originalDoc.slug,
        reason: "create disabled",
        filePath: originalDoc.relativePath
      });
      continue;
    }

    if (entry && entry.lastSyncedLocalHash === originalDoc.localHash) {
      operations.push({
        kind: "skip",
        type: originalDoc.type,
        slug: originalDoc.slug,
        reason: "unchanged locally",
        filePath: originalDoc.relativePath,
        wpId: entry.wpId
      });
      continue;
    }

    if (entry && !options.allowUpdate) {
      operations.push({
        kind: "skip",
        type: originalDoc.type,
        slug: originalDoc.slug,
        reason: "update disabled",
        filePath: originalDoc.relativePath,
        wpId: entry.wpId
      });
      continue;
    }

    if (!options.apply) {
      operations.push({
        kind: entry ? "update" : "create",
        type: originalDoc.type,
        slug: originalDoc.slug,
        reason: "dry-run",
        filePath: originalDoc.relativePath,
        wpId: entry?.wpId
      });
      continue;
    }

    if (!config.username || !config.applicationPassword) {
      throw new Error("WP_USERNAME and WP_APPLICATION_PASSWORD are required for --apply.");
    }

    if (entry) {
      const remote = await client.getById(originalDoc.type, entry.wpId);
      const remoteChanged = remote.modified_gmt !== entry.wpModifiedGmt;

      if (remoteChanged && !options.allowConflictOverride) {
        conflicts += 1;
        operations.push({
          kind: "conflict",
          type: originalDoc.type,
          slug: originalDoc.slug,
          reason: `remote modified since last sync (${entry.wpModifiedGmt} -> ${remote.modified_gmt})`,
          filePath: originalDoc.relativePath,
          wpId: entry.wpId
        });
        continue;
      }
    }

    const patchedDoc = await uploadMediaAndPatchDoc(client, originalDoc, manifest.media, options.apply);
    const saved = await client.createOrUpdate(patchedDoc, entry?.wpId);

    const nextItem: SyncManifestItem = {
      key,
      type: originalDoc.type,
      slug: originalDoc.slug,
      wpId: saved.id,
      wpModifiedGmt: saved.modified_gmt,
      lastSyncedLocalHash: originalDoc.localHash,
      lastSyncedAt: new Date().toISOString()
    };

    manifest.items[key] = nextItem;

    operations.push({
      kind: entry ? "update" : "create",
      type: originalDoc.type,
      slug: originalDoc.slug,
      reason: "applied",
      filePath: originalDoc.relativePath,
      wpId: saved.id
    });
    applied += 1;
  }

  if (options.apply) {
    await writeManifest(manifest);
  }

  return { operations, applied, conflicts };
}
