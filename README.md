# UGV02 Mowgli Bridge

Custom hardware bridge connecting the Waveshare UGV02 (ESP32) to the MowgliNext ROS 2 stack.

## Goal

Start minimal: publish battery status from UGV02 so Mowgli can understand it.

## Package

`ugv_mowgli_bridge` reads JSON telemetry over serial (`/dev/ttyACM0` @ 115200 by default) and publishes `mowgli_interfaces/msg/Power` on `/power` when message type `T == 110`.

Port and baud come from `config/params.yaml` (overridable at launch).

## Layout

```
src/
├── serial/                 # ROS 2 port of wjwwood/serial (workspace dep)
└── ugv_mowgli_bridge/
    ├── package.xml
    ├── CMakeLists.txt
    ├── config/params.yaml
    ├── launch/ugv_hardware_bridge.launch.py
    ├── include/ugv_mowgli_bridge/ugv_bridge.hpp
    └── src/
        ├── ugv_bridge.cpp
        └── ugv_hardware_bridge.cpp
```

`mowgli_interfaces` is not vendored here; it comes from [MowgliNext](https://github.com/cedbossneo/mowglinext) (`ros2/src/mowgli_interfaces`).

## Development model

| Machine | Role |
|---|---|
| **Mac + Docker Desktop** | Edit code, compile-check against ROS 2 Kilted / `mowgli_interfaces` |
| **Raspberry Pi 4 (mower)** | Rebuild, run with real MowgliNext, talk to UGV02 over USB serial |

Sync **source only**. Never copy `build/` or `install/` from Mac → Pi (different CPU arch and install paths).

Docker Desktop on Mac does **not** reliably pass through USB serial devices. Hardware bring-up of the UGV02 happens on the Pi.

```
Mac (edit + colcon check)  --git/rsync source-->  Pi 4 (rebuild + run + serial)
```

---

## Mac: develop in Docker

### Start the container

```bash
docker compose -f docker-compose.dev.yml up -d --build
docker exec -it ugv02-ros-dev bash
```

### Provide `mowgli_interfaces`

`docker-compose.dev.yml` mounts your Mac MowgliNext clone read-only:

| Host (Mac) | Container |
|---|---|
| `/Volumes/iHome/Users/dub/Code/mowglinext` (default) | `/mowgli` |

Override with `MOWGLI_HOST` if the path changes:

```bash
MOWGLI_HOST=/other/path/mowglinext docker compose -f docker-compose.dev.yml up -d
```

Inside the container, link the interfaces package into this workspace (once per container/workspace):

```bash
ln -sfn /mowgli/ros2/src/mowgli_interfaces /workspace/src/mowgli_interfaces
```

If you already have a built Mowgli overlay, you can skip the symlink and source that install instead:

```bash
source /mowgli/ros2/install/setup.bash
```

### Install bridge deps and build

`nlohmann-json3-dev` is installed in the image. The `serial` package is built from `src/serial` in this workspace.

```bash
source /opt/ros/kilted/setup.bash
# if using a prebuilt Mowgli overlay instead of the symlink:
# source /mowgli/ros2/install/setup.bash

cd /workspace
colcon build --packages-select serial mowgli_interfaces ugv_mowgli_bridge
source install/setup.bash
```

If you sourced a Mowgli install overlay, omit `mowgli_interfaces` from `--packages-select`.

Use this step to catch compile/API errors. Run against hardware on the Pi.

---

## Sync Mac → Pi

### Recommended: Git

```bash
# Mac
git add -A
git commit -m "Update UGV bridge"
git push

# Pi
cd ~/ugv02-mower-bridge   # or your clone path
git pull
```

### Fast iteration: rsync

Set your Pi host once:

```bash
export PI_HOST=pi@192.168.1.50          # user@ip or hostname
export PI_REPO=~/ugv02-mower-bridge     # remote path to this repo
```

Then from the Mac repo root:

```bash
./scripts/sync-to-pi.sh
```

This syncs `src/` (including `serial` and `ugv_mowgli_bridge`), excluding `mowgli_interfaces` — use the mower's existing MowgliNext tree for that.

Or manually:

```bash
rsync -avz --delete \
  --exclude build --exclude install --exclude log --exclude .git \
  --exclude mowgli_interfaces \
  ./src/ \
  "${PI_HOST}:${PI_REPO}/src/"
```

---

## Pi 4: build and run with Mowgli

MowgliNext is already installed on the mower. After `git pull` or rsync:

```bash
ssh pi@MOWER_IP
cd ~/ugv02-mower-bridge

# Enter the ROS/Mowgli environment used on the mower
# (container name depends on your mowglinext install)
docker exec -it <mowgli-ros-container> bash

source /opt/ros/kilted/setup.bash
source /path/to/mowgli/install/setup.bash   # provides mowgli_interfaces

cd /path/to/ugv02-mower-bridge            # workspace mounted into that container
colcon build --packages-select serial ugv_mowgli_bridge
source install/setup.bash

ros2 launch ugv_mowgli_bridge ugv_hardware_bridge.launch.py
# or: ros2 run ugv_mowgli_bridge ugv_hardware_bridge
```

Confirm the UGV02 appears (often `/dev/ttyACM0`) and that the process/container has device access (`privileged` or an explicit `/dev` mapping).

Change port/baud in `src/ugv_mowgli_bridge/config/params.yaml` if needed.

---

## Day-to-day loop

1. Edit on Mac (Cursor + Docker).
2. `colcon build` in the Mac container to verify compile.
3. `git push` or `./scripts/sync-to-pi.sh`.
4. On Pi: `git pull` → rebuild → launch → check `/power`.
