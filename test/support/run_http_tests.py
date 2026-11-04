# Copyright (C) 2026 iceman50. GPL-3.0-or-later.
"""Hermetic HTTP/TLS/proxy regression tests; never changes the OS trust store."""
import argparse
import http.server
import os
from pathlib import Path
import select
import shutil
import socket
import socketserver
import ssl
import subprocess
import tempfile
import threading
import time

parser = argparse.ArgumentParser()
parser.add_argument("--program", default="build/release-mingw-x64/test/dcpp/dcpp_tests.exe")
parser.add_argument("--geoip-program", help="Also run the GeoIP UI/integration executable against a local MMDB fixture")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
work = root / "build" / "http-integration"
work.mkdir(parents=True, exist_ok=True)
openssl = shutil.which("openssl")
if not openssl:
    raise SystemExit("openssl is required")

with tempfile.TemporaryDirectory(dir=work) as tmp:
    tmp = Path(tmp)
    def command(*args):
        subprocess.run([openssl, *map(str, args)], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    ca, ca_key = tmp / "ca.pem", tmp / "ca.key"
    command("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-subj", "/CN=DCPP HTTP test CA",
            "-addext", "basicConstraints=critical,CA:TRUE", "-keyout", ca_key, "-out", ca)
    def certificate(name, san):
        key, csr, cert, ext = [tmp / (name + suffix) for suffix in (".key", ".csr", ".pem", ".ext")]
        command("req", "-new", "-newkey", "rsa:2048", "-nodes", "-subj", "/CN=" + name, "-keyout", key, "-out", csr)
        ext.write_text("subjectAltName=" + san + "\nbasicConstraints=CA:FALSE\nextendedKeyUsage=serverAuth\n")
        command("x509", "-req", "-in", csr, "-CA", ca, "-CAkey", ca_key, "-CAcreateserial", "-days", "1",
                "-extfile", ext, "-out", cert)
        return cert, key

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path == "/stall":
                time.sleep(2)
                return
            if self.path in ("/close", "/truncated", "/abrupt"):
                self.send_response(200)
                if self.path == "/truncated":
                    self.send_header("Content-Length", "10")
                self.end_headers()
                self.wfile.write(b"hello")
                self.wfile.flush()
                self.close_connection = True
                if self.path != "/abrupt":
                    try:
                        self.connection.unwrap()  # Sends an authenticated close_notify.
                    except (ssl.SSLError, OSError):
                        pass
                return
            redirects = {"/relative": "auth", "/cross-origin": f"https://127.0.0.1:{self.server.server_port}/auth",
                         "/downgrade": "http://127.0.0.1/auth", "/loop": "/loop"}
            if self.path in redirects:
                self.send_response(302)
                self.send_header("Location", redirects[self.path])
                body = b""
            else:
                self.send_response(200)
                body = ((root / "test/dcpp/data/geoip/GeoIP2-Country-Test.mmdb").read_bytes()
                        if self.path == "/geoip" else self.headers.get("Authorization", "none").encode())
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        def do_POST(self):
            body = self.rfile.read(int(self.headers["Content-Length"]))
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        def log_message(self, *unused):
            pass

    def server(cert):
        instance = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(*cert)
        instance.socket = context.wrap_socket(instance.socket, server_side=True)
        return instance

    good = server(certificate("localhost", "DNS:localhost,IP:127.0.0.1"))
    wrong = server(certificate("wrong.test", "DNS:wrong.test"))
    connects = []
    class Proxy(socketserver.BaseRequestHandler):
        def handle(self):
            self.request.settimeout(5)
            header = b""
            while not header.endswith(b"\r\n\r\n"):
                byte = self.request.recv(1)
                if not byte or len(header) > 8192:
                    return
                header += byte
            connects.append(header)
            expected = f"CONNECT localhost:{good.server_port} HTTP/1.1\r\n".encode()
            if not header.startswith(expected) or b"Authorization:" in header:
                self.request.sendall(b"HTTP/1.1 403 Forbidden\r\n\r\n")
                return
            with socket.create_connection(("127.0.0.1", good.server_port), timeout=5) as upstream:
                self.request.sendall(b"HTTP/1.1 200 Connection established\r\n\r\n")
                while True:
                    readers, _, _ = select.select([self.request, upstream], [], [], 5)
                    if not readers:
                        return
                    for reader in readers:
                        data = reader.recv(65536)
                        if not data:
                            return
                        (upstream if reader is self.request else self.request).sendall(data)
    proxy = socketserver.ThreadingTCPServer(("127.0.0.1", 0), Proxy)
    servers = [good, wrong, proxy]
    threads = [threading.Thread(target=s.serve_forever, daemon=True) for s in servers]
    for thread in threads:
        thread.start()
    try:
        env = os.environ.copy()
        env["SSL_CERT_FILE"] = str(ca)
        env["DCPP_HTTP_TEST_HTTPS"] = f"https://localhost:{good.server_port}"
        env["DCPP_HTTP_TEST_BAD_NAME"] = f"https://localhost:{wrong.server_port}/"
        env["DCPP_HTTP_TEST_PROXY"] = f"http://127.0.0.1:{proxy.server_address[1]}"
        result = subprocess.run([str((root / args.program).resolve()),
                                 "--gtest_filter=HttpUrl.*:HttpTransport.*:HttpManagerTest.*"],
                                cwd=root, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
        (work / "results.log").write_bytes(result.stdout)
        print(result.stdout.decode("utf-8", errors="replace"))
        if not connects or any(b"Authorization:" in header for header in connects):
            raise SystemExit("CONNECT missing or leaked origin authorization")
        if args.geoip_program and result.returncode == 0:
            env["DCPLUSPLUS_GEOIP_TEST_URL"] = env["DCPP_HTTP_TEST_HTTPS"] + "/geoip"
            result = subprocess.run([str((root / args.geoip_program).resolve())], cwd=root, env=env,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
            (work / "geoip-results.log").write_bytes(result.stdout)
            print(result.stdout.decode("utf-8", errors="replace"))
    finally:
        for s in servers:
            s.shutdown()
            s.server_close()
        for thread in threads:
            thread.join()
raise SystemExit(result.returncode)
