# SPDX-FileCopyrightText: 2026 Jacek Ziółkowski (sndth)
# SPDX-License-Identifier: MIT

from pathlib import Path

import argparse
import sys
import venv
import subprocess

CLANG_FORMAT = "clang-format==23.1.1"
ROOT = Path(__file__).resolve().parent.parent
VENV = ROOT / "build" / "format-venv"
BIN = VENV / ("Scripts" if sys.platform == "win32" else "bin")

if not VENV.exists():
    venv.create(VENV, with_pip=True)

subprocess.run(
    [
        BIN / "python",
        "-m",
        "pip",
        "install",
        "--quiet",
        "--disable-pip-version-check",
        CLANG_FORMAT,
    ],
    check=True,
)

files = sorted(
    p
    for folder in ("src", "test")
    for p in (ROOT / folder).rglob("*")
    if p.suffix in {".cpp", ".h", ".hpp"}
)

parser = argparse.ArgumentParser()
parser.add_argument("--check", action="store_true", help="Check formatting without changing files")
args = parser.parse_args()
options = ["--dry-run", "--Werror"] if args.check else ["-i"]
subprocess.run([BIN / "clang-format", *options, *files], check=True)
print(f"{'Checked' if args.check else 'Formatted'} {len(files)} file(s)")
