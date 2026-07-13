# Mac build commands

This repo develops against ROS 2 Kilted inside Docker (`Dockerfile.dev` + `docker-compose.dev.yml`).  
Use these commands from the **repo root**.

---

## Which shell?

| Where | Shell | Why |
|---|---|---|
| **Host (Mac / Linux)** | Terminal: `zsh` (macOS default) or `bash` | Runs Docker / Compose. Platform flags belong here. |
| **Inside the container** | `bash` (`docker exec -it ugv02-ros-dev bash`) | ROS setup scripts and `colcon` expect bash. |

Host commands below are plain bash-compatible; paste them into Terminal as-is.

---

## Why `--platform linux/amd64`?

Base image `osrf/ros:kilted-desktop` is published for **linux/amd64**.

On Apple Silicon (or other **arm64** hosts) Docker may warn or fail:

```text
The requested image's platform (linux/amd64) does not match the
detected host platform (linux/arm64/v8) and no specific platform was requested
```

Pinning the platform tells Docker to pull/run the amd64 image (via QEMU emulation on arm64 hosts). On a native amd64 machine the flag is harmless.

---

## 1. Build / start the dev container (host shell)

From the repo root on the host:

```bash
# Build the image for amd64, then start it detached
docker compose -f docker-compose.dev.yml build --build-arg BUILDKIT_INLINE_CACHE=1
DOCKER_DEFAULT_PLATFORM=linux/amd64 docker compose -f docker-compose.dev.yml up -d --build
```

One-shot equivalent (build + start, platform forced for pull/build/run):

```bash
DOCKER_DEFAULT_PLATFORM=linux/amd64 docker compose -f docker-compose.dev.yml up -d --build
```

Optional: point Compose at a different MowgliNext checkout on the host:

```bash
MOWGLI_HOST=/path/to/mowglinext \
  DOCKER_DEFAULT_PLATFORM=linux/amd64 \
  docker compose -f docker-compose.dev.yml up -d --build
```

Enter the container:

```bash
docker exec -it ugv02-ros-dev bash
```

What this does:

- Builds from `Dockerfile.dev` (ROS Kilted desktop + bridge deps).
- Starts service `ros-dev` as container `ugv02-ros-dev`.
- Mounts this repo at `/workspace` and MowgliNext at `/mowgli` (read-only).

---

## 2. Wire `mowgli_interfaces` (inside container)

Once per container / workspace (bash inside `ugv02-ros-dev`):

```bash
ln -sfn /mowgli/ros2/src/mowgli_interfaces /workspace/src/mowgli_interfaces
```

Or, if Mowgli is already built on the mounted tree, skip the symlink and source the overlay later:

```bash
source /mowgli/ros2/install/setup.bash
```

---

## 3. Colcon build (inside container)

```bash
source /opt/ros/kilted/setup.bash
# only if using a prebuilt Mowgli overlay instead of the symlink:
# source /mowgli/ros2/install/setup.bash

cd /workspace
colcon build --packages-select serial mowgli_interfaces ugv_mowgli_bridge
source install/setup.bash
```

If you sourced a Mowgli install overlay, drop `mowgli_interfaces` from `--packages-select`.

What this does:

- Loads the ROS 2 Kilted environment.
- Builds the serial library, interfaces (if selected), and the UGV bridge package.
- Sources the workspace so `ros2` can find the new packages.

This step is for compile-checking on Mac/Docker. Run against real hardware on the Pi (see `Pibuild.md`).

---

## Quick rebuild after Dockerfile changes

Host shell, repo root:

```bash
DOCKER_DEFAULT_PLATFORM=linux/amd64 docker compose -f docker-compose.dev.yml up -d --build
docker exec -it ugv02-ros-dev bash
```

Then re-run the colcon steps in §3 (symlink in §2 only if missing).

---

## 4. If the build succeeded — commit to git (host shell)

Only after `colcon build` finishes cleanly. Do this on the **Mac host** (repo root), not inside the container — the workspace is bind-mounted, so source edits are already on disk.

Do **not** commit `build/`, `install/`, or `log/` (those are arch-specific artifacts).

```bash
cd /path/to/this/repo   # Mac checkout, not the container path

git status
git add -A
git status              # confirm only source/docs/config you intend to ship

git commit -m "$(cat <<'EOF'
Update UGV bridge after successful Mac colcon build.

EOF
)"
git push
```

Adjust the commit message to match what changed. After push, continue on the Pi with `Pibuild.md`.
