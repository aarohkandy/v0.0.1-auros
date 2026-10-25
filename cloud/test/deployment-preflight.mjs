// Read-only: ties the configured public route to this exact Worker deployment.
import { pathToFileURL } from 'node:url';
export function endpointInputs(origin, source, nonce, deployed) {
  for (const value of [origin, deployed]) {
    const url = new URL(value);
    if (url.protocol !== 'https:' || url.origin !== value || url.username || url.password) throw new Error('invalid exact HTTPS endpoint');
  }
  if (!/^https:\/\/auros-wishes\.[a-z0-9-]+\.workers\.dev$/.test(deployed)) throw new Error('invalid deployed Worker URL');
  if (!/^[a-f0-9]{40}$/.test(source) || !/^[a-f0-9]{64}$/.test(nonce)) throw new Error('invalid deployment identity');
  if (new URL(origin).hostname.endsWith('.workers.dev') && origin !== deployed) throw new Error('configured default endpoint differs from deployed Worker');
}
export async function checkDeployment(origin, source, nonce, deployed, fetcher = fetch) {
  endpointInputs(origin, source, nonce, deployed);
  const response = await fetcher(origin + '/v1/health', { redirect: 'error', cache: 'no-store', signal: AbortSignal.timeout(8000) });
  if (response.status !== 200) throw new Error('deployment health did not succeed');
  const reader = response.body?.getReader();
  if (!reader) throw new Error('deployment health body absent');
  const chunks = []; let length = 0;
  for (;;) {
    const {done, value} = await reader.read(); if (done) break;
    length += value.length;
    if (length > 4096) { await reader.cancel(); throw new Error('deployment health oversized'); }
    chunks.push(value);
  }
  const health = JSON.parse(Buffer.concat(chunks).toString('utf8'));
  if (health.ok !== true || health.deployment?.source_sha !== source || health.deployment?.nonce !== nonce) throw new Error('public endpoint does not identify the deployed Worker');
}
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  try {
    const args = process.argv.slice(2);
    if (args.length !== 4) throw new Error('expected endpoint, source, nonce and deployed URL');
    endpointInputs(...args);
    for (let attempt = 0; ; attempt++) {
      try { await checkDeployment(...args); break; }
      catch (error) { if (attempt === 5) throw error; await new Promise(resolve => setTimeout(resolve, 5000)); }
    }
    console.log('Public endpoint matches this Worker deployment.');
  } catch (error) { console.error('Deployment identity refused: ' + error.message); process.exitCode = 1; }
}
