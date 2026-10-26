---
layout: default
title: "Build"
---

# Source-built native server

`.github/workflows/build-native.yml` builds **fresh upstream CTranslate2 4.8.4**
and this repository's `cpp/ct2-server` together. It does not link the old
`cpp/lib` archives or use the vendored CTranslate2, tokenizer, SentencePiece or
JsonCpp headers. Ordinary builds stage output without changing tracked files.
The workflow also exports replacement static SDKs, verifies the existing Xcode
and Visual Studio Release projects against them, and can open a PR updating
the tracked dependencies those projects consume.

## Platforms and triggers

| Artifact | Native GitHub-hosted runner | Compiler/backend |
| --- | --- | --- |
| macOS Intel (`x86_64`) | `macos-15-intel` | Apple Clang, Accelerate |
| macOS Apple Silicon (`arm64`) | `macos-15` | Apple Clang, Accelerate |
| Windows x64 (`x86_64`) | `windows-2025` | MSVC x64, static Ruy |
| Windows ARM64 (`arm64`) | `windows-11-arm` | MSVC ARM64, static Ruy |

Windows ARM uses the actual native ARM64 runner, native ARM64 MSVC tools
initialized with `VsDevCmd`, Ninja, and the `aarch64-pc-windows-msvc` Rust target, not an x64 build with
architecture macros. The ARM runner label is subject to GitHub availability
(including repository/plan restrictions); no emulated or x64 fallback occurs.
MSVC ARM64 currently uses upstream's portable/generic kernels, not the GCC
NEON path; this trades performance for portability without false defines.
Intel builds use upstream runtime CPU dispatch, not `-march=native` or a
process-wide AVX2 requirement.
Windows explicitly selects Ninja rather than inheriting the runner's NMake
default; installed Visual Studio 2022/2026 tools are discovered with `vswhere`.

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
* `update_libraries: true` (default): after all four jobs succeed, a dispatch
  on **main** additionally opens the static-library replacement PR described
  below. Disable this input for a distribution-only build.

CLI equivalents:

```sh
gh workflow run build-native.yml --ref main -f signed_macos=false -f update_libraries=true
gh workflow run build-native.yml --ref main -f signed_macos=true -f update_libraries=false
```

Artifacts are attached to the Actions run for 14 days, with platform,
architecture and signing state in the name. They contain a distribution
archive plus SHA-256 checksum; this workflow does not publish a GitHub
Release or modify the existing component `publish.yml`.

## Static SDKs and library-update pull requests

Every successful matrix build also uploads `ct2-sdk-macOS-arm64`,
`ct2-sdk-macOS-x86_64`, `ct2-sdk-Windows-arm64`, or
`ct2-sdk-Windows-x86_64`. Each contains a ZIP with five reusable static
archives, matching public headers, dependency notices and `build-info.json`.
Local exports are under `.build-native/sdk/<platform>-<architecture>/`;
the ZIPs are in `.build-native/sdk-artifacts/`.

The export uses CMake's resolved link closure rather than copying only CT2's
top-level archive. Abseil is folded into SentencePiece, and any static
Ruy/CPU-feature dependencies are folded into CT2. The exported archives are
independently link-tested, and the actual IDE Release project is compiled
and HTTP-smoke-tested against a staged copy of the replacement dependencies.
Windows project verification selects the installed MSVC toolset explicitly.

With `update_libraries: true`, a trusted manual run on main downloads all four
SDKs, checks identical source revisions and public headers, combines the Mac
architectures with `lipo`, and opens a **reviewable PR**, not a direct main push:

| Project input | PR replacement |
| --- | --- |
| Xcode, `cpp/a/` | Universal arm64/x86_64 `libctranslate2.a`, `libsentencepiece.a`, `libtokenizers_cpp.a`, `libtokenizers_c.a`, `libjsoncpp.a` |
| Visual Studio ARM64, `cpp/lib/arm64/` | `ctranslate2.lib`, `sentencepiece.lib`, `tokenizers_cpp.lib`, `tokenizers_c.lib`, `jsoncpp.lib` |
| Visual Studio x64, `cpp/lib/windows64/` | The same five `.lib` filenames, built for x64 |
| Shared `cpp/include/` | Matching CT2, half-float, nlohmann/json, JsonCpp, tokenizer and SentencePiece public headers |

That PR also updates the IDE link settings: no obsolete separate CPU-feature
or OpenMP archive; CoreFoundation and Accelerate on Mac; the Rust-required
Windows system libraries; portable architecture flags and Mac 13.0 minimum.
Xcode's default architecture becomes `ARCHS_STANDARD` instead of Intel-only.
Only the existing **Release x64/ARM64** Visual Studio configurations are
refreshed; the exported Windows libraries use `/MT`, not the Debug CRT.
Dependency notices live in `cpp/dependency-licenses/` and provenance in
`cpp/native-dependencies.json`. Unused legacy archives are not deleted.

All four builds must succeed; the workflow never proposes a partial update.
The aggregate stage also builds the Xcode project against both slices of the
universal archives. A main-branch change during the run makes publication fail
rather than proposing stale replacements. Each run uses its own
`automation/native-libraries-<run>-<attempt>` branch, leaving existing PRs and
manual edits untouched. PR/push builds never create dependency PRs, preventing
an update loop. Static archives are not signed/notarized; signing applies to
the executable/DMG distribution only.

The repository must allow **Settings → Actions → General → Workflow permissions
→ Allow GitHub Actions to create and approve pull requests**. This workflow
only creates a PR: it does not approve or merge it. Its privileged publication
job alone requests `contents: write` and `pull-requests: write`.
PRs opened with `GITHUB_TOKEN` do not automatically trigger other Actions
workflows. If those PR checks are required, provide the optional
`NATIVE_LIBRARIES_PR_TOKEN` secret (a dedicated token with repository contents
and pull-request write access); the workflow uses it for checkout/push/PR creation.

After you merge the generated PR, pull main in your normal checkout. The
replacement `.a`/`.lib` files and matching headers will then be available
at the paths above, not just inside the runner's `.build-native/` directory.

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
The normal build and SDK export remain staged output. Only merging the
generated dependency-update PR replaces tracked IDE-project libraries/headers.
