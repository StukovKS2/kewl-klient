#!/usr/bin/env python3
"""Run IDAPython through headless-ida on Windows.

The upstream CLI interprets any colon in idat_path as HOST:PORT, which makes
absolute Windows paths unusable. This wrapper calls the Python API directly.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# This wrapper has the same filename as the installed package. Remove the
# tools directory from import resolution so `headless_ida` resolves to the
# virtualenv package rather than recursively importing this file.
_tools_dir = str(Path(__file__).resolve().parent)
sys.path = [entry for entry in sys.path if Path(entry or ".").resolve() != Path(_tools_dir).resolve()]
from headless_ida import HeadlessIda


def main() -> None:
    parser = argparse.ArgumentParser(description="Run an IDAPython script headlessly")
    parser.add_argument("--ida", required=True, help="Path to IDA's idat.exe")
    parser.add_argument("--binary", required=True, help="Path to the input binary or IDB")
    parser.add_argument("--script", help="IDAPython script to execute")
    parser.add_argument("--command", help="One-line Python command to execute")
    parser.add_argument("--ftype")
    parser.add_argument("--processor")
    args = parser.parse_args()

    if bool(args.script) == bool(args.command):
        parser.error("provide exactly one of --script or --command")

    headless = HeadlessIda(
        args.ida,
        args.binary,
        ftype=args.ftype,
        processor=args.processor,
    )
    namespace = {"headlessida": headless, "HeadlessIda": HeadlessIda}
    if args.script:
        script = Path(args.script)
        source = script.read_text(encoding="utf-8")
        exec(compile(source, str(script), "exec"), namespace, namespace)
    else:
        exec(compile(args.command, "<headless-ida-command>", "exec"), namespace, namespace)


if __name__ == "__main__":
    main()
