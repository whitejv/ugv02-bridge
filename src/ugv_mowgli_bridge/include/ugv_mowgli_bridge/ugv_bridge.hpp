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
