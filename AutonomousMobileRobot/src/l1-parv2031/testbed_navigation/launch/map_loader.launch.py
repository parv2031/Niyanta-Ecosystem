# Import LaunchDescription which represents the entire launch configuration.
# Every ROS2 launch file must return a LaunchDescription object containing
# all the nodes/actions that should be started.
from launch import LaunchDescription

# Import Node action which allows launching ROS2 nodes from a launch file.
from launch_ros.actions import Node

# Utility function used to locate the installed path of a ROS2 package.
# It returns the directory path of the package from the workspace install space.
from ament_index_python.packages import get_package_share_directory

# Python library used to build file paths in a platform-independent way.
import os


# Main function required by ROS2 launch system.
# This function must return a LaunchDescription object.
def generate_launch_description():

    # ---------------------------------------------------------
    # Locate the bringup package
    # ---------------------------------------------------------

    # Get the directory path of the 'testbed_bringup' package.
    # This package contains the simulation assets such as maps.
    bringup_dir = get_package_share_directory('testbed_bringup')


    # ---------------------------------------------------------
    # Construct path to the map file
    # ---------------------------------------------------------

    # Build the absolute path to the map YAML file.
    # The YAML file contains metadata describing the map:
    # - resolution
    # - origin
    # - path to the .pgm image
    map_file = os.path.join(bringup_dir, 'maps', 'testbed_world.yaml')


    # ---------------------------------------------------------
    # Map Server Node
    # ---------------------------------------------------------

    # Launch the Nav2 map server node.
    # The map server loads the occupancy grid map and publishes it on the /map topic.
    # Other navigation components (AMCL, planners, costmaps) subscribe to this map.
    map_server = Node(
        package='nav2_map_server',   # Package providing the map server
        executable='map_server',     # Executable that loads and publishes the map
        name='map_server',           # Node name visible in ROS graph
        output='screen',             # Print node logs to terminal
        parameters=[{
            'yaml_filename': map_file   # Path to the map configuration file
        }]
    )


    # ---------------------------------------------------------
    # Lifecycle Manager Node
    # ---------------------------------------------------------

    # Nav2 nodes are lifecycle nodes, meaning they go through states such as:
    #
    # unconfigured → inactive → active
    #
    # The lifecycle manager automatically transitions nodes through these states.
    # Without this manager, the map_server would remain inactive.
    lifecycle_manager = Node(
        package='nav2_lifecycle_manager',    # Package responsible for lifecycle management
        executable='lifecycle_manager',      # Lifecycle manager executable
        name='lifecycle_manager_map',        # Name of lifecycle manager node
        output='screen',                     # Print logs to terminal
        parameters=[{
            'use_sim_time': True,            # Use simulation time when running Gazebo
            'autostart': True,               # Automatically configure and activate nodes
            'node_names': ['map_server']     # List of lifecycle nodes this manager controls
        }]
    )


    # ---------------------------------------------------------
    # Launch Description
    # ---------------------------------------------------------

    # Return the LaunchDescription containing the nodes to launch.
    # This launch file starts:
    #
    # 1. Map server (loads and publishes the map)
    # 2. Lifecycle manager (activates the map server automatically)
    return LaunchDescription([
        map_server,
        lifecycle_manager
    ])