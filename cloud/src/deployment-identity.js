// Public deployment identity is separate from the unpublished smoke correlation.
export function deploymentIdentity(env) {
  return /^[a-f0-9]{40}$/.test(env.DEPLOYMENT_SOURCE_SHA || "") && /^[a-f0-9]{64}$/.test(env.DEPLOYMENT_NONCE || "")
    ? { source_sha: env.DEPLOYMENT_SOURCE_SHA, nonce: env.DEPLOYMENT_NONCE } : null;
}
export function smokeLogIdentity(env, request) {
  const identity = deploymentIdentity(env);
  const token = env.SMOKE_CORRELATION_TOKEN || "";
  if (!identity || !/^[a-f0-9]{64}$/.test(token) || request.headers.get("x-auros-smoke-correlation") !== token) return {};
  return { deployment_source: identity.source_sha, deployment_nonce: identity.nonce, smoke_correlation: token };
}
