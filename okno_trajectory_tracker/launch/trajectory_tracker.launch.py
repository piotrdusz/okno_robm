from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        Node(
            package="okno_trajectory_tracker",
            executable="trajectory_tracker_node",
            name="trajectory_tracker",
            output="screen",
            parameters=[{"use_sim_time": True}],
        ),
    ])