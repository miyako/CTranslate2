"""Export, assemble, seal, verify or install the exact native resource set."""

import argparse
import json
import os
import subprocess

from native_resources import ROOT, SERVERS, assemble, export, install, seal, verify_final


parser = argparse.ArgumentParser(__doc__)
parser.add_argument("command", choices=["export", "assemble", "seal", "verify", "install"])
parser.add_argument("--target")
args = parser.parse_args()
source = os.environ["GITHUB_SHA"]
work = ROOT / ".build-native"
output = work / "resources"
team = os.environ.get("NOTARYTOOL_TEAM_ID", "")

if args.command == "export":
    export(work / "dist", work / "resource-bundle", args.target, source)
elif args.command == "assemble":
    targets = assemble(work / "resource-downloads", output, source)
    (work / "resource-targets.json").write_text(json.dumps(targets))
elif args.command == "seal":
    seal(output, source, json.loads((work / "resource-targets.json").read_text()), team)
else:
    verify_final(output, source, team)
    binary = output / SERVERS[0]
    binary.chmod(0o755)
    subprocess.run(["python3", str(ROOT / "scripts/ci/smoke-server.py"), str(binary)], check=True)
    if args.command == "install":
        install(output, ROOT / "CTranslate2/Resources")
