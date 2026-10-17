#!/bin/bash
# Daily backup for a GTPS deployment: database dump + items.dat + configs.
# Keeps the last 14 archives in ~/backups. Adjust paths to your layout:
#   GTPS_DIR  - server working directory (default ~/projects/Gurotopia)
set -e
OUT="${GTPS_BACKUP_DIR:-$HOME/backups}"; mkdir -p "$OUT"
TS=$(date +%Y%m%d-%H%M)
SRV="${GTPS_DIR:-$HOME/projects/Gurotopia}"
MR="$HOME/opt/mariadb-root"
export LD_LIBRARY_PATH="$MR/usr/lib/x86_64-linux-gnu:$HOME/opt/gtps-deps/root/usr/lib/x86_64-linux-gnu:/usr/lib/x86_64-linux-gnu"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

"$MR/usr/bin/mariadb-dump" -h 127.0.0.1 -P 3306 -u root --skip-password \
  --single-transaction --skip-lock-tables gurotopia > "$TMP/gurotopia.sql"
cp "$SRV/items.dat" "$TMP/" 2>/dev/null || true
cp "$SRV/server_data.php" "$TMP/" 2>/dev/null || true
cp "$SRV/mysql_login.txt" "$TMP/" 2>/dev/null || true

tar -czf "$OUT/gtps-$TS.tar.gz" -C "$TMP" .
ls -1t "$OUT"/gtps-*.tar.gz 2>/dev/null | tail -n +15 | xargs -r rm -f
echo "backup ok: $OUT/gtps-$TS.tar.gz"
