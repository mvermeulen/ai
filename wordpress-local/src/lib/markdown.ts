import path from "node:path";
import fs from "node:fs/promises";
import matter from "gray-matter";
import { marked } from "marked";
import slugify from "slugify";
import { findMarkdownFiles, PAGES_DIR, POSTS_DIR } from "./fs-utils.js";
import { sha256 } from "./hash.js";
import type { ContentType, LocalDocument, LocalFrontmatter } from "../types.js";

marked.setOptions({
  gfm: true,
  breaks: true
});

const LOCAL_MEDIA_PATTERN = /!\[[^\]]*\]\(([^)]+)\)/g;

function normalizeSlug(input: string): string {
  return slugify(input, {
    lower: true,
    strict: true,
    trim: true
  });
}

function extractMediaRefs(markdown: string): string[] {
  const refs: string[] = [];
  const matches = markdown.matchAll(LOCAL_MEDIA_PATTERN);

  for (const match of matches) {
    const raw = match[1]?.trim();
    if (!raw) {
      continue;
    }

    if (raw.startsWith("http://") || raw.startsWith("https://") || raw.startsWith("data:")) {
      continue;
    }

    refs.push(raw);
  }

  return [...new Set(refs)];
}

function parseFrontmatter(data: unknown): LocalFrontmatter {
  const frontmatter = (data ?? {}) as Record<string, unknown>;

  return {
    title: String(frontmatter.title ?? "Untitled"),
    slug: frontmatter.slug ? String(frontmatter.slug) : undefined,
    status: frontmatter.status as LocalFrontmatter["status"],
    date: frontmatter.date ? String(frontmatter.date) : undefined,
    excerpt: frontmatter.excerpt ? String(frontmatter.excerpt) : undefined,
    tags: Array.isArray(frontmatter.tags) ? frontmatter.tags.map(String) : undefined,
    categories: Array.isArray(frontmatter.categories)
      ? frontmatter.categories.map(String)
      : undefined,
    strava_routes: Array.isArray(frontmatter.strava_routes)
      ? frontmatter.strava_routes.map(String)
      : undefined
  };
}

async function loadByType(type: ContentType, baseDir: string): Promise<LocalDocument[]> {
  const files = await findMarkdownFiles(baseDir);
  const docs: LocalDocument[] = [];

  for (const filePath of files) {
    const raw = await fs.readFile(filePath, "utf8");
    const parsed = matter(raw);
    const frontmatter = parseFrontmatter(parsed.data);
    const slug = normalizeSlug(frontmatter.slug ?? frontmatter.title);
    const mediaRefs = extractMediaRefs(parsed.content);

    let htmlBody = await marked.parse(parsed.content);
    if (Array.isArray(htmlBody)) {
      htmlBody = htmlBody.join("\n");
    }

    docs.push({
      type,
      filePath,
      relativePath: path.relative(process.cwd(), filePath),
      slug,
      frontmatter,
      markdownBody: parsed.content,
      htmlBody,
      localHash: sha256(raw),
      mediaRefs
    });
  }

  return docs;
}

export async function loadLocalContent(): Promise<LocalDocument[]> {
  const [posts, pages] = await Promise.all([
    loadByType("post", POSTS_DIR).catch(() => []),
    loadByType("page", PAGES_DIR).catch(() => [])
  ]);

  return [...posts, ...pages].sort((a, b) => a.relativePath.localeCompare(b.relativePath));
}
