"""Build the real IDE project against the exported SDK in an isolated copy."""

import os
import pathlib
import shutil
import subprocess
import sys

from native_sdk import LIBRARIES, library_name, run, update_ide_projects

root = pathlib.Path(__file__).resolve().parents[2]
sdk = pathlib.Path(sys.argv[1]).resolve()
arch = sys.argv[2]
check = root / ".build-native/ide-validation" / sdk.name
if check.exists():
    shutil.rmtree(check)
cpp = check / "cpp"
cpp.mkdir(parents=True)
shutil.copytree(root / "cpp/ct2-server", cpp / "ct2-server")
shutil.copytree(root / "cpp/ct2-server.xcodeproj", cpp / "ct2-server.xcodeproj",
                ignore=shutil.ignore_patterns("xcuserdata", "project.xcworkspace"))
for filename in ("ct2-server.xcconfig", "ct2-server.vcxproj"):
    shutil.copyfile(root / "cpp" / filename, cpp / filename)
shutil.copytree(root / ".build-native/build/portable-headers", cpp / "include")
shutil.copytree(sdk / "include", cpp / "include", dirs_exist_ok=True)
library_dir = cpp / ("lib/arm64" if arch == "arm64" else "lib/windows64") if os.name == "nt" else cpp / "a"
library_dir.mkdir(parents=True)
for name in LIBRARIES:
    filename = library_name(name, os.name == "nt")
    shutil.copyfile(sdk / "libraries" / filename, library_dir / filename)
update_ide_projects(check)
output = check / "output"
if os.name == "nt":
    vswhere = pathlib.Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
    installation = subprocess.check_output([
        str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.Component.MSBuild",
        "-property", "installationPath"], text=True).strip()
    version = subprocess.check_output([
        str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.Component.MSBuild",
        "-property", "installationVersion"], text=True).strip()
    toolset = {"17": "v143", "18": "v145"}[version.split(".")[0]]
    msbuild = pathlib.Path(installation) / "MSBuild/Current/Bin/MSBuild.exe"
    run(str(msbuild), str(cpp / "ct2-server.vcxproj"), "/m:3", "/nologo",
        "/p:Configuration=Release", f"/p:Platform={'ARM64' if arch == 'arm64' else 'x64'}",
        f"/p:PlatformToolset={toolset}", f"/p:OutDir={output}{os.sep}",
        f"/p:IntDir={check / 'intermediates'}{os.sep}")
    binary = output / "ct2-server.exe"
else:
    run("xcodebuild", "-project", str(cpp / "ct2-server.xcodeproj"), "-target", "ct2-server",
        "-configuration", "Release", "-arch", arch, "CODE_SIGNING_ALLOWED=NO",
        f"CONFIGURATION_BUILD_DIR={output}", f"OBJROOT={check / 'intermediates'}")
    binary = output / "ct2-server"
run(sys.executable, str(root / "scripts/ci/smoke-server.py"), str(binary))
