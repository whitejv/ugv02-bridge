# UGV02 Mowgli Bridge

Custom hardware bridge connecting the Waveshare UGV02 (ESP32) to the MowgliNext ROS 2 stack.

## Goal

Start minimal: publish battery status from UGV02 so Mowgli can understand it.

This package **replaces** MowgliNext’s stock serial hardware bridge (`mowgli_hardware` / `hardware_bridge`) for the Waveshare UGV02. Do not run both at once — they fight over the serial device and both publish `/hardware_bridge/power`.

## Package

`ugv_mowgli_bridge` reads JSON telemetry over serial (`/dev/ttyS0` @ 115200 by default on this Pi 4) and publishes `mowgli_interfaces/msg/Power` on `/hardware_bridge/power` when message type `T == 1001` (Waveshare base feedback; voltage field `v` is centivolts).

Port and baud come from `config/params.yaml` (overridable at launch).

**Scope today:** battery / `/hardware_bridge/power` only. It does not yet replace STM32 cmd_vel, heartbeat, IMU, emergency, or mower-control traffic from the stock bridge.

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
mowgli-orig-files/          # baseline copies of Mowgli launch files
mowgli-replace-files/       # patched launch files (use_hardware_bridge)
deploy/docker-compose.override.yml
scripts/
├── apply-mowgli-replace-files.sh
├── ugv-cutover-entrypoint.sh
└── sync-to-pi.sh
```

`mowgli_interfaces` is not vendored here; it comes from [MowgliNext](https://github.com/cedbossneo/mowglinext) (`ros2/src/mowgli_interfaces`).

## Development model

| Machine | Role |
|---|---|
| **Raspberry Pi 4 (mower)** | **Primary** for this project: edit (Cursor SSH), build, and run inside the official Mowgli ROS container with real USB serial |
| **Mac + Docker Desktop** | **Optional** compile-check (`Macbuild.md`) — useful for larger work, not required here |

**Same-container cutover:** Mowgli stays installer-managed; this repo stays in its own git tree. Patched launch files live under `mowgli-replace-files/` and are copied into `../mowglinext` with a script when you choose. Runtime join is a compose override that bind-mounts this clone into `mowgli-ros2`.

```
Pi: apply-mowgli-replace-files.sh → copy deploy override into mowglinext/docker → docker compose up -d
```

Never copy `build/` or `install/` between Mac and Pi (different CPU arch and install paths). Docker Desktop on Mac does **not** reliably pass through USB serial — hardware bring-up is on the Pi.

Full steps: **[Pibuild.md](Pibuild.md)**.

---

## Pi 4: build and run (summary)

See **[Pibuild.md](Pibuild.md)** for the full guide. In short:

1. Clone or `git pull` this repo on the Pi.
2. Apply patched launch files: `./scripts/apply-mowgli-replace-files.sh` (writes into `../mowglinext` only when you run it).
3. Copy [`deploy/docker-compose.override.yml`](deploy/docker-compose.override.yml) to `~/mowglinext/docker/docker-compose.override.yml` (adjust paths if needed).
4. `cd ~/mowglinext/docker && docker compose up -d mowgli` — entrypoint builds the UGV packages if needed, launches `full_system` with `use_hardware_bridge:=false`, then starts `ugv_hardware_bridge`.
5. Confirm `/hardware_bridge/power` is published by `ugv_hardware_bridge`.

### Disable stock / enable UGV (why)

| Interface | Role |
|---|---|
| Stock `hardware_bridge` | COBS serial to STM32; publishes `/hardware_bridge/power`, status, IMU; motors/heartbeat |
| `ugv_mowgli_bridge` | JSON serial to UGV02 ESP32; publishes `/hardware_bridge/power` for battery (`T == 110`) |

Only one should own the serial device and `/hardware_bridge/power`. Persistent cutover: `use_hardware_bridge:=false` via the replace-files + compose override (do **not** use `HARDWARE_BACKEND=mavros` for this).

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
2. In the Mowgli container: rebuild → ensure stock `hardware_bridge` is off → launch UGV bridge → check `/hardware_bridge/power`.
3. Commit when you want (Pi or Mac).

Optional larger workflow: compile-check on Mac (`Macbuild.md`), then sync/pull on the Pi and follow `Pibuild.md`.
