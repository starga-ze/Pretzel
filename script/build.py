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
    
    # 1. Ensure only the BUILD-time dependencies (toolchain + source libs + libpq-dev)
    #    are present. Runtime services (Prometheus/Grafana/PostgreSQL server/pgAdmin)
    #    are NOT installed here — those belong to `./pretzel install`. This keeps a
    #    plain build fast and free of service-provisioning side effects.
    try:
        install_module = importlib.import_module("script.install")
        if hasattr(install_module, "run_build_deps"):
            print("[*] Ensuring build dependencies are installed...")
            install_module.run_build_deps()
    except Exception as e:
        print(f"[Error] Failed to load/run dependency installation: {e}")
        return

    # 2. Create build-specific directory and run CMake Configure
    os.makedirs(BUILD_DIR, exist_ok=True)
    run_cmd(["cmake", ROOT_DIR], cwd=BUILD_DIR, msg="Configuring main project with CMake")
    
    # 3. Run Make Build utilizing multi-cores (ex: make -j8)
    run_cmd(["make", MAKE_JOBS], cwd=BUILD_DIR, msg=f"Compiling main project with {NUM_CORES} jobs")
    print("[*] Project build complete")

def run():
    build_project()

if __name__ == "__main__":
    run()
