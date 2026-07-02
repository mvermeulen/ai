import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { test } from "node:test";
import { createApp } from "../src/server.js";

async function withServer<T>(fn: (baseUrl: string, root: string) => Promise<T>): Promise<T> {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "wpsync2-server-test-"));
  const app = createApp(root);
  const server = app.listen(0);
  await new Promise<void>((resolve) => server.once("listening", resolve));
  const address = server.address();
  const port = typeof address === "object" && address ? address.port : 0;
  try {
    return await fn(`http://127.0.0.1:${port}`, root);
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
  }
}

test("unconfigured dashboard redirects everything to /setup", async () => {
  await withServer(async (base) => {
    const res = await fetch(`${base}/`, { redirect: "manual" });
    assert.equal(res.status, 302);
    assert.equal(res.headers.get("location"), "/setup");
  });
});

test("setup -> new post -> edit -> save -> dashboard listing flow", async () => {
  await withServer(async (base) => {
    let res = await fetch(`${base}/setup`, {
      method: "POST",
      headers: { "content-type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams({ baseUrl: "https://example.test/site", username: "mike" }),
      redirect: "manual",
    });
    assert.equal(res.status, 302);

    res = await fetch(`${base}/`);
    assert.equal(res.status, 200);
    assert.match(await res.text(), /No local content yet/);

    res = await fetch(`${base}/new/post`, {
      method: "POST",
      headers: { "content-type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams({ title: "Crossing into New Mexico" }),
      redirect: "manual",
    });
    assert.equal(res.status, 302);
    assert.equal(res.headers.get("location"), "/content/crossing-into-new-mexico");

    res = await fetch(`${base}/content/crossing-into-new-mexico`);
    assert.equal(res.status, 200);
    let body = await res.text();
    assert.match(body, /Start writing here/);

    res = await fetch(`${base}/content/crossing-into-new-mexico`, {
      method: "POST",
      headers: { "content-type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams({
        title: "Crossing into New Mexico",
        status: "draft",
        date: "",
        categories: "new-mexico",
        tags: "desert, wind",
        excerpt: "A windy crossing.",
        local_content: '<!-- wp:paragraph --><p class="wp-block-paragraph">Fighting headwinds all day.</p><!-- /wp:paragraph -->',
      }),
      redirect: "manual",
    });
    assert.equal(res.status, 302);

    res = await fetch(`${base}/content/crossing-into-new-mexico`);
    body = await res.text();
    assert.match(body, /Fighting headwinds all day/);

    res = await fetch(`${base}/`);
    body = await res.text();
    assert.match(body, /Crossing into New Mexico/);
    assert.match(body, /not yet synced/);
  });
});

test("GPX tool parses an uploaded file and renders a route summary block", async () => {
  await withServer(async (base) => {
    await fetch(`${base}/setup`, {
      method: "POST",
      headers: { "content-type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams({ baseUrl: "https://example.test/site", username: "mike" }),
    });

    const gpx = `<gpx><trk><trkseg>
      <trkpt lat="35.0" lon="-106.0"><ele>1000</ele></trkpt>
      <trkpt lat="35.01" lon="-106.0"><ele>1050</ele></trkpt>
    </trkseg></trk></gpx>`;
    const form = new FormData();
    form.append("gpx", new Blob([gpx], { type: "application/gpx+xml" }), "ride.gpx");

    const res = await fetch(`${base}/gpx`, { method: "POST", body: form });
    assert.equal(res.status, 200);
    const body = await res.text();
    assert.match(body, /wp:group/);
    assert.match(body, /km/);
  });
});

test("push review page reports a friendly error banner without credentials", async () => {
  await withServer(async (base) => {
    await fetch(`${base}/setup`, {
      method: "POST",
      headers: { "content-type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams({ baseUrl: "https://example.test/site", username: "mike" }),
    });
    const res = await fetch(`${base}/push`);
    const body = await res.text();
    assert.match(body, /No application password available/);
  });
});
