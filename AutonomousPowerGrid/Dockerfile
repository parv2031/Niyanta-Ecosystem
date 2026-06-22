FROM ubuntu:22.04

# Prevent interactive prompts during apt-get
ENV DEBIAN_FRONTEND=noninteractive

# Install python and c++ build tools + ZMQ + Raylib deps + xvfb
RUN apt-get update && apt-get install -y \
    python3 python3-pip \
    g++ cmake make libzmq3-dev nlohmann-json3-dev \
    libasound2-dev libx11-dev libxrandr-dev libxi-dev libgl1-mesa-dev libglu1-mesa-dev \
    libxcursor-dev libxinerama-dev libwayland-dev libxkbcommon-dev git xvfb \
    && rm -rf /var/lib/apt/lists/*

# Compile and install Raylib globally
RUN git clone --depth 1 https://github.com/raysan5/raylib.git /tmp/raylib && \
    cd /tmp/raylib/src && make PLATFORM=PLATFORM_DESKTOP && make install && \
    rm -rf /tmp/raylib

WORKDIR /app

# Install python dependencies before copying code to leverage Docker layer caching
RUN pip3 install --default-timeout=1000 --no-cache-dir numpy torch gymnasium stable-baselines3 pyzmq

# Copy the entire project
COPY . .

# Compile C++ Simulator
RUN mkdir -p build && cd build && \
    g++ -O3 -std=c++17 ../src/cpp/simulator.cpp -o simulator -lzmq -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

# Start script to run both simulator and training loop
RUN echo '#!/bin/bash\n\
# Start C++ Simulator in background with fake display\n\
cd /app/build && xvfb-run -a ./simulator --headless &\n\
\n\
# Wait a second for simulator to bind socket\n\
sleep 1\n\
\n\
# Start Python SAC Training\n\
cd /app && python3 src/python/train_sac.py\n\
' > /app/start.sh && chmod +x /app/start.sh

CMD ["/app/start.sh"]
