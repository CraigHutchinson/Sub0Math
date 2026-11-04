#!/usr/bin/env python3
"""WebAssembly determinism leg for the Sub0 family: configure, build and test a repository with Emscripten, run
under Node, so every digest a test asserts is checked against the same bits the native compilers produce.

    python <Sub0Math>/tools/wasm/wasm_gate.py --repo . --prefix SUB0PHYSICS [--all] [--builddir build-wasm]

The repository's CMake options are named <PREFIX>_BUILD_BENCHMARKS and <PREFIX>_WARNINGS_AS_ERRORS (for example SUB0MATH,
SUB0PHYSICS). Tests run through CMake's crosscompiling emulator, which is the emsdk's own Node.

The Emscripten SDK is found, in order, at --emsdk, the EMSDK_DIR variable, the EMSDK variable (set once an
emsdk_env script has run) and <repo>/../emsdk. Without one this prints a clear message and exits 0: the leg is
opt-in for machines that have the toolchain (docs/CI.md, "WebAssembly leg"). The SDK is only entered for the child processes;
nothing is installed or registered permanently.

Flags the leg adds (none changes a numeric result):
  -fexceptions   doctest reports a failed REQUIRE by throwing; without it the run aborts at the first one.
  -sNODERAWFS=1  tests read their golden files straight from the host file system.
  -sALLOW_MEMORY_GROWTH=1  the default linear memory is small for the larger runs.

The perf gate's per-clone state (.git/sub0perf/config.json) is backed up first and restored if a configure
rewrote it (the leg turns the hooks and the after-build check off, so it should not).
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional

COMPILE_FLAGS = "-fexceptions"
LINK_FLAGS = "-fexceptions -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1"
SKIP_EXIT = 0  # with --allow-missing-sdk
MISSING_SDK_EXIT = 2  # a full-level attestation must not pass without the WASM leg


def find_emsdk(explicit: Optional[str], repo: Path, environ: dict) -> Optional[Path]:
    """The Emscripten SDK directory (the one holding emsdk_env.*), or None."""
    candidates = []
    if explicit:
        candidates.append(Path(explicit))
    for name in ("EMSDK_DIR", "EMSDK"):
        if environ.get(name):
            candidates.append(Path(environ[name]))
    candidates.append(repo.resolve().parent / "emsdk")
    for path in candidates:
        if (path / "emsdk_env.bat").is_file() or (path / "emsdk_env.sh").is_file():
            return path
    return None


def parse_environment(text: str, separator: str) -> dict:
    env = {}
    for entry in text.split(separator):
        if "=" in entry and not entry.startswith("="):
            key, _, value = entry.partition("=")
            env[key] = value
    return env


def emsdk_environment(emsdk: Path, base: dict) -> dict:
    """The environment an emsdk_env script would set, captured from a child shell (this process stays untouched)."""
    if os.name == "nt":
        script = emsdk / "emsdk_env.bat"
        # Under MSYS or Git Bash (MSYSTEM set) emsdk prints bash exports instead of setting variables.
        base = {k: v for k, v in base.items() if k.upper() != "MSYSTEM"}
        out = subprocess.run(f'call "{script}" >nul 2>&1 && set', shell=True, cwd=emsdk, env=base,
                             capture_output=True, text=True, check=True).stdout
        return parse_environment(out, "\n")
    script = emsdk / "emsdk_env.sh"
    out = subprocess.run(["bash", "-c", f'. "{script}" >/dev/null 2>&1 && env -0'], cwd=emsdk, env=base,
                         capture_output=True, text=True, check=True).stdout
    return parse_environment(out, "\0")


def env_get(env: dict, name: str) -> str:
    for key, value in env.items():
        if key.upper() == name.upper():
            return value
    return ""


def configure_arguments(repo: Path, builddir: str, prefix: str) -> list:
    return [
        "cmake", "-S", str(repo), "-B", builddir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
        f"-D{prefix}_BUILD_BENCHMARKS=OFF", f"-D{prefix}_WARNINGS_AS_ERRORS=ON",
        "-DSUB0MATH_PERF_ON_BUILD=OFF", "-DSUB0MATH_PERF_GIT_HOOKS=OFF",
        f"-DCMAKE_CXX_FLAGS={COMPILE_FLAGS}", f"-DCMAKE_EXE_LINKER_FLAGS={LINK_FLAGS}",
    ]


class PerfConfigGuard:
    """Backs up .git/sub0perf/config.json and restores it if the leg changed it."""

    def __init__(self, repo: Path):
        self.path = repo / ".git" / "sub0perf" / "config.json"
        self.saved = self.path.read_bytes() if self.path.is_file() else None

    def restore(self) -> bool:
        now = self.path.read_bytes() if self.path.is_file() else None
        if now == self.saved:
            return False
        if self.saved is None:
            self.path.unlink()
        else:
            self.path.write_bytes(self.saved)
        return True


def run(command: list, env: dict, cwd: Path) -> int:
    print("+", " ".join(command), flush=True)
    executable = shutil.which(command[0], path=env_get(env, "PATH"))
    if executable is None:
        print(f"wasm gate: {command[0]} not found on the emsdk's PATH", file=sys.stderr)
        return 127
    return subprocess.run([executable] + command[1:], env=env, cwd=cwd).returncode


def main(argv: Optional[list] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", default=".", help="repository to build (default: current directory)")
    ap.add_argument("--prefix", required=True, help="the repository's CMake option prefix, e.g. SUB0PHYSICS")
    ap.add_argument("--builddir", default="build-wasm")
    ap.add_argument("--emsdk", help="the Emscripten SDK directory (default: EMSDK_DIR, EMSDK, <repo>/../emsdk)")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--allow-missing-sdk", action="store_true",
                    help="exit 0 when no Emscripten SDK is found (default: fail, so a missing SDK is never reported as a pass)")
    ap.add_argument("--all", action="store_true", help="also run the suites labelled full (ctest without -LE full)")
    args = ap.parse_args(argv)

    repo = Path(args.repo).resolve()
    emsdk = find_emsdk(args.emsdk, repo, dict(os.environ))
    if emsdk is None:
        print(f"wasm gate: {'SKIPPED' if args.allow_missing_sdk else 'FAILED'}, no Emscripten SDK found (set EMSDK_DIR, or clone emsdk next to this repository "
              "and install it; see docs/CI.md in Sub0Math, 'WebAssembly leg'). Nothing was built or tested.")
        return SKIP_EXIT if args.allow_missing_sdk else MISSING_SDK_EXIT

    env = emsdk_environment(emsdk, dict(os.environ))
    guard = PerfConfigGuard(repo)
    try:
        status = run(["emcmake"] + configure_arguments(repo, args.builddir, args.prefix), env, repo)
        if status == 0:
            status = run(["cmake", "--build", args.builddir, "-j", str(args.jobs)], env, repo)
        if status == 0:
            ctest = ["ctest", "--test-dir", args.builddir, "-j", str(args.jobs), "--output-on-failure"]
            if not args.all:
                ctest += ["-LE", "full"]
            status = run(ctest, env, repo)
    finally:
        if guard.restore():
            print("wasm gate: the configure rewrote .git/sub0perf/config.json; restored it")
    return status


if __name__ == "__main__":
    sys.exit(main())
