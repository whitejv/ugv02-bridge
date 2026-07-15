#include "ugv_mowgli_bridge/ugv_bridge.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

namespace
{
constexpr int kFeedbackBaseInfo = 1001;
constexpr double kGravity = 9.80665;
constexpr double kDegToRad = M_PI / 180.0;
}  // namespace

UGVBridge::UGVBridge() : Node("ugv_hardware_bridge")
{
  // Pi 4 with uart1..5 overlays: GPIO14/15 (header pins 8/10) are /dev/ttyS0.
  declare_parameter<std::string>("serial_port", "/dev/ttyS0");
  declare_parameter<int>("baud_rate", 115200);
  declare_parameter<double>("wheel_track", 0.22);
  // ICM-20948 typical FS settings used by Waveshare firmware raw fields.
  declare_parameter<double>("accel_lsb_per_g", 16384.0);
  declare_parameter<double>("gyro_lsb_per_dps", 131.0);

  serial_port_name_ = get_parameter("serial_port").as_string();
  baud_rate_ = get_parameter("baud_rate").as_int();
  wheel_track_ = get_parameter("wheel_track").as_double();
  accel_lsb_per_g_ = get_parameter("accel_lsb_per_g").as_double();
  gyro_lsb_per_dps_ = get_parameter("gyro_lsb_per_dps").as_double();
  last_cmd_time_ = now();

  power_pub_ = create_publisher<mowgli_interfaces::msg::Power>("/hardware_bridge/power", 10);
  status_pub_ = create_publisher<mowgli_interfaces::msg::Status>("/hardware_bridge/status", 10);
  emergency_pub_ =
      create_publisher<mowgli_interfaces::msg::Emergency>("/hardware_bridge/emergency", 10);
  wheel_odom_pub_ = create_publisher<nav_msgs::msg::Odometry>("/wheel_odom", 10);
  imu_pub_ = create_publisher<sensor_msgs::msg::Imu>("/imu/data", 10);
  battery_state_pub_ = create_publisher<sensor_msgs::msg::BatteryState>("/battery_state", 10);

  mower_control_srv_ = create_service<mowgli_interfaces::srv::MowerControl>(
      "/hardware_bridge/mower_control",
      [this](const std::shared_ptr<mowgli_interfaces::srv::MowerControl::Request> req,
             std::shared_ptr<mowgli_interfaces::srv::MowerControl::Response> res) {
        mow_enabled_ = (req->mow_enabled != 0);
        res->success = true;
        RCLCPP_INFO(get_logger(), "mower_control stub: mow_enabled=%u (no blade on UGV02)",
                    req->mow_enabled);
      });

  emergency_stop_srv_ = create_service<mowgli_interfaces::srv::EmergencyStop>(
      "/hardware_bridge/emergency_stop",
      [this](const std::shared_ptr<mowgli_interfaces::srv::EmergencyStop::Request> req,
             std::shared_ptr<mowgli_interfaces::srv::EmergencyStop::Response> res) {
        if (req->emergency != 0u) {
          emergency_active_ = true;
          send_emergency_stop_cmd();
          {
            std::lock_guard<std::mutex> lock(cmd_mutex_);
            last_cmd_vx_ = 0.0;
            last_cmd_wz_ = 0.0;
          }
          RCLCPP_WARN(get_logger(), "emergency_stop asserted");
        } else {
          emergency_active_ = false;
          RCLCPP_INFO(get_logger(), "emergency_stop released");
        }
        publish_emergency(now());
        res->success = true;
      });

  cmd_vel_sub_ = create_subscription<geometry_msgs::msg::TwistStamped>(
      "/cmd_vel", 10, std::bind(&UGVBridge::on_cmd_vel, this, std::placeholders::_1));

  // Refresh motion cmds so ESP32 3s motion watchdog does not stop mid-run.
  cmd_repeat_timer_ =
      create_wall_timer(std::chrono::milliseconds(500), std::bind(&UGVBridge::on_cmd_repeat, this));

  try {
    serial_port_.setPort(serial_port_name_);
    serial_port_.setBaudrate(baud_rate_);
    serial::Timeout timeout = serial::Timeout::simpleTimeout(100);
    serial_port_.setTimeout(timeout);
    serial_port_.open();
    RCLCPP_INFO(get_logger(), "Serial port opened: %s @ %d baud", serial_port_name_.c_str(),
                baud_rate_);

    send_json({{"T", 142}, {"cmd", 50}});
    send_json({{"T", 131}, {"cmd", 1}});
    send_json({{"T", 143}, {"cmd", 0}});
  } catch (const std::exception& e) {
    RCLCPP_ERROR(get_logger(), "Failed to open serial port: %s", e.what());
  }

  read_thread_ = std::make_unique<std::thread>(&UGVBridge::serial_read_loop, this);
  RCLCPP_INFO(get_logger(), "UGV02 Mowgli bridge ready (power/status/emergency/cmd_vel/odom/imu)");
}

