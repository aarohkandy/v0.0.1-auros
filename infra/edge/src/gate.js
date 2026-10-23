// The launch gate (WP-74): how many people may download today, and whether
// downloads are paused. Pure logic over a storage with get/put, so it is tested
// without Cloudflare; worker.js puts it in one Durable Object, whose storage
// calls cannot interleave, so the count is exact.

export const DEFAULT_CAPS = [0, 25, 100, 300, 0]; // wave 0 = closed; 0 in the last place = no cap

export function parseCaps(s) {
  const caps = String(s ?? "").split(",").map((x) => Number(x.trim()));
  return caps.length >= 2 && caps.every((n) => Number.isInteger(n) && n >= 0) ? caps : DEFAULT_CAPS;
}

export const utcDay = (now) => new Date(now).toISOString().slice(0, 10);

export class Gate {
  constructor(storage, caps = DEFAULT_CAPS) { this.s = storage; this.caps = caps; }

  async load() {
    return (await this.s.get("gate")) ?? { wave: 0, day: "", count: 0, paused: false, reason: "", since: 0, history: {} };
  }

  capOf(g) { // 0 = closed, null = no cap
    if (g.wave === 0) return 0;
    const c = this.caps[g.wave] ?? 0;
    return c > 0 ? c : null;
  }

  view(g, now) {
    const day = utcDay(now);
    const cap = this.capOf(g);
    const today = g.day === day ? g.count : 0;
    return { wave: g.wave, cap, today, date: day, paused: g.paused, reason: g.paused ? g.reason : "",
             since: g.paused ? g.since : 0, open: !g.paused && g.wave > 0 && (cap === null || today < cap) };
  }

  async state(now) { return this.view(await this.load(), now); }

  // One person is about to be sent the installer: may she be? Counts her if so.
  async admit(now) {
    const g = await this.load();
    const day = utcDay(now);
    if (g.day !== day) { g.day = day; g.count = 0; }
    const v = this.view(g, now);
    if (g.paused) return { ok: false, why: "paused", state: v };
    if (g.wave === 0) return { ok: false, why: "closed", state: v };
    if (v.cap !== null && g.count >= v.cap) return { ok: false, why: "full", state: v };
    g.count++;
    g.history[day] = g.count;
    for (const d of Object.keys(g.history).sort().slice(0, -60)) delete g.history[d]; // 60 days
    await this.s.put("gate", g);
    return { ok: true, state: this.view(g, now) };
  }

  async pause(reason, now) {
    const g = await this.load();
    if (!g.paused) { g.paused = true; g.reason = String(reason || "paused").slice(0, 200); g.since = now; await this.s.put("gate", g); }
    return this.view(g, now);
  }

  async resume(now) { const g = await this.load(); g.paused = false; g.reason = ""; await this.s.put("gate", g); return this.view(g, now); }

  async setWave(n, now) {
    if (!Number.isInteger(n) || n < 0 || n >= this.caps.length) throw new RangeError(`wave must be 0 to ${this.caps.length - 1}`);
    const g = await this.load(); g.wave = n; await this.s.put("gate", g); return this.view(g, now);
  }

  async history() { return (await this.load()).history; }
  async putStatus(s) { await this.s.put("status", s); }
  async getStatus() { return (await this.s.get("status")) ?? null; }
}
