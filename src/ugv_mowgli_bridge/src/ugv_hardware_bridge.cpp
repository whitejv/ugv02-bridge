#include "ugv_mowgli_bridge/ugv_bridge.hpp"

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<UGVBridge>();

    RCLCPP_INFO(node->get_logger(), "UGV02 Mowgli Hardware Bridge Started");

    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
