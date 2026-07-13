#!/usr/bin/env bash
# Sync workspace source from this Mac repo to the Pi.
# Usage:
#   export PI_HOST=pi@192.168.1.50
#   export PI_REPO=~/ugv02-mower-bridge
#   ./scripts/sync-to-pi.sh

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PI_HOST="${PI_HOST:?Set PI_HOST, e.g. export PI_HOST=pi@192.168.1.50}"
PI_REPO="${PI_REPO:-~/ugv02-mower-bridge}"

rsync -avz --delete \
  --exclude build \
  --exclude install \
  --exclude log \
  --exclude .git \
  --exclude mowgli_interfaces \
  "${ROOT}/src/" \
  "${PI_HOST}:${PI_REPO}/src/"

echo "Synced to ${PI_HOST}:${PI_REPO}/src/"
echo "On the Pi: rebuild with colcon (serial + ugv_mowgli_bridge), then relaunch."
