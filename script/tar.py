#!/usr/bin/env python3
"""
script/tar.py

Packages compiled outputs or target source directories into a single tar.gz archive for deployment.
"""

import tarfile
import os

# List of target directories to include in the packaging
TARGET_ITEMS = ['shared', 'ipcd', 'engined', 'icmpd', 'mgmtd', 'authd', 'topologyd']
TAR_FILENAME = "pretzel-ims.tar.gz"

def run():
    root_dir = os.getcwd()
    output_dir = os.path.join(root_dir, "tmp")
    output_path = os.path.join(output_dir, TAR_FILENAME)

    # Create temporary output directory
    os.makedirs(output_dir, exist_ok=True)
    print(f"[*] Archiving targets: {TARGET_ITEMS}\n[*] Output TAR.GZ file: {output_path}")

    # Open tar object in GZIP compression mode
    with tarfile.open(output_path, "w:gz") as tar:
        for target in TARGET_ITEMS:
            full_target_path = os.path.join(root_dir, target)
            
            # Check if target path actually exists before archiving
            if not os.path.exists(full_target_path):
                print(f"  > Skipping target: {target} (Not found)")
                continue
                
            print(f"  > Archiving directory: {target}")
            # Save using relative path names (arcname=target) inside the archive instead of absolute paths
            tar.add(name=full_target_path, arcname=target, recursive=True)

    print(f"\n[*] Successfully generated {TAR_FILENAME} in {output_path}.")

if __name__ == "__main__":
    run()
