#!/usr/bin/env python3
"""
generator_e2e_test.py — end-to-end test of the ufsrv_config generation pipeline.

Runs the REAL generator host binary against the REAL cold catalogue
(ufsrv_config_generator_fields.c.input) in decomposed mode (--header + --source),
then runs the standalone verifier (ufsrv_config_verify.py) over the generated
output and asserts it reports zero failures.

This is the same pipeline the build's `ufsrv_config_all` target chains
(verify_catalogue -> generate -> verify_generated), but exercised here as a
single CTest-registered integration test against a throw-away staging dir.

Exit codes:
  0 — generation succeeded AND the verifier reported zero failures
  1 — generation or verification failed
  2 — precondition not met (generator binary or catalogue missing) — treated
      as a SKIP by CTest
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile


def _run(cmd, label):
    """Run a command, returning its combined output. Exit on non-zero."""
    print(f"\n=== {label} ===")
    print("  $ " + " ".join(cmd))
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    out = proc.stdout or ""
    if proc.returncode != 0:
        print(out)
        print(f"  [FAIL] {label} exited {proc.returncode}")
        sys.exit(1)
    print("  [OK] " + label)
    return out


def _generator_flags(staging, args):
    """Common generator flags, mirroring cmake-modules/ufsrv_config_generator.cmake."""
    flags = [
        "--output-dir=" + staging,
        "--include-prefix=" + args.include_prefix,
        "--repo-dir=" + args.repo_dir,
        "--literals-header=" + args.literals_header,
    ]
    if args.prefix:
        flags.append("--prefix=" + args.prefix)
    return flags


def main():
    ap = argparse.ArgumentParser(description="End-to-end ufsrv_config generation + verification test")
    ap.add_argument("--generator", required=True, help="path to ufsrv_config_generator binary")
    ap.add_argument("--catalogue", required=True, help="path to the cold catalogue .c.input")
    ap.add_argument("--verifier", required=True, help="path to ufsrv_config_verify.py")
    ap.add_argument("--repo-dir", required=True, help="repo root (for VERSION provenance)")
    ap.add_argument("--literals-header", required=True, help="path to ufsrv_config_literals_defs.h")
    ap.add_argument("--include-prefix", default="ufsrvcorelib", help="include prefix (default: ufsrvcorelib)")
    ap.add_argument("--prefix", default="", help="optional generator --prefix (e.g. adjunct)")
    args = ap.parse_args()

    # Preconditions — SKIP (exit 2) if the generator or catalogue is absent so a
    # partial build (tests ON, generator not built) degrades gracefully.
    if not os.path.isfile(args.generator):
        print(f"SKIP: generator binary not found: {args.generator}")
        print("      build it first: cmake --build build --target ufsrv_config_generator")
        return 2
    if not os.path.isfile(args.catalogue):
        print(f"SKIP: catalogue not found: {args.catalogue}")
        return 2
    if not os.path.isfile(args.verifier):
        print(f"SKIP: verifier not found: {args.verifier}")
        return 2

    staging = tempfile.mkdtemp(prefix="ufsrv_config_e2e_")
    try:
        flags = _generator_flags(staging, args)

        # Full generation — decomposed mode writes one file per logical artefact.
        _run([args.generator, "--header"] + flags, "generate (--header)")
        _run([args.generator, "--source"] + flags, "generate (--source)")

        # Integration — verify the generated artefacts against the catalogue.
        verify_cmd = [
            sys.executable, args.verifier,
            "--catalogue", args.catalogue,
            "--generated-dir", staging,
        ]
        proc = subprocess.run(verify_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if proc.returncode != 0:
            print("\n=== verify (--generated-dir) ===")
            print(proc.stdout or "")
            print("[FAIL] verifier reported failures (exit %d)" % proc.returncode)
            return 1

        # Success — surface just the verifier's tail so CI logs stay short.
        tail = "\n".join((proc.stdout or "").strip().splitlines()[-3:])
        print("\n=== verify (--generated-dir) ===")
        print(tail)
        print("\n==============================================================")
        print("PASS: full generation + verification succeeded")
        print("==============================================================")
        return 0
    finally:
        shutil.rmtree(staging, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
