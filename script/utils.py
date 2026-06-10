"""
script/utils.py

Defines global constants (paths, versions, etc.) and common utility functions for the Pretzel project.
Contains core utilities relied upon by other scripts (install, build, start, etc.).
"""

import os
import subprocess
import sys
import shutil
import urllib.request
import tarfile

# ---------------------------------------------------------
# 1. Directory and Path Configuration
# ---------------------------------------------------------
SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, os.pardir))

BUILD_DIR = os.path.join(ROOT_DIR, "build")
CERT_DIR = os.path.join(ROOT_DIR, "cert")

INSTALL_ROOT = os.path.join(ROOT_DIR, "3rd_party", "install")
THIRD_PARTY_DIR = os.path.join(ROOT_DIR, "3rd_party")

# ---------------------------------------------------------
# 2. 3rd-Party Dependency Versions and Paths
# ---------------------------------------------------------
OPENSSL_VERSION = "3.2.0"
OPENSSL_DIR = os.path.join(THIRD_PARTY_DIR, "openssl")
OPENSSL_INSTALL = os.path.join(INSTALL_ROOT, "openssl")
OPENSSL_TAR = os.path.join(OPENSSL_DIR, f"openssl-{OPENSSL_VERSION}.tar.gz")
OPENSSL_SRC_PATH = os.path.join(OPENSSL_DIR, f"openssl-{OPENSSL_VERSION}")

SPDLOG_VERSION = "1.13.0"
SPDLOG_DIR = os.path.join(THIRD_PARTY_DIR, "spdlog")
SPDLOG_INSTALL = os.path.join(INSTALL_ROOT, "spdlog")
SPDLOG_TAR = os.path.join(SPDLOG_DIR, f"spdlog-{SPDLOG_VERSION}.tar.gz")
SPDLOG_SRC_PATH = os.path.join(SPDLOG_DIR, f"spdlog-{SPDLOG_VERSION}")

BOOST_VERSION = "1.84.0"
BOOST_VERSION_UNDERSCORE = BOOST_VERSION.replace(".", "_")
BOOST_DIR = os.path.join(THIRD_PARTY_DIR, "boost")
BOOST_INSTALL = os.path.join(INSTALL_ROOT, "boost")
BOOST_TAR = os.path.join(BOOST_DIR, f"boost_{BOOST_VERSION_UNDERSCORE}.tar.gz")
BOOST_SRC_PATH = os.path.join(BOOST_DIR, f"boost_{BOOST_VERSION_UNDERSCORE}")

JSON_VERSION = "3.11.3"
JSON_DIR = os.path.join(THIRD_PARTY_DIR, "nlohmann_json")
JSON_INSTALL = os.path.join(INSTALL_ROOT, "nlohmann_json")
JSON_TAR = os.path.join(JSON_DIR, f"json-{JSON_VERSION}.tar.gz")
JSON_SRC_PATH = os.path.join(JSON_DIR, f"json-{JSON_VERSION}")

PROMETHEUS_VERSION = "3.5.0"
PROMETHEUS_DIR = os.path.join(THIRD_PARTY_DIR, "prometheus")
PROMETHEUS_TAR = os.path.join(PROMETHEUS_DIR, f"prometheus-{PROMETHEUS_VERSION}.linux-amd64.tar.gz")
PROMETHEUS_SRC_PATH = os.path.join(PROMETHEUS_DIR, f"prometheus-{PROMETHEUS_VERSION}.linux-amd64")

NODE_EXPORTER_VERSION = "1.8.2"
NODE_EXPORTER_DIR = os.path.join(THIRD_PARTY_DIR, "node_exporter")
NODE_EXPORTER_TAR = os.path.join(NODE_EXPORTER_DIR, f"node_exporter-{NODE_EXPORTER_VERSION}.linux-amd64.tar.gz")
NODE_EXPORTER_SRC_PATH = os.path.join(NODE_EXPORTER_DIR, f"node_exporter-{NODE_EXPORTER_VERSION}.linux-amd64")

# PostgreSQL is installed from the distro APT repo (not built from source) and
# runs under its own systemd unit (postgresql.service). Pretzel provisions a
# dedicated login role + database; pz-mgmtd connects over localhost TCP.
# These defaults MUST match the "database" block in config/running-config.json.
PG_SERVICE     = "postgresql"        # distro-managed systemd unit
PG_DB_NAME     = "pretzel"
PG_DB_USER     = "pretzel"
PG_DB_PASSWORD = "pretzel"           # localhost-only dev default — harden for prod
PG_DB_HOST     = "127.0.0.1"
PG_DB_PORT     = 5432

