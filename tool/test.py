"""Run each doctest in a fresh process: hooks and loaded ASIs are process-wide."""

import argparse
import fnmatch
from pathlib import Path
import random
import subprocess
import sys
import time
import xml.etree.ElementTree as ET


def cases(executable):
    result = subprocess.run(
        [str(executable), "--list-test-cases", "--reporters=xml"],
        cwd=executable.parent,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=30,
    )
    if result.returncode:
        raise RuntimeError(f"Test discovery failed:\n{result.stdout}\n{result.stderr}")
    return [
        case.attrib["name"]
        for case in ET.fromstring(result.stdout).findall("TestCase")
        if case.attrib.get("skipped") != "true"
    ]


def run(executable, pattern="*", timeout=45, seed=1, report=None):
    executable = Path(executable).resolve(strict=True)
    names = [name for name in cases(executable) if fnmatch.fnmatchcase(name, pattern)]
    if not names:
        raise RuntimeError(f"No tests match {pattern!r}")
    random.Random(seed).shuffle(names)
    suite = ET.Element("testsuite", name="Modme", tests=str(len(names)))
    failures = 0
    for name in names:
        escaped = name.replace("\\", "\\\\").replace(",", "\\,")
        start = time.monotonic()
        try:
            result = subprocess.run(
                [str(executable), f"--test-case={escaped}", "--no-colors", "--no-breaks", "--abort-after=1"],
                cwd=executable.parent,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=timeout,
            )
            failed = result.returncode != 0
            output = result.stdout + result.stderr
        except subprocess.TimeoutExpired:
            failed = True
            output = f"Test exceeded {timeout} seconds"
        case = ET.SubElement(suite, "testcase", name=name, time=f"{time.monotonic() - start:.3f}")
        if failed:
            failures += 1
            ET.SubElement(case, "failure").text = output
            print(f"FAIL {name}\n{output}", flush=True)
        else:
            print(f"PASS {name}", flush=True)
    suite.set("failures", str(failures))
    if report:
        report = Path(report)
        report.parent.mkdir(parents=True, exist_ok=True)
        ET.indent(suite)
        ET.ElementTree(suite).write(report, encoding="utf-8", xml_declaration=True)
    print(f"{len(names) - failures}/{len(names)} tests passed (seed {seed})", flush=True)
    return failures


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--match", default="*")
    parser.add_argument("--timeout", type=int, default=45)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    sys.exit(bool(run(args.executable, args.match, args.timeout, args.seed, args.report)))
