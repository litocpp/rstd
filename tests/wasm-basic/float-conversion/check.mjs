import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const report = JSON.parse(await readFile(process.argv[2], 'utf8'));
const artifact = report.artifacts.find(
    value => value.package === 'rstd-wasm-float-conversion' && value.kind === 'bin');
const file = artifact.files.find(value => value.role === 'runtime').path;
const module = await WebAssembly.compile(await readFile(file));
assert.deepEqual(WebAssembly.Module.imports(module), []);
const { exports: wasm } = await WebAssembly.instantiate(module);
let checks = 0;

function check(actual, expected, context) {
    assert.equal(actual, expected, context);
    ++checks;
}

function adjacentPositive(value, width, offset) {
    const view = new DataView(new ArrayBuffer(8));
    if (width === 32) {
        view.setFloat32(0, value);
        view.setUint32(0, view.getUint32(0) + offset);
        return view.getFloat32(0);
    }
    view.setFloat64(0, value);
    view.setBigUint64(0, view.getBigUint64(0) + BigInt(offset));
    return view.getFloat64(0);
}

for (const width of [32, 64]) {
    for (const bits of [8, 16, 32, 64]) {
        for (const signed of [true, false]) {
            const upper = 2 ** (bits - Number(signed));
            const min = signed ? -(1n << BigInt(bits - 1)) : 0n;
            const max = (1n << BigInt(bits - Number(signed))) - 1n;
            const below = adjacentPositive(upper, width, -1);
            const above = adjacentPositive(upper, width, 1);
            const name = `cast_f${width}_${signed ? 'i' : 'u'}${bits}`;
            const values = [0, -0, NaN, Infinity, -Infinity, 0.75, -0.75, 42.75, -42.75,
                2 ** (width === 32 ? -149 : -1074), upper, below, above,
                -upper, -below, -above, upper - 0.25, -upper - 0.75,
                upper / 2, upper * 0.75, 300.75, -300.75];
            for (let value of values) {
                if (width === 32) value = Math.fround(value);
                let expected;
                if (Number.isNaN(value)) expected = 0n;
                else if (value === Infinity) expected = max;
                else if (value === -Infinity) expected = min;
                else {
                    expected = BigInt(Math.trunc(value));
                    if (expected < min) expected = min;
                    if (expected > max) expected = max;
                }
                let actual = BigInt(wasm[name](value));
                if (!signed) actual = BigInt.asUintN(bits, actual);
                check(actual, expected, `${name}(${value})`);
            }
        }
    }
}

for (const value of [0, -0, -0.75, 42, 42.75, -1, NaN, Infinity, -Infinity,
    2 ** 32 - 1, 2 ** 32 - 0.5, 2 ** 32]) {
    check(wasm.rstd_wasm_float_checked(value),
        Number(Number.isInteger(value) && value >= 0 && value < 2 ** 32),
        `checked(${value})`);
}

for (const [value, expected] of [
    [1 + 2 ** -24, 1], [1 + 3 * 2 ** -24, 1 + 2 ** -22],
    [-(1 + 2 ** -24), -1], [(2 - 2 ** -23) * 2 ** 127, (2 - 2 ** -23) * 2 ** 127],
    [(2 - 2 ** -24) * 2 ** 127, Infinity], [-((2 - 2 ** -24) * 2 ** 127), -Infinity],
    [2 ** -150, 0], [1.5 * 2 ** -149, 2 ** -148], [-0, -0], [NaN, NaN],
]) check(wasm.cast_f64_f32(value), expected, `cast_f64_f32(${value})`);

check(wasm.cast_u64_f32(16777217n), 16777216, 'u64 halfway even down');
check(wasm.cast_u64_f32(16777219n), 16777220, 'u64 halfway even up');
console.log(`${checks} checks passed; imports: none; ${file}`);
