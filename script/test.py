"""
script/test.py

Builds the project, then builds and runs the unit test suite in tests/.

Split from build.py so that `./pretzel build` stays a product-only build: the test executables
are EXCLUDE_FROM_ALL, and this is the only command that compiles them. That keeps an ordinary
build fast, and keeps a broken or missing test framework from ever blocking one.

Usage:
    ./pretzel test                  # build, then run the whole suite
    ./pretzel test -R Legacy        # ... only cases matching a ctest regex

Exits non-zero when a test fails, so it is usable as a gate.
"""

import subprocess
import sys

from script.build import build_project
from script.utils import BUILD_DIR, MAKE_JOBS, run_cmd


def _has_tests() -> bool:
    """
    True when ctest knows of at least one test. Distinguishes "the suite passed" from "there
    was nothing to run" — the latter means GoogleTest is missing, which should be reported
    rather than silently counted as success.
    """
    result = subprocess.run(["ctest", "-N"], cwd=BUILD_DIR, capture_output=True, text=True)
    if result.returncode != 0:
        return False
    return "Total Tests: 0" not in result.stdout


def run():
    # Same pipeline as `./pretzel build` — dependency check, cmake, make.
    build_project()

    run_cmd(["make", "build-tests", MAKE_JOBS], cwd=BUILD_DIR, msg="Compiling the test suite")

    if not _has_tests():
        print("[Error] No tests are registered — GoogleTest is probably not installed.")
        print("        Run 'sudo ./pretzel install' to fetch it, then try again.")
        sys.exit(1)

    # Anything after the subcommand is handed to ctest, so `./pretzel test -R Legacy` works.
    extra = sys.argv[2:]

    # run_cmd exits with ctest's status, so a failing test fails this command.
    run_cmd(["ctest", "--output-on-failure"] + extra, cwd=BUILD_DIR, msg="Running the test suite")

    print("[*] All tests passed")


if __name__ == "__main__":
    run()
