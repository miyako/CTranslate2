"""Archive staged files, including Cargo's pre-1980 normalized timestamps."""

import argparse
import hashlib
import pathlib
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument("--name", required=True)
args = parser.parse_args()
if pathlib.Path(args.name).name != args.name or args.name in (".", ".."):
    parser.error("The archive name must be a filename")
root = pathlib.Path(__file__).resolve().parents[2]
stage = root / ".build-native/dist"
packages = root / ".build-native/packages"
packages.mkdir(exist_ok=True)
archive = packages / f"{args.name}.zip"
with zipfile.ZipFile(
    archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6,
    strict_timestamps=False,
) as output:
    for path in sorted(stage.rglob("*")):
        if path.is_file():
            output.write(path, path.relative_to(stage).as_posix())
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
archive.with_suffix(".zip.sha256").write_text(f"{digest}  {archive.name}\n")
print(f"Created {archive.name} ({archive.stat().st_size} bytes)")
