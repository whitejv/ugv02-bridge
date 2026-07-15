#include "ugv_mowgli_bridge/ugv_bridge.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>

UGVBridge::UGVBridge() : Node("ugv_hardware_bridge")
{
    // Pi 4 with uart1..5 overlays: GPIO14/15 (header pins 8/10) are /dev/ttyS0.
    this->declare_parameter<std::string>("serial_port", "/dev/ttyS0");
    this->declare_parameter<int>("baud_rate", 115200);
    serial_port_name_ = this->get_parameter("serial_port").as_string();
    baud_rate_ = this->get_parameter("baud_rate").as_int();

    // Match stock hardware_bridge remapping: BT/diagnostics subscribe here.
    power_pub_ = this->create_publisher<mowgli_interfaces::msg::Power>(
        "/hardware_bridge/power", 10);

    // Open serial
    try {
        serial_port_.setPort(serial_port_name_);
        serial_port_.setBaudrate(baud_rate_);
        // Wait up to 100ms for line endings; without this, readline() can
        // return partial frames and JSON parse fails silently.
        serial::Timeout timeout = serial::Timeout::simpleTimeout(100);
        serial_port_.setTimeout(timeout);
        serial_port_.open();
        RCLCPP_INFO(this->get_logger(), "Serial port opened: %s @ %d baud",
                    serial_port_name_.c_str(), baud_rate_);

        // Waveshare ESP32: enable continuous base feedback (T:1001 stream).
        // Default firmware often has this on; send explicitly for ROS bring-up.
        serial_port_.write("{\"T\":142,\"cmd\":50}\n");
        serial_port_.write("{\"T\":131,\"cmd\":1}\n");
        serial_port_.write("{\"T\":143,\"cmd\":0}\n");
    } catch (const std::exception& e) {
        RCLCPP_ERROR(this->get_logger(), "Failed to open serial port: %s", e.what());
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
    std::string pending;
    while (running_ && rclcpp::ok()) {
        try {
            if (serial_port_.isOpen() && serial_port_.available() > 0) {
                line = serial_port_.readline(1024, "\n");
                if (line.empty()) {
                    continue;
                }
                pending += line;
                // Assemble complete newline-terminated frames (handles partials).
                std::size_t pos;
                while ((pos = pending.find('\n')) != std::string::npos) {
                    std::string frame = pending.substr(0, pos + 1);
                    pending.erase(0, pos + 1);
                    parse_and_publish_json(frame);
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
        // ESP32 lines are often "\r\n"; strip trailing whitespace before parse.
        std::string trimmed = line;
        while (!trimmed.empty() &&
               (trimmed.back() == '\r' || trimmed.back() == '\n' ||
                trimmed.back() == ' ' || trimmed.back() == '\t')) {
            trimmed.pop_back();
        }
        // Drop leading garbage before the JSON object (mid-stream joins).
        const auto brace = trimmed.find('{');
        if (brace == std::string::npos) {
            return;
        }
        if (brace > 0) {
            trimmed = trimmed.substr(brace);
        }
        if (trimmed.empty()) {
            return;
        }

        json j = json::parse(trimmed);
        // Waveshare ugv_base_ros: FEEDBACK_BASE_INFO == 1001 (not T:110).
        if (!j.contains("T")) {
            return;
        }
        const int type = j["T"].is_number() ? j["T"].get<int>() : -1;
        if (type == 1001) {
            publish_power(j);
        }
    } catch (...) {
        // Ignore bad / partial JSON lines
    }
}

void UGVBridge::publish_power(const json& j)
{
    auto msg = mowgli_interfaces::msg::Power();
    msg.stamp = this->get_clock()->now();

    // Firmware publishes "v" as centivolts (int), e.g. 1203 -> 12.03 V.
    const float v_raw = j.value("v", 0.0f);
    msg.v_battery = v_raw / 100.0f;
    msg.v_charge = 0.0f;
    msg.charge_current = 0.0f;
    msg.charger_enabled = false;
    msg.charger_status = "";

    // Rough 3S Li-ion estimate for logging only (not part of Power.msg).
    // Empty ~9.0 V, full ~12.6 V.
    const float battery_level = (msg.v_battery > 9.0f) ?
        std::min(100.0f, (msg.v_battery - 9.0f) / 0.036f) : 0.0f;

    power_pub_->publish(msg);

    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
        "Battery: %.2fV  Level: %.0f%%",
        msg.v_battery, battery_level);
}
