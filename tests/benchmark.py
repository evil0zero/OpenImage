"""Report local file throughput and compression ratios from real executions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import shutil
import statistics
import subprocess
import tempfile
import time


def measured(action):
    start = time.perf_counter()
    action()
    return time.perf_counter() - start


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--mib", type=int, default=32)
    parser.add_argument("--runs", type=int, default=3)
    options = parser.parse_args()
    if options.mib < 1 or options.runs < 1:
        parser.error("mib and runs must be positive")
    executable = options.executable.resolve()

    def run(*args):
        subprocess.run([executable, "--cli", *args], check=True, capture_output=True, timeout=300)

    def sha256(path):
        with path.open("rb") as stream:
            return hashlib.file_digest(stream, "sha256").digest()

    results = {}
    with tempfile.TemporaryDirectory(prefix="openimage-benchmark-") as temporary:
        root = Path(temporary)
        source = root / "mixed.raw"
        image = root / "mixed.omi"
        restored = root / "restored.raw"
        rng = random.Random(20261106)
        with source.open("wb") as stream:
            for index in range(options.mib):
                stream.write(rng.randbytes(1 << 20) if index % 2 else bytes(1 << 20))
        expected = sha256(source)
        copy_seconds = []
        for _ in range(options.runs):
            copy_seconds.append(measured(lambda: shutil.copyfile(source, restored)))
            restored.unlink()
        for codec in ("raw", "zstd", "lz4", "lzma2"):
            backup, verify, restore = [], [], []
            for _ in range(options.runs):
                backup.append(measured(lambda: run("backup", source, image, codec)))
                verify.append(measured(lambda: run("verify", image)))
                restore.append(measured(lambda: run("restore", image, restored)))
                if sha256(restored) != expected:
                    raise RuntimeError("Benchmark restore digest mismatch")
                image_bytes = image.stat().st_size
                image.unlink()
                restored.unlink()
            results[codec] = {
                "backup_seconds": backup, "verify_seconds": verify, "restore_seconds": restore,
                "backup_mib_s_median": options.mib / statistics.median(backup),
                "restore_mib_s_median": options.mib / statistics.median(restore),
                "image_to_source_ratio": image_bytes / source.stat().st_size,
            }
    print(json.dumps({
        "os": platform.platform(), "processor": platform.processor(),
        "logical_processors": os.cpu_count(), "input_mib": options.mib,
        "dataset": "alternating one-MiB zero and seeded pseudorandom chunks",
        "copy_seconds": copy_seconds, "codecs": results,
    }, indent=2))


if __name__ == "__main__":
    main()
