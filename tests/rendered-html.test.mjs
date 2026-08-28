import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

async function render() {
  const workerUrl = new URL("../dist/server/index.js", import.meta.url);
  workerUrl.searchParams.set("test", `${process.pid}-${Date.now()}`);
  const { default: worker } = await import(workerUrl.href);

  return worker.fetch(
    new Request("http://localhost/", {
      headers: { accept: "text/html" },
    }),
    {
      ASSETS: {
        fetch: async () => new Response("Not found", { status: 404 }),
      },
    },
    {
      waitUntil() {},
      passThroughOnException() {},
    },
  );
}

test("server-renders the Pico 2 W macro controller", async () => {
  const response = await render();
  assert.equal(response.status, 200);
  assert.match(response.headers.get("content-type") ?? "", /^text\/html\b/i);

  const html = await response.text();
  assert.match(html, /<title>Pico 2W Macro Controller<\/title>/i);
  assert.match(html, /Pico <span>2W<\/span> Macro/);
  assert.match(html, /Mouse Turbo/);
  assert.match(html, /Space Turbo/);
  assert.match(html, /AFK Walk/);
  assert.match(html, /STOP ALL MACROS/);
  assert.match(html, /aria-label="Pico 2W macro controller"/);
  assert.doesNotMatch(html, /Your site is taking shape|Building your site/);
});

test("keeps the Pico command routes and release safety controls in source", async () => {
  const [page, layout, css] = await Promise.all([
    readFile(new URL("../app/page.tsx", import.meta.url), "utf8"),
    readFile(new URL("../app/layout.tsx", import.meta.url), "utf8"),
    readFile(new URL("../app/globals.css", import.meta.url), "utf8"),
  ]);

  assert.match(page, /send\(`\/\$\{control\}\/\$\{isActive \? "start" : "stop"\}`\)/);
  assert.match(page, /send\(`\/\$\{command\}`\)/);
  assert.match(page, /send\("\/walk\/toggle"\)/);
  assert.match(page, /send\("\/stop"\)/);
  assert.match(page, /"ubuntu" \| "cmd" \| "altf4"/);
  assert.match(page, /window\.addEventListener\("blur", releaseAll\)/);
  assert.match(page, /document\.addEventListener\("visibilitychange", releaseAll\)/);
  assert.match(page, /cache:\s*"no-store"/);
  assert.match(layout, /title:\s*"Pico 2W Macro Controller"/);
  assert.match(css, /prefers-reduced-motion/);
});
