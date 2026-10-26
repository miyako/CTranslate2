"""Small, checked compatibility patches applied only to downloaded sources."""

import pathlib
import shutil
import sys

kind, source = sys.argv[1:]
root = pathlib.Path(source)
cmake = root / "CMakeLists.txt"
text = cmake.read_text()
if kind == "ct2":
    replacements = {
        "set(CMAKE_OSX_DEPLOYMENT_TARGET 10.13)": "set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0)",
        'CMAKE_SYSTEM_PROCESSOR MATCHES "(arm64)|(aarch64)"':
            'CMAKE_SYSTEM_PROCESSOR MATCHES "(ARM64)|(arm64)|(aarch64)"',
    }
elif kind == "tokenizers":
    replacements = {
        'set(TOKENIZERS_CPP_CARGO_FLAGS "")':
            'set(TOKENIZERS_CPP_CARGO_FLAGS "--locked")',
    }
    shutil.copyfile(pathlib.Path(__file__).with_name("tokenizers-Cargo.lock"), root / "rust/Cargo.lock")
    sentencepiece = root / "sentencepiece/CMakeLists.txt"
    spm_text = sentencepiece.read_text()
    if "GIT_TAG 20260107.1" not in spm_text and "GIT_TAG 255c84dadd029fd8ad25c5efb5933e47beaa00c7" not in spm_text:
        raise SystemExit("SentencePiece changed its Abseil source; review the pin")
    spm_text = spm_text.replace("GIT_TAG 20260107.1", "GIT_TAG 255c84dadd029fd8ad25c5efb5933e47beaa00c7")
    sentencepiece.write_text(spm_text)
else:
    raise SystemExit(f"Unknown dependency: {kind}")
for old, new in replacements.items():
    if old in text:
        text = text.replace(old, new)
    elif new not in text:
        raise SystemExit(f"Upstream changed; review patch: {old}")
cmake.write_text(text)
