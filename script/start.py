"""
script/start.py

A complete deployment pipeline script that deploys built binaries, configuration files, 
and certificates to the actual operational paths (/opt/nf-nms, /etc/nf-nms) and runs the services via systemd.
"""

import os
import sys
import subprocess
import time
from script.utils import ROOT_DIR, BUILD_DIR, CERT_DIR, run_cmd, install_file, PROMETHEUS_SRC_PATH

# List of child systemd services targeted for start/restart
DAEMONS = [
    "nf-ipcd.service", "nf-engined.service", "nf-authd.service", "nf-mgmtd.service",
    "nf-icmpd.service", "nf-snmpd.service", "nf-topologyd.service",
    "nf-prometheus.service", "nf-grafana.service",
]

# Defines source and destination paths for deployment
BUILD_BIN_DIR = os.path.join(BUILD_DIR, "bin")
SERVICE_DIR = os.path.join(os.path.dirname(__file__), "service")
SYSTEMD_DIR = "/etc/systemd/system"
INSTALL_BIN_DIR = "/opt/nf-nms/bin"
ETC_ROOT_DIR = "/etc/nf-nms"
CERT_INSTALL_DIR = os.path.join(ETC_ROOT_DIR, "cert")

PROMETHEUS_CONFIG_DIR = os.path.join(ETC_ROOT_DIR, "prometheus")
PROMETHEUS_DATA_DIR = "/var/lib/nf-nms/prometheus"
GRAFANA_CONFIG_DIR = os.path.join(ETC_ROOT_DIR, "grafana")
TARGET = "nf-nms.target"

def pre_flight_checks():
    """
    Validates that all required files (built binaries, external dependencies, etc.) 
    are ready before service deployment.
    """
    # 1. Validate built binaries
    if not os.path.isdir(BUILD_BIN_DIR) or not os.listdir(BUILD_BIN_DIR):
        sys.exit("[ERROR] build/bin not found or empty. Run build first.")
        
    # 2. Validate essential Prometheus files
    required_prometheus = [
        os.path.join(PROMETHEUS_SRC_PATH, "prometheus"),
        os.path.join(ROOT_DIR, "prometheus", "prometheus.yml")
    ]
    if not all(os.path.isfile(p) for p in required_prometheus):
        sys.exit("[ERROR] Required prometheus files not found.")

    # 3. Validate Grafana binary installation (assumes installation via OS package manager)
    if not any(os.path.exists(p) for p in ["/usr/share/grafana/bin/grafana", "/usr/sbin/grafana-server"]):
        sys.exit("[ERROR] Grafana binary not found. Install first: sudo apt install -y grafana")


def stop_services():
    """
    [Core Logic] Completely stops existing service processes before deployment.
    Services MUST be brought down before copying files to prevent OS-level locks 
    (Text file busy, Errno 26) when overwriting running binaries.
    """
    print("[*] Stopping existing services...")
    subprocess.run(["systemctl", "stop", TARGET], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    # Delay to ensure processes fully terminate and file locks are released
    time.sleep(2)


def deploy_files():
    """
    Copies all files required for the operational environment (binaries, configs, service units, etc.) 
    to their destination folders.
    """
    print("[*] Deploying files...")
    
    # Create required destination directories
    os.makedirs(INSTALL_BIN_DIR, exist_ok=True)
    os.makedirs(PROMETHEUS_CONFIG_DIR, exist_ok=True)
    os.makedirs(PROMETHEUS_DATA_DIR, exist_ok=True)
    os.makedirs(GRAFANA_CONFIG_DIR, exist_ok=True)

    # 1. Deploy self-compiled NF binaries (Execution permission 755)
    for f in os.listdir(BUILD_BIN_DIR):
        install_file(os.path.join(BUILD_BIN_DIR, f), os.path.join(INSTALL_BIN_DIR, f), 0o755)
        
    # 2. Deploy Prometheus binary and configuration files
    for name in ["prometheus", "promtool"]:
        install_file(os.path.join(PROMETHEUS_SRC_PATH, name), os.path.join(INSTALL_BIN_DIR, name), 0o755)
    install_file(os.path.join(ROOT_DIR, "prometheus", "prometheus.yml"), os.path.join(PROMETHEUS_CONFIG_DIR, "prometheus.yml"))
    install_file(os.path.join(ROOT_DIR, "prometheus", "web.yml"), os.path.join(PROMETHEUS_CONFIG_DIR, "web.yml"))

    # 3. Deploy Grafana configuration file
    install_file(os.path.join(ROOT_DIR, "grafana", "grafana.ini"), os.path.join(GRAFANA_CONFIG_DIR, "grafana.ini"))

    # 4. Deploy Systemd daemon service files
    for f in os.listdir(SERVICE_DIR):
        if f.endswith((".service", ".target")):
            install_file(os.path.join(SERVICE_DIR, f), os.path.join(SYSTEMD_DIR, f))

    # 5. Deploy certificate files (Private key files (.key) are strictly set to 600 permission)
    if os.path.isdir(CERT_DIR):
        os.makedirs(CERT_INSTALL_DIR, exist_ok=True)
        for f in os.listdir(CERT_DIR):
            mode = 0o600 if f.endswith(".key") or "key" in f.lower() else 0o644
            install_file(os.path.join(CERT_DIR, f), os.path.join(CERT_INSTALL_DIR, f), mode)


def start_services():
    """
    Reloads the Systemd daemon and restarts the services after file deployment is complete.
    """
    print("[*] Starting services...")
    # Reload daemon so systemd recognizes newly copied service files
    run_cmd(["systemctl", "daemon-reload"], msg="Reloading systemd")
    
    # Register for automatic start on OS boot (Enable)
    run_cmd(["systemctl", "enable", TARGET], msg=f"Enabling {TARGET}")
    for svc in DAEMONS:
        run_cmd(["systemctl", "enable", svc], msg=f"Enabling {svc}")
        
    # Restart the service target and check status
    run_cmd(["systemctl", "restart", TARGET], msg=f"Restarting {TARGET}")
    subprocess.run(["systemctl", "status", TARGET, "-n", "0", "--no-pager"], check=False)


def run():
    """Main orchestration logic for the deployment pipeline."""
    pre_flight_checks()
    
    # [CRITICAL ORDER] Stop services -> Overwrite new files -> Start services
    stop_services()
    deploy_files()
    start_services()

if __name__ == "__main__":
    run()
