---
layout: default
title: "Build"
---

# Source-built native server

`.github/workflows/build-native.yml` builds **fresh upstream CTranslate2 4.8.4**
and this repository's `cpp/ct2-server` together. It does not link the old
`cpp/lib` archives or use the vendored CTranslate2, tokenizer, SentencePiece or
JsonCpp headers. It never replaces tracked binaries. The legacy Xcode and
Visual Studio projects are not used by this workflow.

## Platforms and triggers

| Artifact | Native GitHub-hosted runner | Compiler/backend |
| --- | --- | --- |
| macOS Intel (`x86_64`) | `macos-15-intel` | Apple Clang, Accelerate |
| macOS Apple Silicon (`arm64`) | `macos-15` | Apple Clang, Accelerate |
| Windows x64 (`x86_64`) | `windows-2025` | MSVC x64, static Ruy |
| Windows ARM64 (`arm64`) | `windows-11-arm` | MSVC ARM64, static Ruy |

Windows ARM uses the actual native ARM64 runner, Visual Studio `-A ARM64`
toolchain, and `aarch64-pc-windows-msvc` Rust target, not an x64 build with
architecture macros. The ARM runner label is subject to GitHub availability
(including repository/plan restrictions); no emulated or x64 fallback occurs.
MSVC ARM64 currently uses upstream's portable/generic kernels, not the GCC
NEON path; this trades performance for portability without false defines.
Intel builds use upstream runtime CPU dispatch, not `-march=native` or a
process-wide AVX2 requirement.

Pull requests and pushes to `main` run when `cpp/**`, `scripts/ci/**`,
the workflow, or this document changes. **All automatic/PR builds are
unsigned and need no secrets**, including fork PRs. No `pull_request_target`
or untrusted-code signing is used. Each matrix leg builds, runs CTest
`server-http-smoke` and `model-support` (model/prompt unit tests), installs
the server, checks its real binary architecture and dynamic dependencies,
and repeats the HTTP smoke against the installed binary. The smoke checks
an empty `/v1/models` and HTTP 400 JSON errors for chat, embeddings, generate
and translate requests without models, confirming the process stays alive.
The CLI has no `--help` option (`-h` means host), so HTTP startup is used
instead. These are build/startup tests, not model inference coverage.

Run manually in Actions → **Build native server** → **Run workflow**:

* `signed_macos: true` (default): two signed/notarized macOS DMGs plus two unsigned
  Windows ZIPs. Only dispatch can sign. Use a trusted reviewed ref.
  A missing secret, invalid signature, rejected notarization or Gatekeeper
  failure fails that macOS job; it **never falls back to unsigned output**.
* `signed_macos: false`: four unsigned ZIPs for development.

CLI equivalents:

```sh
gh workflow run build-native.yml --ref main -f signed_macos=false
gh workflow run build-native.yml --ref main -f signed_macos=true
```

Artifacts are attached to the Actions run for 14 days, with platform,
architecture and signing state in the name. They contain a distribution
archive plus SHA-256 checksum; this workflow does not publish a GitHub
Release or modify the existing component `publish.yml`.

## Signing and notarization secrets

The signed dispatch uses the same repository secrets as `publish.yml`:

| Secret | Exact value expected |
| --- | --- |
| `APPLE_DEVELOPER_ID_CERTIFICATE` | Base64-encoded PKCS#12 (`.p12`) Developer ID Application certificate **with private key** |
| `APPLE_DEVELOPER_ID_CERTIFICATE_PASSWORD` | Password protecting that `.p12` |
| `KEYCHAIN_PASSWORD` | Password for the ephemeral build keychain |
| `CODESIGN_APPLE_ID` | Exact Developer ID Application signing identity name (or its certificate SHA-1), **not** a login email |
| `NOTARYTOOL_APPLE_ID` | Apple account email for notarization |
| `NOTARYTOOL_TEAM_ID` | Apple Developer team ID matching the signing certificate |
| `NOTARYTOOL_PASSWORD` | App-specific password for that Apple account |

`scripts/ci/sign-macos.sh` imports into an isolated keychain, signs the
executable with hardened runtime and secure timestamp, verifies the
signature/team, creates and signs a DMG, waits for an explicitly
`Accepted` notarization result, staples and validates its ticket, and
checks Gatekeeper assessment of the DMG and executable. A shell exit trap
restores the original keychain search list and deletes the private keychain
and decoded certificate on success **or failure**. Apple credentials are
passed only to the signed step and never stored in an artifact. The
notarized/stapled DMG is the distribution container; raw command-line
executables cannot themselves have a notarization ticket stapled.

## Dependency/version lock

