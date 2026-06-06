"""
script/stop.py

Script to safely shut down the running NF-NMS platform (nf-nms.target).
"""

import subprocess
from script.utils import run_cmd

TARGET = "nf-nms.target"

def run():
    # 1. Check if the target is currently running
    status = subprocess.run(["systemctl", "is-active", "--quiet", TARGET], check=False)
    
    # Skip if not active
    if status.returncode != 0:
        print("nf-nms.target not running")
        return

    # 2. Request target daemon to stop
    run_cmd(["systemctl", "stop", TARGET])
    
    # 3. Output final status after termination
    subprocess.run(["systemctl", "status", TARGET, "-n", "0", "--no-pager"])

if __name__ == "__main__":
    run()
