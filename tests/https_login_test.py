#!/usr/bin/env python3
"""HTTPS login-listener probe.

Sends a request to the server_data endpoint and prints the reply.
Default payload is 213 bytes - the size older clients use; pass a different size
as the 2nd argument to verify the server answers any request size.

Usage: python3 https_login_test.py [port] [size]
"""
import socket
import ssl
import sys

HOST = "127.0.0.1"
port = int(sys.argv[1]) if len(sys.argv) > 1 else 443
size = int(sys.argv[2]) if len(sys.argv) > 2 else 213

ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE

payload = b"POST /growtopia/server_data.php HTTP/1.1\r\nHost: www.growtopia1.com\r\n"
payload += b"A" * (size - len(payload))
assert len(payload) == size, len(payload)

with socket.create_connection((HOST, port), timeout=10) as raw:
    with ctx.wrap_socket(raw) as s:
        s.sendall(payload)
        s.settimeout(10)
        chunks = []
        try:
            while True:
                d = s.recv(4096)
                if not d:
                    break
                chunks.append(d)
        except socket.timeout:
            pass

resp = b"".join(chunks)
print("RESP_BYTES:", len(resp))
print("----")
print(resp.decode("utf-8", "replace"))
ok = b"200 OK" in resp and b"RTENDMARKERBS1001" in resp
sys.exit(0 if ok else 1)
