// Prints "input sin cos" as hex bit patterns for the inputs of det_math_reference.js (feed to det_math_ulp.py).
'use strict';
const fs = require('fs');
const path = require('path');
const src = fs.readFileSync(path.join(__dirname, 'det_math_reference.js'), 'utf8')
    .split('const specials')[0]; // generator only
const gen = new Function(src + '; return {input, bitsOf, Count};')();
const hex = (b) => b.toString(16).padStart(16, '0');
const out = [];
for (let i = 0; i < gen.Count; ++i) {
    const x = gen.input(i);
    if (!Number.isFinite(x)) continue;
    out.push(`${hex(gen.bitsOf(x))} ${hex(gen.bitsOf(Math.sin(x)))} ${hex(gen.bitsOf(Math.cos(x)))}`);
}
console.log(out.join('\n'));
