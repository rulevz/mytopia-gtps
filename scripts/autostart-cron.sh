#!/bin/bash
# Install @reboot autostart + daily backup cron entries for the GTPS stack.
# Assumes the helper scripts exist in ~/opt (gtps-start.sh, backup-gtps.sh).
set -e
BOOT="@reboot sleep 15; $HOME/opt/gtps-start.sh >> $HOME/opt/gtps-boot.log 2>&1"
BACKUP="15 3 * * * $HOME/opt/backup-gtps.sh >> $HOME/opt/backup.log 2>&1"

( crontab -l 2>/dev/null | grep -vF "gtps-start.sh" | grep -vF "backup-gtps.sh"; \
  echo "$BOOT"; echo "$BACKUP" ) | crontab -

echo "installed. crontab now:"
crontab -l | tail -4
