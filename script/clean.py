"""
script/clean.py

Cleans up caches, object files, and compilation artifacts generated during the build process.
Preserves installed 3rd-party libraries (3rd_party/install) to save time on the next build.
"""

import os
import shutil
from script.utils import BUILD_DIR

def run():
    # Recursively deletes the entire build/ folder containing build artifacts.
    if os.path.exists(BUILD_DIR):
        shutil.rmtree(BUILD_DIR)
        print("[*] Build folder cleaned (3rd party installs preserved)")
    else:
        print("[*] Build folder already clean.")

if __name__ == "__main__":
    run()
