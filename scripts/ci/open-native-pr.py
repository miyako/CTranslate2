"""Commit only the dependency refresh and open a reviewable, run-specific PR."""

import json
import os
import subprocess

from native_sdk import HEADER_DIRS, HEADER_FILES, LIBRARIES, library_name


def run(*arguments):
    subprocess.run(arguments, check=True)


paths = ["cpp/native-dependencies.json", "cpp/dependency-licenses",
         "cpp/ct2-server.xcodeproj/project.pbxproj", "cpp/ct2-server.vcxproj"]
paths += [f"cpp/include/{name}" for name in (*HEADER_DIRS, *HEADER_FILES)]
paths += [f"cpp/a/{library_name(name)}" for name in LIBRARIES]
paths += [f"cpp/lib/{directory}/{library_name(name, True)}"
          for directory in ("arm64", "windows64") for name in LIBRARIES]
run("git", "fetch", "origin", "main")
main = subprocess.check_output(["git", "rev-parse", "origin/main"], text=True).strip()
if main != os.environ["GITHUB_SHA"]:
    raise SystemExit("main changed during verification; rerun before proposing replacements")
run("git", "add", "--", *paths)
diff = subprocess.run(["git", "diff", "--cached", "--quiet"], check=False)
if diff.returncode == 0:
    print("Dependency files are unchanged; no pull request needed")
    raise SystemExit(0)
if diff.returncode != 1:
    raise subprocess.CalledProcessError(diff.returncode, diff.args)
run("git", "config", "user.name", "github-actions[bot]")
run("git", "config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com")
branch = f"automation/native-libraries-{os.environ['GITHUB_RUN_ID']}-{os.environ['GITHUB_RUN_ATTEMPT']}"
run("git", "switch", "-c", branch)
run("git", "commit", "-m", "Update native CTranslate2 libraries and matching headers",
    "-m", "Co-authored-by: Copilot App <223556219+Copilot@users.noreply.github.com>")
run("git", "push", "--set-upstream", "origin", branch)
repository = os.environ["GITHUB_REPOSITORY"]
request = {
    "title": "Update native CTranslate2 libraries and matching headers",
    "head": branch,
    "base": "main",
    "body": (
        "Rebuild all native static dependencies from the pinned sources.\n\n"
        "- Replace the five archives used by Xcode with universal arm64/x86_64 archives.\n"
        "- Replace the five Release libraries for each Windows architecture.\n"
        "- Update matching headers, dependency notices, provenance and IDE link settings.\n"
        "- Fold Abseil into SentencePiece and Ruy/CPU features into CTranslate2.\n\n"
        f"Build: https://github.com/{repository}/actions/runs/{os.environ['GITHUB_RUN_ID']}\n\n"
        "Review and merge manually. This workflow does not approve or merge this PR. "
        "PRs created with GITHUB_TOKEN do not automatically trigger other workflows; "
        "use a dedicated NATIVE_LIBRARIES_PR_TOKEN if automatic PR checks are required."
    ),
}
response = subprocess.check_output(
    ["gh", "api", "--method", "POST", f"repos/{repository}/pulls", "--input", "-"],
    input=json.dumps(request), text=True,
)
url = json.loads(response)["html_url"]
print(url)
with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as summary:
    summary.write(f"Native dependency update PR: {url}\n")
