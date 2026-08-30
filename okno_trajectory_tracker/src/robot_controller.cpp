#include "okno_trajectory_tracker/robot_controller.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

#include "tf2/utils.h"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"

namespace okno_trajectory_tracker
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr char kOdomFrame[] = "odom";
}  // namespace

RobotController::RobotController()
: Node("trajectory_tracker")
{
  odometry_subscription_ = create_subscription<nav_msgs::msg::Odometry>(
    "/odom", rclcpp::SensorDataQoS(),
    std::bind(&RobotController::odometryCallback, this, std::placeholders::_1));
  goal_pose_subscription_ = create_subscription<geometry_msgs::msg::PoseStamped>(
    "/goal_pose", 10,
    std::bind(&RobotController::goalPoseCallback, this, std::placeholders::_1));
  laser_scan_subscription_ = create_subscription<sensor_msgs::msg::LaserScan>(
    "/scan", rclcpp::SensorDataQoS(),
    std::bind(&RobotController::laserScanCallback, this, std::placeholders::_1));

  command_publisher_ = create_publisher<geometry_msgs::msg::Twist>("/cmd_vel", 10);
  path_publisher_ = create_publisher<nav_msgs::msg::Path>("/reference_path", 10);
  target_pose_publisher_ = create_publisher<geometry_msgs::msg::PoseStamped>("/target_pose", 10);

  control_timer_ = create_wall_timer(
    std::chrono::duration<double>(control_period_),
    std::bind(&RobotController::controlLoop, this));

  RCLCPP_INFO(get_logger(), "Trajectory tracker is ready. Waiting for /goal_pose.");
}

void RobotController::odometryCallback(const nav_msgs::msg::Odometry::SharedPtr message)
{
  current_pose_ = poseFromMessage(message->pose.pose);
  has_odometry_ = true;
}

void RobotController::goalPoseCallback(const geometry_msgs::msg::PoseStamped::SharedPtr message)
{
  if (!has_odometry_) {
    RCLCPP_WARN(get_logger(), "Ignoring goal: no odometry received yet.");
    return;
  }

  if (!message->header.frame_id.empty() && message->header.frame_id != kOdomFrame) {
    RCLCPP_WARN(get_logger(), "Ignoring goal outside the odom frame.");
    return;
  }

  path_start_pose_ = current_pose_;
  goal_pose_ = poseFromMessage(message->pose);
  const double distance = std::hypot(goal_pose_.x - path_start_pose_.x, goal_pose_.y - path_start_pose_.y);
  path_duration_ = std::max(4.0, distance / path_velocity_);
  path_start_time_ = now();
  state_ = ControllerState::PathTracking;
  publishReferencePath();

  RCLCPP_INFO(
    get_logger(), "New goal: x=%.2f, y=%.2f, theta=%.2f rad.",
    goal_pose_.x, goal_pose_.y, goal_pose_.theta);
}

void RobotController::laserScanCallback(const sensor_msgs::msg::LaserScan::SharedPtr message)
{
  latest_scan_ = message;
}

void RobotController::controlLoop()
{
  if (!has_odometry_) {
    return;
  }

  geometry_msgs::msg::Twist command;

  if (state_ == ControllerState::PathTracking) {
    const double elapsed = (now() - path_start_time_).seconds();
    if (elapsed >= path_duration_) {
      state_ = ControllerState::GoalPositionApproaching;
      goal_approach_start_time_ = now();
      RCLCPP_INFO(get_logger(), "Trajectory complete. Approaching final pose.");
    } else {
      const TrajectorySample target = trajectoryAt(elapsed / path_duration_);
      command = pathTrackingControl(current_pose_, target.pose, target.velocity);
      publishTargetPose(target.pose);
    }
  }

  if (state_ == ControllerState::GoalPositionApproaching) {
    command = goalPositionApproachingControl(current_pose_);
    publishTargetPose(goal_pose_);

    if (goalReached(current_pose_)) {
      state_ = ControllerState::Finished;
      command = geometry_msgs::msg::Twist{};
      RCLCPP_INFO(get_logger(), "Goal reached.");
    }
  }

  if (state_ == ControllerState::Ready || state_ == ControllerState::Finished) {
    command = geometry_msgs::msg::Twist{};
  }

  command_publisher_->publish(collisionAvoidance(command));
}

RobotController::Pose2D RobotController::poseFromMessage(const geometry_msgs::msg::Pose & pose) const
{
  Pose2D result;
  result.x = pose.position.x;
  result.y = pose.position.y;
  result.theta = tf2::getYaw(pose.orientation);
  return result;
}

