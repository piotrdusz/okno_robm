#include <memory>

#include "rclcpp/rclcpp.hpp"

#include "okno_trajectory_tracker/robot_controller.hpp"

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<okno_trajectory_tracker::RobotController>());
  rclcpp::shutdown();
  return 0;
}