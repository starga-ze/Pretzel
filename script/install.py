"""
script/install.py

Downloads and builds system packages (APT) and 3rd-party C++ dependencies required to run the project.
Installed libraries are isolated under the `3rd_party/install/` directory.
"""

import os
import sys
import subprocess
import shutil

from script.utils import (
    INSTALL_ROOT, NUM_CORES, MAKE_JOBS, run_cmd, download_and_extract, build_cmake_project,
    OPENSSL_VERSION, OPENSSL_DIR, OPENSSL_INSTALL, OPENSSL_TAR, OPENSSL_SRC_PATH,
    SPDLOG_VERSION, SPDLOG_DIR, SPDLOG_INSTALL, SPDLOG_TAR, SPDLOG_SRC_PATH,
    BOOST_VERSION, BOOST_VERSION_UNDERSCORE, BOOST_DIR, BOOST_INSTALL, BOOST_TAR, BOOST_SRC_PATH,
    JSON_VERSION, JSON_DIR, JSON_INSTALL, JSON_TAR, JSON_SRC_PATH,
    PROMETHEUS_VERSION, PROMETHEUS_DIR, PROMETHEUS_TAR, PROMETHEUS_SRC_PATH,
)

def install_openssl():
    """Compiles and installs the OpenSSL library from source."""
    # Skip installation if the static library (libssl.a) already exists (idempotency)
    if os.path.exists(os.path.join(OPENSSL_INSTALL, "lib64", "libssl.a")):
        print("[*] OpenSSL already built and installed, skipping...")
        return

    os.makedirs(OPENSSL_INSTALL, exist_ok=True)
    url = f"https://www.openssl.org/source/openssl-{OPENSSL_VERSION}.tar.gz"
    download_and_extract(url, OPENSSL_TAR, OPENSSL_DIR, "Extracting OpenSSL source")

    # Uses OpenSSL's custom Configure script
    run_cmd(["./Configure", "linux-x86_64", "no-shared", f"--prefix={OPENSSL_INSTALL}"], cwd=OPENSSL_SRC_PATH, msg="Configuring OpenSSL")
    run_cmd(["make", MAKE_JOBS], cwd=OPENSSL_SRC_PATH, msg=f"Compiling OpenSSL with {NUM_CORES} jobs")
    run_cmd(["make", "install"], cwd=OPENSSL_SRC_PATH, msg="Installing OpenSSL")
    print("[*] OpenSSL installation complete.")

def install_spdlog():
    """Builds and installs spdlog, a high-performance C++ logging library, using CMake."""
    if os.path.exists(os.path.join(SPDLOG_INSTALL, "lib", "cmake", "spdlog", "spdlogConfig.cmake")):
        print("[*] spdlog already installed, skipping...")
        return

    url = f"https://github.com/gabime/spdlog/archive/refs/tags/v{SPDLOG_VERSION}.tar.gz"
    download_and_extract(url, SPDLOG_TAR, SPDLOG_DIR, "Extracting spdlog source")

    build_cmake_project(
        src_path=SPDLOG_SRC_PATH,
        install_prefix=SPDLOG_INSTALL,
        extra_args=["-DSPDLOG_BUILD_SHARED=OFF", "-DSPDLOG_BUILD_EXAMPLES=OFF", "-DSPDLOG_BUILD_TESTS=OFF"]
    )
    print("[*] spdlog installation complete.")

def install_boost():
    """Installs the C++ Boost library using the b2 engine (primarily utilizing asio and thread features)."""
    if os.path.exists(os.path.join(BOOST_INSTALL, "include", "boost", "asio.hpp")):
        print("[*] Boost already built and installed, skipping...")
        return

    url = f"https://archives.boost.io/release/{BOOST_VERSION}/source/boost_{BOOST_VERSION_UNDERSCORE}.tar.gz"
    download_and_extract(url, BOOST_TAR, BOOST_DIR, "Extracting Boost source")

    # Boost custom bootstrap and build process
    run_cmd(["./bootstrap.sh", f"--prefix={BOOST_INSTALL}"], cwd=BOOST_SRC_PATH, msg="Bootstrapping Boost")
    run_cmd(
        ["./b2", f"-j{NUM_CORES}", "variant=release", "link=static", "threading=multi", "runtime-link=static", "--with-system", "--with-thread", "install"], 
        cwd=BOOST_SRC_PATH, 
        msg="Building Boost"
    )
    print("[*] Boost installation complete.")

def install_json():
    """Installs the nlohmann_json header-only library."""
    if os.path.exists(os.path.join(JSON_INSTALL, "share", "cmake", "nlohmann_json", "nlohmann_jsonConfig.cmake")):
        print("[*] nlohmann_json already installed, skipping...")
        return

    url = f"https://github.com/nlohmann/json/archive/refs/tags/v{JSON_VERSION}.tar.gz"
    download_and_extract(url, JSON_TAR, JSON_DIR, "Extracting nlohmann_json")

    build_cmake_project(
        src_path=JSON_SRC_PATH,
        install_prefix=JSON_INSTALL,
        extra_args=["-DJSON_BuildTests=OFF"]
    )
    print("[*] nlohmann_json installation complete.")

