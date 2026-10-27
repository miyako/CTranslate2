import base64
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
MOCK_TOOL = r'''
import json
import os
import pathlib
import sys

tool = pathlib.Path(sys.argv[0]).name
args = sys.argv[1:]
state = pathlib.Path(os.environ["MOCK_STATE"])
with (state / "calls.jsonl").open("a") as calls:
    calls.write(json.dumps([tool, *args]) + "\n")
if tool == "security":
    if args[:2] == ["list-keychains", "-d"] and "-s" not in args:
        print('    "original.keychain-db"')
    elif args[0] == "create-keychain":
        pathlib.Path(args[-1]).touch()
    elif args[0] == "find-identity":
        print('1) TESTHASH "Test Developer ID"')
elif tool == "codesign":
    if "--verify" in args and os.environ.get("MOCK_BAD_SIGNATURE") == "1":
        sys.exit(1)
    if "-dvv" in args:
        print("TeamIdentifier=TESTTEAM")
elif tool == "hdiutil":
    pathlib.Path(args[-1]).write_bytes(b"test DMG")
elif tool == "xcrun":
    if args[0] == "notarytool":
        print(json.dumps({"status": os.environ.get("MOCK_NOTARY_STATUS", "Accepted")}))
    elif args[0] == "stapler":
        action = args[1]
        counter = state / (action + ".count")
        count = int(counter.read_text()) + 1 if counter.exists() else 1
        counter.write_text(str(count))
        mode = os.environ.get("MOCK_STAPLER_MODE")
        if action == os.environ.get("MOCK_STAPLER_ACTION", "validate"):
            if mode == "network" or (mode == "transient" and count == 1):
                print('error is Error Domain=NSURLErrorDomain Code=-1005 "The network connection was lost."')
                sys.exit(68)
            if mode == "invalid":
                print("The ticket is invalid.")
                sys.exit(68)
elif tool == "spctl":
    if "execute" in args:
        print("rejected (the code is valid but does not seem to be an app)")
        sys.exit(3)
    if os.environ.get("MOCK_GATEKEEPER_REJECT") == "1":
        sys.exit(3)
'''


@unittest.skipIf(os.name == "nt", "macOS signing uses a POSIX shell")
class SigningTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        tools = self.root / "tools"
        tools.mkdir()
        for name in ("security", "codesign", "hdiutil", "xcrun", "spctl", "sleep"):
            tool = tools / name
            tool.write_text(f"#!{sys.executable}\n" + MOCK_TOOL)
            tool.chmod(0o755)
        self.env = {
            **os.environ,
            "PATH": str(tools) + os.pathsep + os.environ["PATH"],
            "MOCK_STATE": str(self.root),
            "APPLE_DEVELOPER_ID_CERTIFICATE": base64.b64encode(b"test certificate").decode(),
            "APPLE_DEVELOPER_ID_CERTIFICATE_PASSWORD": "test password",
            "KEYCHAIN_PASSWORD": "test keychain password",
            "CODESIGN_APPLE_ID": "Test Developer ID",
            "NOTARYTOOL_APPLE_ID": "test@example.invalid",
            "NOTARYTOOL_TEAM_ID": "TESTTEAM",
            "NOTARYTOOL_PASSWORD": "test notary password",
            "BUILD_ARCH": "arm64",
        }
        binary = self.root / ".build-native/dist/bin/ct2-server"
        binary.parent.mkdir(parents=True)
        binary.write_bytes(b"test CLI")

    def sign(self, **environment):
        result = subprocess.run(
            ["bash", str(ROOT / "scripts/ci/sign-macos.sh")],
            cwd=self.root, env={**self.env, **environment},
            text=True, capture_output=True,
        )
        self.calls = [json.loads(line) for line in
                      (self.root / "calls.jsonl").read_text().splitlines()]
        self.assertFalse((self.root / ".build-native/signing/certificate.p12").exists())
        self.assertFalse((self.root / ".build-native/signing/distribution.keychain-db").exists())
        self.assertIn(["security", "list-keychains", "-d", "user", "-s",
                       "original.keychain-db"], self.calls)
        checksum = self.root / ".build-native/packages/ct2-server-macos-arm64-signed.dmg.sha256"
        self.assertEqual(checksum.exists(), result.returncode == 0)
        return result

    def test_notarized_cli_is_not_assessed_as_an_app(self):
        result = self.sign()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(any(call[0] == "spctl" and "open" in call for call in self.calls))
        self.assertFalse(any(call[0] == "spctl" and "execute" in call for call in self.calls))
        verified = [call[-1] for call in self.calls if call[:2] == ["codesign", "--verify"]]
        self.assertEqual(len(verified), 2)
        self.assertTrue(verified[0].endswith("/ct2-server"))
        self.assertTrue(verified[1].endswith(".dmg"))

    def test_stapler_retries_network_errors(self):
        result = self.sign(MOCK_STAPLER_MODE="transient")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / "validate.count").read_text(), "2")
        self.assertIn("::warning::Apple ticket service network failure", result.stderr)

    def test_stapling_retries_network_errors(self):
        result = self.sign(MOCK_STAPLER_MODE="transient", MOCK_STAPLER_ACTION="staple")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.root / "staple.count").read_text(), "2")

    def test_network_retries_are_bounded(self):
        result = self.sign(MOCK_STAPLER_MODE="network")
        self.assertEqual(result.returncode, 68)
        self.assertEqual((self.root / "validate.count").read_text(), "3")

    def test_invalid_ticket_fails_without_retry(self):
        result = self.sign(MOCK_STAPLER_MODE="invalid")
        self.assertEqual(result.returncode, 68)
        self.assertEqual((self.root / "validate.count").read_text(), "1")
        self.assertFalse(any(call[0] == "spctl" for call in self.calls))

    def test_rejected_notarization_fails(self):
        result = self.sign(MOCK_NOTARY_STATUS="Invalid")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(any(call[:2] == ["xcrun", "stapler"] for call in self.calls))

    def test_invalid_signature_fails(self):
        result = self.sign(MOCK_BAD_SIGNATURE="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(any(call[0] == "xcrun" for call in self.calls))

    def test_gatekeeper_rejection_fails(self):
        result = self.sign(MOCK_GATEKEEPER_REJECT="1")
        self.assertEqual(result.returncode, 3)


if __name__ == "__main__":
    unittest.main()
