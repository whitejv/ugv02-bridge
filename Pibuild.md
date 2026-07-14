# Pi build commands (same-container)

Canonical guide for running the UGV bridge on the **Raspberry Pi 4** (mower).

## Philosophy

| Piece | Who owns it |
|---|---|
| **Mowgli stack** | Official installer (`~/mowgli` or your install dir) |
| **This repo** | Your code only (`ugv02-mower-bridge`) |
| **Join point** | Runtime: bind-mount this repo into the **existing** Mowgli ROS container via `docker-compose.override.yml` |

No sidecar `ugv-bridge` container. One ROS graph, one DDS domain, `mowgli_interfaces` already on the path.

This package **replaces** MowgliNext’s stock serial `hardware_bridge` for the UGV02. Do not run both — they fight over the serial device and both publish `/power`.

**Scope today:** battery / `/power` only (`T == 110`). Not yet a full replacement for cmd_vel, heartbeat, IMU, emergency, or mower-control.

For this small project you can develop entirely on the Pi (Cursor SSH). Mac Docker is optional — see `Macbuild.md`.

---

## Which shell?

| Where | Shell | Why |
|---|---|---|
| **Pi host** | `bash` over SSH | `git`, compose override, `docker compose` / `docker exec`. |
| **Inside the Mowgli ROS container** | `bash` | ROS setup scripts and `colcon` expect bash. |

---

## 1. Clone or pull this repo (Pi host)

```bash
ssh pi@MOWER_IP

# first time:
# git clone <your-remote-url> ~/ugv02-mower-bridge

cd ~/ugv02-mower-bridge   # or your clone path
git pull
```

Confirm sources are present:

```bash
git log -1 --oneline
ls src/ugv_mowgli_bridge
```

---

## 2. Join at runtime: `docker-compose.override.yml`

Create or edit this file in your **Mowgli installation directory** (where you ran the installer — often `~/mowgli`), **not** as the live compose file at this repo’s root.

Goals:

1. Bind-mount this clone into the Mowgli ROS service.
2. Do **not** add a separate `ugv-bridge` service.
3. Keep stock `hardware_bridge` out of bringup (see §3).

Example (adjust service name, user path, and container paths to match your install):

```yaml
# Place in Mowgli install dir, e.g. ~/mowgli/docker-compose.override.yml
services:
  mowgli:   # ← use the real service name from docker compose config / docker compose ps
    volumes:
      # Host clone → path inside the Mowgli ROS container
      - /home/USER/ugv02-mower-bridge:/ugv02-mower-bridge
    # Ensure the stock hardware_bridge node is NOT started.
    # Prefer a launch that omits hardware_bridge_node, or a custom
    # bringup launch you control. Do NOT use HARDWARE_BACKEND=mavros
    # for this cutover (that selects MAVROS, not the UGV02 bridge).
    #
    # Example shape only — match your installer’s command:
    # command: >
    #   bash -c "source /opt/ros/kilted/setup.bash &&
    #            source /ros2_ws/install/setup.bash &&
    #            ros2 launch mowgli_bringup full_system.launch.py
    #            <args that skip hardware_bridge_node>"
```

Find the real service / container name:

```bash
cd ~/mowgli   # or your Mowgli install dir
docker compose config --services
docker compose ps
```

Apply the override and restart:

```bash
cd ~/mowgli
docker compose up -d
```

---

## 3. Disable Mowgli’s stock serial interface

Stock bringup starts `hardware_bridge` (`mowgli_hardware`) — COBS serial to the STM32 on `/dev/mowgli` or `/dev/ttyACM0`.

Check inside the container:

```bash
docker exec -it <mowgli-ros-container> bash

ros2 node list | grep -i hardware
ros2 topic info /power
```

### Session (quick test)

```bash
ros2 lifecycle set /hardware_bridge shutdown 2>/dev/null || true
pkill -f hardware_bridge_node || true

ros2 node list | grep -i hardware || echo "stock hardware_bridge not running"
```

### Persistent (survives reboot / stack restart)

Keep `hardware_bridge_node` out of the launch used by the Mowgli service (compose `command:` and/or `mowgli_bringup` launch files). Pointing `serial_port` in `hardware_bridge.yaml` at a dummy path is not enough — the node can still claim `/power`.

---

## 4. Build and enable the UGV bridge (inside Mowgli container)

```bash
docker exec -it <mowgli-ros-container> bash

source /opt/ros/kilted/setup.bash
source /ros2_ws/install/setup.bash   # typical Mowgli overlay; provides mowgli_interfaces

cd /ugv02-mower-bridge               # path from the override mount
colcon build --packages-select serial ugv_mowgli_bridge
source install/setup.bash

ros2 node list | grep -i hardware || echo "stock hardware_bridge not running"
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null

ros2 launch ugv_mowgli_bridge ugv_hardware_bridge.launch.py
# or: ros2 run ugv_mowgli_bridge ugv_hardware_bridge
```

Notes:

- Omit `mowgli_interfaces` from `--packages-select` — use the mower’s Mowgli install.
- Port/baud: `src/ugv_mowgli_bridge/config/params.yaml` (often `/dev/ttyACM0` @ 115200).
- The Mowgli container already needs `/dev` access (`privileged` or device mounts from the installer).

Verify cutover:

```bash
ros2 topic info /power          # publisher should be ugv_hardware_bridge
ros2 topic echo /power
```

---

## Day-to-day loop (Pi + Cursor SSH)

1. Connect Cursor to the Pi; open `~/ugv02-mower-bridge`.
2. Edit and save.
3. In the Mowgli container: `colcon build --packages-select serial ugv_mowgli_bridge` then re-launch (or restart) `ugv_hardware_bridge`.
4. Check: `ros2 topic echo /power` (and container logs if needed).

Optional: commit from the Pi (or Mac) and `git push` when you want a remote backup. Never copy `build/` or `install/` between machines — different CPU arch.
