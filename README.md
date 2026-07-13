# UGV02 Mowgli Bridge

Custom hardware bridge connecting the Waveshare UGV02 (ESP32) to the MowgliNext ROS 2 stack.

## Goal

Start minimal: publish battery status from UGV02 so Mowgli can understand it.

## Package

`ugv_mowgli_bridge` reads JSON telemetry over serial (`/dev/ttyACM0` @ 115200) and publishes `mowgli_interfaces/msg/Power` on `/power` when message type `T == 110`.

## Layout

```
src/ugv_mowgli_bridge/
├── package.xml
├── CMakeLists.txt
├── config/params.yaml
├── launch/ugv_hardware_bridge.launch.py
├── include/ugv_mowgli_bridge/ugv_bridge.hpp
└── src/
    ├── ugv_bridge.cpp
    └── ugv_hardware_bridge.cpp
```

## Dev container

```bash
docker compose -f docker-compose.dev.yml up -d --build
docker exec -it ugv02-ros-dev bash
```

Inside the container, build with your Mowgli/ROS 2 workspace overlay as needed.
