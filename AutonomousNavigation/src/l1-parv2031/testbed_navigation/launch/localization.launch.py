# Import LaunchDescription which represents the entire launch configuration.
# Every ROS2 launch file must return a LaunchDescription object.
from launch import LaunchDescription

# Import Node action which allows launching ROS2 nodes from a launch file.
from launch_ros.actions import Node

# Utility function used to locate installed ROS2 packages.
# It returns the absolute path of the package inside the workspace install directory.
from ament_index_python.packages import get_package_share_directory

# Python library used for constructing file paths.
# It ensures correct path formatting across different operating systems.
import os


# Main function required by the ROS2 launch system.
# This function returns a LaunchDescription object containing the nodes to start.
def generate_launch_description():

    # ---------------------------------------------------------
    # Locate the package directory
    # ---------------------------------------------------------

    # Get the path to the 'testbed_navigation' package.
    # This allows us to locate configuration files stored inside the package.
    pkg_dir = get_package_share_directory('testbed_navigation')

    # Construct the full path to the AMCL parameter file.
    # This YAML file contains localization parameters such as:
    # - particle counts
    # - frame IDs
    # - scan topic
    amcl_params = os.path.join(pkg_dir, 'config', 'amcl_params.yaml')


    # ---------------------------------------------------------
    # AMCL Node
    # ---------------------------------------------------------

    # Launch the Adaptive Monte Carlo Localization (AMCL) node.
    # AMCL performs probabilistic localization using a particle filter.
    # It estimates the robot's pose relative to the map using:
    # - laser scan data
    # - odometry information
    amcl_node = Node(
        package='nav2_amcl',       # Package containing the AMCL localization node
        executable='amcl',         # Executable responsible for localization
        name='amcl',               # Node name visible in ROS graph
        output='screen',           # Prints node logs directly to terminal
        parameters=[amcl_params, {'use_sim_time': True}],
        # Parameters include:
        # 1. YAML configuration file containing AMCL parameters
        # 2. Simulation time parameter required when running Gazebo
    )


    # ---------------------------------------------------------
    # Lifecycle Manager Node
    # ---------------------------------------------------------

    # Nav2 nodes use ROS2 Lifecycle Nodes.
    # Lifecycle nodes go through states such as:
    #
    # unconfigured → inactive → active
    #
    # The lifecycle manager automatically transitions nodes
    # through these states so they become operational.
    lifecycle_manager = Node(
        package='nav2_lifecycle_manager',    # Package responsible for lifecycle management
        executable='lifecycle_manager',      # Lifecycle manager executable
        name='lifecycle_manager_localization',  # Name of lifecycle manager node
        output='screen',                     # Print logs to terminal
        parameters=[
            {'use_sim_time': True},          # Use simulation time from Gazebo
            {'autostart': True},             # Automatically configure and activate nodes
            {'node_names': ['amcl']}         # List of lifecycle nodes to manage
        ]
    )


    # ---------------------------------------------------------
    # Launch Description
    # ---------------------------------------------------------

    # Return the LaunchDescription object containing the nodes
    # that should be started when this launch file runs.
    #
    # This launch file starts:
    # 1. AMCL localization node
    # 2. Lifecycle manager to automatically activate AMCL
    return LaunchDescription([
        amcl_node,
        lifecycle_manager
    ])