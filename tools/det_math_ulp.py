"""Maximum ulp error of Math.sin / Math.cos (= sub0math::det::sin / cos) against mpmath at 200 bits.

Reads the inputs from `node tools/det_math_dump.js` (hex bit patterns), one per line, and the node results.
Usage: node tools/det_math_dump.js | python tools/det_math_ulp.py
"""
import struct
import sys

from mpmath import mp, mpf, sin, cos

mp.prec = 200


def f(b):
    return struct.unpack('<d', struct.pack('<Q', int(b, 16)))[0]


def ulp(x):
    import math
    return math.ulp(x)


worst = {'sin': (0.0, None), 'cos': (0.0, None)}
rows = 0
buckets = {}
for line in sys.stdin:
    xb, sb, cb = line.split()
    x, s, c = f(xb), f(sb), f(cb)
    rows += 1
    for name, got, fn in (('sin', s, sin), ('cos', c, cos)):
        exact = fn(mpf(x))
        u = ulp(got) if got != 0 else 5e-324
        err = float(abs(mpf(got) - exact) / mpf(u))
        if err > worst[name][0]:
            worst[name] = (err, x)
        cls = 'small(|x|<=4)' if abs(x) <= 4 else 'medium(<=2^20)' if abs(x) <= 1048576 else 'large'
        buckets[(name, cls)] = max(buckets.get((name, cls), 0.0), err)
print('inputs', rows)
for k in sorted(buckets):
    print(k, 'max ulp error %.4f' % buckets[k])
print('worst', worst)
