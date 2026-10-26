"""Model-free startup and HTTP regression checks for a native executable."""

import json
import pathlib
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

binary = pathlib.Path(sys.argv[1]).resolve()
with socket.socket() as available:
    available.bind(("127.0.0.1", 0))
    port = available.getsockname()[1]
base = f"http://127.0.0.1:{port}"
client = urllib.request.build_opener(urllib.request.ProxyHandler({}))
process = subprocess.Popen([str(binary), "-s", "-h", "127.0.0.1", "-p", str(port)])
try:
    for attempt in range(100):
        if process.poll() is not None:
            raise RuntimeError(f"Server exited prematurely: {process.returncode}")
        try:
            with client.open(base + "/v1/models", timeout=1) as response:
                models = json.load(response)
            break
        except (urllib.error.URLError, TimeoutError):
            time.sleep(0.1)
    else:
        raise RuntimeError("Server did not become responsive")
    if models != {"object": "list", "data": []}:
        raise RuntimeError(f"Expected an empty model list: {models}")
    for route, payload in (
        ("/v1/chat/completions", {"messages": [{"role": "user", "content": "Hello"}]}),
        ("/v1/embeddings", {"input": ["Hello"]}),
        ("/v1/generate", {"prompt": "Hello"}),
        ("/v1/translate", {"source": ["Hello"]}),
    ):
        request = urllib.request.Request(
            base + route, data=json.dumps(payload).encode(),
            headers={"Content-Type": "application/json"},
        )
        try:
            with client.open(request, timeout=3) as response:
                raise RuntimeError(f"{route}: expected HTTP 400, got {response.status}")
        except urllib.error.HTTPError as error:
            body = json.load(error)
            if error.code != 400 or not isinstance(body.get("error"), dict):
                raise RuntimeError(f"{route}: invalid missing-model response: {body}") from error
        if process.poll() is not None:
            raise RuntimeError(f"Server crashed after {route}")
    with client.open(base + "/v1/models", timeout=3) as response:
        if json.load(response)["data"] != []:
            raise RuntimeError("Server model list changed during empty-model smoke")
    print("HTTP smoke passed: empty models and four structured missing-model errors")
finally:
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)
