"""Export and apply the static dependency bundle used by the IDE projects."""

import hashlib
import json
import os
import pathlib
import re
import shlex
import shutil
import subprocess

LIBRARIES = ("ctranslate2", "sentencepiece", "tokenizers_cpp", "tokenizers_c", "jsoncpp")
HEADER_DIRS = ("ctranslate2", "half_float", "nlohmann", "json")
HEADER_FILES = ("tokenizers_cpp.h", "sentencepiece_processor.h")


def run(*arguments):
    subprocess.run(arguments, check=True)


def library_name(name, windows=False):
    return f"{name}.lib" if windows else f"lib{name}.a"


def archive_groups(build):
    """Use CMake's resolved link closure, including Rust, Abseil and Ruy."""
    build = build.resolve()
    reply = build / ".cmake/api/v1/reply"
    index = json.loads(sorted(reply.glob("index-*.json"))[-1].read_text())
    codemodel = json.loads((reply / index["reply"]["codemodel-v2"]["jsonFile"]).read_text())
    configurations = codemodel["configurations"]
    configuration = next((item for item in configurations if item["name"] == "Release"), None)
    if configuration is None:
        raise RuntimeError("CMake has no Release configuration")
    target = next(item for item in configuration["targets"] if item["name"] == "ct2-server")
    target = json.loads((reply / target["jsonFile"]).read_text())
    groups = {name: [] for name in LIBRARIES}
    for fragment in target["link"]["commandFragments"]:
        if fragment["role"] != "libraries":
            continue
        for token in shlex.split(fragment["fragment"], posix=os.name != "nt"):
            token = token.strip('"')
            if not token.endswith((".a", ".lib")):
                continue
            path = pathlib.Path(token)
            path = path if path.is_absolute() else build / path
            if not path.is_file():
                if "/" not in token and "\\" not in token:
                    continue  # System libraries such as ws2_32.lib.
                raise RuntimeError(f"Missing link dependency: {path}")
            path = path.resolve()
            name = next((name for name in LIBRARIES
                         if path.name in (library_name(name), library_name(name, True))), None)
            if name is None:
                relative = path.relative_to(build).as_posix()
                if relative.startswith("_deps/ctranslate2-build/"):
                    name = "ctranslate2"
                elif "/sentencepiece/" in relative:
                    name = "sentencepiece"
                else:
                    raise RuntimeError(f"Unclassified static dependency: {relative}")
            if path not in groups[name]:
                groups[name].append(path)
    for name, archives in groups.items():
        if not archives:
            raise RuntimeError(f"No static archive found for {name}")
    return groups


def cache_value(build, key):
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if line.startswith(key + ":"):
            return line.split("=", 1)[1]
    raise RuntimeError(f"Missing CMake cache key: {key}")


def merge_archives(build, sources, destination, arch):
    if os.name == "nt":
        response = destination.with_suffix(".rsp")
        response.write_text("\n".join(f'"{path}"' for path in sources))
        try:
            run(cache_value(build, "CMAKE_AR"), "/NOLOGO", f"/OUT:{destination}",
                f"/MACHINE:{'ARM64' if arch == 'arm64' else 'X64'}", f"@{response}")
        finally:
            response.unlink()
    else:
        run("/usr/bin/libtool", "-static", "-o", str(destination), *(str(path) for path in sources))
        run("lipo", str(destination), "-verify_arch", arch)
    if destination.stat().st_size >= 100 * 1024 * 1024:
        raise RuntimeError(f"Archive exceeds GitHub's regular-file limit: {destination}")


def copy_headers(source, destination):
    shutil.copytree(source, destination, dirs_exist_ok=True)
    for path in destination.rglob("*"):
        if path.is_file():
            path.write_bytes(path.read_bytes().replace(b"\r\n", b"\n"))


