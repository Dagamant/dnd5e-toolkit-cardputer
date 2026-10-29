// Cardputer Link: pushes a compact, plain-text view of the game to a
// D&D 5E Toolkit Cardputer (its Foundry app). Runs only in a GM's browser.
// The formats are described in the firmware's src/core/foundry.h.

const ID = "cardputer-link";
const SEP = "\n\x1e\n";
// The Cardputer keeps each section in RAM; these match its limits.
const LIMITS = { actors: 6000, combat: 3000, chat: 6000, journals: 6000, page: 10000, sheet: 10000 };
const CHAT_COUNT = 40;

// ------------------------------------------------------------------ text helpers

// The Cardputer's font is plain ASCII.
const SUBST = {
  "‘": "'", "’": "'", "“": '"', "”": '"', "–": "-", "—": "-", "−": "-",
  "…": "...", "×": "x", " ": " ", "•": "*", "½": "1/2", "¼": "1/4", "¾": "3/4",
};
function ascii(s) {
  return String(s ?? "")
    .replace(/[‘’“”–—−…× •½¼¾]/g, c => SUBST[c])
    .normalize("NFKD").replace(/[̀-ͯ]/g, "")
    .replace(/[^\t\n\x20-\x7e]/g, "");
}
// One tab-separated field: no tabs, newlines or record separators.
const field = s => ascii(s).replace(/[\t\r\n\x1e]+/g, " ").trim();
const num = v => (Number.isFinite(+v) ? Math.trunc(+v) : 0);
const signed = v => (v >= 0 ? `+${v}` : `${v}`);

// Foundry and dnd5e inline markup: @UUID[...]{Label}, [[/r 1d20]]{Label}, &Reference[prone]
function unenrich(html) {
  return String(html ?? "")
    .replace(/@Embed\[[^\]]*\](\{[^}]*\})?/g, "")
    .replace(/@\w+\[([^\]]*)\](?:\{([^}]*)\})?/g, (_, path, label) => label ?? path.split(".").pop())
    .replace(/&Reference\[([^\]\s]+)[^\]]*\](?:\{([^}]*)\})?/g, (_, id, label) => label ?? id)
    .replace(/\[\[([^\]]*)\]\](?:\{([^}]*)\})?/g, (_, inner, label) => label ?? inner.replace(/^\/\w+\s*/, ""));
}

function plain(html) {
  const src = unenrich(html).replace(/<br\s*\/?>/gi, " ");
  const doc = new DOMParser().parseFromString(`<div>${src}</div>`, "text/html");
  return ascii(doc.body.textContent).replace(/\s+/g, " ").trim();
}

