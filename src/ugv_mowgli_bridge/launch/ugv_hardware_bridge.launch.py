from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os


def generate_launch_description():
    params_file = os.path.join(
        get_package_share_directory('ugv_mowgli_bridge'),
        'config',
        'params.yaml',
    )

    return LaunchDescription([
        Node(
            package='ugv_mowgli_bridge',
            executable='ugv_hardware_bridge',
            name='ugv_hardware_bridge',
            output='screen',
            parameters=[params_file],
        ),
    ])
