import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const path = process.argv[2];
const bytes = await readFile(path);
const module = await WebAssembly.compile(bytes);
assert.deepEqual(WebAssembly.Module.imports(module), []);
const cases = [
    'rstd_runtime_contract',
    'rstd_runtime_in_place',
    'rstd_runtime_reuse',
    'rstd_runtime_initial_memory',
    'rstd_runtime_oom',
    'rstd_runtime_mixed',
];
for (const name of cases) {
    const { exports } = await WebAssembly.instantiate(module);
    assert.equal(exports[name](), 0, name);
}
const { exports } = await WebAssembly.instantiate(module);
for (const name of ['dlmalloc', 'dlfree', 'dlcalloc', 'sbrk', 'malloc', 'free']) {
    assert.equal(exports[name], undefined, `${name} must stay private`);
}
console.log(`${cases.length} checks passed; ${bytes.length} bytes; no imports; ${path}`);
