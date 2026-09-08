#!/bin/bash
# maradb 13.0.1 release candidate, random root password, GEF (bata24 fork) preinstalled
set -euo pipefail
cd "$(dirname "$0")"
IMAGE=nday:mariadb
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  echo "[+] building $IMAGE ..."
  docker build -t "$IMAGE" .
fi
exec docker run --rm --name mdb \
  -e MARIADB_ROOT_PASSWORD="$(head -c 100 /dev/urandom | base64)" \
  -e MARIADB_USER=example-user \
  -e MARIADB_PASSWORD=my_cool_secret \
  -e MARIADB_DATABASE=appdb \
  -p 127.0.0.1:3306:3306 \
  "$IMAGE"
