# UGV02 Mowgli Bridge

Custom hardware bridge connecting the Waveshare UGV02 (ESP32) to the MowgliNext ROS 2 stack.

## Goal

Start minimal: publish battery status from UGV02 so Mowgli can understand it.

This package **replaces** MowgliNext’s stock serial hardware bridge (`mowgli_hardware` / `hardware_bridge`) for the Waveshare UGV02. Do not run both at once — they fight over the serial device and both publish `/power`.

## Package

`ugv_mowgli_bridge` reads JSON telemetry over serial (`/dev/ttyACM0` @ 115200 by default) and publishes `mowgli_interfaces/msg/Power` on `/power` when message type `T == 110`.

Port and baud come from `config/params.yaml` (overridable at launch).

**Scope today:** battery / `/power` only. It does not yet replace STM32 cmd_vel, heartbeat, IMU, emergency, or mower-control traffic from the stock bridge.

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
| **Raspberry Pi 4 (mower)** | **Primary** for this project: edit (Cursor SSH), build, and run inside the official Mowgli ROS container with real USB serial |
| **Mac + Docker Desktop** | **Optional** compile-check (`Macbuild.md`) — useful for larger work, not required here |

**Same-container cutover:** Mowgli stays installer-managed; this repo stays in its own git tree; they join at runtime by bind-mounting the clone into the Mowgli container (`docker-compose.override.yml` in the Mowgli install dir). No sidecar bridge container.

```
Pi: clone this repo → override mounts it into Mowgli → disable stock hardware_bridge → colcon + launch ugv_hardware_bridge
```

Never copy `build/` or `install/` between Mac and Pi (different CPU arch and install paths). Docker Desktop on Mac does **not** reliably pass through USB serial — hardware bring-up is on the Pi.

Full steps: **[Pibuild.md](Pibuild.md)**.

---

## Pi 4: build and run (summary)

See **[Pibuild.md](Pibuild.md)** for the full guide. In short:

1. Clone or `git pull` this repo on the Pi.
2. In the Mowgli install directory, add a `docker-compose.override.yml` that bind-mounts the clone into the existing Mowgli ROS service (not a second `ugv-bridge` service).
3. Keep stock `hardware_bridge` out of bringup (do **not** use `HARDWARE_BACKEND=mavros` for this).
4. `docker compose up -d`, then `docker exec` into the Mowgli container.
5. Source ROS + Mowgli install → `colcon build --packages-select serial ugv_mowgli_bridge` → launch `ugv_hardware_bridge`.
6. Confirm `/power` is published by `ugv_hardware_bridge`.

### Disable stock / enable UGV (why)

| Interface | Role |
|---|---|
| Stock `hardware_bridge` | COBS serial to STM32; publishes `/power`, `/status`, IMU; motors/heartbeat |
| `ugv_mowgli_bridge` | JSON serial to UGV02 ESP32; publishes `/power` for battery (`T == 110`) |

Only one should own the UGV serial device and `/power`. Quick stop for testing: `pkill -f hardware_bridge_node` inside the container. Persistent: omit `hardware_bridge_node` from the Mowgli launch used on the Pi.

---

## Optional: Mac Docker compile-check

Not required for this small project. If you want an offline Kilted build against a local MowgliNext clone, see **[Macbuild.md](Macbuild.md)** (`Dockerfile.dev` + `docker-compose.dev.yml`).

---

## Optional: sync helpers

If you edit on a Mac and want to push source to the Pi without using git on the mower:

```bash
export PI_HOST=pi@192.168.1.50
export PI_REPO=~/ugv02-mower-bridge
./scripts/sync-to-pi.sh
```

Or `git push` from Mac / Pi and `git pull` on the other machine. Still rebuild on the Pi after syncing.

---

## Day-to-day loop

1. On Pi (Cursor SSH): edit this repo.
2. In the Mowgli container: rebuild → ensure stock `hardware_bridge` is off → launch UGV bridge → check `/power`.
3. Commit when you want (Pi or Mac).

Optional larger workflow: compile-check on Mac (`Macbuild.md`), then sync/pull on the Pi and follow `Pibuild.md`.