# Detect CPU cores for build optimization
NUM_CORES = os.cpu_count() or 1
MAKE_JOBS = f"-j{NUM_CORES}"


# ---------------------------------------------------------
# 3. Common Utility Functions
# ---------------------------------------------------------

def run_cmd(cmd, cwd=ROOT_DIR, msg=None):
    """
    Executes a shell command by spawning a subprocess.
    
    Args:
        cmd (list): List of command arguments (e.g., ['make', 'install']).
        cwd (str): Working directory path to execute the command. Defaults to ROOT_DIR.
        msg (str, optional): Message to print to the console before execution.
        
    Raises:
        SystemExit: Exits the program if the command fails (return code != 0) or is not found.
    """
    if msg:
        print(f"[*] {msg}...")
    try:
        result = subprocess.run(cmd, cwd=cwd)
        if result.returncode != 0:
            print(f"[Error] Command failed: {' '.join(cmd)}")
            sys.exit(result.returncode)
    except FileNotFoundError:
        print(f"[Error] Command not found: {cmd[0]}")
        sys.exit(1)
    except Exception as e:
        print(f"[Error] Unexpected error: {e}")
        sys.exit(1)


def download_and_extract(url, tar_path, dest_dir, msg=None):
    """
    Downloads a file from the specified URL and extracts it.
    Uses Python's built-in urllib to remove external dependencies like wget.
    
    Args:
        url (str): The remote URL of the file to download.
        tar_path (str): The full local path to save the compressed file.
        dest_dir (str): The target directory to extract the contents into.
        msg (str, optional): Log message to print during extraction.
    """
    os.makedirs(dest_dir, exist_ok=True)
    
    # 1. File Download Logic
    if not os.path.exists(tar_path):
        print(f"[*] Downloading: {url}")
        try:
            urllib.request.urlretrieve(url, tar_path)
        except Exception as e:
            print(f"[Error] Download failed: {e}\nPlease check your network connection.")
            sys.exit(1)

    # 2. Extraction Logic (Prevent duplicate extraction)
    # Attempts extraction only if the base directory (without .tar.gz) does not exist
    extract_target = tar_path.replace('.tar.gz', '')
    if not os.path.exists(extract_target) and not os.path.exists(os.path.join(dest_dir, 'prometheus')): 
        print(f"[*] {msg or 'Extracting archive'}...")
        with tarfile.open(tar_path, "r:gz") as tar:
            tar.extractall(path=dest_dir)


def build_cmake_project(src_path, install_prefix, extra_args=None):
    """
    Executes a standard CMake-based C++ project pipeline: Configure -> Build -> Install.
    
    Args:
        src_path (str): Source directory containing the CMakeLists.txt.
        install_prefix (str): Destination path for the built artifacts (-DCMAKE_INSTALL_PREFIX).
        extra_args (list, optional): Additional arguments to pass during CMake Configure.
    """
    build_dir = os.path.join(src_path, "build_temp")
    os.makedirs(build_dir, exist_ok=True)
    
    cmake_cmd = ["cmake", "..", f"-DCMAKE_INSTALL_PREFIX={install_prefix}"]
    if extra_args:
        cmake_cmd.extend(extra_args)
        
    run_cmd(cmake_cmd, cwd=build_dir, msg=f"Configuring project at {src_path}")
    run_cmd(["make", MAKE_JOBS], cwd=build_dir)
    run_cmd(["make", "install"], cwd=build_dir, msg=f"Installing project to {install_prefix}")


def install_file(src, dst, mode=0o644):
    """
    Copies a file to the destination and sets permissions (chmod).
    (Critical) To prevent 'Text file busy (Errno 26)' when overwriting running binaries,
    it removes (unlinks) the existing file before copying.
    
    Args:
        src (str): Source file path.
        dst (str): Destination file path.
        mode (int): File permissions to set (default: 0o644).
        
    Returns:
        bool: False if the source file is missing (skipped), True on success.
    """
    if not os.path.isfile(src):
        return False
        
    # [Fix Error] Unlink (remove) the file first to bypass OS-level locks on running binaries
    if os.path.exists(dst):
        try:
            os.remove(dst)
        except OSError:
            pass
            
    shutil.copy(src, dst)
    os.chmod(dst, mode)
    print(f"[*] Installed: {dst}")
    return True
