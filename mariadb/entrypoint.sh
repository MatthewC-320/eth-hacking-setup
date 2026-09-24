#!/bin/bash
set -e

echo "[watchdog] starting on 127.0.0.1:3333"
/usr/local/bin/watchdog &

exec /usr/local/bin/docker-entrypoint.sh "$@"