def export_sdk(root, arch):
    build = root / ".build-native/build"
    windows = os.name == "nt"
    platform = "Windows" if windows else "macOS"
    sdk = root / f".build-native/sdk/{platform}-{arch}"
    if sdk.exists():
        shutil.rmtree(sdk)
    libraries = sdk / "libraries"
    libraries.mkdir(parents=True)
    for name, sources in archive_groups(build).items():
        merge_archives(build, sources, libraries / library_name(name, windows), arch)
    headers = sdk / "include"
    copy_headers(build / "_deps/ctranslate2-src/include", headers)
    copy_headers(build / "_deps/jsoncpp-src/include/json", headers / "json")
    for source in (
        build / "_deps/tokenizers_cpp-src/include/tokenizers_cpp.h",
        build / "_deps/tokenizers_cpp-src/sentencepiece/src/sentencepiece_processor.h",
    ):
        (headers / source.name).write_bytes(source.read_bytes().replace(b"\r\n", b"\n"))
    shutil.copytree(root / ".build-native/dist/licenses", sdk / "licenses")
    vendor = root / ".build-native/dist/rust-vendor"
    for source in vendor.rglob("*"):
        if source.is_file() and source.name.startswith(("LICENSE", "COPYING", "NOTICE")):
            name = source.relative_to(vendor).as_posix().replace("/", "__")
            shutil.copyfile(source, sdk / "licenses" / f"rust-{name}")
    shutil.copyfile(root / ".build-native/dist/build-info.json", sdk / "build-info.json")
    return sdk


def tree_digest(directory):
    return {path.relative_to(directory).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in directory.rglob("*") if path.is_file()}


def update_ide_projects(root):
    project = root / "cpp/ct2-server.xcodeproj/project.pbxproj"
    text = project.read_text()
    # CPU feature dependencies are now folded into CT2; OpenMP is disabled.
    text = "\n".join(line for line in text.splitlines()
                     if not any(identifier in line for identifier in (
                         "D18C69672EFEEAC400A4774A", "D18C69662EFEEAC400A4774A",
                         "D190B5EE2F115CBC00F8D023", "D190B5ED2F115CB200F8D023"))) + "\n"
    if "CoreFoundation.framework" not in text:
        text = text.replace("/* Begin PBXBuildFile section */", """/* Begin PBXBuildFile section */
        C72000012F80000000000001 /* CoreFoundation.framework in Frameworks */ = {isa = PBXBuildFile; fileRef = C72000022F80000000000001 /* CoreFoundation.framework */; };""")
        text = text.replace("/* Begin PBXFileReference section */", """/* Begin PBXFileReference section */
        C72000022F80000000000001 /* CoreFoundation.framework */ = {isa = PBXFileReference; lastKnownFileType = wrapper.framework; name = CoreFoundation.framework; path = System/Library/Frameworks/CoreFoundation.framework; sourceTree = SDKROOT; };""")
        text = text.replace("D1E212412F6CE3BA00D88A5F /* Accelerate.framework in Frameworks */,",
                            "D1E212412F6CE3BA00D88A5F /* Accelerate.framework in Frameworks */,\n"
                            "\t\t\t\tC72000012F80000000000001 /* CoreFoundation.framework in Frameworks */,")
        text = text.replace("D1E212402F6CE3BA00D88A5F /* Accelerate.framework */,",
                            "D1E212402F6CE3BA00D88A5F /* Accelerate.framework */,\n"
                            "\t\t\t\tC72000022F80000000000001 /* CoreFoundation.framework */,")
    text = text.replace("ARCHS = x86_64;", 'ARCHS = "$(ARCHS_STANDARD)";')
    text = text.replace("MACOSX_DEPLOYMENT_TARGET = 11.5;", "MACOSX_DEPLOYMENT_TARGET = 13.0;")
    text = re.sub(r"OTHER_CPLUSPLUSFLAGS = \(.*?\);", 'OTHER_CPLUSPLUSFLAGS = "";', text, flags=re.S)
    project.write_text(text)

    project = root / "cpp/ct2-server.vcxproj"
    text = project.read_text(encoding="utf-8-sig")
    dependencies = ("ntdll.lib;Userenv.lib;tokenizers_cpp.lib;sentencepiece.lib;tokenizers_c.lib;"
                    "ctranslate2.lib;jsoncpp.lib;ws2_32.lib;wsock32.lib;bcrypt.lib;"
                    "iphlpapi.lib;psapi.lib;%(AdditionalDependencies)")
    for platform in ("x64", "ARM64"):
        pattern = (r'(<ItemDefinitionGroup Condition="\'\$\(Configuration\)\|\$\(Platform\)\''
                   rf"=='Release\|{platform}'\">)(.*?)(</ItemDefinitionGroup>)")
        match = re.search(pattern, text, flags=re.S)
        if match is None:
            raise RuntimeError(f"Missing Release {platform} project configuration")
        settings = match[2]
        settings = re.sub(r"<AdditionalDependencies>.*?</AdditionalDependencies>",
                          f"<AdditionalDependencies>{dependencies}</AdditionalDependencies>", settings)
        settings = re.sub(r"<PreprocessorDefinitions>.*?</PreprocessorDefinitions>",
                          "<PreprocessorDefinitions>WIN32;NOMINMAX;_CRT_SECURE_NO_WARNINGS;"
                          "NDEBUG;_CONSOLE;%(PreprocessorDefinitions)</PreprocessorDefinitions>", settings)
        settings = re.sub(r"<AdditionalOptions>.*?</AdditionalOptions>",
                          "<AdditionalOptions>/Zc:__cplusplus /Zc:lambda /Zc:preprocessor "
                          "%(AdditionalOptions)</AdditionalOptions>", settings)
        text = text[:match.start(2)] + settings + text[match.end(2):]
    project.write_text(text, encoding="utf-8")


