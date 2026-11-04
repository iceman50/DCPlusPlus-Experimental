# Copyright (C) 2026 iceman50. GPL-3.0-or-later.
"""Run HttpConnection integration against a public HTTPS URL and untrusted loopback TLS.

Does not modify Windows certificate stores. Requires openssl on PATH.
"""
import argparse
import http.server
import os
from pathlib import Path
import shutil
import ssl
import subprocess
import tempfile
import threading

parser = argparse.ArgumentParser()
parser.add_argument("--program", default="build/release-mingw-x64/test/dcpp/dcpp_tests.exe")
parser.add_argument("--https-url", default="https://example.com/")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
work = root / "build" / "plugin-http-integration"
work.mkdir(parents=True, exist_ok=True)
openssl = shutil.which("openssl")
if not openssl:
    raise SystemExit("openssl is required to create the untrusted loopback certificate")

with tempfile.TemporaryDirectory(dir=work) as directory:
    cert = Path(directory) / "cert.pem"
    key = Path(directory) / "key.pem"
    subprocess.run([openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                    "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
                    "-keyout", str(key), "-out", str(cert)], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            self.send_response(200)
            self.send_header("Content-Length", "9")
            self.end_headers()
            self.wfile.write(b"untrusted")

        def log_message(self, *unused):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        env = os.environ.copy()
        env["DCPP_PLUGIN_HTTP_OK_URL"] = args.https_url
        env["DCPP_PLUGIN_HTTP_BAD_CERT_URL"] = f"https://127.0.0.1:{server.server_port}/"
        result = subprocess.run([str((root / args.program).resolve()),
                                 "--gtest_filter=PluginRepair.OptInHTTPSRequestsAreIsolatedAndAtomic"],
                                cwd=root, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=150)
        (work / "results.log").write_bytes(result.stdout)
        print(result.stdout.decode("utf-8", errors="replace"))
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
raise SystemExit(result.returncode)
