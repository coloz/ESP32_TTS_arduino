#!/usr/bin/env python3
"""Flash an ESP-SR voice data file into an Arduino sketch partition."""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PARTITIONS = ROOT / "extras" / "partitions" / "tts_8mb.csv"
STANDARD_PARTITIONS = ROOT / "extras" / "partitions" / "tts_8mb_standard.csv"
BUNDLED_MODELS = {
    "small": (
        ROOT / "extras" / "voice_data" / "esp_tts_voice_data_xiaoxin_small.dat",
        2913777,
        "cc9a81fd716b3c07fae3ca2f802dc026081896f2e34db9b9db117d4de5a85c01",
        DEFAULT_PARTITIONS,
    ),
    "standard": (
        ROOT / "extras" / "voice_data" / "esp_tts_voice_data_xiaoxin.dat",
        3821311,
        "b0b9ad9fdaa4a560ee839ce6a4659f08af3fded7c72d0784d83186859a081e55",
        STANDARD_PARTITIONS,
    ),
}


def parse_size(value: str) -> int:
    value = value.strip()
    if not value:
        raise ValueError("empty size")
    multiplier = 1
    if value[-1].lower() == "k":
        multiplier = 1024
        value = value[:-1]
    elif value[-1].lower() == "m":
        multiplier = 1024 * 1024
        value = value[:-1]
    return int(value.strip(), 0) * multiplier


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def find_partition(csv_path: Path, label: str) -> tuple[int, int]:
    next_offset = 0x9000
    with csv_path.open("r", encoding="utf-8-sig", newline="") as handle:
        rows = csv.reader(line for line in handle if not line.lstrip().startswith("#"))
        for line_number, row in enumerate(rows, 1):
            if not row or all(not item.strip() for item in row):
                continue
            if len(row) < 5:
                raise ValueError(f"{csv_path}:{line_number}: expected at least 5 columns")

            name, partition_type, _subtype, offset_text, size_text = (
                item.strip() for item in row[:5]
            )
            size = parse_size(size_text)
            alignment = 0x10000 if partition_type.lower() == "app" else 0x1000
            offset = parse_size(offset_text) if offset_text else align_up(next_offset, alignment)
            if offset < next_offset:
                raise ValueError(f"{csv_path}:{line_number}: overlapping partition {name}")
            next_offset = offset + size
            if name == label:
                return offset, size
    raise ValueError(f"partition {label!r} was not found in {csv_path}")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Flash the ESP-SR voice set into the voice_data partition."
    )
    parser.add_argument("--port", required=True, help="Serial port, for example COM5 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=921600)
    model_group = parser.add_mutually_exclusive_group()
    model_group.add_argument(
        "--voice", choices=BUNDLED_MODELS, default="small",
        help="Bundled voice set (default: small)",
    )
    model_group.add_argument("--model", type=Path, help="Custom voice data file")
    parser.add_argument(
        "--sha256",
        dest="expected_sha256",
        help="Expected SHA-256 (required for a custom --model)",
    )
    parser.add_argument("--partitions", type=Path)
    parser.add_argument("--partition", default="voice_data")
    parser.add_argument("--offset", type=lambda value: int(value, 0), help="Override CSV offset")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    bundled = None if args.model is not None else BUNDLED_MODELS[args.voice]
    model = (args.model if args.model is not None else bundled[0]).resolve()
    default_partitions = DEFAULT_PARTITIONS if bundled is None else bundled[3]
    partitions = (args.partitions or default_partitions).resolve()
    if not model.is_file():
        parser.error(f"model file does not exist: {model}")
    if args.offset is None and not partitions.is_file():
        parser.error(f"partition table does not exist: {partitions}")

    try:
        if args.offset is None:
            offset, partition_size = find_partition(partitions, args.partition)
        else:
            offset, partition_size = args.offset, None
    except ValueError as error:
        parser.error(str(error))

    model_size = model.stat().st_size
    using_bundled_model = bundled is not None
    if using_bundled_model and model_size != bundled[1]:
        parser.error(
            f"bundled model size mismatch: expected {bundled[1]}, got {model_size}"
        )
    if partition_size is not None and model_size > partition_size:
        parser.error(
            f"model is {model_size} bytes, larger than the {partition_size}-byte partition"
        )

    digest = sha256(model)
    if using_bundled_model:
        expected_digest = bundled[2]
        if (
            args.expected_sha256 is not None
            and args.expected_sha256.lower() != expected_digest
        ):
            parser.error("--sha256 cannot override the bundled model checksum")
    else:
        if args.expected_sha256 is None:
            parser.error(
                "a custom --model requires --sha256; "
                f"the selected file currently hashes to {digest}"
            )
        expected_digest = args.expected_sha256.lower()

    if re.fullmatch(r"[0-9a-f]{64}", expected_digest) is None:
        parser.error("--sha256 must contain exactly 64 hexadecimal characters")
    if digest != expected_digest:
        parser.error(f"model checksum mismatch: expected {expected_digest}, got {digest}")

    command = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        "esp32s3",
        "--port",
        args.port,
        "--baud",
        str(args.baud),
        "--before",
        "default-reset",
        "--after",
        "hard-reset",
        "write-flash",
        hex(offset),
        str(model),
    ]

    print(f"Model: {model} ({model_size} bytes, sha256={digest})")
    print(f"Target: {args.partition} at {offset:#x}")
    if using_bundled_model:
        print(f'Runtime initialization: tts.begin("{args.partition}")')
    else:
        print(
            "Runtime validation: "
            f'tts.begin("{args.partition}", {model_size}, "{digest}")'
        )
    print("Command:", subprocess.list2cmdline(command))
    if args.dry_run:
        return 0

    if importlib.util.find_spec("esptool") is None:
        print(
            f"esptool is not installed. Run: {sys.executable} -m pip install esptool",
            file=sys.stderr,
        )
        return 2
    return subprocess.run(command, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
