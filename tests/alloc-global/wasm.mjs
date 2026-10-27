import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const module = await WebAssembly.compile(await readFile(process.argv[2]));
const checks = [
  'rstd_global_layouts',
  'rstd_global_zero_sized',
  'rstd_global_dyn_resize',
  'rstd_global_failure',
  'rstd_global_collections',
];
for (const name of checks) {
  const { exports } = await WebAssembly.instantiate(module, {
    rstd: { panic() { throw new Error(`${name}: unexpected panic`); } },
  });
  assert.equal(exports[name](), 0, name);
}
console.log(`${checks.length} Global checks passed`);
