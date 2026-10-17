#!/bin/bash
# Generate a self-signed development certificate for the HTTPS login listener.
#
# NOTE: modern retail clients (5.4x+) REJECT self-signed certificates
# (SSL alert 46) and show a generic connectivity error. For production use a
# real certificate - e.g. a Let's Encrypt IP certificate or a normal cert for a
# domain that fits the 18-byte client patch slot. See docs/DEV-NOTES.md.
set -e
cd "$(dirname "$0")/.."
mkdir -p resources/ctx
openssl req -x509 -newkey rsa:2048 \
  -keyout resources/ctx/server.key -out resources/ctx/server.crt \
  -days 3650 -nodes -subj "/C=XX/O=GTPS Dev/CN=www.growtopia1.com"
echo "Wrote resources/ctx/server.crt + resources/ctx/server.key (self-signed, dev only)"
