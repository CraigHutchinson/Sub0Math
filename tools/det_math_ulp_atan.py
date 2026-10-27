"""Maximum ulp error of Math.atan / Math.atan2 (= sub0math::det::atan / atan2) against mpmath at 200 bits.

Usage: node tools/det_math_dump_atan.js | python tools/det_math_ulp_atan.py
"""
import math
import struct
import sys

from mpmath import mp, mpf, atan, atan2

mp.prec = 200


def f(b):
    return struct.unpack('<d', struct.pack('<Q', int(b, 16)))[0]


worst = {}
for line in sys.stdin:
    kind, yb, xb, rb = line.split()
    y, x, got = f(yb), f(xb), f(rb)
    exact = atan(mpf(y)) if kind == '1' else atan2(mpf(y), mpf(x))
    u = math.ulp(got) if got != 0 else 5e-324
    err = float(abs(mpf(got) - exact) / mpf(u))
    name = 'atan' if kind == '1' else 'atan2'
    if err > worst.get(name, (0.0,))[0]:
        worst[name] = (err, y, x)
for k, v in worst.items():
    print(k, 'max ulp error %.4f' % v[0], 'at y,x =', v[1], v[2])
