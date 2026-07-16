# Pi build commands (same-container)

Canonical guide for running the UGV bridge on the **Raspberry Pi 4** (mower).
Use these steps for a **routine / reproducible cutover build**.

## Philosophy

| Piece | Who owns it |
|---|---|
| **Mowgli stack** | Official installer (`~/mowglinext` on this Pi) |
| **This repo** | Bridge code, `params.yaml`, replace-files, compose cutover, UGV robot profile |
| **Join point** | Runtime: bind-mount this repo into the **existing** Mowgli ROS container via `docker-compose.override.yml` |
| **GUI / `mowgli_robot.yaml`** | Operator settings (battery thresholds, dock, NTRIP, geometry) — **not** bridge cutover |

No sidecar `ugv-bridge` container. One ROS graph, one DDS domain, `mowgli_interfaces` already on the path.

This package **replaces** MowgliNext’s stock serial `hardware_bridge` for the UGV02. Do not run both at once — they fight over the serial device and both publish `/hardware_bridge/*`.

**Interfaces today:** power/status/emergency, stub mower services, `/cmd_vel` → `T:13`, `/wheel_odom`, `/imu/data`, `/battery_state` from Waveshare `T:1001`.

Never copy Mac `build/` or `install/` to the Pi (different CPU arch). Compile only inside `mowgli-ros2`.

---

## Which shell?

| Where | Shell | Why |
|---|---|---|
| **Pi host** | `bash` over SSH | `git`, apply patches, compose override, `docker compose` / `docker exec`. |
| **Inside the Mowgli ROS container** | `bash` | ROS setup scripts and `colcon` expect bash. |

Paths used on this Pi:

| Host | Container |
|---|---|
| `~/ugv02-bridge` | `/ugv02-bridge` |
| `~/mowglinext/docker` | compose project dir |
| container name `mowgli-ros2` | service name `mowgli` |

---

## Routine cutover build (full sequence)

Run these on the **Pi host** unless a step says “inside container”.

### 1. Pull this repo

```bash
cd ~/ugv02-bridge
git pull
git log -1 --oneline
ls src/ugv_mowgli_bridge
```

### 2. Patch Mowgli launch files (source tree)

Patched copies live in this repo so `~/mowglinext` is only changed when you apply them:

| Directory | Purpose |
|---|---|
| `mowgli-orig-files/` | Baseline snapshots of the stock launch files |
| `mowgli-replace-files/` | Same paths with `use_hardware_bridge` (default `true`) |

```bash
cd ~/ugv02-bridge
./scripts/apply-mowgli-replace-files.sh --dry-run   # preview
./scripts/apply-mowgli-replace-files.sh             # copy replace → ../mowglinext
```

Files written into `~/mowglinext`:

- `ros2/src/mowgli_bringup/launch/mowgli.launch.py`
- `ros2/src/mowgli_bringup/launch/full_system.launch.py`

Confirm the patch is present:

```bash
grep -n use_hardware_bridge \
  ~/mowglinext/ros2/src/mowgli_bringup/launch/mowgli.launch.py \
  ~/mowglinext/ros2/src/mowgli_bringup/launch/full_system.launch.py
```

Restore stock launch files later if needed:

```bash
./scripts/apply-mowgli-replace-files.sh --restore
```

Override destination if your checkout is elsewhere:

```bash
MOWGLI_ROOT=/path/to/mowglinext ./scripts/apply-mowgli-replace-files.sh
```

### 3. Install compose override

Copy the example into the Mowgli **docker** directory (where `docker-compose.yaml` lives):

```bash
cp ~/ugv02-bridge/deploy/docker-compose.override.yml \
   ~/mowglinext/docker/docker-compose.override.yml
```

That override does three things:

1. Bind-mounts `~/ugv02-bridge` → `/ugv02-bridge`
2. Bind-mounts the patched source launch files over `/ros2_ws/install/mowgli_bringup/share/mowgli_bringup/launch/` so the image picks up `use_hardware_bridge` without rebuilding the Mowgli image
3. Sets the `mowgli` service command to `/ugv02-bridge/scripts/ugv-cutover-entrypoint.sh`

Confirm service names:

```bash
cd ~/mowglinext/docker
docker compose config --services
docker compose ps
```

### 4. (Recommended) Confirm ESP32 serial on the host

```bash
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || echo "no USB serial yet"
```

Default in `src/ugv_mowgli_bridge/config/params.yaml` is `/dev/ttyACM0` @ 115200. Edit that file before building/restarting if your device name differs.

The bridge still starts without a device (serial open fails, node stays up); battery messages only appear once the port exists.

### 5. Recreate `mowgli` so the override takes effect

```bash
cd ~/mowglinext/docker
docker compose up -d mowgli
```

Confirm the running container picked up the override:

```bash
docker inspect mowgli-ros2 --format 'Cmd={{json .Config.Cmd}}'
docker inspect mowgli-ros2 --format '{{range .Mounts}}{{.Source}} -> {{.Destination}}{{"\n"}}{{end}}' \
  | grep -E 'ugv02-bridge|mowgli.launch|full_system'
```

Expected:

