"""Strict, portable inventories for the component's native server refresh."""

import hashlib
import json
import pathlib
import shutil
import struct
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[2]
LIMIT = 100 * 1024 * 1024
TARGETS = ("macOS-arm64", "macOS-x86_64", "Windows-arm64", "Windows-x86_64")
SERVERS = (
    "bin/macOS/ct2-server",
    "bin/Windows/ct2-server.exe",
    "bin/WindowsARM/ct2-server.exe",
)
PINS = {
    "ctranslate2_commit": "5de572ef60b67a0276c5989ee73d0072808ff4c3",
    "tokenizers_cpp_commit": "c586c52f93f7b060753bd2388eb96a105cb7374d",
    "jsoncpp_commit": "89e2973c754a9c02a49974d839779b151e95afd6",
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def portable_key(path, root):
    return path.relative_to(root).as_posix()


def inventory(root, manifest):
    result = {}
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"Symlink in resource bundle: {path}")
        if path.is_file() and path != root / manifest:
            if not 0 < path.stat().st_size < LIMIT:
                raise ValueError(f"Empty or GitHub-oversized resource: {path}")
            result[portable_key(path, root)] = digest(path)
    return result


def write_manifest(root, name, metadata):
    manifest = root / name
    manifest.write_text(
        json.dumps({**metadata, "files": inventory(root, name)}, indent=2) + "\n",
        encoding="utf-8",
    )
    if not 0 < manifest.stat().st_size < LIMIT:
        raise ValueError(f"GitHub-oversized manifest: {manifest}")


def read_manifest(root, name):
    if not 0 < (root / name).stat().st_size < LIMIT:
        raise ValueError(f"Empty or GitHub-oversized manifest: {root / name}")
    data = json.loads((root / name).read_text(encoding="utf-8"))
    if data["files"] != inventory(root, name):
        raise ValueError(f"Resource inventory mismatch: {root}")
    return data


def architecture(path, platform, arches):
    data = path.read_bytes()
    if platform == "Windows":
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("Invalid PE executable")
        actual = {struct.unpack_from("<H", data, pe + 4)[0]}
        expected = {{"arm64": 0xAA64, "x86_64": 0x8664}[a] for a in arches}
    else:
        expected = {{"arm64": 0x100000C, "x86_64": 0x1000007}[a] for a in arches}
        if data[:4] == b"\xca\xfe\xba\xbe":
            count = struct.unpack_from(">I", data, 4)[0]
            actual = {struct.unpack_from(">I", data, 8 + n * 20)[0] for n in range(count)}
        elif data[:4] == b"\xcf\xfa\xed\xfe":
            actual = {struct.unpack_from("<I", data, 4)[0]}
        else:
            raise ValueError("Invalid Mach-O executable")
    if actual != expected:
        raise ValueError(f"Wrong executable architecture: {path}")


def export(stage, output, target, source):
    if target not in TARGETS or output.exists():
        raise ValueError("Expected a fresh output directory and supported target")
    platform, arch = target.split("-")
    binary = stage / "bin" / ("ct2-server.exe" if platform == "Windows" else "ct2-server")
    architecture(binary, platform, [arch])
    info = json.loads((stage / "build-info.json").read_text())
    if info["server_commit"] != source or info["architecture"] != arch:
        raise ValueError("Wrong server source/architecture")
    if info["target"] != target or not all(info.get(k) for k in ("compiler", "cmake", "rust")):
        raise ValueError("Missing native toolchain provenance")
    if info["rust_lock_sha256"] != digest(ROOT / "scripts/ci/tokenizers-Cargo.lock"):
        raise ValueError("Wrong build lockfile")
    for key, value in PINS.items():
        if info[key] != value:
            raise ValueError(f"Wrong dependency: {key}")
    output.mkdir(parents=True)
    shutil.copyfile(binary, output / binary.name)
    (output / binary.name).chmod(0o755)
    shutil.copyfile(stage / "build-info.json", output / "build-info.json")
    shutil.copytree(stage / "licenses", output / "licenses")
    shutil.copytree(stage / "source-headers", output / "licenses/source-headers")
    # Keep the Rust closure's license texts and package metadata, not build sources.
    for crate in sorted((stage / "rust-vendor").iterdir()):
        for path in sorted(crate.rglob("*")):
            if path.is_file() and (
                path.name.upper().startswith(("LICENSE", "COPYING", "NOTICE", "AUTHORS"))
                or path.name == "Cargo.toml"
            ):
                destination = output / "licenses/rust" / path.relative_to(stage / "rust-vendor")
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, destination)
    shutil.copyfile(ROOT / "scripts/ci/tokenizers-Cargo.lock", output / "Cargo.lock")
    shutil.copyfile(ROOT / "LICENSE", output / "licenses/server-LICENSE")
    write_manifest(output, "manifest.json", {
        "target": target, "source": source, "dependencies": PINS,
        "backend": "Accelerate" if platform == "macOS" else "Ruy",
        "runtime": "system-only", "rust_lock_sha256": digest(output / "Cargo.lock"),
    })


