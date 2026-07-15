#!/usr/bin/env bash
# Copy patched Mowgli bringup launch files from this repo into a local
# mowglinext checkout. Does not touch docker or restart containers.
#
# Usage:
#   ./scripts/apply-mowgli-replace-files.sh
#   MOWGLI_ROOT=/path/to/mowglinext ./scripts/apply-mowgli-replace-files.sh
#   ./scripts/apply-mowgli-replace-files.sh --dry-run
#   ./scripts/apply-mowgli-replace-files.sh --restore   # restore from mowgli-orig-files

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MOWGLI_ROOT="${MOWGLI_ROOT:-${ROOT}/../mowglinext}"
REPLACE_ROOT="${ROOT}/mowgli-replace-files"
ORIG_ROOT="${ROOT}/mowgli-orig-files"
DRY_RUN=0
RESTORE=0

for arg in "$@"; do
  case "${arg}" in
    --dry-run) DRY_RUN=1 ;;
    --restore) RESTORE=1 ;;
    -h|--help)
      sed -n '2,12p' "$0"
      exit 0
      ;;
    *)
      echo "Unknown argument: ${arg}" >&2
      exit 1
      ;;
  esac
done

SOURCE_ROOT="${REPLACE_ROOT}"
MODE="apply replace-files"
if [[ "${RESTORE}" -eq 1 ]]; then
  SOURCE_ROOT="${ORIG_ROOT}"
  MODE="restore orig-files"
fi

if [[ ! -d "${SOURCE_ROOT}" ]]; then
  echo "Missing source tree: ${SOURCE_ROOT}" >&2
  exit 1
fi

if [[ ! -d "${MOWGLI_ROOT}" ]]; then
  echo "mowglinext root not found: ${MOWGLI_ROOT}" >&2
  echo "Set MOWGLI_ROOT to your checkout path." >&2
  exit 1
fi

mapfile -t FILES < <(cd "${SOURCE_ROOT}" && find . -type f | sed 's|^\./||' | sort)
if [[ "${#FILES[@]}" -eq 0 ]]; then
  echo "No files under ${SOURCE_ROOT}" >&2
  exit 1
fi

echo "Mode: ${MODE}"
echo "From: ${SOURCE_ROOT}"
echo "To:   ${MOWGLI_ROOT}"
echo

for rel in "${FILES[@]}"; do
  src="${SOURCE_ROOT}/${rel}"
  dst="${MOWGLI_ROOT}/${rel}"
  dst_dir="$(dirname "${dst}")"

  if [[ ! -d "${dst_dir}" ]]; then
    echo "ERROR: destination directory missing: ${dst_dir}" >&2
    echo "Refusing to create new paths under mowglinext." >&2
    exit 1
  fi

  if [[ "${DRY_RUN}" -eq 1 ]]; then
    echo "DRY-RUN: cp ${src} -> ${dst}"
  else
    cp -a "${src}" "${dst}"
    echo "Copied: ${rel}"
  fi
done

echo
if [[ "${DRY_RUN}" -eq 1 ]]; then
  echo "Dry run complete (no files written)."
else
  echo "Done. Restart the Mowgli ROS container (or re-launch) for launch changes to take effect."
  echo "Installed image launch files still need bind-mounts or a rebuild if the container uses /ros2_ws/install."
fi