// HTML -> the Cardputer's text format: "# " headings, "* " bullets,
// "a | b | c" table rows (first row = heading), blank lines between paragraphs.
function htmlToText(html) {
  const src = unenrich(html).replace(/<br\s*\/?>/gi, " ");
  const doc = new DOMParser().parseFromString(`<div>${src}</div>`, "text/html");
  const out = [];
  const tidy = s => ascii(s).replace(/\s+/g, " ").trim();
  const inline = el => tidy(el.textContent);
  const BLOCK = /^(P|DIV|SECTION|ARTICLE|BLOCKQUOTE|ASIDE|FIGURE|FIGCAPTION|H[1-6]|UL|OL|LI|TABLE|HR|DL|IMG|SCRIPT|STYLE)$/;
  function block(el) {
    const tag = el.tagName;
    if (/^H[1-6]$/.test(tag)) { const t = inline(el); if (t) out.push(`# ${t}`); }
    else if (tag === "UL" || tag === "OL") {
      let n = 0;
      for (const li of el.querySelectorAll(":scope > li")) {
        const t = inline(li);
        if (t) out.push(tag === "OL" ? `* ${++n}. ${t}` : `* ${t}`);
      }
      out.push("");
    } else if (tag === "TABLE") {
      const rows = [...el.querySelectorAll("tr")]
        .map(tr => [...tr.querySelectorAll("th, td")].map(c => inline(c).replace(/\|/g, "/")))
        .filter(r => r.some(Boolean));
      const wide = rows.some(r => r.length > 1);
      for (const r of rows) out.push(wide ? r.join(" | ") + (r.length === 1 ? " |" : "") : r[0]);
      out.push("");
    } else if (tag === "DL") {
      for (const dt of el.querySelectorAll("dt")) {
        const dd = dt.nextElementSibling?.tagName === "DD" ? inline(dt.nextElementSibling) : "";
        out.push(`${inline(dt)}: ${dd}`);
      }
      out.push("");
    } else if (!/^(IMG|HR|SCRIPT|STYLE)$/.test(tag)) {
      walk(el);   // P, DIV, LI, ... : may hold more blocks
    }
  }
  // Runs of text and inline elements become one paragraph.
  function walk(node) {
    let buf = "";
    const flush = () => { const t = tidy(buf); if (t) out.push(t, ""); buf = ""; };
    for (const el of node.childNodes) {
      if (el.nodeType === Node.TEXT_NODE) buf += el.textContent;
      else if (el.nodeType !== Node.ELEMENT_NODE) continue;
      else if (BLOCK.test(el.tagName)) { flush(); block(el); }
      else buf += el.textContent;
    }
    flush();
  }
  walk(doc.body.firstElementChild);
  return out.join("\n").replace(/\n{3,}/g, "\n\n").trim();
}

function cut(text, limit, note = "\n(cut short)") {
  return text.length <= limit ? text : text.slice(0, limit - note.length) + note;
}

// ------------------------------------------------------------------ party

// True when the document is in the folder or one of its subfolders.
function inFolder(doc, folderId) {
  for (let f = doc.folder; f; f = f.folder) if (f.id === folderId) return true;
  return false;
}

function partyActors() {
  const folder = game.settings.get(ID, "actorFolder");
  if (folder) return game.actors.filter(a => inFolder(a, folder));
  try {   // dnd5e's primary party, when one is set
    const party = game.settings.get("dnd5e", "primaryParty")?.actor;
    const members = party?.system?.members?.map(m => m.actor).filter(a => a?.type === "character");
    if (members?.length) return members;
  } catch (e) { /* no party setting */ }
  const owned = game.actors.filter(a => a.type === "character" && a.hasPlayerOwner);
  return owned.length ? owned : game.actors.filter(a => a.type === "character");
}

function subtitle(a) {
  const classes = Object.values(a.classes ?? {}).map(c =>
    `${c.name} ${c.system?.levels ?? ""}${c.subclass ? ` (${c.subclass.name})` : ""}`.trim());
  const species = a.system?.details?.race?.name ?? a.items.find(i => i.type === "race")?.name;
  const level = a.system?.details?.level;
  return [level ? `Level ${level}` : "", classes.join(" / "), species].filter(Boolean).join(" - ");
}

function isPrepared(sp) {
  const s = sp.system;
  if (s.level === 0) return true;
  if (s.preparation) return s.preparation.mode !== "prepared" || !!s.preparation.prepared;   // dnd5e 4.x
  if (s.prepared !== undefined) return s.prepared > 0 || (s.method && s.method !== "spell"); // dnd5e 5.x
  return true;
}

function attackLine(item) {
  let hit = "", dmg = "";
  try {
    const act = item.system.activities?.find?.(x => x.type === "attack") ?? null;
    hit = act?.labels?.toHit ?? item.labels?.toHit ?? "";
    const parts = act?.labels?.damage ?? item.labels?.damages ?? [];
    dmg = parts.map(d => plain(d.label ?? `${d.formula ?? ""} ${d.damageType ?? ""}`)).filter(Boolean).join(", ");
  } catch (e) { /* data shape differs: name only */ }
  return `${field(item.name)} | ${field(hit)} | ${field(dmg)}`;
}

