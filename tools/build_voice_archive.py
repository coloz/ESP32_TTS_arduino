#!/usr/bin/env python3
"""Build the ESP32-S3 archive that embeds the two Xiaoxin voice models."""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VOICE_DIR = ROOT / "extras" / "voice_data"
OUTPUT = ROOT / "src" / "esp32s3" / "libESP32TTSVoice.a"
MODELS = (
    (
        "esp_tts_voice_data_xiaoxin_small.dat",
        "esp32_tts_voice_small_start",
        2913777,
        "cc9a81fd716b3c07fae3ca2f802dc026081896f2e34db9b9db117d4de5a85c01",
    ),
    (
        "esp_tts_voice_data_xiaoxin.dat",
        "esp32_tts_voice_standard_start",
        3821311,
        "b0b9ad9fdaa4a560ee839ce6a4659f08af3fded7c72d0784d83186859a081e55",
    ),
)


def find_tool(name: str, toolchain: Path | None) -> Path:
    candidates = [name]
    if toolchain is not None:
        candidates.insert(0, str(toolchain / name))
        candidates.insert(1, str(toolchain / f"{name}.exe"))
    for candidate in candidates:
        resolved = shutil.which(candidate)
        if resolved:
            return Path(resolved)
    raise FileNotFoundError(f"could not find {name}; pass --toolchain PATH")


def run(command: list[Path | str], cwd: Path) -> None:
    subprocess.run([str(part) for part in command], cwd=cwd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Create the ESP32-S3 voice-data static archive."
    )
    parser.add_argument(
        "--toolchain",
        type=Path,
        help="directory containing xtensa-esp32s3-elf-objcopy and ar",
    )
    args = parser.parse_args()

    objcopy = find_tool("xtensa-esp32s3-elf-objcopy", args.toolchain)
    ar = find_tool("xtensa-esp32s3-elf-ar", args.toolchain)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="esp32tts-voice-") as directory:
        temporary = Path(directory)
        objects: list[Path] = []
        for filename, public_start, expected_size, expected_sha256 in MODELS:
            source = VOICE_DIR / filename
            if not source.is_file():
                raise FileNotFoundError(source)
            content = source.read_bytes()
            if len(content) != expected_size:
                raise ValueError(
                    f"{source} is {len(content)} bytes; expected {expected_size}"
                )
            actual_sha256 = hashlib.sha256(content).hexdigest()
            if actual_sha256 != expected_sha256:
                raise ValueError(
                    f"{source} SHA-256 is {actual_sha256}; "
                    f"expected {expected_sha256}"
                )

            stem = source.stem
            object_file = temporary / f"{stem}.o"
            original_prefix = f"_binary_{filename.replace('.', '_')}"
            section = f".rodata.{public_start}"
            run(
                [
                    objcopy,
                    "-I",
                    "binary",
                    "-O",
                    "elf32-xtensa-le",
                    "-B",
                    "xtensa",
                    "--rename-section",
                    f".data={section},alloc,load,readonly,data,contents",
                    "--redefine-sym",
                    f"{original_prefix}_start={public_start}",
                    filename,
                    object_file,
                ],
                VOICE_DIR,
            )
            objects.append(object_file)

        if OUTPUT.exists():
            OUTPUT.unlink()
        run([ar, "rcs", OUTPUT, *objects], temporary)

    print(f"Created {OUTPUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
