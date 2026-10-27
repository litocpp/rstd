import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const report = JSON.parse(await readFile(process.argv[2], 'utf8'));
const artifact = report.artifacts.find(value => value.package === 'rstd-wasm-int128' && value.kind === 'bin');
const path = artifact.files.find(value => value.role === 'runtime').path;
const bytes = await readFile(path);
const module = await WebAssembly.compile(bytes);
assert.ok(WebAssembly.Module.imports(module).every(item => item.module === 'rstd' && item.name === 'panic'));
let wasm;
let panic = '';
({ exports: wasm } = await WebAssembly.instantiate(module, {
    rstd: { panic(pointer, size) {
        panic += new TextDecoder().decode(new Uint8Array(wasm.memory.buffer, pointer, size));
    } },
}));

const bits = value => BigInt.asUintN(128, value);
const low = value => BigInt.asUintN(64, value);
const high = value => low(bits(value) >> 64n);
const read = prefix => low(wasm[`${prefix}_low`]()) | (low(wasm[`${prefix}_high`]()) << 64n);
let checks = 0;
function check(operation, a, b, expected, shift = 0) {
    wasm.calculate(operation, low(a), high(a), low(b), high(b), shift);
    assert.equal(read('result'), bits(expected), `${operation}: ${a}, ${b}, ${shift}`);
    ++checks;
}
const max = (1n << 128n) - 1n;
const min = -(1n << 127n);
const values = [0n, 1n, 2n, 10n ** 16n - 1n, 10n ** 16n, 10n ** 32n,
    (1n << 64n) - 1n, 1n << 64n, (1n << 127n) - 1n, 1n << 127n, max];
for (const a of values) {
    for (const b of values) {
        for (const operation of [0, 9]) check(operation, a, b, a * b);
        if (b === 0n) continue;
        for (const operation of [1, 3, 10]) {
            check(operation, a, b, a / b);
            if (operation === 3) {
                assert.equal(read('remainder'), a % b);
                ++checks;
            }
        }
        for (const operation of [2, 11]) check(operation, a, b, a % b);
    }
    for (const shift of [0, 1, 31, 32, 63, 64, 65, 127]) {
        check(6, a, 0n, a << BigInt(shift), shift);
        check(7, a, 0n, BigInt.asIntN(128, a) >> BigInt(shift), shift);
        check(8, a, 0n, a >> BigInt(shift), shift);
    }
}
for (const a of [min, -10001n, -1n, 0n, 1n, -min - 1n]) {
    for (const b of [min, -3n, -1n, 1n, 3n, -min - 1n]) {
        if (a === min && b === -1n) continue;
        check(4, a, b, a / b);
        check(5, a, b, a % b);
    }
}
assert.throws(() => wasm.panic_test(), WebAssembly.RuntimeError);
assert.equal(panic, 'rstd panic: runtime panic test');
console.log(JSON.stringify({ checks, wasmBytes: bytes.length, imports: WebAssembly.Module.imports(module), panic }, null, 2));
