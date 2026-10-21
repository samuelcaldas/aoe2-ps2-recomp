#!/usr/bin/env python3
"""Verify staged HD files against their local per-platform manifest."""
import argparse
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from tools.hd_assets.stage_verifier import find_stage_problems
from tools.hd_assets.stage_plan import stage_root_for


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--platform", required=True, choices=("desktop", "android", "switch"))
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    problems = find_stage_problems(args.platform, args.repo_root)
    for problem in problems:
        sys.stderr.write(f"FAIL: {problem}\n")
    if problems:
        return 1
    root = stage_root_for(args.platform, args.repo_root)
    print(f"OK: staged assets verified at {root}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
