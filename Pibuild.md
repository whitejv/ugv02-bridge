# Pi build commands

Build and run the UGV bridge on the **Raspberry Pi 4** (mower), against the real MowgliNext install and USB serial to the UGV02.

Sync **source only** from Mac. Never copy `build/` or `install/` from Mac → Pi (different CPU arch and install paths).

---

## Which shell?

| Where | Shell | Why |
|---|---|---|
| **Pi host** | `bash` over SSH | `git pull`, finding devices, launching Docker. |
| **Inside the Mowgli ROS container** | `bash` (`docker exec -it … bash`) | ROS setup scripts and `colcon` expect bash. |

---

## 1. Pull the new files (Pi host)

SSH in, then update the clone after the Mac push from `Macbuild.md`:

```bash
ssh pi@MOWER_IP

cd ~/ugv02-mower-bridge   # or your clone path
git pull
```

Confirm the files you expect are present (for example recent bridge source or docs):

```bash
git log -1 --oneline
ls src/ugv_mowgli_bridge
```

Alternative without git: from the Mac, `./scripts/sync-to-pi.sh` (or the `rsync` in `README.md`). Still rebuild on the Pi after syncing.

---

## 2. Enter the Mowgli ROS container (Pi host)

MowgliNext is already installed on the mower. Enter the ROS environment that container provides:

```bash
# container name depends on your mowglinext install
docker exec -it <mowgli-ros-container> bash
```

What this does: drops you into the same ROS 2 Kilted / Mowgli environment the mower already uses, so `mowgli_interfaces` and related deps are available.

---

## 3. Source ROS + Mowgli, then colcon build (inside container)

```bash
source /opt/ros/kilted/setup.bash
source /path/to/mowgli/install/setup.bash   # provides mowgli_interfaces

cd /path/to/ugv02-mower-bridge            # workspace mounted into that container
colcon build --packages-select serial ugv_mowgli_bridge
source install/setup.bash
```

Notes:

- Omit `mowgli_interfaces` from `--packages-select` — use the mower’s existing MowgliNext install for that package.
- Replace `/path/to/mowgli` and `/path/to/ugv02-mower-bridge` with the real paths inside the container (often the host clone mounted into Docker).

What this does:

- Loads ROS 2 Kilted and the Mowgli overlay (interfaces, etc.).
- Rebuilds `serial` and `ugv_mowgli_bridge` for **arm64** on the Pi.
- Sources this workspace so `ros2` can find the bridge.

---

## 4. Launch against hardware (inside container)

Confirm the UGV02 serial device on the Pi (often `/dev/ttyACM0`). The container needs device access (`privileged` or an explicit `/dev` mapping).

```bash
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null

ros2 launch ugv_mowgli_bridge ugv_hardware_bridge.launch.py
# or: ros2 run ugv_mowgli_bridge ugv_hardware_bridge
```

Port and baud come from `src/ugv_mowgli_bridge/config/params.yaml` (overridable at launch).

Sanity-check telemetry:

```bash
ros2 topic echo /power
```

---

## Day-to-day loop

1. Edit + verify on Mac (`Macbuild.md`).
2. Commit and `git push` after a successful Mac build.
3. On Pi: `git pull` → rebuild → launch → check `/power`.
