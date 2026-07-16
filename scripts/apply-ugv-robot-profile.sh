#!/usr/bin/env bash
# Merge or verify UGV-safe keys into Mowgli's GUI-managed mowgli_robot.yaml.
#
# The web UI can overwrite battery_* with YardForce 24–28 V defaults (named
# model presets / schema fallbacks). That makes BT treat an ~11–12 V 3S pack
# as 0% and loop CriticalBatteryDock (UI flaps mowing/idle).
#
# Usage:
#   ./scripts/apply-ugv-robot-profile.sh              # merge profile keys
#   ./scripts/apply-ugv-robot-profile.sh --check       # exit 1 if drift / YardForce-like
#   ./scripts/apply-ugv-robot-profile.sh --dry-run
#   MOWGLI_ROOT=/path/to/mowglinext ./scripts/apply-ugv-robot-profile.sh --check
#
# After apply: restart mowgli so behavior_tree_node reloads params.
#   cd ~/mowglinext/docker && docker compose restart mowgli

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MOWGLI_ROOT="${MOWGLI_ROOT:-${ROOT}/../mowglinext}"
PROFILE="${ROOT}/config/ugv_robot_profile.yaml"
TARGET="${MOWGLI_ROOT}/docker/config/mowgli/mowgli_robot.yaml"

CHECK=0
DRY_RUN=0

for arg in "$@"; do
  case "${arg}" in
    --check) CHECK=1 ;;
    --dry-run) DRY_RUN=1 ;;
    -h|--help)
      sed -n '2,18p' "$0"
      exit 0
      ;;
    *)
      echo "Unknown argument: ${arg}" >&2
      exit 1
      ;;
  esac
done

if [[ ! -f "${PROFILE}" ]]; then
  echo "Missing profile: ${PROFILE}" >&2
  exit 1
fi

if [[ ! -f "${TARGET}" ]]; then
  echo "mowgli_robot.yaml not found: ${TARGET}" >&2
  echo "Set MOWGLI_ROOT to your mowglinext checkout." >&2
  exit 1
fi

export PROFILE TARGET CHECK DRY_RUN

python3 <<'PY'
import os
import sys

import yaml

profile_path = os.environ["PROFILE"]
target_path = os.environ["TARGET"]
check = os.environ["CHECK"] == "1"
dry_run = os.environ["DRY_RUN"] == "1"

PINNED_KEYS = (
    "mower_model",
    "battery_full_voltage",
    "battery_empty_voltage",
    "battery_critical_voltage",
)


def load(path):
    with open(path, encoding="utf-8") as f:
        data = yaml.safe_load(f) or {}
    if not isinstance(data, dict):
        raise SystemExit(f"Expected mapping at top level: {path}")
    return data


def params(doc):
    mowgli = doc.setdefault("mowgli", {})
    if not isinstance(mowgli, dict):
        raise SystemExit("mowgli: must be a mapping")
    rp = mowgli.setdefault("ros__parameters", {})
    if not isinstance(rp, dict):
        raise SystemExit("mowgli.ros__parameters: must be a mapping")
    return rp


def looks_yardforce(rp):
    """True if voltages look like a 24–28 V pack (or missing full while empty is high)."""
    full = rp.get("battery_full_voltage")
    empty = rp.get("battery_empty_voltage")
    critical = rp.get("battery_critical_voltage")
    for label, val in (("full", full), ("empty", empty), ("critical", critical)):
        if val is None:
            continue
        try:
            v = float(val)
        except (TypeError, ValueError):
            continue
        if v >= 20.0:
            return True, f"battery_{label}_voltage={v} looks like YardForce (≥20 V)"
    return False, ""


profile = load(profile_path)
want = params(profile)
missing = [k for k in PINNED_KEYS if k not in want]
if missing:
    raise SystemExit(f"Profile missing keys: {missing}")

target = load(target_path)
have = params(target)

print(f"Profile: {profile_path}")
print(f"Target:  {target_path}")
print()

mismatches = []
for key in PINNED_KEYS:
    expected = want[key]
    actual = have.get(key, "<missing>")
    ok = actual == expected
    mark = "OK" if ok else "DIFF"
    print(f"  [{mark}] {key}: have={actual!r} want={expected!r}")
    if not ok:
        mismatches.append(key)

yf, yf_reason = looks_yardforce(have)
if yf:
    print(f"  [FAIL] YardForce-like pack: {yf_reason}")

if check:
    if mismatches or yf:
        print()
        print("CHECK FAILED — re-apply with:")
        print("  ./scripts/apply-ugv-robot-profile.sh")
        print("then: cd ~/mowglinext/docker && docker compose restart mowgli")
        sys.exit(1)
    print()
    print("CHECK OK — UGV battery / model pins match profile.")
    sys.exit(0)

if not mismatches and not yf:
    print()
    print("Nothing to change — already matches profile.")
    sys.exit(0)

if dry_run:
    print()
    print("DRY-RUN: would update keys:", ", ".join(mismatches or PINNED_KEYS))
    sys.exit(0)

for key in PINNED_KEYS:
    have[key] = want[key]

# Preserve GUI header style when present; otherwise write a short note.
header = (
    "# Mowgli Robot Configuration — managed by mowglinext-gui\n"
    "# UGV battery/model pins last merged from ugv02-bridge "
    "config/ugv_robot_profile.yaml\n"
    "# Changes made here are picked up on container restart.\n\n"
)

with open(target_path, "w", encoding="utf-8") as f:
    f.write(header)
    yaml.safe_dump(
        target,
        f,
        default_flow_style=False,
        sort_keys=False,
        allow_unicode=True,
    )

print()
print(f"Updated: {target_path}")
print("Restart mowgli so BT reloads params:")
print("  cd ~/mowglinext/docker && docker compose restart mowgli")
PY
