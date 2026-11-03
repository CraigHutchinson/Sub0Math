"""A cheap speed check before any build or measuring: is this machine as fast as it was when the baseline was recorded?

Battery is not the only way a machine runs slow. On mains a vendor power mode, a charging power cap or thermal limits can pin
the CPU at its base clock with no boost; every timing then comes out a uniform factor slower, and the gate would measure
everything just to call the run ENVIRONMENT at the end. The check times the benchmark executable's own calibration kernel
(`--calibration`: a fixed dependent arithmetic chain whose time tracks the clock; about 0.3 s, one thread, no build) and
compares it with the `calibration_ns` stored in the baseline, which was timed by the same kernel. Slower than the limit
means `throttled`, handled like battery (see power.py, gate.py).

The default limit is 1.4x. In the 32 local records of the reference host, the median calibration of a run sat within
0.97x..1.29x of the baseline's (the high end is runs that overlapped other load) and a single reading is up to 2.2x after the
quiet-wait idled the CPU; a machine pinned at base clock read 1.88x. 1.4x clears all the normal spread and catches the pinned
machine; a lone reading over the limit is re-read until two agree (runner.steady_calibration), so a cold clock is not a verdict.

Without a built executable or a baseline calibration (a first run on a host) nothing can be compared: the gate proceeds and says so.
`SUB0PERF_FAKE_SPEED=<ratio>` replaces the measurement; it is for tests only.
"""

import os
import subprocess
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

from .common import CALIBRATION, read_json

DEFAULT_LIMIT = 1.4
LIMIT_VAR = "SUB0MATH_PERF_THROTTLE_RATIO"   # slower than the baseline's calibration by more than this: throttled
FAKE_VAR = "SUB0PERF_FAKE_SPEED"             # tests only: the ratio to report


@dataclass
class Check:
    ratio: float | None = None      # the slowest ratio over the targets; None when nothing could be compared
    limit: float = DEFAULT_LIMIT
    notes: list = field(default_factory=list)   # one line per target (or why it was not compared) and warnings


def limit(environ=None, config=None):
    """The ratio above which the machine counts as throttled: the environment, else `throttle_ratio` of the config, else 1.4."""
    environ = os.environ if environ is None else environ
    for raw in (environ.get(LIMIT_VAR), (config or {}).get("throttle_ratio")):
        try:
            value = float(raw)
        except (TypeError, ValueError):
            continue
        if value > 1.0:
            return value
    return DEFAULT_LIMIT


def read_ns(exe):
    """One reading of the calibration kernel, ns per iteration of its chain."""
    from .runner import measure
    with tempfile.TemporaryDirectory() as scratch:
        return measure(exe, ["--calibration"], scratch)[CALIBRATION]


def measure_ratio(exe, base_ns, bound, read=read_ns):
    """(ratio now / baseline, ns now). One reading; only when it is over `bound` are the readings repeated until two agree."""
    now = read(exe)
    if now / base_ns > bound:
        from .runner import steady_calibration
        now = steady_calibration(lambda: read(exe))
    return now / base_ns, now


def check_targets(targets, environ=None, config=None, read=read_ns):
    """Checks (label, exe, baseline path) triples; the result's ratio is the slowest. Never raises: what cannot be run is a note."""
    environ = os.environ if environ is None else environ
    result = Check(limit=limit(environ, config))
    ratios = []
    if environ.get(FAKE_VAR):
        try:
            ratios.append(float(environ[FAKE_VAR]))
        except ValueError:
            result.notes.append(f"sub0perf: WARNING: {FAKE_VAR}={environ[FAKE_VAR]} is not a number; ignored")
        else:
            result.notes.append(f"sub0perf: WARNING: {FAKE_VAR}={environ[FAKE_VAR]} is set: the machine speed is being faked "
                                f"(for tests only)")
    else:
        for label, exe, baseline in targets:
            try:
                base_ns = read_json(baseline).get("calibration_ns") if baseline and Path(baseline).is_file() else None
                if not base_ns:
                    result.notes.append(f"sub0perf: speed check skipped for {label}: the baseline has no calibration (a first run); proceeding")
                elif not (exe and Path(exe).is_file()):
                    result.notes.append(f"sub0perf: speed check skipped for {label}: no built benchmark to time yet; proceeding")
                else:
                    ratio, now = measure_ratio(exe, base_ns, result.limit, read)
                    ratios.append(ratio)
                    result.notes.append(f"sub0perf: speed check {label}: calibration {now:,.0f} ns against the baseline's {base_ns:,.0f} ns "
                                        f"= {ratio:.2f}x (throttled above {result.limit:.2f}x)")
            except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
                result.notes.append(f"sub0perf: speed check skipped for {label}: {type(error).__name__}: {error}; proceeding")
    result.ratio = max(ratios) if ratios else None
    return result