function actorBody(a) {
  const s = a.system, attr = s.attributes ?? {}, L = [];
  const statuses = (a.temporaryEffects ?? []).map(e => field(e.name)).filter(Boolean);
  const top = [];
  const walk = attr.movement?.walk;
  if (walk) top.push(`Speed ${walk} ${attr.movement.units ?? "ft"}`);
  if (attr.init?.total !== undefined) top.push(`Init ${signed(attr.init.total)}`);
  if (attr.prof) top.push(`Prof +${attr.prof}`);
  if (s.skills?.prc?.passive) top.push(`PP ${s.skills.prc.passive}`);
  if (top.length) L.push(top.join("  "));
  if (attr.inspiration) L.push("Heroic Inspiration");
  if (attr.exhaustion) L.push(`Exhaustion ${attr.exhaustion}`);
  if (attr.hp?.value === 0 && attr.death) L.push(`Death saves: ${attr.death.success} ok, ${attr.death.failure} failed`);
  if (statuses.length) L.push(`Conditions: ${[...new Set(statuses)].join(", ")}`);
  if (attr.concentration && a.statuses?.has?.("concentrating")) L.push("Concentrating");

  L.push("", "# Abilities", "Ability | Score | Mod | Save");
  for (const [k, ab] of Object.entries(s.abilities ?? {})) {
    const save = typeof ab.save === "object" ? ab.save?.value : ab.save;
    L.push(`${k.toUpperCase()} | ${ab.value} | ${signed(ab.mod)} | ${signed(num(save))}${ab.proficient ? "*" : ""}`);
  }

  L.push("", "# Skills", "Skill | Bonus");
  for (const [k, sk] of Object.entries(s.skills ?? {})) {
    const label = CONFIG.DND5E?.skills?.[k]?.label ?? k;
    const mark = sk.value >= 2 ? "**" : sk.value >= 1 ? "*" : sk.value > 0 ? "~" : "";
    L.push(`${field(game.i18n.localize(label))}${mark} | ${signed(num(sk.total))}`);
  }

  const weapons = a.items.filter(i => i.type === "weapon" && (i.system.equipped ?? true));
  if (weapons.length) {
    L.push("", "# Attacks", "Weapon | Hit | Damage");
    for (const w of weapons) L.push(attackLine(w));
  }

  const slots = [];
  for (let i = 1; i <= 9; i++) {
    const sl = s.spells?.[`spell${i}`];
    if (sl?.max) slots.push(`${i}: ${sl.value}/${sl.max}`);
  }
  if (s.spells?.pact?.max) slots.push(`Pact (${s.spells.pact.level}): ${s.spells.pact.value}/${s.spells.pact.max}`);
  const spells = a.items.filter(i => i.type === "spell" && isPrepared(i));
  if (slots.length || spells.length) {
    L.push("", "# Spells");
    if (slots.length) L.push(`Slots  ${slots.join("  ")}`);
    if (s.attributes?.spelldc) L.push(`Save DC ${s.attributes.spelldc}`);
    const byLevel = {};
    for (const sp of spells) (byLevel[sp.system.level] ??= []).push(field(sp.name));
    for (const lvl of Object.keys(byLevel).sort((x, y) => x - y))
      L.push(`* ${lvl === "0" ? "Cantrips" : `Level ${lvl}`}: ${byLevel[lvl].sort().join(", ")}`);
  }

  const feats = a.items.filter(i => i.type === "feat").map(i => field(i.name));
  if (feats.length) L.push("", "# Features", feats.join(", "));

  const gear = a.items.filter(i => ["equipment", "consumable", "tool", "loot", "container", "weapon"].includes(i.type));
  if (gear.length) {
    L.push("", "# Inventory", "Item | Qty");
    for (const g of gear.sort((x, y) => x.name.localeCompare(y.name)))
      L.push(`${field(g.name)}${g.system.equipped ? "*" : ""} | ${g.system.quantity ?? 1}`);
    const cur = s.currency ?? {};
    const coins = ["pp", "gp", "ep", "sp", "cp"].filter(c => cur[c]).map(c => `${cur[c]} ${c}`);
    if (coins.length) L.push("", `Coins: ${coins.join(", ")}`);
  }
  return L.join("\n");
}

