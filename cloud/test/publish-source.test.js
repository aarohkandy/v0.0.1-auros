import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync, writeFileSync, chmodSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {spawnSync} from 'node:child_process';

test('cloud mutation guard rejects stale, absent, malformed and unreadable main', () => {
  const dir = mkdtempSync(join(tmpdir(), 'auros-source-'));
  for (const [name, body] of [['timeout', 'shift\nexec "$@"'], ['gh', 'printf "%s\\n" "$CURRENT"\nexit "$API_EXIT"']]) {
    writeFileSync(join(dir, name), '#!/bin/sh\n' + body + '\n'); chmodSync(join(dir, name), 0o700);
  }
  for (const [current, code, success] of [['a'.repeat(40),'0',true], ['b'.repeat(40),'0',false], ['', '0',false], ['hostile', '0',false], ['a'.repeat(40),'1',false]]) {
    const result = spawnSync('bash', [new URL('../../tools/site/require_current_main.sh', import.meta.url).pathname], {env:{...process.env, PATH:`${dir}:${process.env.PATH}`, GITHUB_REPOSITORY:'owner/repo', GITHUB_SHA:'a'.repeat(40), CURRENT:current, API_EXIT:code}});
    assert.equal(result.status === 0, success);
  }
});

test('on-mode log proof requires both exact smoke refusal routes', () => {
  const dir = mkdtempSync(join(tmpdir(), 'auros-log-proof-')); const file = join(dir,'logs');
  const wish = {ev:'req',route:'/v1/wish',status:401}; const ask = {ev:'req',route:'/v1/ask',status:401};
  for (const [rows, success] of [[[],false], [[wish],false], [[wish,{...ask,status:200}],false], [[{ev:'req',route:'/v1/health',status:200}],false], [[wish,ask],true]]) {
    writeFileSync(file, JSON.stringify(rows.map(x=>JSON.stringify(x)))+'\n');
    const result = spawnSync(process.execPath,[new URL('./log-assert.mjs',import.meta.url).pathname,file,'on']);
    assert.equal(result.status===0,success);
  }
  writeFileSync(file,'unparseable but nonempty\n');
  assert.notEqual(spawnSync(process.execPath,[new URL('./log-assert.mjs',import.meta.url).pathname,file,'on']).status,0);
});
