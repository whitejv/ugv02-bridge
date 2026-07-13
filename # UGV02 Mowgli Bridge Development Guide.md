# UGV02 Mowgli Bridge Development Guide

## Project Overview
This repository contains only the custom hardware bridge for connecting the Waveshare UGV02 (ESP32) to the MowgliNext ROS 2 stack.

**Goal**: Start minimal → publish battery status from UGV02 → Mowgli understands it.

---

## Directory Structure
ugv02-mower-bridge/
├── src/
│   └── ugv_mowgli_bridge/
│       ├── package.xml
│       ├── CMakeLists.txt
│       ├── config/
│       │   └── params.yaml
│       ├── include/
│       │   └── ugv_mowgli_bridge/
│       │       └── ugv_bridge.hpp
│       └── src/
│           ├── ugv_bridge.cpp
│           └── ugv_hardware_bridge.cpp
├── docker-compose.dev.yml
├── Dockerfile.dev
└── README.md

---

## Key Files

### 1. `src/ugv_mowgli_bridge/package.xml`
```xml
<?xml version="1.0"?>
<package format="3">
  <name>ugv_mowgli_bridge</name>
  <version>0.1.0</version>
  <description>UGV02 to Mowgli Hardware Bridge</description>
  <maintainer email="your@email.com">Your Name</maintainer>
  <license>MIT</license>

  <depend>rclcpp</depend>
  <depend>mowgli_interfaces</depend>
  <depend>serial</depend>
  <depend>nlohmann_json</depend>

  <exec_depend>ros2launch</exec_depend>

  <export>
    <build_type>ament_cmake</build_type>
  </export>
</package>

cmake_minimum_required(VERSION 3.8)
project(ugv_mowgli_bridge)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(mowgli_interfaces REQUIRED)
find_package(serial REQUIRED)

include_directories(include)

add_executable(ugv_hardware_bridge
  src/ugv_bridge.cpp
  src/ugv_hardware_bridge.cpp
)

ament_target_dependencies(ugv_hardware_bridge
  rclcpp
  mowgli_interfaces
  serial
)

target_link_libraries(ugv_hardware_bridge nlohmann_json::nlohmann_json)

install(TARGETS ugv_hardware_bridge
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY config launch include/
  DESTINATION share/${PROJECT_NAME}
)

ament_package()

#pragma once

#include <rclcpp/rclcpp.hpp>
#include <mowgli_interfaces/msg/power.hpp>
#include <serial/serial.h>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <memory>

using json = nlohmann::json;

class UGVBridge : public rclcpp::Node
{
public:
    UGVBridge();
    ~UGVBridge();

private:
    // Publisher
    rclcpp::Publisher<mowgli_interfaces::msg::Power>::SharedPtr power_pub_;

    // Serial communication
    serial::Serial serial_port_;
    std::string serial_port_name_ = "/dev/ttyACM0";   // ← Change if needed
    int baud_rate_ = 115200;

    // Reading thread
    std::unique_ptr<std::thread> read_thread_;
    bool running_ = true;

    void serial_read_loop();
    void parse_and_publish_json(const std::string& line);
    void publish_power(const json& j);
};

#include "ugv_mowgli_bridge/ugv_bridge.hpp"
#include <iostream>

UGVBridge::UGVBridge() : Node("ugv_hardware_bridge")
{
    power_pub_ = this->create_publisher<mowgli_interfaces::msg::Power>("/power", 10);

    // Open serial
    try {
        serial_port_.setPort(serial_port_name_);
        serial_port_.setBaudrate(baud_rate_);
        serial_port_.open();
        RCLCPP_INFO(this->get_logger(), "✅ Serial port opened: %s @ %d baud", 
                    serial_port_name_.c_str(), baud_rate_);
    } catch (const std::exception& e) {
        RCLCPP_ERROR(this->get_logger(), "❌ Failed to open serial port: %s", e.what());
    }

    // Start reader thread
    read_thread_ = std::make_unique<std::thread>(&UGVBridge::serial_read_loop, this);
}

UGVBridge::~UGVBridge()
{
    running_ = false;
    if (read_thread_ && read_thread_->joinable()) {
        read_thread_->join();
    }
    if (serial_port_.isOpen()) serial_port_.close();
}

void UGVBridge::serial_read_loop()
{
    std::string line;
    while (running_ && rclcpp::ok()) {
        try {
            if (serial_port_.isOpen() && serial_port_.available() > 0) {
                line = serial_port_.readline(1024, "\n");
                if (!line.empty()) {
                    parse_and_publish_json(line);
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } catch (...) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void UGVBridge::parse_and_publish_json(const std::string& line)
{
    try {
        json j = json::parse(line);
        if (j.contains("T") && j["T"] == 110) {
            publish_power(j);
        }
    } catch (...) {
        // Ignore bad JSON lines
    }
}

void UGVBridge::publish_power(const json& j)
{
    auto msg = mowgli_interfaces::msg::Power();
    msg.voltage = j.value("V", 0.0f);
    msg.current = j.value("C", 0.0f);
    msg.battery_level = (msg.voltage > 10.5f) ? 
                        std::min(100.0f, (msg.voltage - 10.5f) / 0.03f) : 0.0f;

    power_pub_->publish(msg);

    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
        "Battery: %.2fV  %.2fA  Level: %.0f%%", 
        msg.voltage, msg.current, msg.battery_level);
}

#include "ugv_mowgli_bridge/ugv_bridge.hpp"

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<UGVBridge>();
    
    RCLCPP_INFO(node->get_logger(), "🚀 UGV02 Mowgli Battery Bridge Started");
    
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}