def install_prometheus():
    """Downloads the Prometheus binary for monitoring (uses pre-compiled binary distribution)."""
    if os.path.exists(os.path.join(PROMETHEUS_SRC_PATH, "prometheus")):
        print("[*] Prometheus already downloaded and extracted, skipping...")
        return

    url = f"https://github.com/prometheus/prometheus/releases/download/v{PROMETHEUS_VERSION}/prometheus-{PROMETHEUS_VERSION}.linux-amd64.tar.gz"
    download_and_extract(url, PROMETHEUS_TAR, PROMETHEUS_DIR, "Extracting Prometheus")
    print("[*] Prometheus installation complete.")

def is_grafana_installed():
    """Checks whether Grafana is already installed."""
    candidates = [
        "/usr/share/grafana/bin/grafana",
        "/usr/sbin/grafana-server",
    ]

    return any(os.path.exists(path) for path in candidates)


def install_grafana():
    """Installs Grafana from the official APT repository."""
    if is_grafana_installed():
        print("[*] Grafana already installed, skipping...")
        return

    print("[*] Installing Grafana...")

    run_cmd([
        "sudo",
        "apt",
        "install",
        "-y",
        "apt-transport-https",
        "software-properties-common",
        "wget",
        "gpg",
    ])

    keyring_path = "/usr/share/keyrings/grafana.gpg"
    source_list_path = "/etc/apt/sources.list.d/grafana.list"

    run_cmd([
        "bash",
        "-c",
        f"wget -q -O - https://apt.grafana.com/gpg.key | "
        f"sudo gpg --dearmor -o {keyring_path}"
    ])

    run_cmd([
        "bash",
        "-c",
        f"echo 'deb [signed-by={keyring_path}] https://apt.grafana.com stable main' | "
        f"sudo tee {source_list_path} > /dev/null"
    ])

    run_cmd(["sudo", "apt", "update"])
    run_cmd(["sudo", "apt", "install", "-y", "grafana"])

    if not is_grafana_installed():
        print("[ERROR] Grafana installation failed.")
        sys.exit(1)

    print("[*] Grafana installation complete.")


def get_gpp_version():
    """Extracts the major version of the currently installed g++ compiler."""
    try:
        out = subprocess.check_output(["g++", "--version"], stderr=subprocess.STDOUT)
        return int(out.decode().split("\n")[0].split()[-1].split(".")[0])
    except Exception:
        return 0

def install_system_packages():
    """
    Installs the basic build toolchain and required packages for the OS (Ubuntu/Debian) via APT.
    Groups required packages into a single apt command to optimize speed.
    """
    packages_to_install = []
    
    # 1. Build tools check (make, gcc, cmake, etc.)
    required_tools = ["make", "gcc", "g++", "cmake"]
    if any(shutil.which(tool) is None for tool in required_tools):
        packages_to_install.extend(["build-essential", "cmake"])

    # 2. GCC 9 version check (required for C++17 support)
    if get_gpp_version() < 9:
        packages_to_install.extend(["software-properties-common", "g++-9"])

    # 3. unixODBC check (for DB integration)
    if not os.path.exists("/usr/include/sql.h"):
        packages_to_install.extend(["unixodbc", "unixodbc-dev"])

    # Early exit if no packages need to be installed
    if not packages_to_install:
        print("[*] System dependencies (build tools, GCC >= 9, unixODBC) are already satisfied.")
        return

    print(f"[*] Installing system packages: {', '.join(packages_to_install)}")
    run_cmd(["sudo", "apt", "update"])
    
    # Add PPA repository for GCC-9 if required
    if "g++-9" in packages_to_install:
        run_cmd(["sudo", "add-apt-repository", "-y", "ppa:ubuntu-toolchain-r/test"])
        run_cmd(["sudo", "apt", "update"])

    # Batch install packages
    run_cmd(["sudo", "apt", "install", "-y"] + packages_to_install)

    # Set default gcc version to 9 using update-alternatives
    if "g++-9" in packages_to_install:
        run_cmd(["sudo", "update-alternatives", "--install", "/usr/bin/g++", "g++", "/usr/bin/g++-9", "20"])

def run():
    """The main entry point function that orchestrates the installation process."""
    os.makedirs(INSTALL_ROOT, exist_ok=True)
    
    # Install system packages first (secure the build toolchain)
    install_system_packages()
    
    # Sequentially build 3rd-party source codes
    install_openssl()
    install_spdlog()
    install_boost()
    install_json()
    install_prometheus()
    install_grafana()
    
    print("[*] All dependencies installed successfully.")

if __name__ == "__main__":
    run()
