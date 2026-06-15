#!/usr/bin/env python3
"""Fetch the IEEE OUI (MA-L) registry and emit a compact lookup table.

Output: shared/data/oui.tsv with one "aabbcc<TAB>Vendor" line per 24-bit OUI
(lowercase, no separators). engined's OuiResolver loads this at startup to map a
host MAC (learned via ARP) to its vendor, which lets us identify SNMP-less hosts
(laptops, APs, phones, …).

Usage: python3 script/fetch_oui.py [output_path]
Falls back to a small curated subset if the IEEE list cannot be downloaded, so the
build/deploy never hard-fails on a missing network.
"""
import csv
import io
import os
import sys
import urllib.request

IEEE_URL = "https://standards-oui.ieee.org/oui/oui.csv"

# Minimal fallback (common vendors) used only when the download fails.
FALLBACK = {
    "0050569": None,  # placeholder; real entries are 6 hex chars below
    "005056": "VMware, Inc.",
    "001c14": "VMware, Inc.",
    "000c29": "VMware, Inc.",
    "ec58ea": "Ruckus Wireless",
    "684f64": "Dell Inc.",
    "f4ee08": "Dell Inc.",
    "4cd98f": "Dell Inc.",
    "6015 2b": "Palo Alto Networks",
    "60152b": "Palo Alto Networks",
    "8c367a": "Hewlett Packard",
    "001b21": "Intel Corporate",
    "a4c3f0": "Intel Corporate",
    "f018 98": "Apple, Inc.",
    "f01898": "Apple, Inc.",
    "00000c": "Cisco Systems",
    "0027e3": "Aruba Networks",
}


def norm(mac6: str):
    h = "".join(c for c in mac6 if c in "0123456789abcdefABCDEF").lower()
    return h if len(h) == 6 else None


def fetch():
    req = urllib.request.Request(IEEE_URL, headers={"User-Agent": "pretzel-oui/1.0"})
    with urllib.request.urlopen(req, timeout=30) as r:
        data = r.read().decode("utf-8", "replace")
    out = {}
    for row in csv.DictReader(io.StringIO(data)):
        assign = norm(row.get("Assignment", ""))
        org = (row.get("Organization Name", "") or "").strip()
        if assign and org:
            out[assign] = org
    return out


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(__file__), "..", "shared", "data", "oui.tsv")
    out_path = os.path.abspath(out_path)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)

    try:
        table = fetch()
        src = "IEEE"
    except Exception as e:  # noqa: BLE001
        sys.stderr.write(f"[fetch_oui] download failed ({e}); using fallback subset\n")
        table = {k: v for k, v in FALLBACK.items() if norm(k) and v}
        src = "fallback"

    with open(out_path, "w", encoding="utf-8") as f:
        for prefix in sorted(table):
            f.write(f"{prefix}\t{table[prefix]}\n")

    sys.stderr.write(f"[fetch_oui] wrote {len(table)} entries ({src}) -> {out_path}\n")


if __name__ == "__main__":
    main()