- `Cmd=["/ugv02-bridge/scripts/ugv-cutover-entrypoint.sh"]`
- mounts for `/ugv02-bridge` and the two launch files

### 6. Watch the automatic colcon build + launch

On first start (or when `src/` is newer than `install/`), the entrypoint runs:

```text
colcon build --packages-select serial ugv_mowgli_bridge
```

then launches:

1. `ros2 launch mowgli_bringup full_system.launch.py … use_hardware_bridge:=false`
2. `ros2 launch ugv_mowgli_bridge ugv_hardware_bridge.launch.py`

Follow logs:

```bash
docker logs -f mowgli-ros2
```

Look for:

```text
[ugv-cutover] Building serial + ugv_mowgli_bridge...
Finished <<< serial
Finished <<< ugv_mowgli_bridge
[ugv-cutover] Launching full_system with use_hardware_bridge:=false
[ugv-cutover] Launching ugv_hardware_bridge
```

`Ctrl+C` exits the log follow only (does not stop the container).

Artifacts land on the host at `~/ugv02-bridge/build/` and `~/ugv02-bridge/install/` (bind-mounted).

### 7. Verify cutover

```bash
docker exec -it mowgli-ros2 bash -lc '
  set +u
  source /opt/ros/kilted/setup.bash
  source /ros2_ws/install/setup.bash
  source /ugv02-bridge/install/setup.bash
  set +u

  echo "=== nodes ==="
  ros2 node list | grep -E "hardware_bridge|ugv" || true

  echo "=== /hardware_bridge/power ==="
  ros2 topic info /hardware_bridge/power -v

  echo "=== serial devices ==="
  ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || echo "no USB serial"
'
```

**Success looks like:**

| Check | Expected |
|---|---|
| Stock node | `/hardware_bridge` **absent** |
| UGV node | `/ugv_hardware_bridge` **present** |
| Power publisher | only `ugv_hardware_bridge` on `/hardware_bridge/power` |
| Status / emergency | `/hardware_bridge/status`, `/hardware_bridge/emergency` publishing |
| Drive / sensors | `/cmd_vel` consumed; `/wheel_odom`, `/imu/data`, `/battery_state` publishing |
| Subscribers | typically `behavior_tree_node`, `diagnostics_node`, `foxglove_bridge` |

Echo live battery (needs ESP32 on `/dev/ttyS0`):

```bash
docker exec -it mowgli-ros2 bash -lc '
  set +u
  source /opt/ros/kilted/setup.bash
  source /ros2_ws/install/setup.bash
  source /ugv02-bridge/install/setup.bash
  ros2 topic echo /hardware_bridge/power --once
  ros2 topic echo /hardware_bridge/status --once
  ros2 topic hz /wheel_odom
'
```

Do **not** use `HARDWARE_BACKEND=mavros` for this cutover — that selects MAVROS, not the UGV bridge.

Also verify UGV-safe battery pins (GUI can overwrite these — see below):

```bash
cd ~/ugv02-bridge
./scripts/apply-ugv-robot-profile.sh --check
```

---

## Stable UGV operation (keep it from drifting)

### Ownership

| Owned by | Examples | Rule |
|---|---|---|
| **ugv02-bridge** | serial `/dev/ttyS0`, `T:13` / `T:1001`, emergency heartbeat, replace-files, compose cutover | Re-apply after Mowgli upgrades; never rely on session `pkill` of the stock bridge |
| **`mowgli_robot.yaml` (web GUI)** | `battery_*`, dock pose, NTRIP, geometry, lidar toggles | Pin 3S voltages; keep **mower model = CUSTOM** |

Bridge cutover is restart-stable via replace-files + compose override + entrypoint. The fragile surface is **GUI-managed** `~/mowglinext/docker/config/mowgli/mowgli_robot.yaml`.

### GUI footgun (YardForce voltages)

Schema / named mower presets default to **~28.5 / 24 / 23 V**. Selecting a YardForce (or similar) model card + Save writes those onto disk. BT then maps an ~11–12 V 3S pack to **0%** and loops `CriticalBatteryDock` — the UI looks like it is toggling mowing/idle and battery looks empty.

**Do not** pick a YardForce/Sabo model on this robot. Stay on **CUSTOM** with:

| Key | UGV value |
|---|---|
| `battery_full_voltage` | `12.6` |
| `battery_empty_voltage` | `10.5` |
| `battery_critical_voltage` | `10.8` |

Pinned copy in this repo: [`config/ugv_robot_profile.yaml`](config/ugv_robot_profile.yaml).

```bash
cd ~/ugv02-bridge
./scripts/apply-ugv-robot-profile.sh --check    # exit 1 if drifted
./scripts/apply-ugv-robot-profile.sh            # merge pins into mowgli_robot.yaml
cd ~/mowglinext/docker && docker compose restart mowgli
```

Run `--check` after any GUI Settings save or Mowgli update. The script does **not** auto-apply on container start (avoids fighting the GUI mid-edit).

### Smoke checklist (after any change)