// One line per actor; the Cardputer asks for a full sheet when it opens one.
function actorsText() {
  const lines = [];
  let size = 0;
  for (const a of partyActors()) {
    const hp = a.system.attributes?.hp ?? {};
    const line = [a.id, field(a.name), field(subtitle(a)), num(hp.value), num(hp.max), num(hp.temp),
                  num(a.system.attributes?.ac?.value)].join("\t");
    if (size + line.length + 1 > LIMITS.actors) break;
    size += line.length + 1;
    lines.push(line);
  }
  return lines.join("\n");
}

function sheetText(id) {
  const a = game.actors.get(id);
  if (!a) return `${id}\tNot found\nThat actor no longer exists.`;
  let body = "";
  try { body = actorBody(a); } catch (e) { console.warn(`${ID} | sheet for ${a.name}`, e); body = "(Couldn't read this sheet.)"; }
  return cut(`${a.id}\t${field(a.name)}\n${body.replace(/\x1e/g, "")}`, LIMITS.sheet);
}

// ------------------------------------------------------------------ combat

function combatText() {
  const c = game.combat;
  if (!c?.turns?.length) return "";
  const L = [[num(c.round), num(c.turn), field(c.scene?.name ?? "")].join("\t")];
  c.turns.forEach((cb, i) => {
    const a = cb.actor, hp = a?.system?.attributes?.hp;
    const flags = (c.started && i === c.turn ? "c" : "") + (cb.hidden ? "h" : "") + (cb.isDefeated ? "d" : "") +
                  (cb.hasPlayerOwner ? "p" : "");
    const conds = [...new Set((a?.temporaryEffects ?? []).map(e => field(e.name)).filter(Boolean))].join(", ");
    const init = cb.initiative === null || cb.initiative === undefined ? "" : String(Math.floor(cb.initiative));
    L.push([field(cb.name), init, hp ? num(hp.value) : "", hp ? num(hp.max) : "",
            num(a?.system?.attributes?.ac?.value), flags, conds].join("\t"));
  });
  return cut(L.join("\n"), LIMITS.combat, "");
}

// ------------------------------------------------------------------ chat

function rollInfo(rolls) {
  const r = rolls[0];
  const dice = rolls.flatMap(x => x.dice ?? []).map(d => `[${d.results.filter(v => v.active !== false).map(v => v.result).join(",")}]`).join(" ");
  const d20 = (r.dice ?? []).find(d => d.faces === 20);
  const nat = d20?.results?.find(v => v.active !== false)?.result;
  const crit = r.isCritical ?? nat === 20, fumble = r.isFumble ?? nat === 1;
  return {
    formula: field(r.formula),
    total: rolls.map(x => x.total).join(" / "),
    dice: field(dice),
    flags: (crit ? "c" : "") + (fumble ? "f" : ""),
  };
}

function chatText() {
  const msgs = game.messages.contents.slice(-CHAT_COUNT);
  const recs = [];
  let size = 0;
  for (let i = msgs.length - 1; i >= 0; i--) {   // newest first until the budget is used
    const m = msgs[i];
    const speaker = m.alias ?? m.speaker?.alias ?? m.author?.name ?? "";
    const t = new Date(m.timestamp);
    const time = `${String(t.getHours()).padStart(2, "0")}:${String(t.getMinutes()).padStart(2, "0")}`;
    let formula = "", total = "", dice = "", flags = "", text = "";
    if (m.rolls?.length) ({ formula, total, dice, flags } = rollInfo(m.rolls));
    else text = cut(plain(m.content), 300, "...");
    if (m.whisper?.length) flags += "w";
    if (m.blind) flags += "b";
    const rec = [m.id, time, field(speaker), field(plain(m.flavor)), formula, total, dice, flags].join("\t") +
                (text ? `\n${text}` : "");
    if (size + rec.length + 3 > LIMITS.chat) break;
    size += rec.length + 3;
    recs.unshift(rec);
  }
  return recs.join(SEP);
}

