"""Check a small, explicit set of fault mutations in Modme's own code.

This is a regression check, not an exhaustive mutation score. Each mutation must
compile and make its selected test fail; compiler errors are infrastructure failures.
"""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from test import run

ROOT = Path(__file__).resolve().parent.parent
# id, source, original text, replacement, test name pattern
MUTATIONS = [
    ("priority-order", "src/mods.cpp", "a > b", "a < b", "the mod with the higher priority*"),
    ("disabled-mod", "src/mods.cpp", "if (settings_of(name, config).enable)", "if (true)", "disabled mods give no files*"),
    ("conflict-winner", "src/mods.cpp", "files.try_emplace(std::move(key), file)", "files.insert_or_assign(std::move(key), file)", "the first mod in case-insensitive*"),
    ("write-protection", "src/hooks.cpp", "if (access & write_access)", "if (false)", "files opened for writing*"),
    ("config-validation", "src/config.cpp", "validate_config(root);", "/* validation removed */", "reload config rejects*"),
    ("reload-fallback", "src/modme.cpp", "const auto config = try_read_config(config_file);", "const auto config = std::optional<modme_config>(read_config(config_file));", "hot reload picks up*"),
    ("sector-rounding", "src/archive.cpp", "(size + sector - 1) / sector", "size / sector", "mod entries that fit*"),
    ("entry-size", "src/archive.cpp", "entry.size = uint32_t(it->second.sectors);\n      continue;", "/* size not updated */\n      continue;", "mod entries that fit*"),
    ("new-entry", "src/archive.cpp", "table.push_back(added);", "/* new entry discarded */", "new mod entries are added*"),
    ("archive-limit", "src/archive.cpp", "archive.size + bytes > max_size", "false", "archives never grow past*"),
    ("private-iocp", "src/archive_hooks.cpp", "reinterpret_cast<ULONG_PTR>(event) | 1", "reinterpret_cast<ULONG_PTR>(event) | 0", "virtual archive reads deliver exactly*"),
    ("iocp-key", "src/archive_hooks.cpp", "file->completion_key = key;", "file->completion_key = 0;", "virtual archive reads deliver exactly*"),
    ("iocp-suppression", "src/archive_hooks.cpp", "!(event & 1)", "true", "archive completion suppression*"),
    ("read-position", "src/archive_hooks.cpp", "file->position += length;", "/* position not advanced */", "sequential archive reads*"),
]


def find_msbuild():
    if found := shutil.which("MSBuild.exe"):
        return found
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    location = subprocess.check_output(
        [str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.Component.MSBuild", "-property", "installationPath"],
        text=True,
    ).strip()
    return str(Path(location) / "MSBuild/Current/Bin/MSBuild.exe")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=["x86", "x64"], default="x86")
    parser.add_argument("--configuration", default="Release")
    parser.add_argument("--solution", type=Path, default=ROOT / "build/Modme.sln")
    parser.add_argument("--only", action="append", choices=[m[0] for m in MUTATIONS])
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    if args.list:
        for name, source, _, _, pattern in MUTATIONS:
            print(f"{name}: {source} -> {pattern}")
        return 0
    selected = [m for m in MUTATIONS if not args.only or m[0] in args.only]
    report = ROOT / "build/mutations" / args.arch
    report.mkdir(parents=True, exist_ok=True)
    executable = ROOT / "build/bin" / args.arch / args.configuration / "TestModme.exe"
    command = [find_msbuild(), str(args.solution.resolve()), "/m", f"/p:Configuration={args.configuration}",
               f"/p:Platform={'Win32' if args.arch == 'x86' else 'x64'}", "/verbosity:minimal", "/nologo"]

    def build(name):
        with (report / f"{name}-build.log").open("w", encoding="utf-8") as log:
            return subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=300).returncode == 0

    # Check every anchor before touching any source file.
    for name, source, before, _, _ in selected:
        if (ROOT / source).read_text(encoding="utf-8").count(before) != 1:
            raise RuntimeError(f"{name}: expected exactly one mutation anchor")
    if not build("baseline") or run(executable, report=report / "baseline.xml"):
        raise RuntimeError("Baseline build/tests failed; no mutations were run")

    results = []
    try:
        for name, source, before, after, pattern in selected:
            path = ROOT / source
            original = path.read_bytes()
            newline = b"\r\n" if b"\r\n" in original else b"\n"
            mutated = original.replace(before.replace("\n", newline.decode()).encode(), after.replace("\n", newline.decode()).encode(), 1)
            try:
                path.write_bytes(mutated)
                if not build(name):
                    status = "build-error"
                else:
                    with (report / f"{name}-test.log").open("w", encoding="utf-8") as log:
                        process = subprocess.run(
                            [sys.executable, str(ROOT / "tool/test.py"), str(executable), "--match", pattern,
                             "--report", str(report / f"{name}.xml")],
                            cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=90,
                        )
                    output = (report / f"{name}-test.log").read_text(encoding="utf-8")
                    if any(marker in output for marker in ("Test exceeded", "Test discovery failed:", "Traceback")):
                        status = "test-error"
                    else:
                        status = "killed" if process.returncode else "survived"
                results.append({"mutation": name, "source": source, "status": status})
                print(f"{name}: {status}", flush=True)
            finally:
                if path.read_bytes() != mutated:
                    raise RuntimeError(f"{source} changed during mutation; refusing to overwrite it")
                path.write_bytes(original)
    finally:
        (report / "results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
        # Never leave an executable containing the last mutation in build/bin.
        if not build("restored"):
            raise RuntimeError("Sources restored, but rebuilding the original failed")
    print(f"{sum(r['status'] == 'killed' for r in results)}/{len(selected)} mutations killed", flush=True)
    return int(len(results) != len(selected) or any(r["status"] != "killed" for r in results))


if __name__ == "__main__":
    sys.exit(main())
