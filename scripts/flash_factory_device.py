#!/usr/bin/env python3
"""Flash per-device factory data for a numbered Orbix unit (matches QR label N).

Defaults are read from orbix-6-plug-hub/sdkconfig and partitions.csv:
  - VID/PID from CONFIG_DEVICE_VENDOR_ID / CONFIG_DEVICE_PRODUCT_ID
  - Flash offset from CONFIG_CHIP_FACTORY_NAMESPACE_PARTITION_LABEL (fctry -> 0x3E0000)
  - Validates factory data provider + FACTORY_* credential providers are enabled

Plug on/off state lives in the separate 'nvs' partition and is not touched when
flashing to 'fctry'.

Examples:
  python3 scripts/flash_factory_device.py 5
  python3 scripts/flash_factory_device.py 5 -p /dev/ttyACM0
  python3 scripts/flash_factory_device.py 5 --dry-run
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass

DEFAULT_FIRMWARE_DIR = "orbix-6-plug-hub"
DEFAULT_PARTITION_LABEL = "fctry"
DEFAULT_VENDOR_ID = 0xFFF2
DEFAULT_PRODUCT_ID = 0x8001


@dataclass(frozen=True)
class PartitionInfo:
    name: str
    offset: int
    size: int


@dataclass(frozen=True)
class SdkconfigInfo:
    partition_label: str
    runtime_nvs_label: str
    vendor_id: int
    product_id: int
    factory_data_provider: bool
    test_setup_params: bool
    factory_dac_provider: bool
    factory_commissionable_provider: bool
    factory_instance_info_provider: bool
    example_dac_provider: bool


def _project_root() -> str:
    return os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


def _vid_pid_dir(vendor_id: int, product_id: int) -> str:
    return f"{vendor_id:04x}_{product_id:04x}"


def read_sdkconfig(sdkconfig_path: str) -> SdkconfigInfo:
    partition_label = DEFAULT_PARTITION_LABEL
    runtime_nvs_label = "nvs"
    vendor_id = DEFAULT_VENDOR_ID
    product_id = DEFAULT_PRODUCT_ID
    factory_data_provider = False
    test_setup_params = False
    factory_dac_provider = False
    factory_commissionable_provider = False
    factory_instance_info_provider = False
    example_dac_provider = False

    if not os.path.isfile(sdkconfig_path):
        return SdkconfigInfo(
            partition_label=partition_label,
            runtime_nvs_label=runtime_nvs_label,
            vendor_id=vendor_id,
            product_id=product_id,
            factory_data_provider=factory_data_provider,
            test_setup_params=test_setup_params,
            factory_dac_provider=factory_dac_provider,
            factory_commissionable_provider=factory_commissionable_provider,
            factory_instance_info_provider=factory_instance_info_provider,
            example_dac_provider=example_dac_provider,
        )

    with open(sdkconfig_path, encoding="utf-8") as f:
        for line in f:
            stripped = line.strip()
            if match := re.match(
                r'^CONFIG_CHIP_FACTORY_NAMESPACE_PARTITION_LABEL="([^"]+)"',
                stripped,
            ):
                partition_label = match.group(1)
            elif match := re.match(
                r'^CONFIG_ESP_MATTER_NVS_PART_NAME="([^"]+)"',
                stripped,
            ):
                runtime_nvs_label = match.group(1)
            elif match := re.match(r"^CONFIG_DEVICE_VENDOR_ID=(0x[0-9A-Fa-f]+)", stripped):
                vendor_id = int(match.group(1), 0)
            elif match := re.match(r"^CONFIG_DEVICE_PRODUCT_ID=(0x[0-9A-Fa-f]+)", stripped):
                product_id = int(match.group(1), 0)
            elif stripped == "CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER=y":
                factory_data_provider = True
            elif stripped == "CONFIG_ENABLE_TEST_SETUP_PARAMS=y":
                test_setup_params = True
            elif stripped == "CONFIG_FACTORY_PARTITION_DAC_PROVIDER=y":
                factory_dac_provider = True
            elif stripped == "CONFIG_FACTORY_COMMISSIONABLE_DATA_PROVIDER=y":
                factory_commissionable_provider = True
            elif stripped == "CONFIG_FACTORY_DEVICE_INSTANCE_INFO_PROVIDER=y":
                factory_instance_info_provider = True
            elif stripped == "CONFIG_EXAMPLE_DAC_PROVIDER=y":
                example_dac_provider = True

    return SdkconfigInfo(
        partition_label=partition_label,
        runtime_nvs_label=runtime_nvs_label,
        vendor_id=vendor_id,
        product_id=product_id,
        factory_data_provider=factory_data_provider,
        test_setup_params=test_setup_params,
        factory_dac_provider=factory_dac_provider,
        factory_commissionable_provider=factory_commissionable_provider,
        factory_instance_info_provider=factory_instance_info_provider,
        example_dac_provider=example_dac_provider,
    )


def read_partition_info(partitions_csv: str, partition_name: str) -> PartitionInfo:
    if not os.path.isfile(partitions_csv):
        raise FileNotFoundError(f"Partition table not found: {partitions_csv}")

    with open(partitions_csv, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [part.strip() for part in line.split(",")]
            if len(parts) < 5:
                continue
            if parts[0] == partition_name:
                return PartitionInfo(
                    name=partition_name,
                    offset=int(parts[3], 0),
                    size=int(parts[4], 0),
                )

    raise ValueError(
        f"Partition '{partition_name}' not found in {partitions_csv}."
    )


def find_device_files(
    mfg_root: str,
    sequence: int,
    vendor_id: int,
    product_id: int,
) -> tuple[str, str | None, str | None]:
    """Return (partition_bin, qrcode_png, onb_codes_csv) for a device sequence."""
    out_top = os.path.join(mfg_root, _vid_pid_dir(vendor_id, product_id))
    seq_dir = os.path.join(out_top, str(sequence))

    if not os.path.isdir(seq_dir):
        raise FileNotFoundError(
            f"Device folder not found: {seq_dir}\n"
            f"Generate factory data first: scripts/generate_orbix_qr.sh"
        )

    partition_bins = glob.glob(os.path.join(seq_dir, "*", "*-partition.bin"))
    qrcode_pngs = glob.glob(os.path.join(seq_dir, "*", "*-qrcode.png"))
    onb_csvs = glob.glob(os.path.join(seq_dir, "*", "*-onb_codes.csv"))

    if len(partition_bins) != 1:
        raise FileNotFoundError(
            f"Expected one *-partition.bin under {seq_dir}, found {len(partition_bins)}"
        )

    return (
        partition_bins[0],
        qrcode_pngs[0] if qrcode_pngs else None,
        onb_csvs[0] if onb_csvs else None,
    )


def _find_esptool() -> str:
    for name in ("esptool.py", "esptool"):
        path = shutil.which(name)
        if path:
            return path
    raise RuntimeError("esptool not found. Run: source $IDF_PATH/export.sh")


def detect_serial_port(explicit: str | None = None) -> str:
    """Pick a serial port: explicit flag, then $ESPPORT, then auto-scan."""
    if explicit:
        return explicit

    if env_port := os.environ.get("ESPPORT"):
        return env_port

    candidates: list[str] = []

    by_id = "/dev/serial/by-id"
    if os.path.isdir(by_id):
        for name in sorted(os.listdir(by_id)):
            path = os.path.join(by_id, name)
            if os.path.exists(path):
                candidates.append(os.path.realpath(path))

    for pattern in ("/dev/ttyACM*", "/dev/ttyUSB*"):
        candidates.extend(sorted(glob.glob(pattern)))

    unique: list[str] = []
    seen: set[str] = set()
    for path in candidates:
        if path not in seen:
            seen.add(path)
            unique.append(path)

    if len(unique) == 1:
        return unique[0]
    if not unique:
        raise RuntimeError(
            "No serial port found. Connect the devkit USB cable, or set "
            "ESPPORT or use -p /dev/ttyACM0"
        )

    raise RuntimeError(
        "Multiple serial ports found; specify one with -p:\n  "
        + "\n  ".join(unique)
    )


def collect_sdkconfig_warnings(
    sdkconfig: SdkconfigInfo,
    partition_label: str,
    vendor_id: int,
    product_id: int,
) -> list[str]:
    warnings: list[str] = []

    if not sdkconfig.factory_data_provider:
        warnings.append(
            "CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER is not set — firmware will "
            "ignore the flashed *-partition.bin."
        )

    if sdkconfig.test_setup_params:
        warnings.append(
            "CONFIG_ENABLE_TEST_SETUP_PARAMS is enabled — device uses hardcoded test "
            "credentials instead of per-device factory data."
        )

    if sdkconfig.example_dac_provider:
        warnings.append(
            "CONFIG_EXAMPLE_DAC_PROVIDER is enabled — firmware uses example "
            "credentials, not the factory partition."
        )

    if sdkconfig.factory_data_provider and not (
        sdkconfig.factory_dac_provider
        and sdkconfig.factory_commissionable_provider
        and sdkconfig.factory_instance_info_provider
    ):
        warnings.append(
            "Factory data provider is on but FACTORY_* credential providers are "
            "incomplete. Enable CONFIG_FACTORY_PARTITION_DAC_PROVIDER, "
            "CONFIG_FACTORY_COMMISSIONABLE_DATA_PROVIDER, and "
            "CONFIG_FACTORY_DEVICE_INSTANCE_INFO_PROVIDER."
        )

    if partition_label == sdkconfig.runtime_nvs_label:
        warnings.append(
            f"Factory partition '{partition_label}' is the same as runtime NVS "
            f"({sdkconfig.runtime_nvs_label}). Flashing will erase plug on/off "
            "state, fabrics, and Wi-Fi. Use 'fctry' for factory data instead."
        )

    if vendor_id != sdkconfig.vendor_id or product_id != sdkconfig.product_id:
        warnings.append(
            f"Mfg VID/PID 0x{vendor_id:04X}/0x{product_id:04X} != sdkconfig "
            f"0x{sdkconfig.vendor_id:04X}/0x{sdkconfig.product_id:04X} — "
            "commissioning will fail."
        )

    return warnings


def validate_partition_bin(partition_bin: str, partition: PartitionInfo) -> str | None:
    bin_size = os.path.getsize(partition_bin)
    if bin_size > partition.size:
        return (
            f"Partition image is {bin_size} bytes but '{partition.name}' is only "
            f"{partition.size} bytes ({hex(partition.size)}) in partitions.csv."
        )
    return None


def flash_partition(
    port: str,
    offset: int,
    partition_bin: str,
    baud: int,
    dry_run: bool,
) -> None:
    esptool = _find_esptool()
    cmd = [
        esptool,
        "-p",
        port,
        "-b",
        str(baud),
        "write_flash",
        hex(offset),
        partition_bin,
    ]

    print("Command:")
    print("  " + " ".join(cmd))
    if dry_run:
        print("(dry-run: not flashing)")
        return

    subprocess.run(cmd, check=True)


def main() -> int:
    project_root = _project_root()
    firmware_dir = os.path.join(project_root, DEFAULT_FIRMWARE_DIR)
    default_mfg = os.path.join(project_root, "mfg_qr_codes")
    default_partitions = os.path.join(firmware_dir, "partitions.csv")
    default_sdkconfig = os.path.join(firmware_dir, "sdkconfig")

    sdkconfig = read_sdkconfig(default_sdkconfig)

    parser = argparse.ArgumentParser(
        description="Flash factory partition for device N (matches QR label N).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Current sdkconfig defaults:\n"
            f"  VID/PID     : 0x{sdkconfig.vendor_id:04X} / 0x{sdkconfig.product_id:04X}\n"
            f"  Factory NVS : {sdkconfig.partition_label}\n"
            f"  Runtime NVS : {sdkconfig.runtime_nvs_label} (plug state, untouched)\n"
            f"  Providers   : "
            f"{'factory partition' if sdkconfig.factory_data_provider else 'not factory'}"
        ),
    )
    parser.add_argument("sequence", type=int, help="Device number, e.g. 5")
    parser.add_argument(
        "-p",
        "--port",
        default=None,
        help="Serial port (default: auto-detect, or $ESPPORT if set)",
    )
    parser.add_argument("--mfg-dir", default=default_mfg, help="mfg_qr_codes root")
    parser.add_argument("--sdkconfig", default=default_sdkconfig)
    parser.add_argument("--partitions-csv", default=default_partitions)
    parser.add_argument(
        "--partition-label",
        default=None,
        help="Override factory partition name (default: from sdkconfig)",
    )
    parser.add_argument(
        "--offset",
        type=lambda x: int(x, 0),
        default=None,
        help="Override flash offset (default: from partitions.csv)",
    )
    parser.add_argument("--baud", type=int, default=460800)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument(
        "--force",
        action="store_true",
        help="Flash despite sdkconfig warnings",
    )
    args = parser.parse_args()

    if args.sequence < 1:
        print("ERROR: sequence must be >= 1", file=sys.stderr)
        return 1

    sdkconfig = read_sdkconfig(args.sdkconfig)
    partition_label = args.partition_label or sdkconfig.partition_label
    vendor_id, product_id = sdkconfig.vendor_id, sdkconfig.product_id

    warnings = collect_sdkconfig_warnings(
        sdkconfig, partition_label, vendor_id, product_id
    )
    if warnings:
        print("WARNINGS:")
        for warning in warnings:
            print(f"  * {warning}")
        print()
        if not args.force and not args.dry_run:
            print("Aborting. Use --force to flash anyway.", file=sys.stderr)
            return 2

    try:
        partition = read_partition_info(args.partitions_csv, partition_label)
        offset = args.offset if args.offset is not None else partition.offset
        port = detect_serial_port(args.port)
        partition_bin, qrcode_png, onb_csv = find_device_files(
            args.mfg_dir,
            args.sequence,
            vendor_id,
            product_id,
        )
    except (FileNotFoundError, ValueError, RuntimeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    size_warning = validate_partition_bin(partition_bin, partition)
    if size_warning:
        print(f"ERROR: {size_warning}", file=sys.stderr)
        return 1

    print(f"Device #{args.sequence}")
    print(f"  VID/PID   : 0x{vendor_id:04X} / 0x{product_id:04X}")
    print(f"  Mfg path  : mfg_qr_codes/{_vid_pid_dir(vendor_id, product_id)}/{args.sequence}/")
    print(f"  Image     : {partition_bin}")
    if qrcode_png:
        print(f"  QR label  : {qrcode_png}")
    if onb_csv:
        print(f"  Onboarding: {onb_csv}")
    print(f"  Port      : {port}" + (" (auto-detected)" if not args.port else ""))
    print(f"  Flash     : {partition_label} @ {hex(offset)} ({partition.size} bytes)")
    if partition_label != sdkconfig.runtime_nvs_label:
        print(f"  Runtime   : {sdkconfig.runtime_nvs_label} partition left intact (plug state)")
    print()

    try:
        flash_partition(port, offset, partition_bin, args.baud, args.dry_run)
    except subprocess.CalledProcessError as exc:
        print(f"ERROR: esptool failed (exit {exc.returncode})", file=sys.stderr)
        return exc.returncode
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    if not args.dry_run:
        print(f"Done: device #{args.sequence} factory data flashed.")
        if qrcode_png:
            print(f"Attach QR label #{args.sequence} from: {qrcode_png}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
