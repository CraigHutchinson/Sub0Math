// Generates the reference values that tests/test_det_math.cpp pins: V8's Math.sin and Math.cos (fdlibm).
// Run with Node: `node tools/det_math_reference.js`. The input generator mirrors the one in the test exactly
// (SplitMix64 on the index, only exactly-rounded arithmetic), so the digests agree if and only if every
// result is bit-identical to V8's.
'use strict';

const MASK = (1n << 64n) - 1n;
const f64 = new Float64Array(1);
const u64 = new BigUint64Array(f64.buffer);

function bitsOf(x) { f64[0] = x; return u64[0]; }
function fromBits(b) { u64[0] = b; return f64[0]; }

function splitmix64(i) {
    let z = (BigInt(i) * 0x9E3779B97F4A7C15n + 0x9E3779B97F4A7C15n) & MASK;
    z = ((z ^ (z >> 30n)) * 0xBF58476D1CE4E5B9n) & MASK;
    z = ((z ^ (z >> 27n)) * 0x94D049BB133111EBn) & MASK;
    return (z ^ (z >> 31n)) & MASK;
}

const HalfPi = 1.5707963267948966;
const Count = 60000;

function input(i) {
    const u = splitmix64(i);
    const unit = Number(u >> 11n) * 2 ** -53; // [0, 1), exact
    const signed = unit * 2 - 1;
    switch (i % 6) {
    case 0: return signed * 4;
    case 1: return signed * 1000;
    case 2: return signed * 1048576;
    case 3: {
        const e = splitmix64(i + 1000003) % 2047n;
        return fromBits((u & 0x800FFFFFFFFFFFFFn) | (e << 52n));
    }
    case 4: return Math.trunc(signed * 1048576) * HalfPi;
    default: {
        const k = Math.trunc(signed * 1099511627776);
        const x = k * HalfPi;
        const step = Number(u & 3n) - 1; // -1, 0, +1 ulp
        return fromBits(bitsOf(x) + BigInt(step));
    }
    }
}

const specials = [
    0, -0, 5e-324, -5e-324, 1e-300, 2 ** -27, 2 ** -26, 0.1, 0.5, 0.7853981633974483, 0.7853981633974484,
    1, 1.5707963267948966, 1.5707963267948968, 2, 2.356194490192345, 3, 3.141592653589793, 4, 4.71238898038469,
    6.283185307179586, 10, 100, 1e5, 823549.6, 823550, 1e6, 1e10, 1e15, 2 ** 53, 1e22, 1e100, 1e300,
    1.7976931348623157e308, -1.7976931348623157e308, -1, -3.141592653589793, -1e22, 2 ** 100,
    Infinity, -Infinity, NaN,
];

function fnv(h, b) { return ((h ^ b) * 0x100000001B3n) & MASK; }

let h = 0xCBF29CE484222325n;
for (let i = 0; i < Count; ++i) {
    const x = input(i);
    h = fnv(h, bitsOf(Math.sin(x)));
    h = fnv(h, bitsOf(Math.cos(x)));
}
const hex = (b) => '0x' + b.toString(16).padStart(16, '0');
console.log('// Count', Count, 'digest', hex(h));
console.log('// {input bits, sin bits, cos bits}');
for (const x of specials) {
    const s = Number.isNaN(Math.sin(x)) ? 0x7ff8000000000000n : bitsOf(Math.sin(x));
    const c = Number.isNaN(Math.cos(x)) ? 0x7ff8000000000000n : bitsOf(Math.cos(x));
    const xb = Number.isNaN(x) ? 0x7ff8000000000000n : bitsOf(x);
    console.log(`    {${hex(xb)}, ${hex(s)}, ${hex(c)}}, // ${x}`);
}

// ---- exp and log ----
function expInput(i) {
    const u = splitmix64(i + 7000000);
    const signed = Number(u >> 11n) * 2 ** -53 * 2 - 1;
    switch (i % 4) {
    case 0: return signed * 745;
    case 1: return signed * 2;
    case 2: return signed * 1e-3;
    default: {
        const e = 1023n - 40n + (splitmix64(i + 9000000) % 52n); // magnitudes 2^-40 .. 2^11
        return fromBits((u & 0x800FFFFFFFFFFFFFn) | (e << 52n));
    }
    }
}
function logInput(i) {
    const u = splitmix64(i + 11000000);
    const unit = Number(u >> 11n) * 2 ** -53;
    switch (i % 4) {
    case 0: return fromBits(u & 0x7FFFFFFFFFFFFFFFn); // any non-negative pattern, including subnormals, inf, NaN
    case 1: return unit * 100;
    case 2: return 1 + (unit * 2 - 1) * 2 ** -18; // around 1
    default: return unit * 2 + 0.5;
    }
}
const ExpLogCount = 40000;
let he = 0xCBF29CE484222325n;
let hl = 0xCBF29CE484222325n;
const canon = (v) => (Number.isNaN(v) ? 0x7ff8000000000000n : bitsOf(v));
for (let i = 0; i < ExpLogCount; ++i) {
    he = fnv(he, canon(Math.exp(expInput(i))));
    hl = fnv(hl, canon(Math.log(logInput(i))));
}
console.log('// ExpLogCount', ExpLogCount, 'exp digest', hex(he), 'log digest', hex(hl));
for (const x of [0, -0, 1, -1, 0.5, 2, 10, 100, 709, 709.78, 710, -745, -745.2, -746, 1e-10, 5e-324, Infinity, -Infinity, NaN]) {
    console.log(`    {${hex(canon(x))}, ${hex(canon(Math.exp(x)))}, ${hex(canon(Math.log(x)))}}, // ${x}`);
}
