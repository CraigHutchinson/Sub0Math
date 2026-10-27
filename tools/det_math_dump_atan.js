// Prints "y x atan2" (hex bit patterns) for the finite atan2 inputs of det_math_reference.js, for det_math_ulp_atan.py.
'use strict';
const fs = require('fs');
const path = require('path');
const src = fs.readFileSync(path.join(__dirname, 'det_math_reference.js'), 'utf8')
    .split('const specials')[0] + fs.readFileSync(path.join(__dirname, 'det_math_reference.js'), 'utf8')
    .split('// ---- tan, atan, atan2 ----')[1].split('const TanCount')[0];
const gen = new Function(src + '; return {atan2Input, atanInput, bitsOf};')();
const hex = (b) => b.toString(16).padStart(16, '0');
const out = [];
for (let i = 0; i < 50000; ++i) {
    const [y, x] = gen.atan2Input(i);
    if (!Number.isFinite(x) || !Number.isFinite(y)) continue;
    out.push(`2 ${hex(gen.bitsOf(y))} ${hex(gen.bitsOf(x))} ${hex(gen.bitsOf(Math.atan2(y, x)))}`);
}
for (let i = 0; i < 40000; ++i) {
    const x = gen.atanInput(i);
    if (!Number.isFinite(x)) continue;
    out.push(`1 ${hex(gen.bitsOf(x))} 0000000000000000 ${hex(gen.bitsOf(Math.atan(x)))}`);
}
console.log(out.join('\n'));