def verify_bundle(bundle, target, source):
    data = read_manifest(bundle, "manifest.json")
    if data["target"] != target or data["source"] != source or data["dependencies"] != PINS:
        raise ValueError(f"Mixed source or target: {target}")
    if data["rust_lock_sha256"] != digest(ROOT / "scripts/ci/tokenizers-Cargo.lock"):
        raise ValueError("Wrong Rust lockfile")
    info = json.loads((bundle / "build-info.json").read_text())
    if (info["server_commit"] != source or info["target"] != target
            or any(info[key] != value for key, value in PINS.items())):
        raise ValueError("Wrong build provenance")
    binary = bundle / ("ct2-server.exe" if target.startswith("Windows") else "ct2-server")
    platform, arch = target.split("-")
    architecture(binary, platform, [arch])
    if not any((bundle / "licenses").rglob("*LICENSE*")):
        raise ValueError("Missing dependency licenses")
    return data


def assemble(downloads, output, source):
    if {p.name for p in downloads.iterdir()} != set(TARGETS) or output.exists():
        raise ValueError("Require exactly four native targets and a fresh destination")
    manifests = {target: verify_bundle(downloads / target, target, source) for target in TARGETS}
    for target in TARGETS:
        platform, arch = target.split("-")
        bundle = downloads / target
        notices = output / "native/targets" / target
        notices.mkdir(parents=True)
        for name in ("licenses", "Cargo.lock", "build-info.json", "manifest.json"):
            src = bundle / name
            if src.is_dir():
                shutil.copytree(src, notices / name)
            else:
                shutil.copyfile(src, notices / name)
        if platform == "Windows":
            folder = "WindowsARM" if arch == "arm64" else "Windows"
            destination = output / "bin" / folder / "ct2-server.exe"
            destination.parent.mkdir(parents=True)
            shutil.copyfile(bundle / "ct2-server.exe", destination)
    binary = output / SERVERS[0]
    binary.parent.mkdir(parents=True)
    subprocess.run(["lipo", "-create", *(str(downloads / t / "ct2-server")
                                      for t in TARGETS if t.startswith("macOS")),
                    "-output", str(binary)], check=True)
    binary.chmod(0o755)
    architecture(binary, "macOS", ["arm64", "x86_64"])
    return manifests


def seal(output, source, targets, team):
    write_manifest(output, "native/manifest.json", {
        "source": source, "targets": targets, "macos_team": team,
        "hash_scope": "final native refresh bytes; component signing may change executable bytes",
    })


def verify_final(output, source, team):
    data = read_manifest(output, "native/manifest.json")
    if data["source"] != source or data["macos_team"] != team or not team:
        raise ValueError("Wrong source or signing team")
    if set(data["targets"]) != set(TARGETS):
        raise ValueError("Incomplete target set")
    notary = json.loads((output / "native/notarization.json").read_text())
    if notary.get("status") != "Accepted" or not notary.get("id"):
        raise ValueError("Universal notarization was not Accepted")
    allowed = set(SERVERS) | {"native/notarization.json"}
    for target in TARGETS:
        metadata = output / "native/targets" / target
        original = json.loads((metadata / "manifest.json").read_text())
        if (original != data["targets"][target] or original["source"] != source
                or original["target"] != target):
            raise ValueError("Mixed target provenance")
        if original["dependencies"] != PINS:
            raise ValueError("Wrong dependency provenance")
        binary_name = "ct2-server.exe" if target.startswith("Windows") else "ct2-server"
        allowed.add(f"native/targets/{target}/manifest.json")
        for name, checksum in original["files"].items():
            if name != binary_name:
                allowed.add(f"native/targets/{target}/{name}")
                if digest(metadata / name) != checksum:
                    raise ValueError(f"Changed target notice: {target}/{name}")
    for name in SERVERS:
        platform = "macOS" if "/macOS/" in name else "Windows"
        arches = ["arm64", "x86_64"] if platform == "macOS" else [
            "arm64" if "/WindowsARM/" in name else "x86_64"]
        architecture(output / name, platform, arches)
        if platform == "Windows":
            target = "Windows-" + arches[0]
            if digest(output / name) != data["targets"][target]["files"]["ct2-server.exe"]:
                raise ValueError("Windows server changed after native smoke")
    if set(data["files"]) != allowed:
        raise ValueError("Unexpected resource output")
    binary = output / SERVERS[0]
    subprocess.run(["codesign", "--verify", "--strict", str(binary)], check=True)
    signature = subprocess.check_output(["codesign", "-dvv", str(binary)], stderr=subprocess.STDOUT,
                                        text=True)
    if f"TeamIdentifier={team}\n" not in signature or "runtime" not in signature or "Timestamp=" not in signature:
        raise ValueError("Expected timestamped Developer ID hardened-runtime signature")
    for arch in ("arm64", "x86_64"):
        linked = subprocess.check_output(["otool", "-arch", arch, "-L", str(binary)], text=True)
        for line in linked.splitlines()[1:]:
            if not line.strip().startswith(("/usr/lib/", "/System/Library/")):
                raise ValueError(f"Non-system dependency: {line}")


def install(output, resources):
    for name in SERVERS:
        shutil.copyfile(output / name, resources / name)
        (resources / name).chmod(0o755)
    if (resources / "native").exists():
        shutil.rmtree(resources / "native")
    shutil.copytree(output / "native", resources / "native")
    for path in output.rglob("*"):
        if path.is_file() and path.read_bytes() != (resources / path.relative_to(output)).read_bytes():
            raise ValueError(f"Installed resource changed: {path}")
