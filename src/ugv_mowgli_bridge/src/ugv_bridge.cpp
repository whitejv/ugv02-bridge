#include "ugv_mowgli_bridge/ugv_bridge.hpp"

#include <algorithm>
#include <chrono>
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