1. `/ugv_hardware_bridge` present; stock `/hardware_bridge` **absent**
2. `ros2 topic echo /hardware_bridge/power --once` → ~11–12 V
3. `./scripts/apply-ugv-robot-profile.sh --check` passes
4. `/behavior_tree_node/high_level_status` stays mostly `IDLE` (not flapping `CRITICAL_BATTERY_*` / stale `EMERGENCY`)
5. Optional: short `/cmd_vel_teleop` pulse still moves the base

### After Mowgli installer upgrades

```bash
cd ~/ugv02-bridge
./scripts/apply-mowgli-replace-files.sh
./scripts/apply-ugv-robot-profile.sh --check   # or apply if needed
cp deploy/docker-compose.override.yml ~/mowglinext/docker/docker-compose.override.yml
cd ~/mowglinext/docker && docker compose up -d mowgli
```

## Day-to-day rebuild (after first cutover)

### Code-only change in `ugv_mowgli_bridge` / `serial`

Either restart so the entrypoint rebuilds if sources are newer:

```bash
cd ~/mowglinext/docker
docker compose restart mowgli
docker logs -f mowgli-ros2
```

Or rebuild manually without recreating the whole stack:

```bash
docker exec -it mowgli-ros2 bash -lc '
  set +u
  source /opt/ros/kilted/setup.bash
  source /ros2_ws/install/setup.bash
  cd /ugv02-bridge
  colcon build --packages-select serial ugv_mowgli_bridge
  source install/setup.bash
'
```

Then relaunch the UGV node (or restart `mowgli`):

```bash
cd ~/mowglinext/docker
docker compose restart mowgli
```

### Launch-patch change (`mowgli-replace-files/`)

```bash
cd ~/ugv02-bridge
./scripts/apply-mowgli-replace-files.sh
cd ~/mowglinext/docker
docker compose up -d mowgli
```

### Param / serial device change

Edit `src/ugv_mowgli_bridge/config/params.yaml`, rebuild (so `install/.../config/params.yaml` updates), restart `mowgli`.

---

## Manual build only (override already active)

If `/ugv02-bridge` is already mounted and you only want a compile check:

```bash
docker exec -it mowgli-ros2 bash -lc '
  set +u
  source /opt/ros/kilted/setup.bash
  source /ros2_ws/install/setup.bash
  cd /ugv02-bridge
  colcon build --packages-select serial ugv_mowgli_bridge
'
```

Notes:

- Omit `mowgli_interfaces` from `--packages-select` — use Mowgli’s install.
- Always `set +u` (or avoid `set -u`) before sourcing ROS setup scripts; they reference optional unbound vars (e.g. `AMENT_TRACE_SETUP_FILES`). The cutover entrypoint already does this.

---

## Session-only stock bridge stop (no compose cutover)

Quick test without the override entrypoint:

```bash
docker exec -it mowgli-ros2 bash -lc '
  set +u
  source /opt/ros/kilted/setup.bash
  source /ros2_ws/install/setup.bash
  ros2 lifecycle set /hardware_bridge shutdown 2>/dev/null || true
  pkill -f hardware_bridge_node || true
  ros2 node list | grep -i hardware || echo "stock hardware_bridge not running"
'
```

This does **not** survive container restart. Prefer the replace-files + override cutover for a lasting setup.

---

## Troubleshooting

| Symptom | What to do |
|---|---|
| Container restart loop; logs show `AMENT_TRACE_SETUP_FILES: unbound variable` | Entrypoint must `set +u` before sourcing ROS setup. Pull latest `scripts/ugv-cutover-entrypoint.sh` and `docker compose restart mowgli`. |
| Override file present but still stock `Cmd=ros2 launch … full_system` | `cd ~/mowglinext/docker && docker compose up -d mowgli` (recreate), not only `restart`. |
| `Failed to open serial port: No such file or directory` | Plug ESP32; `ls /dev/ttyACM*`; fix `params.yaml`; rebuild + restart. |
| Stock `/hardware_bridge` still present | Confirm launch patches applied + install bind-mounts + `use_hardware_bridge:=false` in `docker logs`. |
| No `/ugv_hardware_bridge` | Check `docker logs mowgli-ros2` for colcon failure; ensure `install/setup.bash` exists on host. |
| Undo cutover | `./scripts/apply-mowgli-replace-files.sh --restore`, remove or rename `~/mowglinext/docker/docker-compose.override.yml`, then `docker compose up -d mowgli`. |

---

## Checklist (copy/paste)

```bash
# Host
cd ~/ugv02-bridge && git pull
./scripts/apply-mowgli-replace-files.sh
cp deploy/docker-compose.override.yml ~/mowglinext/docker/docker-compose.override.yml
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || true
cd ~/mowglinext/docker && docker compose up -d mowgli
docker logs -f mowgli-ros2   # wait for Finished <<< ugv_mowgli_bridge, then Ctrl+C

# Verify
docker exec -it mowgli-ros2 bash -lc '
  set +u
  source /opt/ros/kilted/setup.bash
  source /ros2_ws/install/setup.bash
  source /ugv02-bridge/install/setup.bash
  ros2 node list | grep -E "hardware_bridge|ugv"
  ros2 topic info /hardware_bridge/power -v
'
```
