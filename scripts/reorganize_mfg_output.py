#!/usr/bin/env python3
"""Move esp-matter-mfg-tool UUID folders into numbered sequence directories.

Transforms:
  mfg_qr_codes/fff2_8001/<uuid>/...
Into:
  mfg_qr_codes/fff2_8001/<seq>/<uuid>/...   (seq = 1, 2, 3, ...)
"""

from __future__ import annotations

import argparse
import csv
import glob
import os
import shutil
import sys
import uuid as uuid_mod


def _project_root() -> str:
    return os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def _vid_pid_dir(vendor_id: int, product_id: int) -> str:
    return f"{vendor_id:04x}_{product_id:04x}"


def _is_already_reorganized(out_top: str) -> bool:
    """True when numbered folders already contain UUID subdirectories."""
    for name in os.listdir(out_top):
        if not name.isdigit():
            continue
        seq_path = os.path.join(out_top, name)
        if not os.path.isdir(seq_path):
            continue
        for child in os.listdir(seq_path):
            child_path = os.path.join(seq_path, child)
            if os.path.isdir(child_path) and child != "internal":
                try:
                    uuid_mod.UUID(child)
                    return True
                except ValueError:
                    continue
    return False


def _uuid_order_from_cn_dacs(out_top: str) -> list[str]:
    """Device order matches row order in cn_dacs-*.csv (CN = UUID)."""
    cn_files = sorted(glob.glob(os.path.join(out_top, "cn_dacs-*.csv")))
    if not cn_files:
        return []

    uuids: list[str] = []
    with open(cn_files[-1], newline="", encoding="utf-8") as f:
        reader = csv.reader(f)
        next(reader, None)
        for row in reader:
            if row and row[0]:
                uuids.append(row[0].strip())
    return uuids


def _uuid_dirs(out_top: str) -> list[str]:
    return [
        name
        for name in os.listdir(out_top)
        if name != "staging"
        and os.path.isdir(os.path.join(out_top, name))
        and not name.isdigit()
    ]


def reorganize(out_top: str, dry_run: bool = False) -> int:
    if not os.path.isdir(out_top):
        print(f"ERROR: output directory not found: {out_top}", file=sys.stderr)
        return 1

    if _is_already_reorganized(out_top):
        print(f"Already reorganized: {out_top}/<1..N>/<uuid>/ (nothing to do)")
        return 0

    uuids = _uuid_order_from_cn_dacs(out_top)
    if not uuids:
        uuids = sorted(
            _uuid_dirs(out_top),
            key=lambda u: os.path.getctime(os.path.join(out_top, u)),
        )
        print("NOTE: cn_dacs-*.csv not found; using folder creation order.", file=sys.stderr)

    if not uuids:
        print(f"ERROR: no device folders under {out_top}", file=sys.stderr)
        return 1

    for seq, uuid in enumerate(uuids, start=1):
        src = os.path.join(out_top, uuid)
        seq_dir = os.path.join(out_top, str(seq))
        dst = os.path.join(seq_dir, uuid)

        if not os.path.isdir(src):
            print(f"ERROR: missing folder for device {seq}: {uuid}", file=sys.stderr)
            return 1
        if os.path.exists(dst):
            print(f"ERROR: already exists (already reorganized?): {dst}", file=sys.stderr)
            return 1

        print(f"  {seq:>3} -> {seq}/{uuid}")
        if not dry_run:
            os.makedirs(seq_dir, exist_ok=True)
            shutil.move(src, dst)

    print(f"Done: {len(uuids)} device folder(s) under {out_top}/<1..{len(uuids)}>/")
    return 0


def main() -> int:
    project_root = _project_root()
    default_outdir = os.path.join(project_root, "mfg_qr_codes")

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--outdir",
        default=default_outdir,
        help=f"mfg-tool output root (default: {default_outdir})",
    )
    parser.add_argument("--vendor-id", type=lambda x: int(x, 0), default=0xFFF2)
    parser.add_argument("--product-id", type=lambda x: int(x, 0), default=0x8001)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    out_top = os.path.join(args.outdir, _vid_pid_dir(args.vendor_id, args.product_id))
    return reorganize(out_top, dry_run=args.dry_run)


if __name__ == "__main__":
    sys.exit(main())
