cmake_minimum_required(VERSION 3.8)
project(okno_trajectory_tracker)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

find_package(ament_cmake REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)

add_library(robot_controller src/robot_controller.cpp)
target_include_directories(robot_controller PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
ament_target_dependencies(robot_controller
  geometry_msgs
  nav_msgs
  rclcpp
  sensor_msgs
  tf2
  tf2_geometry_msgs)

add_executable(trajectory_tracker_node src/trajectory_tracker_node.cpp)
target_link_libraries(trajectory_tracker_node robot_controller)
ament_target_dependencies(trajectory_tracker_node rclcpp)

install(TARGETS
  robot_controller
  trajectory_tracker_node
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY include/
  DESTINATION include)

install(DIRECTORY launch
  DESTINATION share/${PROJECT_NAME})

ament_package()