# Import LaunchDescription which represents the entire launch configuration.
# In ROS2, every launch file must return a LaunchDescription object.
from launch import LaunchDescription

# IncludeLaunchDescription allows one launch file to include and execute another launch file.
# This is useful for modular systems where different subsystems have their own launch files.
from launch.actions import IncludeLaunchDescription

# PythonLaunchDescriptionSource tells ROS2 that the launch file we are including
# is written in Python (which is the standard format for ROS2 launch files).
from launch.launch_description_sources import PythonLaunchDescriptionSource

# Utility function used to locate installed ROS2 packages.
# It returns the path to the package inside the ROS2 workspace install directory.
from ament_index_python.packages import get_package_share_directory

# Standard Python library used for constructing file paths in a platform-independent way.
import os


# Main function required by ROS2 launch system.
# This function must return a LaunchDescription object containing all nodes/actions to start.
def generate_launch_description():

    # ---------------------------------------------------------
    # Locate the directories of required ROS2 packages
    # ---------------------------------------------------------

    # Get the absolute path to the testbed_bringup package.
    # This package contains the Gazebo simulation launch file for the robot.
    bringup_pkg = get_package_share_directory('testbed_bringup')

    # Get the absolute path to the testbed_navigation package.
    # This package contains our custom navigation launch files.
    navigation_pkg = get_package_share_directory('testbed_navigation')


    # ---------------------------------------------------------
    # Construct full paths to the launch files we want to include
    # ---------------------------------------------------------

    # Path to the main simulation launch file.
    # This launches Gazebo, loads the robot model, and starts RViz.
    bringup_launch = os.path.join(
        bringup_pkg,
        'launch',
        'testbed_full_bringup.launch.py'
    )

    # Path to the map loader launch file.
    # This launch file starts the nav2_map_server node to publish the map.
    map_loader_launch = os.path.join(
        navigation_pkg,
        'launch',
        'map_loader.launch.py'
    )

    # Path to the localization launch file.
    # This launch file starts the AMCL localization node.
    localization_launch = os.path.join(
        navigation_pkg,
        'launch',
        'localization.launch.py'
    )

    # Path to the navigation launch file.
    # This launch file starts the navigation stack components:
    # planner_server, controller_server, bt_navigator, behavior_server.
    navigation_launch = os.path.join(
        navigation_pkg,
        'launch',
        'navigation.launch.py'
    )


    # ---------------------------------------------------------
    # Include each launch file as an action
    # ---------------------------------------------------------

    # Include the simulation bringup launch file.
    # This starts the robot simulation environment in Gazebo.
    bringup = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(bringup_launch)
    )

    # Include the map loader launch file.
    # This loads the static map using nav2_map_server.
    map_loader = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(map_loader_launch)
    )

    # Include the localization launch file.
    # This starts the AMCL node to estimate the robot's pose on the map.
    localization = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(localization_launch)
    )

    # Include the navigation launch file.
    # This launches the main Nav2 navigation stack responsible for planning and control.
    navigation = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(navigation_launch)
    )


    # ---------------------------------------------------------
    # Return LaunchDescription with all included launch files
    # ---------------------------------------------------------

    # The LaunchDescription object contains all actions that should be executed.
    # Here we start the full navigation pipeline by launching all subsystems:
    #
    # 1. Simulation (Gazebo + Robot + RViz)
    # 2. Map Server
    # 3. Localization (AMCL)
    # 4. Navigation Stack (Planner + Controller + Behavior Tree)
    #
    # This allows the entire system to be started with a single command.
    return LaunchDescription([
        bringup,
        map_loader,
        localization,
        navigation
    ])