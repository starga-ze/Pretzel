"""
script/stop.py

Script to safely shut down the running Pretzel platform (pretzel.target).
"""

import subprocess
from script.utils import run_cmd, PG_SERVICE

TARGET = "pretzel.target"

def run():
    # 1. Check if the target is currently running
    status = subprocess.run(["systemctl", "is-active", "--quiet", TARGET], check=False)

    # Skip if not active
    if status.returncode != 0:
        print("pretzel.target not running")
    else:
        # 2. Request target daemon to stop
        run_cmd(["systemctl", "stop", TARGET])

        # 3. Output final status after termination
        subprocess.run(["systemctl", "status", TARGET, "-n", "0", "--no-pager"])

    # 4. PostgreSQL is a persistent data dependency: it runs under its own
    # postgresql.service (NOT PartOf pretzel.target) so it deliberately stays up
    # across `./pretzel stop` and deploys — the database must not be torn down.
    pg_active = subprocess.run(
        ["systemctl", "is-active", "--quiet", PG_SERVICE], check=False
    ).returncode == 0
    print(f"[*] {PG_SERVICE}: {'running (left up by design)' if pg_active else 'not running'}")

if __name__ == "__main__":
    run()
