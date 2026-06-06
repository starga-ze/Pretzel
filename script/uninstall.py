"""
script/uninstall.py

Script used to perform a Hard Reset by completely removing both the build artifacts (build)
and all downloaded/built 3rd-party dependencies.
"""

import os
import shutil
from script.utils import BUILD_DIR, THIRD_PARTY_DIR

def run():
    print("[*] Starting uninstallation...")
    
    # 1. Remove proprietary code build artifacts
    if os.path.exists(BUILD_DIR):
        shutil.rmtree(BUILD_DIR)
        print(f"[*] Removed build directory...: {BUILD_DIR}")
        
    # 2. Batch remove downloaded external library source code and compilation artifacts
    if os.path.exists(THIRD_PARTY_DIR):
        shutil.rmtree(THIRD_PARTY_DIR)
        print(f"[*] Removed all 3rd party dependencies...: {THIRD_PARTY_DIR}")
    else:
        print("[*] 3rd party folder already clean.")
        
    print("[*] All project and dependency files uninstalled successfully.")

if __name__ == "__main__":
    run()