RobotController::TrajectorySample RobotController::trajectoryAt(double progress) const
{
  const double t = std::clamp(progress, 0.0, 1.0);
  const double dx = goal_pose_.x - path_start_pose_.x;
  const double dy = goal_pose_.y - path_start_pose_.y;
  const double distance = std::hypot(dx, dy);
  const double bend = std::min(0.5, 0.2 * distance);

  const double normal_x = distance > 1e-6 ? -dy / distance : 0.0;
  const double normal_y = distance > 1e-6 ? dx / distance : 0.0;
  const double lateral_offset = bend * std::sin(kPi * t);

  const double derivative_x = dx + normal_x * bend * kPi * std::cos(kPi * t);
  const double derivative_y = dy + normal_y * bend * kPi * std::cos(kPi * t);

  TrajectorySample sample;
  sample.pose.x = path_start_pose_.x + t * dx + normal_x * lateral_offset;
  sample.pose.y = path_start_pose_.y + t * dy + normal_y * lateral_offset;
  sample.pose.theta = std::atan2(derivative_y, derivative_x);

  const double next_t = std::min(1.0, t + control_period_ / path_duration_);
  const double next_derivative_x = dx + normal_x * bend * kPi * std::cos(kPi * next_t);
  const double next_derivative_y = dy + normal_y * bend * kPi * std::cos(kPi * next_t);
  const double next_theta = std::atan2(next_derivative_y, next_derivative_x);

  sample.velocity.linear.x = std::hypot(derivative_x, derivative_y) / path_duration_;
  sample.velocity.angular.z = shortestAngularDistance(sample.pose.theta, next_theta) / control_period_;
  return sample;
}

void RobotController::publishReferencePath()
{
  nav_msgs::msg::Path path;
  path.header.frame_id = kOdomFrame;
  path.header.stamp = now();

  constexpr int kSamples = 100;
  for (int sample_index = 0; sample_index <= kSamples; ++sample_index) {
    const TrajectorySample sample = trajectoryAt(static_cast<double>(sample_index) / kSamples);
    geometry_msgs::msg::PoseStamped pose;
    pose.header = path.header;
    pose.pose.position.x = sample.pose.x;
    pose.pose.position.y = sample.pose.y;
    pose.pose.orientation.z = std::sin(sample.pose.theta / 2.0);
    pose.pose.orientation.w = std::cos(sample.pose.theta / 2.0);
    path.poses.push_back(pose);
  }

  path_publisher_->publish(path);
}

void RobotController::publishTargetPose(const Pose2D & pose)
{
  geometry_msgs::msg::PoseStamped message;
  message.header.frame_id = kOdomFrame;
  message.header.stamp = now();
  message.pose.position.x = pose.x;
  message.pose.position.y = pose.y;
  message.pose.orientation.z = std::sin(pose.theta / 2.0);
  message.pose.orientation.w = std::cos(pose.theta / 2.0);
  target_pose_publisher_->publish(message);
}

geometry_msgs::msg::Twist RobotController::pathTrackingControl(
  const Pose2D & current_pose,
  const Pose2D & target_pose,
  const geometry_msgs::msg::Twist & target_velocity) const
{
  geometry_msgs::msg::Twist command;

  // TODO 1: Calculate the pose errors in the robot coordinate frame.
  const double error_x_robot = 0.0;
  const double error_y_robot = 0.0;
  const double error_theta = 0.0;

  // TODO 2: Implement the Samson trajectory-tracking controller.
  command.linear.x = target_velocity.linear.x + k1_ * error_x_robot;
  command.angular.z = target_velocity.angular.z +
    k2_ * sign(target_velocity.linear.x) * error_y_robot + k3_ * error_theta;

  return command;
}

geometry_msgs::msg::Twist RobotController::goalPositionApproachingControl(
  const Pose2D & current_pose) const
{
  geometry_msgs::msg::Twist target_velocity;

  // TODO 3: Add an oscillating linear reference velocity based on the lateral error.
  target_velocity.linear.x = 0.0;

  return pathTrackingControl(current_pose, goal_pose_, target_velocity);
}

geometry_msgs::msg::Twist RobotController::collisionAvoidance(
  const geometry_msgs::msg::Twist & command) const
{
  geometry_msgs::msg::Twist safe_command = command;

  // TODO 4 (additional): Use latest_scan_ to stop and turn away from an obstacle ahead.

  return safe_command;
}

bool RobotController::goalReached(const Pose2D & current_pose) const
{
  const double position_error = std::hypot(
    goal_pose_.x - current_pose.x,
    goal_pose_.y - current_pose.y);
  const double orientation_error = std::abs(
    shortestAngularDistance(current_pose.theta, goal_pose_.theta));
  return position_error < pose_tolerance_xy_ && orientation_error < pose_tolerance_theta_;
}

double RobotController::shortestAngularDistance(double from, double to)
{
  return std::atan2(std::sin(to - from), std::cos(to - from));
}

double RobotController::sign(double value)
{
  return (value > 0.0) - (value < 0.0);
}

}  // namespace okno_trajectory_tracker