"""Configure, build, test and stage a native, source-built server."""

import argparse
import json
import os
import pathlib
import platform
import shutil
import struct
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--arch", choices=["x86_64", "arm64"], required=True)
args = parser.parse_args()
machine = platform.machine().lower()
actual = "arm64" if machine in ("arm64", "aarch64") else "x86_64"
if actual != args.arch:
    raise SystemExit(f"Native runner required: requested {args.arch}, got {machine}")
root = pathlib.Path(__file__).resolve().parents[2]
os.chdir(root)
build = root / ".build-native/build"
stage = root / ".build-native/dist"
configure = ["cmake", "-S", "cpp", "-B", str(build), "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=ON"]
if os.name == "nt":
    configure += ["-A", "ARM64" if args.arch == "arm64" else "x64"]
else:
    configure += ["-G", "Ninja", f"-DCMAKE_OSX_ARCHITECTURES={args.arch}"]


def run(*command):
    subprocess.run(command, check=True)


run(*configure)
run("cmake", "--build", str(build), "--config", "Release", "--target", "ct2-server", "model-support-test",
    "--parallel", "3")
run("ctest", "--test-dir", str(build), "-C", "Release", "--output-on-failure",
    "--no-tests=error", "-R", "^(server-http-smoke|model-support)$")
if stage.exists():
    shutil.rmtree(stage)
run("cmake", "--install", str(build), "--config", "Release", "--prefix", str(stage), "--component", "server")
binary = stage / "bin" / ("ct2-server.exe" if os.name == "nt" else "ct2-server")
if os.name == "nt":
    data = binary.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    machine_id = struct.unpack_from("<H", data, pe + 4)[0]
    if machine_id != (0xAA64 if args.arch == "arm64" else 0x8664):
        raise SystemExit(f"Wrong PE machine: {machine_id:#x}")
    optional = pe + 24
    if struct.unpack_from("<H", data, optional)[0] != 0x20B:
        raise SystemExit("Expected a PE32+ executable")
    import_rva = struct.unpack_from("<I", data, optional + 120)[0]
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    sections = optional + struct.unpack_from("<H", data, pe + 20)[0]

    def offset(rva):
        for index in range(section_count):
            section = sections + index * 40
            size, virtual, raw_size, raw = struct.unpack_from("<IIII", data, section + 8)
            if virtual <= rva < virtual + max(size, raw_size):
                return raw + rva - virtual
        raise SystemExit(f"Unmapped PE RVA: {rva:#x}")

    imports = []
    descriptor = offset(import_rva)
    while any(data[descriptor:descriptor + 20]):
        name_offset = offset(struct.unpack_from("<I", data, descriptor + 12)[0])
        name = data[name_offset:data.index(b"\0", name_offset)].decode("ascii")
        imports.append(name)
        if name.lower().startswith(("vcruntime", "msvcp", "vcomp", "libomp", "libiomp")):
            raise SystemExit(f"Unexpected non-static compiler runtime: {name}")
        if not name.lower().startswith(("api-ms-win-", "ext-ms-win-")) and not (
            pathlib.Path(os.environ["SystemRoot"]) / "System32" / name
        ).is_file():
            raise SystemExit(f"Non-system runtime dependency: {name}")
        descriptor += 20
    print("Windows system imports:", ", ".join(imports))
else:
    run("lipo", str(binary), "-verify_arch", args.arch)
    linked = subprocess.check_output(["otool", "-L", str(binary)], text=True)
    for line in linked.splitlines()[1:]:
        library = line.strip().split(" (")[0]
        if not library.startswith(("/usr/lib/", "/System/Library/")):
            raise SystemExit(f"Non-system runtime dependency: {library}")
run(sys.executable, str(root / "scripts/ci/smoke-server.py"), str(binary))
licenses = stage / "licenses"
licenses.mkdir()
headers = stage / "source-headers"
shutil.copytree(build / "portable-headers", headers)
for name, source in (
    ("CTranslate2", build / "_deps/ctranslate2-src"),
    ("jsoncpp", build / "_deps/jsoncpp-src"),
    ("tokenizers-cpp", build / "_deps/tokenizers_cpp-src"),
):
    for license_file in source.rglob("*"):
        if not license_file.name.startswith(("LICENSE", "COPYING", "NOTICE")):
            continue
        if license_file.is_file() and ".git" not in license_file.parts:
            relative = license_file.relative_to(source).as_posix().replace("/", "__")
            shutil.copyfile(license_file, licenses / f"{name}-{relative}")
manifest = {
    "ctranslate2": "4.8.4",
    "ctranslate2_commit": "5de572ef60b67a0276c5989ee73d0072808ff4c3",
    "tokenizers_cpp_commit": "c586c52f93f7b060753bd2388eb96a105cb7374d",
    "jsoncpp_commit": "89e2973c754a9c02a49974d839779b151e95afd6",
    "server_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
    "architecture": args.arch,
    "platform": platform.platform(),
    "cmake": subprocess.check_output(["cmake", "--version"], text=True).splitlines()[0],
    "rust": subprocess.check_output(["rustc", "--version"], text=True).strip(),
}
(stage / "build-info.json").write_text(json.dumps(manifest, indent=2) + "\n")
# Include the complete Rust closure's licensing metadata and sources: static Rust
# libraries may contain crates with attribution beyond the top-level license.
run("cargo", "vendor", "--locked", "--manifest-path",
    str(build / "_deps/tokenizers_cpp-src/rust/Cargo.toml"), str(stage / "rust-vendor"))
