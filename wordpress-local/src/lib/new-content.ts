import fs from "node:fs/promises";
import path from "node:path";
import slugify from "slugify";
import { ensureWorkspaceLayout, PAGES_DIR, POSTS_DIR } from "./fs-utils.js";
import type { ContentType } from "../types.js";

function today(): string {
  return new Date().toISOString().slice(0, 10);
}

export async function createNewContent(type: ContentType, title: string): Promise<string> {
  await ensureWorkspaceLayout();

  const slug = slugify(title, { lower: true, strict: true, trim: true });
  const dir = type === "post" ? POSTS_DIR : PAGES_DIR;
  const filePath = path.join(dir, `${today()}-${slug}.md`);

  const template = [
    "---",
    `title: ${JSON.stringify(title)}`,
    `slug: ${slug}`,
    "status: draft",
    "tags: []",
    "categories: []",
    "strava_routes: []",
    "---",
    "",
    "Write your content here.",
    "",
    "![Optional image](media/replace-me.jpg)",
    ""
  ].join("\n");

  await fs.writeFile(filePath, template, { flag: "wx" });
  return filePath;
}
