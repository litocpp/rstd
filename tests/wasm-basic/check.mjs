import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const report = JSON.parse(await readFile(process.argv[2], 'utf8'));
const artifact = report.artifacts.find(value => value.package === 'rstd-wasm-basic' && value.kind === 'bin');
const path = artifact.files.find(value => value.role === 'runtime').path;
const module = await WebAssembly.compile(await readFile(path));
const cases = [
    ['rstd_wasm_basic', 22],
    ['rstd_wasm_fs_unsupported', 1],
    ['rstd_wasm_allocator_reuse', 10],
    ['rstd_wasm_collections', 28],
    ['rstd_wasm_allocator_contract', 1],
    ['rstd_wasm_allocator_split', 1],
];
for (const [name, expected] of cases) {
    // Pointer-identity checks require an isolated heap, not the preceding case's free list.
    const { exports: wasm } = await WebAssembly.instantiate(module, {
        rstd: { panic() { throw new Error('Unexpected panic'); } },
    });
    assert.equal(wasm[name](), expected, name);
}
console.log(`${cases.length} checks passed; ${path}`);