// ------------------------------------------------------------------ journals

function journalsText() {
  const L = [];
  const folder = game.settings.get(ID, "journalFolder");
  const entries = game.journal.contents.filter(e => !folder || inFolder(e, folder)).sort((a, b) =>
    (a.folder?.name ?? "").localeCompare(b.folder?.name ?? "") || a.name.localeCompare(b.name));
  let size = 0;
  for (const e of entries) {
    for (const p of e.pages.contents.filter(p => p.type === "text").sort((x, y) => x.sort - y.sort)) {
      const line = [e.id, p.id, field(e.folder?.name ?? ""), field(e.name), field(p.name)].join("\t");
      if (size + line.length + 1 > LIMITS.journals) return L.join("\n");
      size += line.length + 1;
      L.push(line);
    }
  }
  return L.join("\n");
}

function pageText(entryId, pageId) {
  const p = game.journal.get(entryId)?.pages.get(pageId);
  if (!p) return `${entryId}\t${pageId}\tNot found\nThat page no longer exists.`;
  const body = htmlToText(p.text?.content ?? "");
  return cut(`${entryId}\t${pageId}\t${field(p.name)}\n${body.replace(/\x1e/g, "")}`, LIMITS.page, "\n(cut short - page too long)");
}

// ------------------------------------------------------------------ transport

const BUILD = { actors: actorsText, combat: combatText, chat: chatText, journals: journalsText,
                sheet: () => sheetText(link.sheetId) };

class Link {
  constructor() {
    this.timers = {};
    this.ok = null;        // null unknown, true connected, false lost
    this.pollTimer = null;
    this.sheetId = null;   // the sheet open on the Cardputer: kept up to date
  }

  get base() { return String(game.settings.get(ID, "url") || "/cardputer-link").replace(/\/+$/, ""); }
  get key() { return String(game.settings.get(ID, "key") || "").trim(); }
  get enabled() { return game.user.isGM && game.settings.get(ID, "enabled") && !!this.key; }

  async request(path, init = {}) {
    return fetch(`${this.base}/${path}`, {
      ...init, cache: "no-store",
      headers: { "X-Link-Key": this.key, "Content-Type": "text/plain" },
      signal: AbortSignal.timeout(5000),
    });
  }

  schedule(section, delay = 500) {
    if (!this.enabled || !this.ok || (section === "sheet" && !this.sheetId)) return;
    clearTimeout(this.timers[section]);
    this.timers[section] = setTimeout(() => this.push(section, BUILD[section]()), delay);
  }

  async push(section, text) {
    try {
      const r = await this.request(`push?s=${section}`, { method: "POST", body: cut(text, LIMITS[section], "") });
      if (!r.ok) console.warn(`${ID} | push ${section}: ${r.status} ${await r.text()}`);
    } catch (e) { /* the next poll notices */ }
  }

  async pushAll() {
    for (const s of Object.keys(BUILD)) if (s !== "sheet" || this.sheetId) await this.push(s, BUILD[s]());
  }

  restart() {
    clearTimeout(this.pollTimer);
    this.ok = null;
    this.poll();
  }

  async poll() {
    clearTimeout(this.pollTimer);
    if (!this.enabled) return;
    let next = 2000;
    try {
      const r = await this.request("poll");
      if (r.status === 401) throw new Error("wrong key");
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      const reqs = (await r.text()).split("\n").filter(Boolean);
      for (const req of reqs) {   // the sheet first, so a full resend includes it
        const [what, a, b] = req.split("\t");
        if (what === "sheet") this.sheetId = a;
        else if (what === "page") await this.push("page", pageText(a, b));
      }
      if (this.ok !== true) {
        if (this.ok === false) ui.notifications.info("Cardputer Link: connected again.");
        this.ok = true;
        await this.pushAll();
      } else if (reqs.includes("full")) {
        await this.pushAll();
      } else if (reqs.some(q => q.startsWith("sheet\t"))) {
        await this.push("sheet", BUILD.sheet());
      }
    } catch (e) {
      next = 5000;
      if (this.ok !== false) {
        const why = e.message === "wrong key" ? "the link key is wrong" : "can't reach the Cardputer";
        ui.notifications.warn(`Cardputer Link: ${why}. Is web access on? (${this.base})`);
        console.warn(`${ID} |`, e);
        if (this.base.startsWith("http:") && location.protocol === "https:")
          console.warn(`${ID} | Foundry is on https, so the browser blocks a plain-http address. Use the proxy path (see the module notes).`);
      }
      this.ok = false;
    }
    this.pollTimer = setTimeout(() => this.poll(), next);
  }
}

