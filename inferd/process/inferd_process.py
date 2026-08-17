"""Process-level concerns: config, logging, pidfile, signals.

Everything here is about being a well-behaved unit under systemd, and none of it is
about retrieval. Same split as the C++ Process layer.
"""

import json
import logging
import logging.handlers
import os
import signal
import sys
from pathlib import Path

log = logging.getLogger("inferd.process")

# Deployed location first, then the source tree for a dev run. start.py installs the
# config to /etc/pretzel — the same file every C++ daemon reads — so this must point
# there and not at the lib directory the daemon happens to live in.
CONFIG_PATH = Path("/etc/pretzel/startup-config.json")
DEV_CONFIG_PATH = Path(__file__).resolve().parents[2] / "config" / "startup-config.json"

DEFAULTS = {
    "daemon_name": "pz-inferd",
    "system": {"logger": {"name": "pz-inferd", "file": "/var/log/pretzel/inferd.log"}},
    "service": {
        "ipc": {"socket_path": "/run/pretzel/ipcd.sock", "poll_timeout_sec": 1.0},
        "gateway": {
            "host": "aigw.portkey.ai", "port": 443, "path": "/v1/chat/completions",
            "api_key_env": "PZ_PORTKEY_API_KEY", "api_key_header": "x-portkey-api-key",
            "models": [], "default_model": "gpt-4o", "headers": {},
            "system_prompt": "", "max_tokens": 512, "timeout_sec": 45,
        },
        "retrieval": {
            "model": "intfloat/multilingual-e5-small",
            "table": "rag_chunk",
            "database": {"host": "127.0.0.1", "port": 5432, "name": "pretzel_rag", "user": "pretzel"},
        },
    },
}


def _merge(base, override):
    """Config is a partial overlay on DEFAULTS, so a key nobody set still has a value.
    A missing config file is a valid state — the defaults are the deployment shape."""
    out = dict(base)
    for k, v in (override or {}).items():
        out[k] = _merge(base[k], v) if isinstance(v, dict) and isinstance(base.get(k), dict) else v
    return out


def load_config():
    path = CONFIG_PATH if CONFIG_PATH.exists() else DEV_CONFIG_PATH
    try:
        raw = json.loads(path.read_text(encoding="utf-8")).get("inferd", {})
    except (OSError, json.JSONDecodeError) as e:
        print(f"config unreadable ({path}): {e} — using defaults", file=sys.stderr)
        raw = {}
    return _merge(DEFAULTS, raw)


def milestone(log, msg, *args):
    """Log a line that has to reach the journal as well as the log file.

    Reserved for the handful of events that answer "is this daemon healthy" — started,
    encoder ready, corpus attached, registered, stopped. Anything that answers "what did it
    do" is an ordinary log.info() and stays in the file.
    """
    log.info(msg, *args, extra={"lifecycle": True})


class _JournalFilter(logging.Filter):
    """Selects the subset of the log that is worth putting in the journal.

    `journalctl -u pz-inferd` is the first thing anyone runs, and it has one question behind
    it: is this daemon up and working. Answering it with the full log would bury that in
    per-turn detail; answering it with nothing — which is what happened before this existed —
    left the command showing only whichever progress bar a third-party library had leaked to
    stderr, rendered as "[229B blob data]" because of the carriage returns in it.

    So: anything at WARNING or above, plus the lines milestone() marks, and nothing else.
    """

    def filter(self, record):
        return record.levelno >= logging.WARNING or getattr(record, "lifecycle", False)


def setup_logging(config, verbose=False):
    """The full log goes to a file when its directory is writable, stderr otherwise.

    Deployed, that means two sinks rather than one: the file gets everything, and the journal
    gets the milestones and the failures through _JournalFilter. The duplication is deliberate
    and bounded — an operator reading `systemctl status` must be able to tell a daemon that is
    working from one that is wedged, without first being told which file to open.
    """
    fmt = logging.Formatter("[%(asctime)s.%(msecs)03d][%(levelname)s][%(name)s] %(message)s",
                            datefmt="%H:%M:%S")
    root = logging.getLogger("inferd")
    root.setLevel(logging.DEBUG if verbose else logging.INFO)
    root.handlers.clear()

    target = config.get("system", {}).get("logger", {}).get("file", "")
    handler = None
    if target:
        try:
            Path(target).parent.mkdir(parents=True, exist_ok=True)
            handler = logging.handlers.RotatingFileHandler(
                target, maxBytes=16 * 1024 * 1024, backupCount=5)
        except OSError:
            handler = None

    if handler is None:
        # No usable file: stderr carries everything, and adding the filtered handler below
        # would print each milestone twice.
        handler = logging.StreamHandler(sys.stderr)
        handler.setFormatter(fmt)
        root.addHandler(handler)
        return root

    handler.setFormatter(fmt)
    root.addHandler(handler)

    journal = logging.StreamHandler(sys.stderr)
    journal.setFormatter(fmt)
    journal.addFilter(_JournalFilter())
    root.addHandler(journal)
    return root


class PidFile:
    """Written for parity with the C++ daemons, which all place one under /run/pretzel.
    It is not the thing that prevents a second instance — systemd is — so an
    unwritable path is a warning, not a failure to start."""

    def __init__(self, name="inferd"):
        self._path = Path(f"/run/pretzel/{name}.pid")

    def __enter__(self):
        try:
            self._path.parent.mkdir(parents=True, exist_ok=True)
            self._path.write_text(f"{os.getpid()}\n")
        except OSError as e:
            log.warning("could not write pidfile %s: %s", self._path, e)
        return self

    def __exit__(self, *exc):
        try:
            self._path.unlink(missing_ok=True)
        except OSError:
            pass
        return False


def set_process_title(name="pz-inferd"):
    """Make the daemon findable the way every other one is.

    The C++ daemons are binaries named pz-*, so `ps aux | grep pz` lists the platform.
    A Python daemon shows up as its interpreter, and "/opt/pretzel/..." contains no "pz"
    substring — so this daemon silently vanished from that search. That is an operability
    regression, not a cosmetic one: the first thing anyone does to check what is running
    is exactly that grep.
    """
    try:
        import setproctitle
        setproctitle.setproctitle(name)
    except ImportError:
        # comm is capped at 15 chars and only changes what `ps -e` shows, not the full
        # cmdline — a partial fix, but better than none if the package is missing.
        try:
            Path("/proc/self/comm").write_text(name[:15])
        except OSError:
            pass


def install_signal_handlers(core):
    """SIGTERM is how systemd stops us. Handled rather than defaulted so the loop
    finishes its current frame and closes the socket instead of vanishing mid-reply."""
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda s, _f: core.stop(signal.Signals(s).name))
