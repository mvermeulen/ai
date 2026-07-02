#!/usr/bin/env node
/**
 * Thin CLI over the same SyncEngine the dashboard uses -- for scripting,
 * cron, or anyone who'd rather not open a browser. No CLI framework
 * dependency: Node's built-in `util.parseArgs` is enough for this surface.
 */
import fs from "node:fs";
import path from "node:path";
import readline from "node:readline";
import { parseArgs } from "node:util";
import { Config, ConfigError, loadConfig, requireCredentials, writeConfigTemplate } from "./config.js";
import { createContent, getContentBySlug, openDb } from "./db.js";
import { SyncEngine } from "./sync.js";
import type { Kind, Status } from "./types.js";
import { WPClient, WPError } from "./wpClient.js";

const ROOT = process.cwd();

function fail(message: string): never {
  console.error(message);
  process.exit(1);
}

function loadConfigOrFail(): Config {
  try {
    return loadConfig(ROOT);
  } catch (err) {
    if (err instanceof ConfigError) fail(err.message);
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

function slugify(text: string): string {
  return text.toLowerCase().trim().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "");
}

async function confirm(question: string): Promise<boolean> {
  const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
  const answer = await new Promise<string>((resolve) => rl.question(`${question} [y/N] `, resolve));
  rl.close();
  return /^y(es)?$/i.test(answer.trim());
}

async function main(): Promise<void> {
  const [cmd, ...rest] = process.argv.slice(2);

  switch (cmd) {
    case "init": {
      const { values } = parseArgs({ args: rest, options: { "base-url": { type: "string" }, username: { type: "string" } } });
      if (!values["base-url"] || !values.username) fail("Usage: wpsync2 init --base-url <url> --username <name>");
      const path = writeConfigTemplate(ROOT, String(values["base-url"]).replace(/\/$/, ""), String(values.username));
      console.log(`Wrote ${path}`);
      console.log(
        "\nNext: create an Application Password under WordPress admin -> Users -> Profile -> " +
          "Application Passwords, then:\n\n    export WPSYNC2_APP_PASSWORD='xxxx xxxx xxxx xxxx xxxx xxxx'\n\n" +
          "Run `wpsync2 test-connection` to confirm it works, or `npm run dashboard` for the browser UI."
      );
      break;
    }

    case "new": {
      const kind = rest[0] as Kind;
      const title = rest.slice(1).join(" ");
      if (kind !== "post" && kind !== "page") fail("Usage: wpsync2 new <post|page> \"Title\"");
      if (!title) fail("Usage: wpsync2 new <post|page> \"Title\"");
      const { values } = parseArgs({
        args: rest.slice(1),
        options: { slug: { type: "string" }, status: { type: "string" } },
        allowPositionals: true,
      });
      const config = loadConfigOrFail();
      const db = openDb(config.dbPath);
      const slug = (values.slug as string) || slugify(title);
      if (getContentBySlug(db, slug)) fail(`Slug '${slug}' already exists.`);
      createContent(db, {
        kind,
        slug,
        title,
        status: (values.status as Status) || "draft",
        local_content: '<!-- wp:paragraph -->\n<p class="wp-block-paragraph">Start writing here.</p>\n<!-- /wp:paragraph -->',
      });
      console.log(`Created local ${kind} '${slug}'. Edit it with the dashboard (npm run dashboard) or sqlite3 ${config.dbPath}.`);
      break;
    }

    case "status": {
      const config = loadConfigOrFail();
      const engine = buildEngine(config, false);
      const status = engine.status();
      const entries = Object.entries(status);
      if (entries.length === 0) {
        console.log('No local content yet. Try: wpsync2 new post "My First Post"');
        break;
      }
      for (const [slug, s] of entries.sort()) console.log(`${s.padStart(10)}  ${slug}`);
      break;
    }

    case "pull": {
      const config = loadConfigOrFail();
      const engine = buildEngine(config, true);
      const outcomes = await engine.pull();
      if (outcomes.length === 0) {
        console.log("Nothing to pull.");
        break;
      }
      for (const o of outcomes) console.log(`${o.action.padStart(22)}  ${o.slug}`);
      const skipped = outcomes.filter((o) => o.action === "skipped-local-changes");
      if (skipped.length) {
        console.log(`\n${skipped.length} file(s) skipped because they have unsynced local edits.`);
      }
      break;
    }

    case "push": {
      const { values, positionals } = parseArgs({
        args: rest,
        options: { apply: { type: "boolean", default: false }, force: { type: "boolean", default: false } },
        allowPositionals: true,
      });
      const config = loadConfigOrFail();
      const engine = buildEngine(config, true);
      const outcomes = await engine.push({ slugs: positionals.length ? positionals : undefined, apply: values.apply as boolean, force: values.force as boolean });
      const noteworthy = outcomes.filter((o) => o.action !== "unchanged");
      if (!noteworthy.length) {
        console.log("Nothing to push -- everything matches the last sync.");
        break;
      }
      for (const o of noteworthy) {
        console.log(`\n=== ${o.slug}: ${o.action} ===`);
        if (o.detail) console.log(o.detail);
        if (o.diff) console.log(o.diff);
        if (o.mergedPreview) console.log(o.mergedPreview);
      }
      console.log(values.apply ? "\nApplied." : "\nDry run only -- re-run with --apply to push these changes.");
      break;
    }

    case "delete": {
      const { values, positionals } = parseArgs({
        args: rest,
        options: {
          "confirm-title": { type: "string" },
          permanent: { type: "boolean", default: false },
          yes: { type: "boolean", default: false },
        },
        allowPositionals: true,
      });
      const slug = positionals[0];
      if (!slug || !values["confirm-title"]) fail('Usage: wpsync2 delete <slug> --confirm-title "Exact Title" [--permanent] [--yes]');
      if (!values.yes) {
        const verb = values.permanent ? "PERMANENTLY DELETE" : "move to trash";
        const ok = await confirm(`About to ${verb} '${slug}' on WordPress. Continue?`);
        if (!ok) {
          console.log("Aborted.");
          break;
        }
      }
      const config = loadConfigOrFail();
      const engine = buildEngine(config, true);
      const outcome = await engine.delete(slug, values["confirm-title"] as string, values.permanent as boolean);
      console.log(outcome.detail);
      break;
    }

    case "export": {
      // Borrows v1's idea: dump the SQLite mirror to plain files so the
      // writing itself can be git-tracked/reviewed, even though the DB
      // itself deliberately isn't (see README).
      const config = loadConfigOrFail();
      const engine = buildEngine(config, false);
      const outDir = path.join(config.root, "export");
      fs.mkdirSync(outDir, { recursive: true });
      const rows = engine.listLocal();
      for (const row of rows) {
        const header =
          `<!--\ntitle: ${row.title}\nslug: ${row.slug}\nkind: ${row.kind}\nstatus: ${row.status}\n` +
          `wp_id: ${row.wp_id ?? ""}\ndate: ${row.date ?? ""}\ncategories: ${row.categories.join(", ")}\n` +
          `tags: ${row.tags.join(", ")}\n-->\n\n`;
        fs.writeFileSync(path.join(outDir, `${row.slug}.html`), header + row.local_content + "\n");
      }
      console.log(`Exported ${rows.length} item(s) to ${outDir}/ -- safe to commit to git for a readable history of your writing.`);
      break;
    }

    case "test-connection": {
      const config = loadConfigOrFail();
      const engine = buildEngine(config, true);
      const me = await engine.whoami();
      console.log(`Connected as ${me.name}`);
      break;
    }

    default:
      console.log(
        "Usage: wpsync2 <command>\n\n" +
          "  init --base-url <url> --username <name>\n" +
          '  new <post|page> "Title" [--slug slug] [--status draft]\n' +
          "  status\n" +
          "  pull\n" +
          "  push [slugs...] [--apply] [--force]\n" +
          '  delete <slug> --confirm-title "Exact Title" [--permanent] [--yes]\n' +
          "  export\n" +
          "  test-connection"
      );
  }
}

main().catch((err) => {
  if (err instanceof WPError || err instanceof ConfigError) {
    fail(err.message);
  }
  console.error(err);
  process.exit(1);
});
