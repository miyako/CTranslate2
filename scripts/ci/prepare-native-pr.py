"""Apply four same-revision SDKs without touching main or an existing PR branch."""

import json
import os
import pathlib
import stat
import subprocess
import zipfile

from native_sdk import apply_sdks

root = pathlib.Path(__file__).resolve().parents[2]
subprocess.run(["git", "fetch", "origin", "main"], check=True)
main = subprocess.check_output(["git", "rev-parse", "origin/main"], text=True).strip()
if main != os.environ["GITHUB_SHA"]:
    raise SystemExit("main changed during this build; rerun before proposing library replacements")
inputs = root / ".build-native/sdk-inputs"
inputs.mkdir()
for name in ("macOS-arm64", "macOS-x86_64", "Windows-arm64", "Windows-x86_64"):
    archive = root / ".build-native/downloads" / f"{name}.zip"
    with zipfile.ZipFile(archive) as source:
        for item in source.infolist():
            path = pathlib.PurePosixPath(item.filename)
            if (path.is_absolute() or ".." in path.parts or not path.parts
                    or path.parts[0] != name or "\\" in item.filename
                    or stat.S_ISLNK(item.external_attr >> 16)):
                raise RuntimeError(f"Unsafe SDK archive entry: {item.filename}")
        source.extractall(inputs)
    manifest = json.loads((inputs / name / "build-info.json").read_text())
    if manifest["server_commit"] != main:
        raise RuntimeError(f"SDK was not built from this main revision: {name}")
apply_sdks(root, inputs)
