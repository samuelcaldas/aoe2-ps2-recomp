#!/usr/bin/env python3
"""Validate GHIDRA_HOME environment variable and analyzeHeadless availability.

Required external prerequisite for exporting function maps with PS2Recomp's ExportPS2Functions.java.
Does not automatically download or build Ghidra.
"""
import os
import sys
from pathlib import Path

def main():
    ghidra_home = os.environ.get("GHIDRA_HOME")
    if not ghidra_home:
        sys.stderr.write(
            "Error: GHIDRA_HOME environment variable is not set.\n"
            "Ghidra is an external prerequisite required for function-map export.\n"
            "Please install Ghidra (10.x or later) and export GHIDRA_HOME=/path/to/ghidra\n"
        )
        sys.exit(1)

    ghidra_path = Path(ghidra_home).resolve()
    if not ghidra_path.exists() or not ghidra_path.is_dir():
        sys.stderr.write(
            f"Error: GHIDRA_HOME points to a non-existent or invalid directory: {ghidra_path}\n"
        )
        sys.exit(1)

    # Check for analyzeHeadless executable
    analyze_headless = ghidra_path / "support" / "analyzeHeadless"
    if not analyze_headless.exists() and not (ghidra_path / "support" / "analyzeHeadless.bat").exists():
        sys.stderr.write(
            f"Error: analyzeHeadless not found under {ghidra_path / 'support'}.\n"
            "Ensure GHIDRA_HOME points to a valid Ghidra installation root.\n"
        )
        sys.exit(1)

    print(f"GHIDRA_HOME verified: {ghidra_path}")
    print(f"analyzeHeadless: {analyze_headless}")
    sys.exit(0)

if __name__ == "__main__":
    main()
