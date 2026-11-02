import json
import os
import pathlib
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import native_resources as resources


def executable(platform, arch):
    if platform == "macOS":
        return b"\xcf\xfa\xed\xfe" + struct.pack(
            "<I", 0x100000C if arch == "arm64" else 0x1000007) + b"source-built fixture"
    data = bytearray(256)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 128)
    data[128:132] = b"PE\0\0"
    struct.pack_into("<H", data, 132, 0xAA64 if arch == "arm64" else 0x8664)
    return data


class ResourceTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = pathlib.Path(temporary.name)
        self.source = "a" * 40
        self.downloads = self.root / "downloads"
        self.downloads.mkdir()
        self.output = self.root / "resources"
        for target in resources.TARGETS:
            platform, arch = target.split("-")
            stage = self.root / target
            (stage / "bin").mkdir(parents=True)
            (stage / "bin" / ("ct2-server.exe" if platform == "Windows" else "ct2-server")
             ).write_bytes(executable(platform, arch))
            (stage / "licenses").mkdir()
            (stage / "licenses/CT2-LICENSE").write_text("CT2 notice")
            (stage / "source-headers").mkdir()
            (stage / "source-headers/httplib.h").write_text("Header license")
            (stage / "rust-vendor/crate").mkdir(parents=True)
            (stage / "rust-vendor/crate/LICENSE").write_text("Rust crate notice")
            (stage / "rust-vendor/crate/Cargo.toml").write_text('license = "MIT"')
            info = {
                **resources.PINS, "server_commit": self.source, "architecture": arch,
                "target": target, "compiler": "test compiler", "cmake": "test cmake",
                "rust": "test rust",
                "rust_lock_sha256": resources.digest(resources.ROOT / "scripts/ci/tokenizers-Cargo.lock"),
            }
            (stage / "build-info.json").write_text(json.dumps(info))
            resources.export(stage, self.downloads / target, target, self.source)

    def assemble(self):
        def lipo(command, **kwargs):
            self.assertEqual(command[:2], ["lipo", "-create"])
            universal = b"\xca\xfe\xba\xbe" + struct.pack(">I", 2)
            universal += struct.pack(">IIIII", 0x100000C, 0, 0, 0, 0)
            universal += struct.pack(">IIIII", 0x1000007, 0, 0, 0, 0)
            pathlib.Path(command[-1]).write_bytes(universal)
        with patch.object(resources.subprocess, "run", side_effect=lipo):
            self.targets = resources.assemble(self.downloads, self.output, self.source)
        (self.output / "native/notarization.json").write_text(
            json.dumps({"id": "test-id", "status": "Accepted"}))
        resources.seal(self.output, self.source, self.targets, "TESTTEAM")

    def verify(self):
        def tool(command, **kwargs):
            if command[0] == "codesign":
                return "TeamIdentifier=TESTTEAM\nflags=runtime\nTimestamp=test\n"
            return "ct2-server:\n\t/usr/lib/libSystem.B.dylib (compatibility version 1)\n"
        with patch.object(resources.subprocess, "run"), patch.object(
                resources.subprocess, "check_output", side_effect=tool):
            resources.verify_final(self.output, self.source, "TESTTEAM")

    def test_complete_portable_inventory_and_preserved_legacy_resources(self):
        self.assemble()
        self.verify()
        self.assertTrue(all("\\" not in key for key in
                            resources.read_manifest(self.output, "native/manifest.json")["files"]))
        legacy = self.root / "installed"
        for name in resources.SERVERS:
            path = legacy / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"old")
            (path.parent / "ct2-translator").write_bytes(b"legacy translator")
        resources.install(self.output, legacy)
        for name in resources.SERVERS:
            self.assertEqual((legacy / name).read_bytes(), (self.output / name).read_bytes())
            self.assertEqual((legacy / name).parent.joinpath("ct2-translator").read_bytes(),
                             b"legacy translator")
        if os.name != "nt":
            self.assertTrue((legacy / resources.SERVERS[0]).stat().st_mode & 0o111)

    def test_windows_manifest_boundary_uses_forward_slashes(self):
        root = pathlib.PureWindowsPath(r"C:\bundle")
        path = root / "licenses" / "crate" / "LICENSE"
        self.assertEqual(resources.portable_key(path, root), "licenses/crate/LICENSE")

    def test_git_checkout_conversion_preserves_hashed_inputs(self):
        repository = self.root / "checkout"
        repository.mkdir()
        def git(*args):
            return subprocess.run(["git", "-C", str(repository), *args], check=True,
                                  capture_output=True)
        git("init", "--quiet")
        git("config", "core.autocrlf", "true")
        git("config", "core.eol", "crlf")
        git("config", "core.safecrlf", "false")
        (repository / ".gitattributes").write_bytes((resources.ROOT / ".gitattributes").read_bytes())
        paths = ("LICENSE", "scripts/ci/tokenizers-Cargo.lock",
                 "CTranslate2/Resources/native/notices.txt", "control.txt")
        content = b"first line\nsecond line\n"
        for name in paths:
            path = repository / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        git("add", "--", ".gitattributes", *paths)
        for name in paths:
            (repository / name).unlink()
        git("checkout-index", "--all", "--force")
        for name in paths[:-1]:
            self.assertEqual((repository / name).read_bytes(), content)
        self.assertEqual((repository / "control.txt").read_bytes(), content.replace(b"\n", b"\r\n"))

    def test_missing_target_rejected_before_output(self):
        (self.downloads / "Windows-arm64").rename(self.root / "missing")
        with self.assertRaises(ValueError):
            resources.assemble(self.downloads, self.output, self.source)
        self.assertFalse(self.output.exists())

    def test_mixed_source_rejected(self):
        with self.assertRaises(ValueError):
            resources.assemble(self.downloads, self.output, "b" * 40)
        self.assertFalse(self.output.exists())

    def test_changed_binary_rejected(self):
        (self.downloads / "Windows-arm64/ct2-server.exe").write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "inventory"):
            resources.verify_bundle(self.downloads / "Windows-arm64", "Windows-arm64", self.source)

    def test_wrong_architecture_rejected_even_with_new_inventory(self):
        bundle = self.downloads / "Windows-arm64"
        (bundle / "ct2-server.exe").write_bytes(executable("Windows", "x86_64"))
        data = resources.read_manifest(self.downloads / "Windows-x86_64", "manifest.json")
        data["target"] = "Windows-arm64"
        resources.write_manifest(bundle, "manifest.json", data)
        with self.assertRaisesRegex(ValueError, "architecture"):
            resources.verify_bundle(bundle, "Windows-arm64", self.source)

    def test_symlinks_and_oversized_files_rejected(self):
        with patch.object(resources, "LIMIT", 2):
            with self.assertRaisesRegex(ValueError, "oversized"):
                resources.inventory(self.downloads, "manifest.json")
        if os.name != "nt":
            (self.downloads / "link").symlink_to(self.downloads / "Windows-arm64/ct2-server.exe")
            with self.assertRaisesRegex(ValueError, "Symlink"):
                resources.inventory(self.downloads, "manifest.json")

    def test_final_byte_changes_rejected(self):
        self.assemble()
        (self.output / resources.SERVERS[0]).write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "inventory"):
            self.verify()

    def test_unexpected_final_resource_rejected_even_when_sealed(self):
        self.assemble()
        (self.output / "native/targets/macOS-arm64/unexpected.txt").write_text("extra")
        resources.seal(self.output, self.source, self.targets, "TESTTEAM")
        with self.assertRaisesRegex(ValueError, "Unexpected resource"):
            self.verify()

    def test_notary_status_is_explicit_gate(self):
        self.assemble()
        (self.output / "native/notarization.json").write_text(
            json.dumps({"id": "test-id", "status": "Invalid"}))
        resources.seal(self.output, self.source, self.targets, "TESTTEAM")
        with self.assertRaisesRegex(ValueError, "Accepted"):
            self.verify()

    def test_windows_final_bytes_match_native_smoke_input(self):
        self.assemble()
        (self.output / resources.SERVERS[1]).write_bytes(executable("Windows", "x86_64") + b"change")
        resources.seal(self.output, self.source, self.targets, "TESTTEAM")
        with self.assertRaisesRegex(ValueError, "native smoke"):
            self.verify()

    def test_final_notices_match_input_inventory(self):
        self.assemble()
        (self.output / "native/targets/macOS-arm64/licenses/CT2-LICENSE").write_text("changed")
        resources.seal(self.output, self.source, self.targets, "TESTTEAM")
        with self.assertRaisesRegex(ValueError, "Changed target notice"):
            self.verify()


if __name__ == "__main__":
    unittest.main()
