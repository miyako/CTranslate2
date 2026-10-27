import json
import pathlib
import shutil
import tempfile
import unittest
import xml.etree.ElementTree as ET
from unittest.mock import patch

from native_sdk import (HEADER_DIRS, HEADER_FILES, LIBRARIES, apply_sdks, archive_groups,
                        cmake_path, copy_headers, library_name, update_ide_projects)

ROOT = pathlib.Path(__file__).resolve().parents[2]


class NativeSDKTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def projects(self):
        for name in ("ct2-server.vcxproj", "ct2-server.xcodeproj/project.pbxproj"):
            destination = self.root / "cpp" / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / "cpp" / name, destination)

    def test_project_updates_are_idempotent(self):
        self.projects()
        update_ide_projects(self.root)
        project = self.root / "cpp/ct2-server.vcxproj"
        tree = ET.parse(project)
        namespace = {"ms": "http://schemas.microsoft.com/developer/msbuild/2003"}
        groups = tree.findall("ms:ItemDefinitionGroup", namespace)
        for platform in ("x64", "ARM64"):
            group = next(group for group in groups if f"Release|{platform}" in group.attrib["Condition"])
            dependencies = group.find("ms:Link/ms:AdditionalDependencies", namespace).text
            for name in LIBRARIES:
                self.assertIn(library_name(name, True), dependencies)
            self.assertNotIn("cpu_features.lib", dependencies)
            self.assertIn("bcrypt.lib", dependencies)
            self.assertIn("WIN32", group.find("ms:ClCompile/ms:PreprocessorDefinitions", namespace).text)
        mac = self.root / "cpp/ct2-server.xcodeproj/project.pbxproj"
        self.assertNotIn("libomp.a", mac.read_text())
        self.assertNotIn("libcpu_features.a", mac.read_text())
        self.assertIn("CoreFoundation.framework in Frameworks", mac.read_text())
        self.assertNotIn("-mavx2", mac.read_text())
        self.assertIn('ARCHS = "$(ARCHS_STANDARD)";', mac.read_text())
        before = (project.read_bytes(), mac.read_bytes())
        update_ide_projects(self.root)
        self.assertEqual(before, (project.read_bytes(), mac.read_bytes()))

    def test_header_line_endings_are_portable(self):
        source = self.root / "source"
        source.mkdir()
        (source / "header.h").write_bytes(b"one\r\ntwo\r\n")
        copy_headers(source, self.root / "headers")
        self.assertEqual((self.root / "headers/header.h").read_bytes(), b"one\ntwo\n")

    def test_windows_cmake_paths_do_not_contain_escape_sequences(self):
        self.assertEqual(cmake_path(pathlib.PureWindowsPath(r"D:\a\CTranslate2\main.cpp")),
                         '"D:/a/CTranslate2/main.cpp"')

    def test_resolved_archive_closure(self):
        reply = self.root / ".cmake/api/v1/reply"
        reply.mkdir(parents=True)
        (reply / "index-1.json").write_text(json.dumps({
            "reply": {"codemodel-v2": {"jsonFile": "model.json"}}}))
        (reply / "model.json").write_text(json.dumps({
            "configurations": [{"name": "Release", "targets": [
                {"name": "ct2-server", "jsonFile": "target.json"}]}]}))
        libraries = [f"_deps/ctranslate2-build/{library_name('ctranslate2')}",
                     "_deps/ctranslate2-build/ruy/libruy.a",
                     f"_deps/tokenizers_cpp-build/sentencepiece/src/{library_name('sentencepiece')}",
                     "_deps/tokenizers_cpp-build/sentencepiece/absl/libabsl_strings.a"]
        libraries += [f"_deps/tokenizers_cpp-build/{library_name(name)}"
                      for name in ("tokenizers_cpp", "tokenizers_c", "jsoncpp")]
        for name in libraries:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"archive")
        (reply / "target.json").write_text(json.dumps({
            "link": {"commandFragments": [{"role": "libraries", "fragment": f'"{name}"'}
                                         for name in libraries]}}))
        groups = archive_groups(self.root)
        self.assertEqual(len(groups["ctranslate2"]), 2)
        self.assertEqual(len(groups["sentencepiece"]), 2)
        (self.root / libraries[0]).unlink()
        with self.assertRaises(RuntimeError):
            archive_groups(self.root)

    def sdks(self):
        self.projects()
        for directory in ("a", "lib/arm64", "lib/windows64"):
            (self.root / "cpp" / directory).mkdir(parents=True, exist_ok=True)
        inputs = self.root / "inputs"
        for platform in ("macOS", "Windows"):
            for arch in ("arm64", "x86_64"):
                sdk = inputs / f"{platform}-{arch}"
                for directory in HEADER_DIRS:
                    headers = sdk / "include" / directory
                    headers.mkdir(parents=True)
                    (headers / "header.h").write_text("matching headers\n")
                for filename in HEADER_FILES:
                    (sdk / "include" / filename).write_text("matching headers\n")
                (sdk / "libraries").mkdir()
                for name in LIBRARIES:
                    (sdk / "libraries" / library_name(name, platform == "Windows")).write_bytes(arch.encode())
                (sdk / "licenses").mkdir()
                (sdk / "licenses/LICENSE").write_text("License")
                (sdk / "build-info.json").write_text(json.dumps({
                    "architecture": arch, "server_commit": "revision",
                    "ctranslate2_commit": "ct2", "tokenizers_cpp_commit": "tokenizer",
                    "jsoncpp_commit": "json"}))
        return inputs

    def test_all_project_library_destinations(self):
        inputs = self.sdks()

        def lipo(*arguments):
            if "-create" in arguments:
                pathlib.Path(arguments[-1]).write_bytes(b"universal")

        with patch("native_sdk.run", side_effect=lipo):
            apply_sdks(self.root, inputs)
        for name in LIBRARIES:
            self.assertEqual((self.root / "cpp/a" / library_name(name)).read_bytes(), b"universal")
            for arch, directory in (("arm64", "arm64"), ("x86_64", "windows64")):
                path = self.root / "cpp/lib" / directory / library_name(name, True)
                self.assertEqual(path.read_bytes(), arch.encode())
        self.assertTrue((self.root / "cpp/native-dependencies.json").exists())

    def test_mixed_headers_and_revisions_are_rejected(self):
        inputs = self.sdks()
        header = inputs / "Windows-arm64/include/tokenizers_cpp.h"
        header.write_text("wrong headers")
        with self.assertRaisesRegex(RuntimeError, "header mismatch"):
            apply_sdks(self.root, inputs)
        header.write_text("matching headers\n")
        manifest = inputs / "Windows-arm64/build-info.json"
        data = json.loads(manifest.read_text())
        data["ctranslate2_commit"] = "wrong version"
        manifest.write_text(json.dumps(data))
        with self.assertRaisesRegex(RuntimeError, "mix SDK revisions"):
            apply_sdks(self.root, inputs)


if __name__ == "__main__":
    unittest.main()
