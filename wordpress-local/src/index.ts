#!/usr/bin/env node
import "dotenv/config";
import { Command } from "commander";
import { loadConfig } from "./config.js";
import { ensureWorkspaceLayout, readManifest, writeManifest } from "./lib/fs-utils.js";
import { createNewContent } from "./lib/new-content.js";
import { writeRemoteAsLocal } from "./lib/remote-to-local.js";
import { planSync, runSync } from "./sync/engine.js";
import { WordPressClient } from "./wp/client.js";
import type { ContentType } from "./types.js";

const program = new Command();

program
  .name("wp-local-sync")
  .description("Offline-first content authoring and safe sync for WordPress")
  .version("0.1.0");

program
  .command("init")
  .description("Create local folders and state files")
  .action(async () => {
    await ensureWorkspaceLayout();
    const manifest = await readManifest();
    await writeManifest(manifest);
    console.log("Workspace initialized.");
    console.log("- content/posts");
    console.log("- content/pages");
    console.log("- media");
    console.log("- .state/manifest.json");
  });

program
  .command("new")
  .description("Create a new local draft")
  .requiredOption("-t, --type <type>", "post | page")
  .requiredOption("--title <title>", "Content title")
  .action(async (opts: { type: ContentType; title: string }) => {
    if (opts.type !== "post" && opts.type !== "page") {
      throw new Error("--type must be post or page");
    }
    const filePath = await createNewContent(opts.type, opts.title);
    console.log(`Created ${filePath}`);
  });

program
  .command("plan")
  .description("Show what sync would do without writing remote changes")
  .option("--allow-create", "Include new local items in the plan")
  .option("--allow-update", "Include updates for already-synced items")
  .action(async (opts: { allowCreate?: boolean; allowUpdate?: boolean }) => {
    const config = loadConfig();

    const plan = await planSync(config, {
      apply: false,
      allowCreate: !!opts.allowCreate,
      allowUpdate: !!opts.allowUpdate,
      allowConflictOverride: false,
      maxApplyOperations: config.defaultMaxApplyOperations,
      approveLargeSync: false
    });

    for (const op of plan.operations) {
      console.log(
        `${op.kind.toUpperCase().padEnd(8)} ${op.type.padEnd(4)} ${op.slug.padEnd(24)} ${op.reason} (${op.filePath})`
      );
    }

    console.log(`\nPotential apply operations: ${plan.applyCandidates}`);
  });

program
  .command("pull")
  .description("Pull remote posts/pages into local markdown for offline editing")
  .option("-t, --type <type>", "post | page | all", "all")
  .option("--limit <n>", "Max number to import per type", "20")
  .option("--force", "Overwrite existing local files")
  .action(async (opts: { type: "post" | "page" | "all"; limit: string; force?: boolean }) => {
    const config = loadConfig();

    if (!config.username || !config.applicationPassword) {
      throw new Error("WP_USERNAME and WP_APPLICATION_PASSWORD are required for pull.");
    }

    const client = new WordPressClient(config.siteUrl, config.username, config.applicationPassword);
    const perType = Number(opts.limit);

    if (!Number.isFinite(perType) || perType <= 0) {
      throw new Error("--limit must be a positive integer");
    }

    const targetTypes: ContentType[] =
      opts.type === "all" ? ["post", "page"] : [opts.type as ContentType];

    for (const type of targetTypes) {
      let written = 0;
      let skipped = 0;
      let page = 1;

      while (written + skipped < perType) {
        const remaining = perType - (written + skipped);
        const batch = await client.list(type, page, Math.min(remaining, 20));
        if (batch.length === 0) {
          break;
        }

        for (const item of batch) {
          const detail = await client.getById(type, item.id);
          const result = await writeRemoteAsLocal(type, detail as any, !!opts.force);
          if (result.written) {
            written += 1;
            console.log(`WROTE ${result.path}`);
          } else {
            skipped += 1;
            console.log(`SKIP  ${result.path} (${result.skippedReason})`);
          }
          if (written + skipped >= perType) {
            break;
          }
        }

        page += 1;
      }

      console.log(`${type}: imported ${written}, skipped ${skipped}`);
    }
  });

program
  .command("sync")
  .description("Sync local content to WordPress with safe defaults")
  .option("--apply", "Apply changes to remote site (default is dry-run)")
  .option("--allow-create", "Permit creating new posts/pages")
  .option("--allow-update", "Permit updating existing posts/pages")
  .option("--allow-conflicts", "Override remote-modified conflict protection")
  .option("--max-ops <n>", "Apply operation cap before requiring explicit approval")
  .option("--approve-large-sync", "Approve apply operation count that exceeds cap")
  .option("--confirm-host <host>", "Require exact host match for apply")
  .action(
    async (opts: {
      apply?: boolean;
      allowCreate?: boolean;
      allowUpdate?: boolean;
      allowConflicts?: boolean;
      maxOps?: string;
      approveLargeSync?: boolean;
      confirmHost?: string;
    }) => {
      const config = loadConfig();
      const maxOps = Number(opts.maxOps ?? config.defaultMaxApplyOperations);

      const result = await runSync(config, {
        apply: !!opts.apply,
        allowCreate: !!opts.allowCreate,
        allowUpdate: !!opts.allowUpdate,
        allowConflictOverride: !!opts.allowConflicts,
        maxApplyOperations: Number.isFinite(maxOps) ? maxOps : config.defaultMaxApplyOperations,
        approveLargeSync: !!opts.approveLargeSync,
        expectedHost: opts.confirmHost
      });

      for (const op of result.operations) {
        const idSuffix = op.wpId ? ` #${op.wpId}` : "";
        console.log(
          `${op.kind.toUpperCase().padEnd(8)} ${op.type.padEnd(4)} ${op.slug.padEnd(24)} ${op.reason}${idSuffix}`
        );
      }

      console.log(`\nApplied: ${result.applied}`);
      console.log(`Conflicts: ${result.conflicts}`);

      if (!opts.apply) {
        console.log("Dry-run only. Re-run with --apply to push changes.");
      }
    }
  );

program.parseAsync(process.argv).catch((err) => {
  console.error(`Error: ${err.message}`);
  process.exitCode = 1;
});
