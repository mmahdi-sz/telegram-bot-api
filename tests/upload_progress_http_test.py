#!/usr/bin/env python3
"""Offline HTTP startup/authentication smoke test; never uses a real bot token."""
import http.client
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="botapi-progress-smoke-") as root:
    root = Path(root)
    (root / "db").mkdir()
    (root / "tmp").mkdir()
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    with (root / "server.log").open("w+") as log:
        process = subprocess.Popen([
            binary, "--api-id=1", "--api-hash=" + "0" * 32,
            "--local", "--http-ip-address=127.0.0.1", f"--http-port={port}",
            f"--dir={root / 'db'}", f"--temp-dir={root / 'tmp'}", "--verbosity=0",
        ], stdout=log, stderr=log)
        try:
            def request(path):
                connection = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
                try:
                    connection.request("POST", path, json.dumps({"upload_id": "smoke"}),
                                       {"Content-Type": "application/json"})
                    response = connection.getresponse()
                    return response.status, json.loads(response.read())
                finally:
                    connection.close()

            deadline = time.monotonic() + 15
            while True:
                if process.poll() is not None:
                    log.seek(0)
                    raise RuntimeError("Server exited: " + log.read())
                try:
                    code, body = request("/not-a-bot")
                    break
                except (OSError, http.client.HTTPException):
                    if time.monotonic() >= deadline:
                        raise
                    time.sleep(0.1)
            assert code == 404 and body["ok"] is False
            for method in ("getUploadProgress", "cancelUpload", "sendVideo"):
                # The malformed token is rejected by ClientManager before any TDLib client is created.
                code, body = request("/botinvalid/" + method)
                assert code == 401 and body["ok"] is False, (method, code, body)
            print("isolated HTTP startup/authentication smoke tests passed")
        finally:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
