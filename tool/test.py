# SPDX-FileCopyrightText: 2026 Jacek Ziółkowski (sndth)
# SPDX-License-Identifier: MIT

"""Run each doctest in a fresh process: hooks and loaded ASIs are process-wide."""

import argparse
import fnmatch
from pathlib import Path
import random
import subprocess
import sys
import time
import xml.etree.ElementTree as ET


def process_failure(result):
    """Keep native Windows status codes visible even when a process prints nothing."""
    return (
        f"exit code {result.returncode} (0x{result.returncode & 0xFFFFFFFF:08X})\n"
        f"stdout:\n{result.stdout or '<empty>'}\n"
        f"stderr:\n{result.stderr or '<empty>'}"
    )


def write_report(suite, report):
    if report:
        report = Path(report)
        report.parent.mkdir(parents=True, exist_ok=True)
        ET.indent(suite)
        ET.ElementTree(suite).write(report, encoding="utf-8", xml_declaration=True)


def cases(executable):
    """List enabled doctests without initializing the fake game or loading its ASIs."""
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
        raise RuntimeError(f"Test discovery failed: {process_failure(result)}")
    return [
        case.attrib["name"]
        for case in ET.fromstring(result.stdout).findall("TestCase")
        if case.attrib.get("skipped") != "true"
    ]


def run(executable, pattern="*", timeout=45, seed=1, report=None):
    """Run matching cases in fresh processes and return the number of failures.

    Discovery failures return 1 and produce an infrastructure error in the report.
    Cases run serially because their fixtures share files beside the executable.
    """
    executable = Path(executable).resolve(strict=True)
    try:
        names = [name for name in cases(executable) if fnmatch.fnmatchcase(name, pattern)]
    except (RuntimeError, OSError, subprocess.TimeoutExpired, ET.ParseError) as error:
        suite = ET.Element("testsuite", name="Modme startup", tests="1", errors="1")
        case = ET.SubElement(suite, "testcase", name="Test discovery")
        ET.SubElement(case, "error").text = str(error)
        write_report(suite, report)
        print(str(error), file=sys.stderr, flush=True)
        return 1
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
            output = process_failure(result) if failed else result.stdout + result.stderr
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
    write_report(suite, report)
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
