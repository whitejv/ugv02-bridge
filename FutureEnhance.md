# Future enhancements — UGV02 ESP32 → Mowgli bridge

Notes from reviewing Waveshare lower-computer firmware
(`~/ros2_ws/src/ugv_base_ros/ROS_Driver/`, especially `json_cmd.h` and
`baseInfoFeedback()` in `ugv_advance.h`) against the current
`ugv_mowgli_bridge` mapping.

Use this as a backlog; do not treat it as a commitment to implement everything.

---

## Already mapped

| ESP32 | Bridge use |
|---|---|
| `T:1001` `v` (centivolts) | `/hardware_bridge/power`, `/battery_state` |
| `L` / `R` (m/s) | `/wheel_odom` twist |
| `ax..gz` | `/imu/data` |
| `T:13` `X`/`Z` | `/cmd_vel` |
| `T:0` | soft e-stop (zeros motors) |
| `T:142` / `T:131` / `T:143` | feedback interval, flow on, echo off |

---

## In `T:1001` but not mapped

From `baseInfoFeedback()` in `ugv_advance.h`:

| Field | Meaning | Why it matters |
|---|---|---|
| **`odl` / `odr`** | Integrated wheel travel in **cm** (`en_odom_* * 100`) | Better for **pose** in `/wheel_odom` than speed-only `L`/`R` (pose is currently left at 0) |
| **`mx` / `my` / `mz`** | Magnetometer raw | Useful later for heading / mag cal; Mowgli has mag paths |
| **`pan` / `tilt`** | Only if `moduleType == 2` (PT gimbal) | Ignore unless a gimbal is mounted |
| Arm fields (`ab`/`as`/…, torques) | Only if `moduleType == 1` | N/A for bare UGV02 |

Commented out in firmware (not sent today): roll/pitch/yaw and quaternions —
**no orientation in stock feedback** unless firmware is changed.

---

## Measured on ESP32 but not in the JSON

`battery_ctrl.h` (INA219) computes:

- `current_mA`, `power_mW`, `busVoltage_V`, `shuntVoltage_mV`

Only `loadVoltage_V → v` is published in `T:1001`. There is **no charging flag**,
charge current, or charger status on the wire — Mowgli docking “is charging”
cannot be truthful without a firmware change (or separate dock sense).

---

## Useful commands not used by the bridge

Worth considering for drive / safety / calibration:

| Cmd | Purpose |
|---|---|
| **`T:1`** `L`/`R` m/s | Alternate drive path; **this** refreshes the motion heartbeat |
| **`T:136`** heartbeat ms | Default 3000; can lengthen if staying on `T:13` |
| **`T:2`** motor PID | Tuning |
| **`T:138` / `139` / `140`** speed rate L/R | Software speed scale + persist |
| **`T:126`–`129`** IMU get/cal/offset | Align with Mowgli IMU cal flow |
| **`T:1002`** IMU-only feedback | Optional denser IMU stream |
| **`T:132`** LED IO4/IO5 | Status lights |
| **`T:3`** OLED text | Debug on the screen |
| **`T:900`** `main`/`module` | Sets wheel diameter, encoder CPR, **TRACK_WIDTH** |

Not relevant for a plain UGV02 drive bridge: arm, gimbal, ESP-NOW, WiFi,
mission-file, bus-servo ID tools.

`T:999` (`CMD_RESET_EMERGENCY`) is defined in `json_cmd.h` / web UI but has
**no `case` in `uart_ctrl.h`** on this firmware — dead. Real stop is `T:0` →
`setGoalSpeed(0,0)`.

---

## Quirks that affect current mapping

1. **`T:13` does not update `lastCmdRecvTime`** — only `T:1` and `T:11` do.
   Heartbeat is a one-shot zero after 3 s from the last `T:1`/`T:11` (or boot).
   Continuous `T:13` can still drive after that, but mixing with `T:1` is the
   intended heartbeat design. Safer long-term: drive via `T:1` (twist → L/R),
   or poke heartbeat via `T:136` / occasional `T:1`.

2. **Firmware `TRACK_WIDTH` for UGV Rover (`mainType=2`) is `0.172` m**; bridge
   `wheel_track: 0.22` is used only for odom `wz` and may disagree with ESP32
   `rosCtrl` kinematics.

3. **No charging / current in feedback** — docking charge detection needs
   firmware to expose INA219 current (or external sense).

---

## Suggested priority

1. **`odl`/`odr` → odom pose** (biggest localization win)
2. Align **`wheel_track`** with firmware `TRACK_WIDTH` (0.172 for Rover), or
   set/read via `T:900`
3. Drive path: **`T:1` + heartbeat**, or document / mitigate `T:13` limitation
4. **`mx`/`my`/`mz`** if mag heading is needed
5. Optional: LED/OLED, IMU cal cmds, speed-rate
6. Skip until hardware exists: gimbal / arm / WiFi / ESP-NOW
7. Charging: **firmware** change, not bridge-only mapping

---

## Related stability notes (not ESP32 protocol)

- GUI-managed `mowgli_robot.yaml` can overwrite battery thresholds with
  YardForce 24–28 V defaults — use `config/ugv_robot_profile.yaml` +
  `scripts/apply-ugv-robot-profile.sh --check`.
- Keep `/hardware_bridge/emergency` publishing frequently (BT treats >2 s
  silence as e-stop).