const link = new Link();

Hooks.once("init", () => {
  const restart = () => game.ready && link.restart();
  game.settings.register(ID, "enabled", {
    name: "Send to the Cardputer", hint: "Only in GM browsers. Turn off in browsers that shouldn't send.",
    scope: "client", config: true, type: Boolean, default: true, onChange: restart,
  });
  game.settings.register(ID, "url", {
    name: "Cardputer address",
    hint: "The path your reverse proxy forwards to the Cardputer's /api/foundry/ (default /cardputer-link). " +
          "If Foundry runs on plain http, the device directly also works: http://192.168.x.x/api/foundry",
    scope: "client", config: true, type: String, default: "/cardputer-link", onChange: restart,
  });
  game.settings.register(ID, "key", {
    name: "Link key", hint: "Shown big in the Cardputer's Foundry app (K) and on its web page (WiFi). Dashes are optional.",
    scope: "client", config: true, type: String, default: "", onChange: restart,
  });
});

// "Parent / Child" names of every folder of one document type.
function folderChoices(type) {
  const path = f => (f.folder ? `${path(f.folder)} / ` : "") + f.name;
  const choices = { "": "All (default)" };
  game.folders.filter(f => f.type === type).map(f => [f.id, path(f)])
    .sort((a, b) => a[1].localeCompare(b[1])).forEach(([id, name]) => choices[id] = name);
  return choices;
}

Hooks.once("setup", () => {
  game.settings.register(ID, "actorFolder", {
    name: "Party folder",
    hint: "Characters shown on the Cardputer's Party tab: everything in this folder and its subfolders. " +
          "All = the dnd5e primary party, or else the player-owned characters. (Reload to see new folders.)",
    scope: "world", config: true, type: String, choices: folderChoices("Actor"), default: "",
    onChange: () => link.schedule("actors", 0),
  });
  game.settings.register(ID, "journalFolder", {
    name: "Journal folder",
    hint: "Journals listed on the Cardputer: this folder and its subfolders. (Reload to see new folders.)",
    scope: "world", config: true, type: String, choices: folderChoices("JournalEntry"), default: "",
    onChange: () => link.schedule("journals", 0),
  });
});

Hooks.once("ready", () => {
  if (!game.user.isGM) return;
  const on = (hooks, section, delay) => hooks.forEach(h => Hooks.on(h, () => link.schedule(section, delay)));
  const actorHooks = ["updateActor", "createItem", "updateItem", "deleteItem",
                      "createActiveEffect", "updateActiveEffect", "deleteActiveEffect"];
  on(actorHooks, "actors");
  on(actorHooks, "combat");
  on(actorHooks, "sheet");
  on(["createCombat", "updateCombat", "deleteCombat", "createCombatant", "updateCombatant", "deleteCombatant",
      "canvasReady"], "combat", 250);
  on(["createChatMessage", "updateChatMessage", "deleteChatMessage"], "chat", 250);
  on(["createFolder", "updateFolder", "deleteFolder"], "actors", 1000);
  on(["createJournalEntry", "updateJournalEntry", "deleteJournalEntry", "createFolder", "updateFolder", "deleteFolder",
      "createJournalEntryPage", "updateJournalEntryPage", "deleteJournalEntryPage"], "journals", 1000);
  link.poll();
});