UGVBridge::~UGVBridge()
{
  running_ = false;
  if (read_thread_ && read_thread_->joinable()) {
    read_thread_->join();
  }
  std::lock_guard<std::mutex> lock(serial_mutex_);
  if (serial_port_.isOpen()) {
    serial_port_.close();
  }
}

void UGVBridge::send_json(const json& j)
{
  const std::string line = j.dump() + "\n";
  std::lock_guard<std::mutex> lock(serial_mutex_);
  if (!serial_port_.isOpen()) {
    return;
  }
  try {
    serial_port_.write(line);
  } catch (const std::exception& e) {
    RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 3000, "Serial write failed: %s", e.what());
  }
}

void UGVBridge::send_motion(double vx, double wz)
{
  if (emergency_active_) {
    send_emergency_stop_cmd();
    return;
  }
  // Waveshare CMD_ROS_CTRL: X m/s, Z rad/s
  send_json({{"T", 13}, {"X", vx}, {"Z", wz}});
}

void UGVBridge::send_emergency_stop_cmd()
{
  send_json({{"T", 0}});
}

void UGVBridge::on_cmd_vel(const geometry_msgs::msg::TwistStamped::SharedPtr msg)
{
  const double vx = msg->twist.linear.x;
  const double wz = msg->twist.angular.z;
  {
    std::lock_guard<std::mutex> lock(cmd_mutex_);
    last_cmd_vx_ = vx;
    last_cmd_wz_ = wz;
    last_cmd_time_ = now();
  }
  send_motion(vx, wz);
}

void UGVBridge::on_cmd_repeat()
{
  double vx = 0.0;
  double wz = 0.0;
  rclcpp::Time t;
  {
    std::lock_guard<std::mutex> lock(cmd_mutex_);
    vx = last_cmd_vx_;
    wz = last_cmd_wz_;
    t = last_cmd_time_;
  }
  // Only refresh non-zero motion (keeps ESP32 moving under Nav2/teleop).
  if (std::abs(vx) < 1e-4 && std::abs(wz) < 1e-4) {
    return;
  }
  if ((now() - t).seconds() > 2.5) {
    // Stale command — let ESP32 watchdog stop rather than extending forever.
    return;
  }
  send_motion(vx, wz);
}

