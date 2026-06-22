FROM osrf/ros:humble-desktop

ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies including xvfb for headless Gazebo support
RUN apt-get update && apt-get install -y \
    python3-pip \
    ros-humble-navigation2 \
    ros-humble-nav2-bringup \
    ros-humble-turtlebot3* \
    ros-humble-gazebo-ros-pkgs \
    xvfb \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy workspace
COPY . /app/

# Build the workspace
RUN /bin/bash -c "source /opt/ros/humble/setup.bash && colcon build"

# We use xvfb-run to fake a display, allowing sim_navigation.launch.py to run cleanly
# even if it tries to spawn Gazebo GUI or RViz inside the Docker container.
RUN echo '#!/bin/bash\n\
source /opt/ros/humble/setup.bash\n\
source /app/install/setup.bash\n\
# Run simulation environment in background\n\
xvfb-run -a ros2 launch testbed_navigation full_navigation.launch.py &\n\
sleep 15\n\
# Run the Takshak bridge\n\
python3 /app/src/l1-parv2031/testbed_navigation/src/takshak_bridge.py\n\
' > /app/start.sh && chmod +x /app/start.sh

CMD ["/app/start.sh"]
