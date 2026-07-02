import fs from "node:fs/promises";
import path from "node:path";
import type { SyncManifest } from "../types.js";

export const WORKSPACE_DIR = process.cwd();
export const CONTENT_DIR = path.join(WORKSPACE_DIR, "content");
export const POSTS_DIR = path.join(CONTENT_DIR, "posts");
export const PAGES_DIR = path.join(CONTENT_DIR, "pages");
export const MEDIA_DIR = path.join(WORKSPACE_DIR, "media");
export const STATE_DIR = path.join(WORKSPACE_DIR, ".state");
export const MANIFEST_FILE = path.join(STATE_DIR, "manifest.json");

export async function ensureWorkspaceLayout(): Promise<void> {
  await fs.mkdir(POSTS_DIR, { recursive: true });
  await fs.mkdir(PAGES_DIR, { recursive: true });
  await fs.mkdir(MEDIA_DIR, { recursive: true });
  await fs.mkdir(STATE_DIR, { recursive: true });
}

export async function readManifest(): Promise<SyncManifest> {
  try {
    const raw = await fs.readFile(MANIFEST_FILE, "utf8");
    const parsed = JSON.parse(raw) as SyncManifest;
    if (parsed.version !== 1 || !parsed.items || !parsed.media) {
      throw new Error("Invalid manifest structure");
    }
    return parsed;
  } catch (err) {
    if ((err as NodeJS.ErrnoException).code === "ENOENT") {
      return { version: 1, items: {}, media: {} };
    }
    throw err;
  }
}

export async function writeManifest(manifest: SyncManifest): Promise<void> {
  await fs.mkdir(STATE_DIR, { recursive: true });
  const output = JSON.stringify(manifest, null, 2);
  await fs.writeFile(MANIFEST_FILE, `${output}\n`, "utf8");
}

export async function findMarkdownFiles(dir: string): Promise<string[]> {
  const entries = await fs.readdir(dir, { withFileTypes: true });
  const out: string[] = [];

  for (const entry of entries) {
    const fullPath = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      out.push(...(await findMarkdownFiles(fullPath)));
      continue;
    }

    if (entry.isFile() && entry.name.toLowerCase().endsWith(".md")) {
      out.push(fullPath);
    }
  }

  return out;
}

export async function fileExists(filePath: string): Promise<boolean> {
  try {
    await fs.access(filePath);
    return true;
  } catch {
    return false;
  }
}
