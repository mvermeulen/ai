import fs from "node:fs/promises";
import path from "node:path";
import matter from "gray-matter";
import { PAGES_DIR, POSTS_DIR, ensureWorkspaceLayout, fileExists } from "./fs-utils.js";
import type { ContentType, WpPostLike } from "../types.js";

function stripHtml(input: string): string {
  return input
    .replace(/<\/?p>/g, "")
    .replace(/<br\s*\/?\s*>/gi, "\n")
    .replace(/<[^>]+>/g, "")
    .replace(/&nbsp;/g, " ")
    .trim();
}

export async function writeRemoteAsLocal(
  type: ContentType,
  item: WpPostLike & { content?: { rendered: string }; title?: { rendered: string } },
  force: boolean
): Promise<{ written: boolean; path: string; skippedReason?: string }> {
  await ensureWorkspaceLayout();
  const base = type === "post" ? POSTS_DIR : PAGES_DIR;
  const filePath = path.join(base, `${item.slug}.md`);

  if (!force && (await fileExists(filePath))) {
    return { written: false, path: filePath, skippedReason: "exists" };
  }

  const content = stripHtml(item.content?.rendered ?? "");
  const title = stripHtml(item.title?.rendered ?? item.slug);

  const body = matter.stringify(content, {
    title,
    slug: item.slug,
    status: item.status,
    imported_modified_gmt: item.modified_gmt
  });

  await fs.writeFile(filePath, body, "utf8");
  return { written: true, path: filePath };
}
