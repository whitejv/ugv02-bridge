cmake_minimum_required(VERSION 3.8)
project(ugv_mowgli_bridge)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(mowgli_interfaces REQUIRED)
find_package(serial REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)

include_directories(include)

add_executable(ugv_hardware_bridge
  src/ugv_bridge.cpp
  src/ugv_hardware_bridge.cpp
)

ament_target_dependencies(ugv_hardware_bridge
  rclcpp
  mowgli_interfaces
  serial
  geometry_msgs
  nav_msgs
  sensor_msgs
)

target_link_libraries(ugv_hardware_bridge nlohmann_json::nlohmann_json)

install(TARGETS ugv_hardware_bridge
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY config launch
  DESTINATION share/${PROJECT_NAME}
)

ament_package()
