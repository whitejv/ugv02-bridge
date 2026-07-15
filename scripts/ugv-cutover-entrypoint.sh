#!/usr/bin/env bash
# Entrypoint for mowgli-ros2 when cutting over to ugv_mowgli_bridge.
# Expects this repo bind-mounted at /ugv02-bridge and patched bringup launch
# files available (source tree apply + install share bind-mounts, or rebuilt
# image). Starts full_system without the stock STM32 bridge, then the UGV bridge.
#
# Usage (compose command override):
#   command: ["/ugv02-bridge/scripts/ugv-cutover-entrypoint.sh"]

set -eo pipefail

UGV_ROOT="${UGV_ROOT:-/ugv02-bridge}"
ROS_SETUP="${ROS_SETUP:-/opt/ros/kilted/setup.bash}"
MOWGLI_SETUP="${MOWGLI_SETUP:-/ros2_ws/install/setup.bash}"
ENABLE_FOXGLOVE="${ENABLE_FOXGLOVE:-true}"

# ROS setup scripts reference optional unbound vars; disable nounset while sourcing.
set +u
# shellcheck disable=SC1090
source "${ROS_SETUP}"
# shellcheck disable=SC1090
source "${MOWGLI_SETUP}"
set -u

cd "${UGV_ROOT}"

need_build=0
if [[ ! -f "${UGV_ROOT}/install/setup.bash" ]]; then
  need_build=1
elif [[ -n "$(find "${UGV_ROOT}/src" -type f -newer "${UGV_ROOT}/install/setup.bash" 2>/dev/null | head -n 1)" ]]; then
  need_build=1
fi

if [[ "${need_build}" -eq 1 ]]; then
  echo "[ugv-cutover] Building serial + ugv_mowgli_bridge..."
  colcon build --packages-select serial ugv_mowgli_bridge
fi

set +u
# shellcheck disable=SC1091
source "${UGV_ROOT}/install/setup.bash"
set -u

cleanup() {
  echo "[ugv-cutover] Stopping..."
  if [[ -n "${FULL_PID:-}" ]]; then kill "${FULL_PID}" 2>/dev/null || true; fi
  if [[ -n "${UGV_PID:-}" ]]; then kill "${UGV_PID}" 2>/dev/null || true; fi
  wait || true
}
trap cleanup EXIT INT TERM

echo "[ugv-cutover] Launching full_system with use_hardware_bridge:=false"
ros2 launch mowgli_bringup full_system.launch.py \
  "enable_foxglove:=${ENABLE_FOXGLOVE}" \
  use_hardware_bridge:=false &
FULL_PID=$!

# Give the graph a moment before attaching the UGV bridge.
sleep 3

echo "[ugv-cutover] Launching ugv_hardware_bridge"
ros2 launch ugv_mowgli_bridge ugv_hardware_bridge.launch.py &
UGV_PID=$!

# Exit if either child exits.
wait -n "${FULL_PID}" "${UGV_PID}"
exit_code=$?
echo "[ugv-cutover] Child exited with ${exit_code}"
exit "${exit_code}"
