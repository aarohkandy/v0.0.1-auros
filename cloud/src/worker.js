// Cloudflare Workers entry: the portable handler (app.js) plus the one
// Durable Object that holds every counter (ledger.js).
import { DurableObject } from "cloudflare:workers";
import Anthropic from "@anthropic-ai/sdk";
import { createApp, config } from "./app.js";
import { Ledger as LedgerLogic } from "./ledger.js";

const HOUR = 3_600_000;

export class Ledger extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.l = new LedgerLogic(ctx.storage, config(env));
    this.armed = false;
  }
  async arm() {
    if (this.armed) return;
    this.armed = true;
    if ((await this.ctx.storage.getAlarm()) === null) await this.ctx.storage.setAlarm(Date.now() + HOUR);
  }
  async register(a) { await this.arm(); return this.l.register(a); }
  async admit(a) { await this.arm(); return this.l.admit(a); }
  settle(a) { return this.l.settle(a); }
  forget(a) { return this.l.forget(a); }
  async alarm() {
    await this.l.sweep(Date.now());
    await this.ctx.storage.setAlarm(Date.now() + HOUR);
  }
}

export default {
  fetch(request, env) {
    return createApp({
      env,
      ledger: env.LEDGER.get(env.LEDGER.idFromName("ledger")),
      anthropic: new Anthropic({ apiKey: env.ANTHROPIC_API_KEY, baseURL: env.ANTHROPIC_BASE_URL || undefined, maxRetries: 2, timeout: 8_000 }),
    })(request);
  },
};
