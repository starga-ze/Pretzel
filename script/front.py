"""
Redeploy only the mgmtd web frontend.

`./pretzel start` runs the whole pipeline — binaries, config, certificates, systemd,
PostgreSQL, pgAdmin — which is far more than a CSS tweak needs. This command copies
mgmtd/www into the served directory and stops there: no daemon restart, no service
churn. mgmtd reads static files through StaticFileCache, so a browser reload picks the
new files up immediately when the cache is in reload mode (see below).

    ./pretzel front

Only the frontend is touched. A change to any C++ source still needs `./pretzel build`
followed by `./pretzel start`.
"""

import filecmp
import os
import shutil

from script.utils import ROOT_DIR

# Must match MGMTD_WWW_INSTALL_DIR in script/start.py and shareDir() in mgmtd/core/MgmtdCore.cpp
# (PRETZEL_SHARE_DIR overrides the latter at runtime).
SHARE_INSTALL_DIR = os.environ.get("PRETZEL_SHARE_DIR", "/opt/pretzel/share")
MGMTD_WWW_INSTALL_DIR = os.path.join(SHARE_INSTALL_DIR, "mgmtd", "www")
MGMTD_WWW_SRC_DIR = os.path.join(ROOT_DIR, "mgmtd", "www")


def _changed_files(src_root, dst_root):
    """Files that differ from the deployed copy, as paths relative to src_root."""
    changed = []
    for dirpath, _dirnames, filenames in os.walk(src_root):
        for name in filenames:
            src = os.path.join(dirpath, name)
            rel = os.path.relpath(src, src_root)
            dst = os.path.join(dst_root, rel)
            if not os.path.exists(dst) or not filecmp.cmp(src, dst, shallow=False):
                changed.append(rel)
    return changed


def run():
    if not os.path.isdir(MGMTD_WWW_SRC_DIR):
        print(f"[*] Error, frontend source not found: {MGMTD_WWW_SRC_DIR}")
        raise SystemExit(1)

    if not os.path.isdir(SHARE_INSTALL_DIR):
        print(f"[*] Error, {SHARE_INSTALL_DIR} does not exist — run './pretzel start' once first.")
        raise SystemExit(1)

    changed = _changed_files(MGMTD_WWW_SRC_DIR, MGMTD_WWW_INSTALL_DIR) \
        if os.path.isdir(MGMTD_WWW_INSTALL_DIR) else None

    if changed is not None and not changed:
        print(f"[*] Frontend already up to date ({MGMTD_WWW_INSTALL_DIR})")
        return

    # Mirror start.py: wipe and re-copy, so a file deleted from the repo also disappears
    # from the served tree rather than lingering.
    if os.path.isdir(MGMTD_WWW_INSTALL_DIR):
        shutil.rmtree(MGMTD_WWW_INSTALL_DIR)
    shutil.copytree(MGMTD_WWW_SRC_DIR, MGMTD_WWW_INSTALL_DIR)

    if changed is None:
        print(f"[*] Deployed frontend -> {MGMTD_WWW_INSTALL_DIR}")
    else:
        print(f"[*] Deployed frontend -> {MGMTD_WWW_INSTALL_DIR} ({len(changed)} file(s) changed)")
        for rel in sorted(changed)[:20]:
            print(f"      {rel}")
        if len(changed) > 20:
            print(f"      ... and {len(changed) - 20} more")

    # StaticFileCache caches file contents at startup unless reload mode is on, so without
    # this a redeploy would appear to do nothing until mgmtd is restarted.
    if os.environ.get("PRETZEL_MGMTD_STATIC_RELOAD", "0") in ("", "0"):
        print("[*] Note: mgmtd caches static files. If the browser still serves the old page,")
        print("    either restart mgmtd (systemctl restart pz-mgmtd) or run it with")
        print("    PRETZEL_MGMTD_STATIC_RELOAD=1 so the cache re-reads from disk.")
