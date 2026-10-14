// Copyright 2026 Daher Alfawares
// SPDX-License-Identifier: AGPL-3.0-only

// A private board in a public project, read the way a browser reads it, under the
// rules: its people see all of it; someone outside the project, and someone signed
// out, see none of it — not its title, its issues, their comments or history, their
// files, nor anything on the project's public pages that would name them.
//
//   the backend on 8081 and the emulators on 8080, built from this checkout
//   node tools/private-boards.mjs

const api = "http://localhost:8081/api";
const firestore = "http://localhost:8080/v1/projects/demo-uione/databases/(default)/documents";
const auth = "http://localhost:9099/identitytoolkit.googleapis.com/v1";

let step = 0;
let failed = 0;
function check(ok, what) {
  console.log(`${ok ? "ok" : "FAIL"} ${++step}  ${what}`);
  if (!ok) failed++;
}

async function person(name) {
  const response = await fetch(`${auth}/accounts:signUp?key=demo`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ email: `${name}-${Date.now()}@example.com`, password: "password", displayName: name, returnSecureToken: true }),
  });
  return (await response.json()).idToken;
}

async function run(token, command, fields) {
  const [ns, entity, action] = command.split("::");
  const response = await fetch(`${api}/${ns}/${entity}/${action}`, {
    method: "POST",
    headers: { "Content-Type": "application/json", Authorization: `Bearer ${token}` },
    body: JSON.stringify(fields),
  });
  const said = await response.json().catch(() => ({}));
  if (!response.ok) throw Object.assign(new Error(`${command}: ${JSON.stringify(said)}`), { said, status: response.status });
  return said;
}

// A view's document as the rules let someone read it: its fields, or the status refused with.
async function read(token, view) {
  const headers = token ? { Authorization: `Bearer ${token}` } : {};
  const response = await fetch(`${firestore}/views/${encodeURIComponent(view)}`, { headers });
  return { status: response.status, text: await response.text() };
}

// Waits until a view is built with what's wanted in it, as the owner reads it.
async function until(token, view, has, ms = 15000) {
  const end = Date.now() + ms;
  for (;;) {
    const got = await read(token, view);
    if (got.status === 200 && got.text.includes(has)) return got;
    if (Date.now() > end) throw new Error(`${view} never held ${has}: ${got.status} ${got.text.slice(0, 200)}`);
    await new Promise((resolve) => setTimeout(resolve, 200));
  }
}

const maya = await person("Maya");
const eve = await person("Eve");
const project = `acme${Date.now() % 100000}`;
await run(maya, "projects::project::create", { slug: project, name: "Acme", visibility: "public", workflow: "basic" });
const board = (await run(maya, "projects::board::create", { project, title: "Secret plans", workflow: "basic", visibility: "private" })).id;
const hidden = (await run(maya, "projects::issue::create", { project, board, title: "Buy the competitor" })).id;
await run(maya, "projects::comment::create", { issue: hidden, body: "Quietly, before Friday" });
const shown = (await run(maya, "projects::issue::create", { project, board: `${project}-main`, title: "Fix the login page" })).id;

// What the project's people see.
await until(maya, `projects::board_page:${board}`, "Buy the competitor");
await until(maya, `projects::issue_page:${hidden}`, "Quietly, before Friday");
await until(maya, `projects::private_boards:${project}`, "Secret plans");
check(true, "the project's people read the private board, its issue and its comment, and find it among the project's private boards");

// What someone outside, and someone signed out, see of it.
for (const [who, token] of [
  ["someone outside the project", eve],
  ["someone signed out", ""],
]) {
  for (const view of [`projects::board_page:${board}`, `projects::issue_page:${hidden}`, `projects::private_boards:${project}`]) {
    const got = await read(token, view);
    check(got.status === 403, `${who} can't read ${view.split(":")[2]}: ${got.status}`);
  }
  const page = await read(token, `projects::project_page:${project}`);
  check(page.status === 200 && page.text.includes("Fix the login page"), `${who} reads the public project's page`);
  check(!/Secret plans|Buy the competitor|Quietly/.test(page.text), `${who} finds nothing of the private board on it`);
}

// A public issue can't be linked to the private one, so its page never names it.
try {
  await run(maya, "projects::link::create", { project, from: shown, to: hidden, relation: "blocks" });
  check(false, "a public issue is refused a link to a private board's issue");
} catch (e) {
  check(e.status >= 400, `a public issue is refused a link to a private board's issue: ${JSON.stringify(e.said)}`);
}

// Made public, the board opens; made private again, it closes, its issue's page with it.
await run(maya, "projects::board::update", { id: board, visibility: "public" });
await until("", `projects::issue_page:${hidden}`, "Buy the competitor");
check(true, "the board made public opens its issue to everyone");
await run(maya, "projects::board::update", { id: board, visibility: "private" });
const end = Date.now() + 15000;
let closed = false;
while (!closed && Date.now() < end) {
  closed = (await read("", `projects::issue_page:${hidden}`)).status === 403;
  if (!closed) await new Promise((resolve) => setTimeout(resolve, 200));
}
check(closed, "made private again, its issue's page closes");

console.log(failed ? `${failed} failed` : "all passed");
process.exitCode = failed ? 1 : 0;
