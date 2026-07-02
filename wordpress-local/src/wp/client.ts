import fs from "node:fs/promises";
import path from "node:path";
import { sha256 } from "../lib/hash.js";
import type { ContentType, LocalDocument, MediaManifestItem, WpPostLike } from "../types.js";

function buildAuthHeader(username: string, appPassword: string): string {
  const token = Buffer.from(`${username}:${appPassword}`).toString("base64");
  return `Basic ${token}`;
}

export class WordPressClient {
  private readonly authHeader: string;

  constructor(
    private readonly siteUrl: URL,
    username: string,
    appPassword: string
  ) {
    this.authHeader = buildAuthHeader(username, appPassword);
  }

  private async request<T>(pathName: string, init: RequestInit = {}): Promise<T> {
    const url = new URL(pathName, this.siteUrl);
    const res = await fetch(url, {
      ...init,
      headers: {
        Authorization: this.authHeader,
        "Content-Type": "application/json",
        ...(init.headers ?? {})
      }
    });

    if (!res.ok) {
      const text = await res.text();
      throw new Error(`WordPress API error ${res.status} on ${url.pathname}: ${text}`);
    }

    return (await res.json()) as T;
  }

  async getById(type: ContentType, id: number): Promise<WpPostLike> {
    return this.request<WpPostLike>(`/wp-json/wp/v2/${type === "post" ? "posts" : "pages"}/${id}`);
  }

  async list(type: ContentType, page: number, perPage: number): Promise<WpPostLike[]> {
    const endpoint = `/wp-json/wp/v2/${type === "post" ? "posts" : "pages"}?per_page=${perPage}&page=${page}&context=edit`;
    return this.request<WpPostLike[]>(endpoint);
  }

  async createOrUpdate(
    doc: LocalDocument,
    wpId: number | undefined
  ): Promise<WpPostLike & { content?: { rendered: string } }> {
    const endpointBase = `/wp-json/wp/v2/${doc.type === "post" ? "posts" : "pages"}`;
    const endpoint = wpId ? `${endpointBase}/${wpId}` : endpointBase;
    const method = wpId ? "POST" : "POST";

    const body = {
      title: doc.frontmatter.title,
      slug: doc.slug,
      status: doc.frontmatter.status ?? "draft",
      content: doc.htmlBody,
      excerpt: doc.frontmatter.excerpt ?? ""
    };

    return this.request<WpPostLike & { content?: { rendered: string } }>(endpoint, {
      method,
      body: JSON.stringify(body)
    });
  }

  async uploadMedia(localPath: string): Promise<MediaManifestItem> {
    const absolute = path.isAbsolute(localPath) ? localPath : path.join(process.cwd(), localPath);
    const bytes = await fs.readFile(absolute);
    const filename = path.basename(absolute);
    const hash = sha256(bytes);

    const url = new URL("/wp-json/wp/v2/media", this.siteUrl);
    const res = await fetch(url, {
      method: "POST",
      headers: {
        Authorization: this.authHeader,
        "Content-Disposition": `attachment; filename=\"${filename}\"`,
        "Content-Type": "application/octet-stream"
      },
      body: bytes
    });

    if (!res.ok) {
      const text = await res.text();
      throw new Error(`Media upload failed for ${localPath}: ${res.status} ${text}`);
    }

    const data = (await res.json()) as { id: number; source_url: string };

    return {
      relPath: localPath,
      hash,
      wpId: data.id,
      sourceUrl: data.source_url,
      uploadedAt: new Date().toISOString()
    };
  }
}
