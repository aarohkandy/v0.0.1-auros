import test from 'node:test';
import assert from 'node:assert/strict';
import {deploymentIdentity, smokeLogIdentity} from '../src/deployment-identity.js';
import {checkDeployment, endpointInputs} from './deployment-preflight.mjs';
import {makeApp} from './helpers.js';
const source = 'a'.repeat(40), nonce = 'b'.repeat(64), token = 'c'.repeat(64);
const env = {DEPLOYMENT_SOURCE_SHA:source, DEPLOYMENT_NONCE:nonce, SMOKE_CORRELATION_TOKEN:token};
const deployed = 'https://auros-wishes.owner.workers.dev';
test('public identity omits correlation and rejects malformed values', () => {
  assert.deepEqual(deploymentIdentity(env), {source_sha:source,nonce});
  for (const invalid of ['', 'x', source+'\n']) assert.equal(deploymentIdentity({...env, DEPLOYMENT_SOURCE_SHA:invalid}),null);
});
test('only exact private correlation adds bounded CI log identity', () => {
  for (const value of ['',nonce,token+'0','x'.repeat(10000)]) assert.deepEqual(smokeLogIdentity(env,new Request(deployed,{headers:{'x-auros-smoke-correlation':value}})),{});
  const value=smokeLogIdentity(env,new Request(deployed,{headers:{'x-auros-smoke-correlation':token}}));
  assert.deepEqual(value,{deployment_source:source,deployment_nonce:nonce,smoke_correlation:token});
});
test('real handler exposes public deployment but never private correlation in health',async()=>{
  const app=makeApp({env});
  const response=await app.handler(new Request(deployed+'/v1/health',{headers:{'x-auros-smoke-correlation':token}}));
  const body=await response.json(); assert.deepEqual(body.deployment,{source_sha:source,nonce});
  assert.equal(JSON.stringify(body).includes(token),false);
  assert.equal(JSON.parse(app.logs[0]).smoke_correlation,token);
  assert.equal(app.calls.length,0);
});
test('default URL cannot target another Worker; custom route must return exact deployment',async()=>{
  assert.throws(()=>endpointInputs('https://auros-wishes.other.workers.dev',source,nonce,deployed));
  for(const origin of ['http://bad.test','https://bad.test/','https://u:p@bad.test','https://bad.test/path']) assert.throws(()=>endpointInputs(origin,source,nonce,deployed));
  const mock=body=>async(url,options)=>{assert.equal(options.redirect,'error');assert.equal(options.cache,'no-store');return Response.json(body);};
  await checkDeployment('https://builds.example',source,nonce,deployed,mock({ok:true,deployment:{source_sha:source,nonce}}));
  for(const body of [{ok:true},{ok:true,deployment:{source_sha:'d'.repeat(40),nonce}},{ok:true,deployment:{source_sha:source,nonce:'d'.repeat(64)}}]) await assert.rejects(checkDeployment('https://builds.example',source,nonce,deployed,mock(body)));
  await assert.rejects(checkDeployment(deployed,source,nonce,deployed,async()=>new Response('x'.repeat(4097))));
});
