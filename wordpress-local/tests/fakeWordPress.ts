/**
 * An in-memory fake of just enough of the WordPress REST API (wp/v2) to
 * exercise SyncEngine end to end, injected via WPClient's `fetchImpl`
 * option. Deliberately not a request-by-request mock registry (v1's
 * Python tests used requests_mock that way) -- modeling actual server
 * state (posts advance their own modified_gmt on update, etc.) makes the
 * conflict/merge tests below far more realistic.
 */
interface FakeItem {
  id: number;
  slug: string;
  title: string;
  content: string;
  status: string;
  date: string;
  modified_gmt: string;
  categories: number[];
  tags: number[];
  excerpt: string;
}

function toWire(item: FakeItem) {
  return {
    id: item.id,
    slug: item.slug,
    status: item.status,
    date: item.date,
    modified_gmt: item.modified_gmt,
    title: { raw: item.title, rendered: item.title },
    content: { raw: item.content, rendered: item.content, protected: false },
    excerpt: { raw: item.excerpt, rendered: item.excerpt },
    categories: item.categories,
    tags: item.tags,
  };
}

export class FakeWordPress {
  posts = new Map<number, FakeItem>();
  pages = new Map<number, FakeItem>();
  media: Array<{ id: number; source_url: string }> = [];
  categories: Array<{ id: number; name: string }> = [];
  tags: Array<{ id: number; name: string }> = [];
  requestLog: Array<{ method: string; path: string }> = [];
  private nextId = 1;

  seedPost(fields: Partial<FakeItem> & { slug: string }): FakeItem {
    const id = this.nextId++;
    const now = new Date().toISOString();
    const item: FakeItem = {
      id,
      title: "",
      content: "",
      status: "publish",
      date: now,
      modified_gmt: now,
      categories: [],
      tags: [],
      excerpt: "",
      ...fields,
    };
    this.posts.set(id, item);
    return item;
  }

  touch(id: number, when: string): void {
    const item = this.posts.get(id) ?? this.pages.get(id);
    if (item) item.modified_gmt = when;
  }

  fetch: typeof fetch = async (input, init) => {
    const url = new URL(String(input));
    const method = (init?.method ?? "GET").toUpperCase();
    this.requestLog.push({ method, path: url.pathname });

    const respond = (status: number, body: unknown, headers?: Record<string, string>) =>
      new Response(JSON.stringify(body), { status, headers: { "content-type": "application/json", ...headers } });

    const segs = url.pathname.split("/").filter(Boolean);
    const v2 = segs.indexOf("v2");
    const resource = segs[v2 + 1];
    const idSeg = segs[v2 + 2];
    const id = idSeg !== undefined ? parseInt(idSeg, 10) : undefined;

    if (resource === "users" && idSeg === "me") {
      return respond(200, { name: "Mike", capabilities: { edit_posts: true } });
    }

    if (resource === "media" && method === "POST") {
      const item = { id: this.nextId++, source_url: `https://example.test/uploads/upload-${this.media.length + 1}.jpg` };
      this.media.push(item);
      return respond(200, item);
    }

    if (resource === "categories" || resource === "tags") {
      const list = resource === "categories" ? this.categories : this.tags;
      if (method === "GET") {
        const search = url.searchParams.get("search")?.toLowerCase();
        return respond(200, search ? list.filter((t) => t.name.toLowerCase() === search) : list);
      }
      if (method === "POST") {
        const body = JSON.parse(String(init?.body));
        const item = { id: this.nextId++, name: body.name };
        list.push(item);
        return respond(200, item);
      }
    }

    const store = resource === "posts" ? this.posts : resource === "pages" ? this.pages : undefined;
    if (!store) return respond(404, { message: "unknown resource" });

    if (method === "GET" && id === undefined) {
      return respond(200, [...store.values()].map(toWire), { "x-wp-totalpages": "1" });
    }
    if (method === "GET" && id !== undefined) {
      const item = store.get(id);
      return item ? respond(200, toWire(item)) : respond(404, { message: "not found" });
    }
    if (method === "POST" && id === undefined) {
      const body = JSON.parse(String(init?.body));
      const newId = this.nextId++;
      const now = new Date().toISOString();
      const item: FakeItem = {
        id: newId,
        slug: body.slug,
        title: body.title,
        content: body.content,
        status: body.status ?? "draft",
        date: body.date ?? now,
        modified_gmt: now,
        categories: body.categories ?? [],
        tags: body.tags ?? [],
        excerpt: body.excerpt ?? "",
      };
      store.set(newId, item);
      return respond(200, toWire(item));
    }
    if (method === "POST" && id !== undefined) {
      const item = store.get(id);
      if (!item) return respond(404, { message: "not found" });
      const body = JSON.parse(String(init?.body));
      Object.assign(item, body, { modified_gmt: new Date().toISOString() });
      return respond(200, toWire(item));
    }
    if (method === "DELETE" && id !== undefined) {
      const item = store.get(id);
      if (!item) return respond(404, { message: "not found" });
      store.delete(id);
      return respond(200, toWire(item));
    }
    return respond(404, { message: "unhandled" });
  };
}