void UGVBridge::serial_read_loop()
{
  std::string line;
  std::string pending;
  while (running_ && rclcpp::ok()) {
    try {
      bool readable = false;
      {
        std::lock_guard<std::mutex> lock(serial_mutex_);
        readable = serial_port_.isOpen() && serial_port_.available() > 0;
        if (readable) {
          line = serial_port_.readline(1024, "\n");
        }
      }
      if (!readable) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      if (line.empty()) {
        continue;
      }
      pending += line;
      std::size_t pos;
      while ((pos = pending.find('\n')) != std::string::npos) {
        std::string frame = pending.substr(0, pos + 1);
        pending.erase(0, pos + 1);
        parse_and_publish_json(frame);
      }
    } catch (...) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}

void UGVBridge::parse_and_publish_json(const std::string& line)
{
  try {
    std::string trimmed = line;
    while (!trimmed.empty() &&
           (trimmed.back() == '\r' || trimmed.back() == '\n' || trimmed.back() == ' ' ||
            trimmed.back() == '\t')) {
      trimmed.pop_back();
    }
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
    if (!j.contains("T") || !j["T"].is_number()) {
      return;
    }
    if (j["T"].get<int>() == kFeedbackBaseInfo) {
      handle_base_feedback(j);
    }
  } catch (...) {
    // Ignore bad / partial JSON lines
  }
}

void UGVBridge::handle_base_feedback(const json& j)
{
  const auto stamp = now();
  publish_power(j, stamp);
  publish_status(j, stamp);
  publish_emergency(stamp);
  publish_wheel_odom(j, stamp);
  publish_imu(j, stamp);
  publish_battery_state(j, stamp);
}

void UGVBridge::publish_power(const json& j, const rclcpp::Time& stamp)
{
  auto msg = mowgli_interfaces::msg::Power();
  msg.stamp = stamp;
  const float v_raw = j.value("v", 0.0f);
  msg.v_battery = v_raw / 100.0f;
  msg.v_charge = 0.0f;
  msg.charge_current = 0.0f;
  msg.charger_enabled = false;
  msg.charger_status = "idle";
  power_pub_->publish(msg);

  const float battery_level =
      (msg.v_battery > 9.0f) ? std::min(100.0f, (msg.v_battery - 9.0f) / 0.036f) : 0.0f;
  RCLCPP_INFO_THROTTLE(get_logger(), *get_clock(), 3000, "Battery: %.2fV  Level: %.0f%%",
                       msg.v_battery, battery_level);
}

void UGVBridge::publish_status(const json& /*j*/, const rclcpp::Time& stamp)
{
  auto msg = mowgli_interfaces::msg::Status();
  msg.stamp = stamp;
  msg.mower_status = mowgli_interfaces::msg::Status::MOWER_STATUS_OK;
  msg.reset_cause = mowgli_interfaces::msg::Status::RESET_CAUSE_UNKNOWN;
  msg.reset_cause_name = "UNKNOWN";
  msg.raspberry_pi_power = true;
  msg.is_charging = false;
  msg.esc_power = false;
  msg.rain_detected = false;
  msg.sound_module_available = false;
  msg.sound_module_busy = false;
  msg.ui_board_available = true;
  msg.mow_enabled = mow_enabled_;
  msg.firmware_debug_enabled = false;
  msg.mower_esc_status = 0;
  msg.mower_esc_temperature = 0.0f;
  msg.mower_esc_current = 0.0f;
  msg.mower_motor_temperature = 0.0f;
  msg.mower_motor_rpm = 0.0f;
  msg.firmware_version = "ugv02-esp32";
  msg.firmware_protocol_version = 0;
  // PreFlightCheck requires this true for autonomy.
  msg.firmware_compatible = true;
  status_pub_->publish(msg);
}

void UGVBridge::publish_emergency(const rclcpp::Time& stamp)
{
  auto msg = mowgli_interfaces::msg::Emergency();
  msg.stamp = stamp;
  msg.active_emergency = emergency_active_;
  msg.latched_emergency = emergency_active_;
  msg.lift_warning = false;
  msg.lift_duration_sec = 0.0f;
  msg.reason = emergency_active_ ? "ugv_emergency_stop" : "";
  emergency_pub_->publish(msg);
}

void UGVBridge::publish_wheel_odom(const json& j, const rclcpp::Time& stamp)
{
  // L/R are wheel speeds (m/s) from Waveshare firmware.
  const double v_l = j.value("L", 0.0);
  const double v_r = j.value("R", 0.0);
  double vx = 0.5 * (v_l + v_r);
  double wz = 0.0;
  if (wheel_track_ > 1e-3) {
    wz = (v_r - v_l) / wheel_track_;
  }

  auto msg = nav_msgs::msg::Odometry();
  msg.header.stamp = stamp;
  msg.header.frame_id = "odom";
  msg.child_frame_id = "base_link";
  msg.twist.twist.linear.x = vx;
  msg.twist.twist.angular.z = wz;
  msg.twist.covariance[0] = 0.01;
  msg.twist.covariance[7] = 1e-4;
  msg.twist.covariance[14] = 1e6;
  msg.twist.covariance[21] = 1e6;
  msg.twist.covariance[28] = 1e6;
  msg.twist.covariance[35] = 9e-4;
  wheel_odom_pub_->publish(msg);
}

void UGVBridge::publish_imu(const json& j, const rclcpp::Time& stamp)
{
  auto msg = sensor_msgs::msg::Imu();
  msg.header.stamp = stamp;
  msg.header.frame_id = "imu_link";

  const double ax = j.value("ax", 0.0);
  const double ay = j.value("ay", 0.0);
  const double az = j.value("az", 0.0);
  const double gx = j.value("gx", 0.0);
  const double gy = j.value("gy", 0.0);
  const double gz = j.value("gz", 0.0);

  msg.linear_acceleration.x = (ax / accel_lsb_per_g_) * kGravity;
  msg.linear_acceleration.y = (ay / accel_lsb_per_g_) * kGravity;
  msg.linear_acceleration.z = (az / accel_lsb_per_g_) * kGravity;
  msg.angular_velocity.x = (gx / gyro_lsb_per_dps_) * kDegToRad;
  msg.angular_velocity.y = (gy / gyro_lsb_per_dps_) * kDegToRad;
  msg.angular_velocity.z = (gz / gyro_lsb_per_dps_) * kDegToRad;

  // Orientation unknown from raw feedback alone.
  msg.orientation_covariance[0] = -1.0;
  msg.angular_velocity_covariance[0] = 0.02;
  msg.angular_velocity_covariance[4] = 0.02;
  msg.angular_velocity_covariance[8] = 0.02;
  msg.linear_acceleration_covariance[0] = 0.04;
  msg.linear_acceleration_covariance[4] = 0.04;
  msg.linear_acceleration_covariance[8] = 0.04;
  imu_pub_->publish(msg);
}

void UGVBridge::publish_battery_state(const json& j, const rclcpp::Time& stamp)
{
  const float v_raw = j.value("v", 0.0f);
  const float voltage = v_raw / 100.0f;
  // Rough 3S pack percentage for docking consumers.
  const float pct01 =
      (voltage > 9.0f) ? std::min(1.0f, (voltage - 9.0f) / 3.6f) : 0.0f;

  auto msg = sensor_msgs::msg::BatteryState();
  msg.header.stamp = stamp;
  msg.header.frame_id = "base_link";
  msg.voltage = voltage;
  msg.current = 0.0f;
  msg.percentage = pct01;
  msg.power_supply_status = sensor_msgs::msg::BatteryState::POWER_SUPPLY_STATUS_DISCHARGING;
  msg.present = true;
  battery_state_pub_->publish(msg);
}
