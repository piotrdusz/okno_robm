#ifndef OKNO_TRAJECTORY_TRACKER__ROBOT_CONTROLLER_HPP_
#define OKNO_TRAJECTORY_TRACKER__ROBOT_CONTROLLER_HPP_

#include <memory>

#include "geometry_msgs/msg/pose_stamped.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "nav_msgs/msg/path.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"

namespace okno_trajectory_tracker
{

class RobotController : public rclcpp::Node
{
public:
  RobotController();

private:
  struct Pose2D
  {
    double x{0.0};
    double y{0.0};
    double theta{0.0};
  };

  struct TrajectorySample
  {
    Pose2D pose;
    geometry_msgs::msg::Twist velocity;
  };

  enum class ControllerState
  {
    Ready,
    PathTracking,
    GoalPositionApproaching,
    Finished
  };

  void odometryCallback(const nav_msgs::msg::Odometry::SharedPtr message);
  void goalPoseCallback(const geometry_msgs::msg::PoseStamped::SharedPtr message);
  void laserScanCallback(const sensor_msgs::msg::LaserScan::SharedPtr message);
  void controlLoop();

  Pose2D poseFromMessage(const geometry_msgs::msg::Pose & pose) const;
  TrajectorySample trajectoryAt(double progress) const;
  void publishReferencePath();
  void publishTargetPose(const Pose2D & pose);
  void publishStopCommand();

  geometry_msgs::msg::Twist pathTrackingControl(
    const Pose2D & current_pose,
    const Pose2D & target_pose,
    const geometry_msgs::msg::Twist & target_velocity) const;
  geometry_msgs::msg::Twist goalPositionApproachingControl(const Pose2D & current_pose) const;
  geometry_msgs::msg::Twist collisionAvoidance(
    const geometry_msgs::msg::Twist & command) const;

  bool goalReached(const Pose2D & current_pose) const;
  static double shortestAngularDistance(double from, double to);
  static double sign(double value);

  ControllerState state_{ControllerState::Ready};
  Pose2D current_pose_;
  Pose2D path_start_pose_;
  Pose2D goal_pose_;
  bool has_odometry_{false};

  sensor_msgs::msg::LaserScan::SharedPtr latest_scan_;
  rclcpp::Time path_start_time_;
  rclcpp::Time goal_approach_start_time_;
  double path_duration_{0.0};

  const double control_period_{0.1};
  const double path_velocity_{0.25};
  const double pose_tolerance_xy_{0.08};
  const double pose_tolerance_theta_{5.0 * 3.14159265358979323846 / 180.0};
  const double k1_{0.0};
  const double k2_{0.0};
  const double k3_{0.0};
  const double lateral_motion_gain_{0.25};
  const double lateral_motion_period_{2.0};

  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odometry_subscription_;
  rclcpp::Subscription<geometry_msgs::msg::PoseStamped>::SharedPtr goal_pose_subscription_;
  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr laser_scan_subscription_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr command_publisher_;
  rclcpp::Publisher<nav_msgs::msg::Path>::SharedPtr path_publisher_;
  rclcpp::Publisher<geometry_msgs::msg::PoseStamped>::SharedPtr target_pose_publisher_;
  rclcpp::TimerBase::SharedPtr control_timer_;
};

}  // namespace okno_trajectory_tracker

#endif  // OKNO_TRAJECTORY_TRACKER__ROBOT_CONTROLLER_HPP_