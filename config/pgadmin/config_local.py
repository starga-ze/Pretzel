# config/pgadmin/config_local.py
#
# Canonical pgAdmin (server-mode) configuration — version-controlled here, the same
# way config/grafana/grafana.ini and config/prometheus/prometheus.yml are. This is
# the single source of truth for pgAdmin's settings. script/start.py deploys it:
#   - into pgAdmin's package dir, where pgAdmin reads `config_local.py` at startup
#   - into /etc/pretzel/pgadmin/ for parity/visibility with the other services
# and reads the PZ_PGADMIN_LISTEN_* values below to bind gunicorn. Edit here, then
# re-run `./pretzel start`.

import os

# Multi-user web mode (login required) rather than single-user desktop mode.
SERVER_MODE = True

# Where the pgAdmin web UI listens. These PZ_-prefixed names are read by
# script/start.py to generate the gunicorn `--bind`; DEFAULT_SERVER/PORT mirror them
# so pgAdmin's own view of the binding stays consistent.
PZ_PGADMIN_LISTEN_ADDRESS = "0.0.0.0"
PZ_PGADMIN_LISTEN_PORT = 5050
DEFAULT_SERVER = PZ_PGADMIN_LISTEN_ADDRESS
DEFAULT_SERVER_PORT = PZ_PGADMIN_LISTEN_PORT

# All writable runtime state lives under the pretzel state/log roots, consistent
# with the other services.
DATA_DIR = "/var/lib/pretzel/pgadmin"
LOG_FILE = "/var/log/pretzel/pgadmin.log"
SQLITE_PATH = os.path.join(DATA_DIR, "pgadmin4.db")
SESSION_DB_PATH = os.path.join(DATA_DIR, "sessions")
STORAGE_DIR = os.path.join(DATA_DIR, "storage")
AZURE_CREDENTIAL_CACHE_DIR = os.path.join(DATA_DIR, "azurecredentialcache")
KERBEROS_CCACHE_DIR = os.path.join(DATA_DIR, "kerberos")
