// Cloudflare Workers entry: the portable handler (app.js) plus the one
// Durable Object that holds every counter (ledger.js).
import { DurableObject } from "cloudflare:workers";
import Anthropic from "@anthropic-ai/sdk";
import { createApp, config } from "./app.js";
import { Ledger as LedgerLogic } from "./ledger.js";
import { BuildJobLedger, GitHubBuilds, createBuildApi, createGitHubAppRequest } from "./builds.js";

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

export class BuildJobs extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.l = new BuildJobLedger(ctx.storage, {
      perNet: Number(env.BUILD_REQUESTS_PER_NET || 1),
      global: Number(env.BUILD_REQUESTS_GLOBAL || 5),
    });
    this.armed = false;
  }
  async arm() {
    if (!this.armed) {
      this.armed = true;
      if ((await this.ctx.storage.getAlarm()) === null) await this.ctx.storage.setAlarm(Date.now() + HOUR);
    }
  }
  async reserve(a) {
    await this.arm();
    return this.l.reserve(a);
  }
  async admit(a) {
    await this.arm();
    return this.l.admit(a);
  }
  claimPoll(a) { return this.l.claimPoll(a); }
  update(a) { return this.l.update(a); }
  delivery(a) { return this.l.delivery(a); }
  deliveryForRecipe(a) { return this.l.deliveryForRecipe(a); }
  beginDiscovery(a) { return this.l.beginDiscovery(a); }
  releaseDiscovery(a) { return this.l.releaseDiscovery(a); }
  adoptDelivery(a) { return this.l.adoptDelivery(a); }
  saveDelivery(a) { return this.l.saveDelivery(a); }
  abandonDelivery(a) { return this.l.abandonDelivery(a); }
  async alarm() {
    await this.l.sweep(Date.now());
    await this.ctx.storage.setAlarm(Date.now() + HOUR);
  }
}

export default {
  fetch(request, env) {
    let buildApi = null;
    if (env.BUILD_JOBS) {
      const githubRequest = env.BUILD_REQUESTS_ENABLED === "1" ? createGitHubAppRequest({
        appId: env.GITHUB_APP_ID,
        installationId: env.GITHUB_APP_INSTALLATION_ID,
        privateKey: env.GITHUB_APP_PRIVATE_KEY,
      }) : async () => { throw new Error("custom builds are disabled"); };
      buildApi = createBuildApi({
        env,
        jobs: env.BUILD_JOBS.get(env.BUILD_JOBS.idFromName("build-jobs")),
        github: new GitHubBuilds({ repository: env.BUILD_GITHUB_REPOSITORY, request: githubRequest }),
      });
    }
    return createApp({
      env,
      ledger: env.LEDGER.get(env.LEDGER.idFromName("ledger")),
      anthropic: new Anthropic({
        apiKey: env.ANTHROPIC_API_KEY,
        baseURL: env.ANTHROPIC_BASE_URL || undefined,
        // Only for a key that is not scoped to a workspace (docs/OWNER-SETUP.md 7).
        defaultHeaders: env.ANTHROPIC_WORKSPACE_ID ? { "anthropic-workspace-id": env.ANTHROPIC_WORKSPACE_ID } : undefined,
        maxRetries: 2,
        timeout: 8_000,
      }),
      buildApi,
    })(request);
  },
};
