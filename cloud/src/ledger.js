// Every counter the service keeps, in one place (AI.md 7.10: one Durable
// Object holds the counters and the concurrency limit). On Cloudflare this
// class runs inside that single Durable Object (src/worker.js); under Node
// (tests, load test) it runs in-process over MapStorage. All operations are
// serialized, so a check and its increment can never interleave.
//
// Stored (each entry {v, exp}; an hourly sweep deletes expired ones):
//   tok:<tid>                 {first: day first registered, est: established}  until the token expires (90 days)
//   dk:<day>                  that day's random HMAC key, deleted after 48 h  (AI.md 7.12)
//   q:<day>:<route>:<HMAC>    her requests that day, keyed by HMAC(day key, tid)
//   n:<day>:<HMAC>            registrations from one network that day, keyed by HMAC(day key, /24 or /64)
//   s:<day>:<pool>            dollars spent from a budget pool that day
//   a:<day>                   the 50 % alert was sent
// Nothing here holds her words, her IP address, or her install_id.

export const DAY_MS = 86_400_000;
export const dayOf = (ms) => new Date(ms).toISOString().slice(0, 10);
const endOf = (day, extraDays = 0) => Date.parse(day) + (1 + extraDays) * DAY_MS;
const b64 = (bytes) => btoa(String.fromCharCode(...new Uint8Array(bytes))).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");

export const POOLS = { est: 0.7, new: 0.2, ask: 0.1 }; // no pool borrows from another

export class MapStorage {
  constructor() { this.m = new Map(); }
  async get(k) { return this.m.get(k); }
  async put(k, v) { this.m.set(k, v); }
  async delete(k) { for (const x of [].concat(k)) this.m.delete(x); }
  async list() { return new Map(this.m); }
}

export class Ledger {
  // cfg: {budgetUsd, quotaEstablished, quotaNew, registerPerNet, maxInflight, establishDays}
  constructor(storage, cfg) {
    this.s = storage;
    this.cfg = cfg;
    this.chain = Promise.resolve();
    this.leases = new Map(); // in memory: a restart frees every slot, which is the safe direction
    this.keys = new Map();
  }

  serial(fn) {
    const p = this.chain.then(fn);
    this.chain = p.catch(() => {});
    return p;
  }

  async val(k, now) {
    const e = await this.s.get(k);
    return e && e.exp > now ? e.v : undefined;
  }

  async hmac(day, data, now) {
    let key = this.keys.get(day);
    if (!key) {
      let raw = await this.val("dk:" + day, now);
      if (!raw) {
        raw = b64(crypto.getRandomValues(new Uint8Array(32)));
        await this.s.put("dk:" + day, { v: raw, exp: Date.parse(day) + 2 * DAY_MS });
      }
      key = await crypto.subtle.importKey("raw", new TextEncoder().encode(raw), { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
      this.keys.clear(); // only today's key stays in memory
      this.keys.set(day, key);
    }
    return b64(await crypto.subtle.sign("HMAC", key, new TextEncoder().encode(data))).slice(0, 22);
  }

  register({ tid, net, now }) {
    return this.serial(async () => {
      const day = dayOf(now);
      const nk = `n:${day}:${await this.hmac(day, "net:" + net, now)}`;
      const n = (await this.val(nk, now)) ?? 0;
      if (n >= this.cfg.registerPerNet) return { ok: false };
      await this.s.put(nk, { v: n + 1, exp: endOf(day, 1) });
      const rec = (await this.val("tok:" + tid, now)) ?? { first: day, est: false };
      const exp = now + 90 * DAY_MS;
      await this.s.put("tok:" + tid, { v: rec, exp });
      return { ok: true, exp };
    });
  }

  // Before calling Claude: is the token alive, under its quota, is there money
  // in its pool and a free slot? Reserves the estimated cost and the slot.
  admit({ tid, route, estUsd, now }) {
    return this.serial(async () => {
      const day = dayOf(now);
      const rec = await this.val("tok:" + tid, now);
      if (!rec) return { status: 401 };
      const qk = `q:${day}:${route}:${await this.hmac(day, "tok:" + tid, now)}`;
      const used = (await this.val(qk, now)) ?? 0;
      if (used >= (rec.est ? this.cfg.quotaEstablished : this.cfg.quotaNew)) return { status: 429 };
      const pool = route === "ask" ? "ask" : rec.est ? "est" : "new";
      const sk = `s:${day}:${pool}`;
      const spent = (await this.val(sk, now)) ?? 0;
      if (spent + estUsd > this.cfg.budgetUsd * POOLS[pool]) return { status: 503, reason: "budget" };
      for (const [id, until] of this.leases) if (until <= now) this.leases.delete(id);
      if (this.leases.size >= this.cfg.maxInflight) return { status: 503, reason: "busy" };
      await this.s.put(qk, { v: used + 1, exp: endOf(day, 1) });
      await this.s.put(sk, { v: spent + estUsd, exp: endOf(day, 2) });
      const lease = crypto.randomUUID();
      this.leases.set(lease, now + 60_000);
      return { status: 200, lease, pool, qk, sk };
    });
  }

  // After the call: free the slot, replace the estimate with the real cost,
  // give the request back if Claude never answered, and mark the token
  // established once it is old enough and has received a wish.
  settle({ tid, lease, qk, sk, estUsd, usd, answered, gotWish, now }) {
    return this.serial(async () => {
      this.leases.delete(lease);
      const day = dayOf(now);
      await this.s.put(sk, { v: ((await this.val(sk, now)) ?? 0) - estUsd + usd, exp: endOf(day, 2) });
      if (!answered) {
        const used = (await this.val(qk, now)) ?? 0;
        if (used > 0) await this.s.put(qk, { v: used - 1, exp: endOf(day, 1) });
      }
      if (gotWish) {
        const e = await this.s.get("tok:" + tid);
        if (e && e.exp > now && !e.v.est && now - Date.parse(e.v.first) >= this.cfg.establishDays * DAY_MS) {
          await this.s.put("tok:" + tid, { v: { ...e.v, est: true }, exp: e.exp });
        }
      }
      let total = 0;
      for (const pool of Object.keys(POOLS)) total += (await this.val(`s:${day}:${pool}`, now)) ?? 0;
      if (total >= this.cfg.budgetUsd / 2 && !(await this.val("a:" + day, now))) {
        await this.s.put("a:" + day, { v: 1, exp: endOf(day, 1) });
        return { alert: true, total };
      }
      return { alert: false };
    });
  }

  // POST /v1/forget: the token and today's and yesterday's counters for it.
  forget({ tid, now }) {
    return this.serial(async () => {
      const gone = ["tok:" + tid];
      for (const day of [dayOf(now), dayOf(now - DAY_MS)]) {
        if (!(await this.val("dk:" + day, now))) continue;
        const h = await this.hmac(day, "tok:" + tid, now);
        gone.push(`q:${day}:wish:${h}`, `q:${day}:ask:${h}`);
      }
      await this.s.delete(gone);
      this.keys.clear();
    });
  }

  sweep(now) {
    return this.serial(async () => {
      const dead = [];
      for (const [k, e] of await this.s.list()) if (!(e && e.exp > now)) dead.push(k);
      for (let i = 0; i < dead.length; i += 128) await this.s.delete(dead.slice(i, i + 128));
      return dead.length;
    });
  }
}