| Dependency | Pinned source |
| --- | --- |
| OpenNMT/CTranslate2 **4.8.4** | `5de572ef60b67a0276c5989ee73d0072808ff4c3` |
| mlc-ai/tokenizers-cpp | `c586c52f93f7b060753bd2388eb96a105cb7374d` |
| open-source-parsers/jsoncpp **1.9.6** | `89e2973c754a9c02a49974d839779b151e95afd6` |
| SentencePiece, msgpack, CT2 cpu_features/Ruy/spdlog and their submodules | Gitlinks in the pinned parent commits, checked out recursively |
| SentencePiece's Abseil **20260107.1** | `255c84dadd029fd8ad25c5efb5933e47beaa00c7` |
| Rust | **1.91.0**, explicit native host triple per matrix leg |
| CMake / Ninja | **3.31.6** / **1.11.1.4** Python packages |

The Rust crate closure and registry checksums are committed in
`scripts/ci/tokenizers-Cargo.lock`; tokenizers-cpp's Cargo invocation is
patched to use `--locked`. Eigen, nlohmann/json, inja and cpp-httplib are
header-only inputs from this repository's checked-out commit. Those are
copied to a clean include directory, preventing accidental old/new ABI
mixing. No OpenMP functions or runtime are linked.

Compatibility patches touch only fetched source: the CT2 macOS deployment
target becomes 13.0, CT2 recognizes CMake's uppercase native `ARM64`
processor name, tokenizer Cargo uses the lockfile, and SentencePiece's
Abseil tag is resolved to its immutable commit. Upstream and Rust source
licenses, header-only inputs and vendored Rust sources accompany each
distribution. `build-info.json` records the server/dependency commits,
architecture and observed tool versions.

To update, change the SHA/version in `cpp/CMakeLists.txt`, patch expectations
and manifest in `scripts/ci/`, and this table together. Regenerate the Cargo
lockfile with the pinned Rust version against the new tokenizer manifest;
review the dependency diff and rerun all four matrix legs before release.
“Reproducible” means pinned source/dependency/tool inputs and repeatable
build commands, **not byte-identical signed output**: hosted OS/Xcode/VS
images receive updates, and signatures/notarization include timestamps.
Network access to GitHub, crates.io and Python package indexes is required.

### Model support

The version change applies to this new source-build path; old checked-in
archives remain unchanged and are not evidence of 4.8.4 support.
[Upstream 4.8.4](https://github.com/OpenNMT/CTranslate2/releases/tag/v4.8.4)
includes Gemma 4/Unified converter loaders, but its converter explicitly
rejects KV-shared configurations (E2B/E4B) and recommends the non-KV-shared
31B model. A successful build does not promise arbitrary Gemma 4 variants,
multimodal functionality, or compatibility with every Hugging Face checkpoint.
Convert models with a matching **4.8.4** Python environment, then use the
resulting CT2 model/tokenizer files with the server; the Python converter
is not bundled in these CPU-only native distributions.

## Runtime dependencies and local reproduction

* macOS: **13.0 or later**, matching Intel/Apple Silicon architecture.
  Only Apple system libraries/frameworks (including Accelerate) are linked;
  no Homebrew `libomp`, MKL, CUDA, or third-party dylibs are required.
* Windows: matching 64-bit architecture on a supported current Windows OS
  (Windows 11 for ARM64). C++, JsonCpp, CT2, tokenizer/Rust and SentencePiece
  libraries use static linkage and MSVC `/MT`; no VC++ redistributable,
  OpenMP DLL, MKL, CUDA or external tokenizer library is intended.
  Import-table validation rejects non-system dependencies and dynamic
  compiler runtimes. Windows artifacts are not Authenticode-signed.
* CT2 is built with `OPENMP_RUNTIME=NONE` to keep distributions portable.
  Its independent execution/pool threads still work, but intra-op OpenMP
  acceleration is unavailable. CPU performance differs from MKL/OpenMP
  builds. No GPU backend is enabled.

Install Git (with submodule access), Python 3.13, Rust/rustup and the platform
compiler/SDK (Xcode command-line tools on macOS, Visual Studio C++ including
the actual target architecture on Windows). Use a short checkout path on
Windows and enable Git long paths if necessary:

```sh
python -m pip install cmake==3.31.6 ninja==1.11.1.4
rustup toolchain install 1.91.0 --profile minimal
# macOS ARM64; use x86_64 for native Intel/x64 hosts:
python scripts/ci/build-native.py --arch arm64
```

For exact Rust matching, set `RUSTUP_TOOLCHAIN` to `1.91.0-` plus one of
`aarch64-apple-darwin`, `x86_64-apple-darwin`, `aarch64-pc-windows-msvc`,
or `x86_64-pc-windows-msvc`. On Windows use the matching Python process
architecture too. The script rejects a host/target architecture mismatch.
The workflow uses Python 3.13 because setup-python does not provide native
Windows ARM64 Python 3.12 builds.
All fetched source, compiler output and installed files live under
`.build-native/`; delete that directory for a completely fresh build.
Installed output is `.build-native/dist/bin/ct2-server[.exe]`.
These files are build output, not replacements for tracked `cpp/lib` or
`cpp/include` contents.
