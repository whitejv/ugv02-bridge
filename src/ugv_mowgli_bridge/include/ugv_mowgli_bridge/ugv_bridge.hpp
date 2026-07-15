#pragma once

#include <geometry_msgs/msg/twist_stamped.hpp>
#include <mowgli_interfaces/msg/emergency.hpp>
#include <mowgli_interfaces/msg/power.hpp>
#include <mowgli_interfaces/msg/status.hpp>
#include <mowgli_interfaces/srv/emergency_stop.hpp>
#include <mowgli_interfaces/srv/mower_control.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/battery_state.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <serial/serial.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

using json = nlohmann::json;

class UGVBridge : public rclcpp::Node
{
public:
  UGVBridge();
  ~UGVBridge();

private:
  // Publishers (Mowgli hardware contract)
  rclcpp::Publisher<mowgli_interfaces::msg::Power>::SharedPtr power_pub_;
  rclcpp::Publisher<mowgli_interfaces::msg::Status>::SharedPtr status_pub_;
  rclcpp::Publisher<mowgli_interfaces::msg::Emergency>::SharedPtr emergency_pub_;
  rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr wheel_odom_pub_;
  rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr imu_pub_;
  rclcpp::Publisher<sensor_msgs::msg::BatteryState>::SharedPtr battery_state_pub_;

  // Services
  rclcpp::Service<mowgli_interfaces::srv::MowerControl>::SharedPtr mower_control_srv_;
  rclcpp::Service<mowgli_interfaces::srv::EmergencyStop>::SharedPtr emergency_stop_srv_;

  // Subscriptions
  rclcpp::Subscription<geometry_msgs::msg::TwistStamped>::SharedPtr cmd_vel_sub_;

  // Timers
  rclcpp::TimerBase::SharedPtr cmd_repeat_timer_;
  // Keep /hardware_bridge/emergency fresh — BT IsEmergency treats >2s silence as e-stop.
  rclcpp::TimerBase::SharedPtr emergency_heartbeat_timer_;

  // Serial
  serial::Serial serial_port_;
  std::string serial_port_name_;
  int baud_rate_;
  double wheel_track_;
  double accel_lsb_per_g_;
  double gyro_lsb_per_dps_;
  std::mutex serial_mutex_;

  // Reader thread
  std::unique_ptr<std::thread> read_thread_;
  std::atomic<bool> running_{true};

  // State
  std::atomic<bool> mow_enabled_{false};
  std::atomic<bool> emergency_active_{false};
  std::mutex cmd_mutex_;
  double last_cmd_vx_{0.0};
  double last_cmd_wz_{0.0};
  rclcpp::Time last_cmd_time_;

  void serial_read_loop();
  void parse_and_publish_json(const std::string& line);
  void handle_base_feedback(const json& j);

  void publish_power(const json& j, const rclcpp::Time& stamp);
  void publish_status(const json& j, const rclcpp::Time& stamp);
  void publish_emergency(const rclcpp::Time& stamp);
  void publish_wheel_odom(const json& j, const rclcpp::Time& stamp);
  void publish_imu(const json& j, const rclcpp::Time& stamp);
  void publish_battery_state(const json& j, const rclcpp::Time& stamp);

  void on_cmd_vel(const geometry_msgs::msg::TwistStamped::SharedPtr msg);
  void on_cmd_repeat();
  void on_emergency_heartbeat();
  void send_json(const json& j);
  void send_motion(double vx, double wz);
  void send_emergency_stop_cmd();
};
