"""End-to-end tests restricted to new temporary regular files."""
import hashlib
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest

EXE = Path(sys.argv.pop(1)).resolve()


class Imaging(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="openimage-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "source-é.raw"
        self.data = b"A" * (1 << 20) + random.Random(42).randbytes((1 << 20) + 7)
        self.source.write_bytes(self.data)

    def run_command(self, *args, code=0):
        result = subprocess.run([EXE, "--cli", *args], capture_output=True, timeout=60)
        self.assertEqual(result.returncode, code, result.stderr.decode(errors="replace"))
        self.assertEqual(self.source.read_bytes(), self.data)
        self.assertEqual(list(self.root.glob(".openimage-*")), [])
        return json.loads(result.stdout) if code == 0 else result.stderr

    def test_round_trip_all_codecs(self):
        for codec in ("raw", "zstd", "lz4", "lzma2"):
            with self.subTest(codec=codec):
                image = self.root / f"{codec}.omi"
                target = self.root / f"{codec}.raw"
                self.run_command("backup", self.source, image, codec)
                self.assertEqual(self.run_command("explore", image)["status"], "unverified_header")
                self.assertEqual(self.run_command("verify", image),
                                 {"status": "verified", "bytes": len(self.data), "codec": codec})
                self.run_command("restore", image, target)
                self.run_command("compare", image, target)
                self.assertEqual(hashlib.sha256(target.read_bytes()).digest(),
                                 hashlib.sha256(self.data).digest())
                target.write_bytes(b"B" + self.data[1:])
                self.run_command("compare", image, target, code=1)

    def test_corruption_and_invalid_metadata(self):
        image = self.root / "original.omi"
        self.run_command("backup", self.source, image, "raw")
        original = image.read_bytes()

        def changed(offset, replacement):
            return original[:offset] + replacement + original[offset + len(replacement):]

        variants = {
            "signature": changed(0, b"X"),
            "unknown codec": changed(8, b"\xff"),
            "unsupported feature": changed(9, b"\x01"),
            "logical overflow": changed(16, struct.pack("<Q", 2**64 - 1)),
            "length overflow": changed(24, struct.pack("<I", 2**32 - 1)),
            "zero length": changed(24, bytes(4)),
            "corrupt raw byte": changed(60, b"B"),
            "missing commit": original[:-40],
            "corrupt commit hash": original[:-1] + bytes([original[-1] ^ 1]),
            "extra bytes": original + b"extra",
        }
        candidate = self.root / "bad.omi"
        output = self.root / "restore.raw"
        for case, data in variants.items():
            with self.subTest(case=case):
                candidate.write_bytes(data)
                self.run_command("verify", candidate, code=1)
                self.run_command("restore", candidate, output, code=1)
                self.assertFalse(output.exists())

    def test_existing_outputs(self):
        target = self.root / "existing.raw"
        target.write_bytes(b"keep me")
        self.run_command("clone", self.source, self.source, code=1)
        self.run_command("backup", self.source, target, code=1)
        image = self.root / "test.omi"
        self.run_command("backup", self.source, image)
        self.run_command("restore", image, target, code=1)
        self.assertEqual(target.read_bytes(), b"keep me")

    def test_aliased_outputs(self):
        alias = self.root / "alias.raw"
        try:
            os.link(self.source, alias)
        except PermissionError:
            self.skipTest("Host denied hard-link creation")
        self.run_command("clone", self.source, alias, code=1)
        self.run_command("backup", self.source, alias, code=1)
        self.assertEqual(alias.read_bytes(), self.data)

    def test_concurrent_publication(self):
        target = self.root / "concurrent.omi"
        command = [EXE, "backup", self.source, target, "raw"]
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as first:
            with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as second:
                first.communicate(timeout=60)
                second.communicate(timeout=60)
        self.assertEqual(sorted([first.returncode, second.returncode]), [0, 1])
        self.run_command("compare", target, self.source)

    def test_clone(self):
        output = self.root / "cloned.raw"
        self.run_command("clone", self.source, output)
        self.assertEqual(output.read_bytes(), self.data)

    def test_directory_and_bad_arguments(self):
        output = self.root / "invalid.raw"
        self.run_command("clone", self.root, output, code=1)
        self.run_command("backup", self.source, output, "invalid", code=2)
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)

