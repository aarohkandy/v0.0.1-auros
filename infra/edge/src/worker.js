// Cloudflare glue: the router (edge.js) plus the one Durable Object that holds
// the gate (gate.js): the day's download count, the wave, the pause, the last
// monitor result. One object, so the count is exact.
import { DurableObject } from "cloudflare:workers";
import { handle } from "./edge.js";
import { Gate, parseCaps } from "./gate.js";

export class Ops extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.gate = new Gate(ctx.storage, parseCaps(env.WAVE_CAPS));
  }
  admit(now) { return this.gate.admit(now); }
  state(now) { return this.gate.state(now); }
  pause(reason, now) { return this.gate.pause(reason, now); }
  resume(now) { return this.gate.resume(now); }
  setWave(n, now) { return this.gate.setWave(n, now); }
  async putStatus(s) { await this.ctx.storage.put("status", s); }
  async getStatus() { return (await this.ctx.storage.get("status")) ?? null; }
}

export default {
  fetch(request, env) {
    const stub = env.OPS.get(env.OPS.idFromName("ops"));
    return handle(request, env, stub, fetch, Date.now());
  },
};
