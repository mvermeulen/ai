/**
 * Thin wrapper around the WordPress core REST API (wp/v2), authenticated
 * with an Application Password. Deliberately dependency-free (uses Node's
 * built-in fetch) -- v1's Python client used `requests`; here there's
 * nothing to install at all for HTTP.
 */

export class WPError extends Error {}
export class WPAuthError extends WPError {}
export class WPNotFoundError extends WPError {}

export interface WPClientOptions {
  baseUrl: string;
  username: string;
  appPassword: string;
  fetchImpl?: typeof fetch;
}

type Kind = "post" | "page";

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

export class WPClient {
  private apiRoot: string;
  private authHeader: string;
  private fetchImpl: typeof fetch;

  constructor(private opts: WPClientOptions) {
    this.apiRoot = `${opts.baseUrl.replace(/\/$/, "")}/wp-json/wp/v2`;
    this.authHeader = "Basic " + Buffer.from(`${opts.username}:${opts.appPassword}`).toString("base64");
    this.fetchImpl = opts.fetchImpl ?? fetch;
  }

  private async request(
    method: string,
    pathOrUrl: string,
    opts: {
      params?: Record<string, string | number | boolean>;
      json?: unknown;
      body?: BodyInit;
      headers?: Record<string, string>;
    } = {}
  ): Promise<{ data: any; headers: Headers }> {
    let url = pathOrUrl.startsWith("http") ? pathOrUrl : `${this.apiRoot}${pathOrUrl}`;
    if (opts.params) {
      const usp = new URLSearchParams();
      for (const [k, v] of Object.entries(opts.params)) usp.set(k, String(v));
      url += (url.includes("?") ? "&" : "?") + usp.toString();
    }
    const headers: Record<string, string> = { Authorization: this.authHeader, ...(opts.headers ?? {}) };
    let body: BodyInit | undefined = opts.body;
    if (opts.json !== undefined) {
      headers["Content-Type"] = "application/json";
      body = JSON.stringify(opts.json);
    }

    let lastErr: unknown;
    for (let attempt = 0; attempt < 3; attempt++) {
      let res: Response;
      try {
        res = await this.fetchImpl(url, { method, headers, body });
      } catch (err) {
        lastErr = err;
        await sleep(300 * (attempt + 1));
        continue;
      }
      if (res.status === 401) {
        throw new WPAuthError("WordPress rejected the credentials (401). Check the application password.");
      }
      if (res.status === 403) {
        throw new WPAuthError(`WordPress refused access (403) to ${pathOrUrl}.`);
      }
      if (res.status === 404) {
        throw new WPNotFoundError(`Not found: ${pathOrUrl}`);
      }
      if (res.status >= 500 && attempt < 2) {
        await sleep(300 * (attempt + 1));
        continue;
      }
      if (!res.ok) {
        const text = await res.text();
        throw new WPError(`${method} ${pathOrUrl} failed: ${res.status} ${text.slice(0, 400)}`);
      }
      const data = await res.json();
      return { data, headers: res.headers };
    }
    throw new WPError(`Could not reach ${url} after 3 attempts: ${lastErr}`);
  }

  async whoami(): Promise<any> {
    return (await this.request("GET", "/users/me", { params: { context: "edit" } })).data;
  }

  async listAll(kind: Kind, status = "any"): Promise<any[]> {
    const p = kind === "post" ? "/posts" : "/pages";
    const out: any[] = [];
    let page = 1;
    for (;;) {
      const { data, headers } = await this.request("GET", p, {
        params: { status, context: "edit", per_page: 50, page },
      });
      out.push(...data);
      const totalPages = parseInt(headers.get("x-wp-totalpages") ?? "1", 10);
      if (page >= totalPages) break;
      page++;
    }
    return out;
  }

  async get(kind: Kind, wpId: number): Promise<any> {
    const p = kind === "post" ? `/posts/${wpId}` : `/pages/${wpId}`;
    return (await this.request("GET", p, { params: { context: "edit" } })).data;
  }

  async create(kind: Kind, payload: unknown): Promise<any> {
    const p = kind === "post" ? "/posts" : "/pages";
    return (await this.request("POST", p, { json: payload })).data;
  }

  async update(kind: Kind, wpId: number, payload: unknown): Promise<any> {
    const p = kind === "post" ? `/posts/${wpId}` : `/pages/${wpId}`;
    return (await this.request("POST", p, { json: payload })).data;
  }

  async del(kind: Kind, wpId: number, force = false): Promise<any> {
    const p = kind === "post" ? `/posts/${wpId}` : `/pages/${wpId}`;
    return (await this.request("DELETE", p, { params: { force } })).data;
  }

  async uploadMedia(filename: string, content: Buffer, mimeType: string): Promise<any> {
    return (
      await this.request("POST", "/media", {
        body: content as unknown as BodyInit,
        headers: { "Content-Disposition": `attachment; filename="${filename}"`, "Content-Type": mimeType },
      })
    ).data;
  }

  async ensureTerms(taxonomy: "category" | "tag", names: string[]): Promise<number[]> {
    const p = taxonomy === "category" ? "/categories" : "/tags";
    const ids: number[] = [];
    for (const name of names) {
      const { data: existing } = await this.request("GET", p, { params: { search: name } });
      const match = (existing as any[]).find((t) => t.name.toLowerCase() === name.toLowerCase());
      if (match) {
        ids.push(match.id);
      } else {
        const created = (await this.request("POST", p, { json: { name } })).data;
        ids.push(created.id);
      }
    }
    return ids;
  }
}
