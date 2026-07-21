"""
script/build.py

Project source code compilation script.
Verifies prior dependencies (install.py) and builds the main project using CMake and Make.
"""

import os
import importlib
from script.utils import ROOT_DIR, BUILD_DIR, MAKE_JOBS, NUM_CORES, run_cmd

def build_project():
    """Configures the CMake environment and compiles the project."""
    
    try:
        install_module = importlib.import_module("script.install")
        if hasattr(install_module, "run_build_deps"):
            print("[*] Ensuring build dependencies are installed...")
            install_module.run_build_deps()
    except Exception as e:
        print(f"[Error] Failed to load/run dependency installation: {e}")
        return

    os.makedirs(BUILD_DIR, exist_ok=True)
    run_cmd(["cmake", ROOT_DIR], cwd=BUILD_DIR, msg="Configuring main project with CMake")
    
    run_cmd(["make", MAKE_JOBS], cwd=BUILD_DIR, msg=f"Compiling main project with {NUM_CORES} jobs")
    print("[*] Project build complete")

def run():
    build_project()

if __name__ == "__main__":
    run()