def apply_sdks(root, inputs):
    names = ("macOS-arm64", "macOS-x86_64", "Windows-arm64", "Windows-x86_64")
    sdks = [inputs / name for name in names]
    canonical = tree_digest(sdks[0] / "include")
    if not canonical:
        raise RuntimeError("SDK headers are missing")
    manifests = [json.loads((sdk / "build-info.json").read_text()) for sdk in sdks]
    for sdk in sdks[1:]:
        if tree_digest(sdk / "include") != canonical:
            raise RuntimeError(f"Architecture-dependent header mismatch: {sdk}")
    for key in ("server_commit", "ctranslate2_commit", "tokenizers_cpp_commit", "jsoncpp_commit"):
        if len({manifest[key] for manifest in manifests}) != 1:
            raise RuntimeError(f"Cannot mix SDK revisions: {key}")
    for name, manifest in zip(names, manifests):
        if manifest["architecture"] != name.split("-")[1]:
            raise RuntimeError(f"Wrong SDK architecture: {name}")
    for directory in HEADER_DIRS:
        destination = root / "cpp/include" / directory
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(sdks[0] / "include" / directory, destination)
    for filename in HEADER_FILES:
        shutil.copyfile(sdks[0] / "include" / filename, root / "cpp/include" / filename)
    for name in LIBRARIES:
        filename = library_name(name)
        destination = root / "cpp/a" / filename
        run("lipo", "-create", str(sdks[0] / "libraries" / filename),
            str(sdks[1] / "libraries" / filename), "-output", str(destination))
        run("lipo", str(destination), "-verify_arch", "arm64", "x86_64")
        if destination.stat().st_size >= 100 * 1024 * 1024:
            raise RuntimeError(f"Universal archive exceeds GitHub's file limit: {filename}")
    for sdk, directory in ((sdks[2], "arm64"), (sdks[3], "windows64")):
        for name in LIBRARIES:
            filename = library_name(name, True)
            shutil.copyfile(sdk / "libraries" / filename, root / "cpp/lib" / directory / filename)
    licenses = root / "cpp/dependency-licenses"
    if licenses.exists():
        shutil.rmtree(licenses)
    for name, sdk in zip(names, sdks):
        shutil.copytree(sdk / "licenses", licenses / name)
    (root / "cpp/native-dependencies.json").write_text(json.dumps(dict(zip(names, manifests)), indent=2) + "\n")
    update_ide_projects(root)
