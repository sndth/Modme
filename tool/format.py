from pathlib import Path

import sys
import venv
import subprocess

CLANG_FORMAT = "clang-format==23.1.1"
ROOT = Path(__file__).resolve().parent.parent
VENV = ROOT / "tool" / ".venv"
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

subprocess.run([BIN / "clang-format", "-i", *files], check=True)
print(f"Formatted {len(files)} file(s)")
