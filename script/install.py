"""
script/install.py

Downloads and builds system packages (APT) and 3rd-party C++ dependencies required to run the project.
Installed libraries are isolated under the `3rd_party/install/` directory.
"""

import os
import sys
import glob
import subprocess
import shutil

from script.utils import (
    INSTALL_ROOT, NUM_CORES, MAKE_JOBS, run_cmd, download_and_extract, build_cmake_project,
    OPENSSL_VERSION, OPENSSL_DIR, OPENSSL_INSTALL, OPENSSL_TAR, OPENSSL_SRC_PATH,
    SPDLOG_VERSION, SPDLOG_DIR, SPDLOG_INSTALL, SPDLOG_TAR, SPDLOG_SRC_PATH,
    BOOST_VERSION, BOOST_VERSION_UNDERSCORE, BOOST_DIR, BOOST_INSTALL, BOOST_TAR, BOOST_SRC_PATH,
    JSON_VERSION, JSON_DIR, JSON_INSTALL, JSON_TAR, JSON_SRC_PATH,
    GTEST_VERSION, GTEST_DIR, GTEST_INSTALL, GTEST_TAR, GTEST_SRC_PATH,
    PROMETHEUS_VERSION, PROMETHEUS_DIR, PROMETHEUS_TAR, PROMETHEUS_SRC_PATH,
    NODE_EXPORTER_VERSION, NODE_EXPORTER_DIR, NODE_EXPORTER_TAR, NODE_EXPORTER_SRC_PATH,
    POSTGRES_EXPORTER_VERSION, POSTGRES_EXPORTER_DIR, POSTGRES_EXPORTER_TAR, POSTGRES_EXPORTER_SRC_PATH,
    PG_SERVICE, PG_DB_NAME, PG_DB_USER, PG_DB_PASSWORD,
    PGADMIN_VERSION, PGADMIN_VENV,
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

def install_googletest():
    """
    Installs GoogleTest, used by tests/ and by nothing else.

    gmock is skipped: the current suite asserts on pure functions and needs no mock objects.
    Turn BUILD_GMOCK back on when a test needs to stand in for Database or the IPC client.
    """
    if os.path.exists(os.path.join(GTEST_INSTALL, "lib", "cmake", "GTest", "GTestConfig.cmake")):
        print("[*] googletest already installed, skipping...")
        return

    url = f"https://github.com/google/googletest/archive/refs/tags/v{GTEST_VERSION}.tar.gz"
    download_and_extract(url, GTEST_TAR, GTEST_DIR, "Extracting googletest")

    build_cmake_project(
        src_path=GTEST_SRC_PATH,
        install_prefix=GTEST_INSTALL,
        extra_args=["-DBUILD_GMOCK=OFF", "-DINSTALL_GTEST=ON"],
    )
    print("[*] googletest installation complete.")


def install_prometheus():
    """Downloads the Prometheus binary for monitoring (uses pre-compiled binary distribution)."""
    if os.path.exists(os.path.join(PROMETHEUS_SRC_PATH, "prometheus")):
        print("[*] Prometheus already downloaded and extracted, skipping...")
        return

    url = f"https://github.com/prometheus/prometheus/releases/download/v{PROMETHEUS_VERSION}/prometheus-{PROMETHEUS_VERSION}.linux-amd64.tar.gz"
    download_and_extract(url, PROMETHEUS_TAR, PROMETHEUS_DIR, "Extracting Prometheus")
    print("[*] Prometheus installation complete.")

def install_node_exporter():
    """Downloads the Node Exporter binary for host metrics collection (pre-compiled binary distribution)."""
    if os.path.exists(os.path.join(NODE_EXPORTER_SRC_PATH, "node_exporter")):
        print("[*] Node Exporter already downloaded and extracted, skipping...")
        return

    url = f"https://github.com/prometheus/node_exporter/releases/download/v{NODE_EXPORTER_VERSION}/node_exporter-{NODE_EXPORTER_VERSION}.linux-amd64.tar.gz"
    download_and_extract(url, NODE_EXPORTER_TAR, NODE_EXPORTER_DIR, "Extracting Node Exporter")
    print("[*] Node Exporter installation complete.")

def install_postgres_exporter():
    """Downloads the Postgres Exporter binary for PostgreSQL metrics collection (pre-compiled binary distribution)."""
    if os.path.exists(os.path.join(POSTGRES_EXPORTER_SRC_PATH, "postgres_exporter")):
        print("[*] Postgres Exporter already downloaded and extracted, skipping...")
        return

    url = f"https://github.com/prometheus-community/postgres_exporter/releases/download/v{POSTGRES_EXPORTER_VERSION}/postgres_exporter-{POSTGRES_EXPORTER_VERSION}.linux-amd64.tar.gz"
    download_and_extract(url, POSTGRES_EXPORTER_TAR, POSTGRES_EXPORTER_DIR, "Extracting Postgres Exporter")
    print("[*] Postgres Exporter installation complete.")

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


def is_postgresql_installed():
    """Checks whether the PostgreSQL client/server is already installed."""
    return shutil.which("psql") is not None


def _pg_row_exists(check_sql):
    """Runs a SELECT as the postgres superuser; True if it returns a '1' row."""
    out = subprocess.run(
        ["sudo", "-u", "postgres", "psql", "-tAc", check_sql],
        capture_output=True, text=True,
    )
    return out.returncode == 0 and out.stdout.strip() == "1"


def provision_postgresql():
    """
    Idempotently creates the dedicated 'pretzel' login role and database.
    Run as the 'postgres' superuser via peer auth; pz-mgmtd later connects to
    this role over localhost TCP using the password in running-config.json.
    """
    # 1. Login role. PG_DB_PASSWORD is the single source of truth (start.py injects the
    # same value into startup-config + the exporter env file), so we must re-sync the
    # role's password on every run — otherwise a pre-existing role keeps its original
    # password while the deployed config moves to a new one, and mgmtd / exporter /
    # pgAdmin all fail auth. Create when missing, ALTER to re-sync when it already exists.
    if _pg_row_exists(f"SELECT 1 FROM pg_roles WHERE rolname='{PG_DB_USER}'"):
        run_cmd(
            ["sudo", "-u", "postgres", "psql", "-c",
             f"ALTER ROLE {PG_DB_USER} LOGIN PASSWORD '{PG_DB_PASSWORD}'"],
            msg=f"Re-syncing PostgreSQL role '{PG_DB_USER}' password",
        )
    else:
        run_cmd(
            ["sudo", "-u", "postgres", "psql", "-c",
             f"CREATE ROLE {PG_DB_USER} LOGIN PASSWORD '{PG_DB_PASSWORD}'"],
            msg=f"Creating PostgreSQL role '{PG_DB_USER}'",
        )

    # 2. Database owned by that role
    if _pg_row_exists(f"SELECT 1 FROM pg_database WHERE datname='{PG_DB_NAME}'"):
        print(f"[*] PostgreSQL database '{PG_DB_NAME}' already exists, skipping...")
    else:
        run_cmd(
            ["sudo", "-u", "postgres", "createdb", "-O", PG_DB_USER, PG_DB_NAME],
            msg=f"Creating PostgreSQL database '{PG_DB_NAME}' (owner={PG_DB_USER})",
        )

    # 3. Grant the built-in pg_monitor role so postgres_exporter (pz-postgres-exporter,
    # connecting as this same role) can read the full pg_stat_* views for Grafana.
    # Idempotent: GRANT on an already-granted role is a no-op.
    run_cmd(
        ["sudo", "-u", "postgres", "psql", "-c", f"GRANT pg_monitor TO {PG_DB_USER}"],
        msg=f"Granting pg_monitor to '{PG_DB_USER}' (for postgres_exporter)",
    )


def is_libpq_dev_installed():
    """Checks whether the libpq client dev headers (libpq-fe.h) are present."""
    return any(os.path.exists(p) for p in (
        "/usr/include/libpq-fe.h",
        "/usr/include/postgresql/libpq-fe.h",
    ))


def install_libpq_dev():
    """
    Installs ONLY the PostgreSQL client dev library (libpq-dev) — the headers/lib the
    C++ layer (shared/db) links against via CMake's find_package(PostgreSQL). Split
    out from install_postgresql() so a pure build can compile without pulling in the
    full PostgreSQL server + provisioning.
    """
    if is_libpq_dev_installed():
        print("[*] libpq-dev already present, skipping...")
        return

    print("[*] Installing libpq-dev (PostgreSQL client headers)...")
    run_cmd(["sudo", "apt", "update"])
    run_cmd(["sudo", "apt", "install", "-y", "libpq-dev"])


def is_libsnmp_dev_installed():
    """Checks whether the Net-SNMP dev headers (net-snmp-config.h) are present."""
    return os.path.exists("/usr/include/net-snmp/net-snmp-config.h")


def install_libsnmp_dev():
    """
    Installs ONLY the Net-SNMP dev library (libsnmp-dev) — the headers/lib that
    pz-scand compiles against (#include <net-snmp/...>) and links against (-lnetsnmp,
    see scand/CMakeLists.txt). Same build-time-only pattern as install_libpq_dev():
    a distro dev package rather than a from-source 3rd_party build.
    """
    if is_libsnmp_dev_installed():
        print("[*] libsnmp-dev already present, skipping...")
        return

    print("[*] Installing libsnmp-dev (Net-SNMP headers)...")
    run_cmd(["sudo", "apt", "update"])
    run_cmd(["sudo", "apt", "install", "-y", "libsnmp-dev"])


def is_xmlsec_dev_installed():
    """Checks whether the xmlsec1 dev package (its pkg-config file) is present."""
    return bool(glob.glob("/usr/lib/*/pkgconfig/xmlsec1-openssl.pc") or
                glob.glob("/usr/lib/pkgconfig/xmlsec1-openssl.pc"))


def install_xmlsec_dev():
    """
    Installs the SAML XML-DSig build dependencies for pz-authd: libxmlsec1-dev (XML signature
    verification), zlib1g-dev (HTTP-Redirect AuthnRequest DEFLATE) and pkg-config, which
    authd/CMakeLists.txt needs to locate xmlsec via pkg_check_modules().

    Same build-time-only pattern as install_libpq_dev(): distro dev packages rather than a
    from-source 3rd_party build. pkg-config is NOT part of build-essential, so it has to be
    named explicitly or a bare machine fails at authd's find_package(PkgConfig REQUIRED).
    """
    if is_xmlsec_dev_installed() and shutil.which("pkg-config") is not None:
        print("[*] xmlsec/zlib dev packages already present, skipping...")
        return

    print("[*] Installing libxmlsec1-dev, zlib1g-dev, pkg-config (SAML build deps)...")
    run_cmd(["sudo", "apt", "update"])
    run_cmd(["sudo", "apt", "install", "-y", "libxmlsec1-dev", "zlib1g-dev", "pkg-config"])


def install_postgresql():
    """
    Installs PostgreSQL server + client dev library (libpq-dev for the C++ layer)
    via APT and provisions the pretzel role/database. The server runs under the
    distro-managed postgresql.service (not wrapped as a pz-* unit).
    """
    if is_postgresql_installed():
        print("[*] PostgreSQL already installed, skipping apt install...")
    else:
        print("[*] Installing PostgreSQL...")
        run_cmd(["sudo", "apt", "update"])
        run_cmd(["sudo", "apt", "install", "-y",
                 "postgresql", "postgresql-contrib", "libpq-dev"])
        if not is_postgresql_installed():
            print("[ERROR] PostgreSQL installation failed.")
            sys.exit(1)

    # The cluster must be up to provision the role/database.
    run_cmd(["sudo", "systemctl", "enable", "--now", PG_SERVICE],
            msg="Enabling and starting postgresql.service")

    provision_postgresql()
    print("[*] PostgreSQL installation complete.")


def is_pgadmin_installed():
    """Checks whether pgAdmin is already installed in its dedicated virtualenv."""
    return os.path.isfile(os.path.join(PGADMIN_VENV, "bin", "gunicorn")) and \
        bool(glob.glob(os.path.join(PGADMIN_VENV, "lib", "python*", "site-packages", "pgadmin4")))


def install_pgadmin():
    """
    Installs pgAdmin 4 (web/server mode) into a dedicated Python virtualenv via pip.
    It is run headless under pz-pgadmin.service (gunicorn) — see script/start.py.
    The apt pgadmin4-web package is deliberately avoided: it pulls in Apache and an
    interactive setup-web.sh, which do not fit this project's unattended
    pz-*.service deployment model.
    """
    if is_pgadmin_installed():
        print("[*] pgAdmin already installed, skipping...")
        return

    print("[*] Installing pgAdmin...")

    # venv + headers needed to build pgAdmin's deps (psycopg, etc.) if no wheel.
    # libpq-dev is also pulled in by install_postgresql(); harmless to ensure here.
    run_cmd(["sudo", "apt", "install", "-y", "python3-venv", "python3-dev", "libpq-dev"])

    os.makedirs(os.path.dirname(PGADMIN_VENV), exist_ok=True)
    run_cmd(["python3", "-m", "venv", PGADMIN_VENV], msg="Creating pgAdmin virtualenv")

    pip = os.path.join(PGADMIN_VENV, "bin", "pip")
    run_cmd([pip, "install", "--upgrade", "pip", "wheel"], msg="Upgrading pip in pgAdmin venv")
    run_cmd([pip, "install", f"pgadmin4=={PGADMIN_VERSION}", "gunicorn"],
            msg=f"Installing pgAdmin4 {PGADMIN_VERSION} + gunicorn")

    if not is_pgadmin_installed():
        print("[ERROR] pgAdmin installation failed.")
        sys.exit(1)

    print("[*] pgAdmin installation complete.")


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

def install_build_deps():
    """
    Build-time ONLY dependencies: the compiler toolchain, the C++ source libraries,
    and libpq-dev headers — everything needed to COMPILE the project (CMake +
    shared/db link against these). Does NOT install the runtime services, and does not
    install test-only libraries (see install_test_deps) — nothing here may require root,
    because script/build.py calls this unprivileged and a plain build must stay
    fast and side-effect-free.
    """
    install_system_packages()
    install_libpq_dev()
    install_libsnmp_dev()
    install_xmlsec_dev()
    install_openssl()
    install_spdlog()
    install_boost()
    install_json()


def install_test_deps():
    """
    Dependencies for tests/ only. Deliberately NOT part of install_build_deps(): no daemon
    links GoogleTest, and `./pretzel build` runs unprivileged, so fetching into the
    root-owned 3rd_party/ would fail there and block an ordinary build.

    Failure is a warning, never fatal — CMake finds GoogleTest with find_package(..., QUIET)
    and skips tests/ when it is absent, so the product still builds and installs.

    SystemExit is caught alongside Exception on purpose: run_cmd() and download_and_extract()
    report failure by calling sys.exit(), which raises SystemExit — and that derives from
    BaseException, not Exception, so catching Exception alone would let a failed download or
    build abort the whole install. KeyboardInterrupt is deliberately NOT caught, so Ctrl+C
    during a slow download still stops everything.
    """
    try:
        install_googletest()
    except (Exception, SystemExit) as e:
        # A SystemExit stringifies to its exit code, which alone reads like nonsense; the real
        # error was already printed by whichever helper bailed out.
        detail = f"exit status {e}" if isinstance(e, SystemExit) else str(e)
        print(f"[WARN] googletest not installed ({detail}) — tests/ will be skipped.")
        print("       The product build is unaffected; run './pretzel install' again to retry.")


def install_runtime_deps():
    """
    Runtime services that the project needs to RUN but not to build: the monitoring
    stack (Prometheus, node/postgres exporters, Grafana), the PostgreSQL server
    (with role/database provisioning) and the pgAdmin web UI. These pull in apt
    packages, download binaries and start systemd units, so they are kept out of the
    build path and only run on a full `./pretzel install`.
    """
    install_prometheus()
    install_node_exporter()
    install_postgres_exporter()
    install_grafana()
    install_postgresql()
    install_pgadmin()


def run():
    """Full install (./pretzel install): build deps + test deps + all runtime services."""
    os.makedirs(INSTALL_ROOT, exist_ok=True)
    install_build_deps()
    install_test_deps()
    install_runtime_deps()
    print("[*] All dependencies installed successfully.")


def run_build_deps():
    """Build-only deps (invoked by script/build.py before CMake/Make)."""
    os.makedirs(INSTALL_ROOT, exist_ok=True)
    install_build_deps()
    print("[*] Build dependencies ready.")


if __name__ == "__main__":
    run()
