# ⚡ Niyanta: Autonomous Systems Orchestrator

![Docker](https://img.shields.io/badge/Docker-Ready-2496ED?style=for-the-badge&logo=docker&logoColor=white)
![C++](https://img.shields.io/badge/C++17-Engine-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)
![ROS 2](https://img.shields.io/badge/ROS_2-Humble-22314E?style=for-the-badge&logo=ros&logoColor=white)
![PyTorch](https://img.shields.io/badge/PyTorch-SAC_RL-EE4C2C?style=for-the-badge&logo=pytorch&logoColor=white)
![Next.js](https://img.shields.io/badge/Next.js-UI-000000?style=for-the-badge&logo=next.js&logoColor=white)

Niyanta (Sanskrit for *Controller* or *Orchestrator*) is a high-performance, full-stack "System of Systems" designed to monitor and manage heterogeneous autonomous agents in real-time. 

It demonstrates the orchestration of **Deep Reinforcement Learning (PyTorch)**, **Autonomous Mobile Robotics (ROS 2 Navigation)**, and **C++ Physics Simulators** seamlessly unified under a custom-built, ultra-low-latency **In-Memory C++ Database**. 

The entire ecosystem is containerized for production and visualized through a modern, glassmorphic **Next.js React Dashboard** using Server-Sent Events (SSE).

---

## 🏗️ System Architecture

The ecosystem relies on an event-driven **Pub/Sub architecture** to prevent bottlenecks between the AI loops and the UI.

```mermaid
graph TD;
    subgraph Orchestration Layer
        TakshakDB[(TakshakDB<br>Custom C++ In-Memory DB)]
    end

    subgraph Autonomous RL Agent
        RL[Python SAC Training Loop] <-->|ZeroMQ TCP| Sim[C++ Power Grid Simulator]
        RL -->|Publish Metrics| TakshakDB
    end

    subgraph Autonomous Robotics
        AMR[ROS 2 Humble Nav2] -->|Publish Odom / LaserScan| TakshakDB
    end

    subgraph Command Center UI
        Next[Next.js Dashboard]
        Chess[C WASM Chess Engine] -->|Cache Evaluations| TakshakDB
        Next --> Chess
        TakshakDB -->|Server-Sent Events (SSE)| Next
    end
```

---

## 🧩 Core Microservices

Niyanta is composed of five distinct, containerized microservices:

### 1. TakshakDB (`/TakshakDB`)
* **Role:** The ultra-fast nervous system of the project.
* **Tech Stack:** C++17, Custom TCP Sockets, RESP Protocol.
* **Features:** A Redis-lite clone built entirely from scratch. Implements an LRU cache, a robust Pub/Sub broker for 30Hz telemetry streaming, and Append-Only File (AOF) disk persistence.

### 2. Autonomous Power Grid (`/AutonomousPowerGrid`)
* **Role:** A Deep Reinforcement Learning environment where an AI learns to balance a 100-substation electrical grid.
* **Tech Stack:** C++ (Raylib), Python (PyTorch, Stable Baselines 3, Gymnasium), ZeroMQ.
* **Features:** A C++ graph physics simulator bridges to a Python SAC (Soft Actor-Critic) agent via ZeroMQ. The agent calculates power distribution for 8,760 hours (1 year) per episode, publishing live rewards to TakshakDB.

### 3. ROS 2 AMR Simulator (`/assignment_ws`)
* **Role:** Autonomous Mobile Robot navigation and mapping.
* **Tech Stack:** ROS 2 (Humble), Nav2, Gazebo, Python `rclpy`.
* **Features:** Runs a headless Gazebo simulation of an AMR. A custom Python bridge node (`takshak_bridge.py`) subscribes to `/odom` and `/scan` topics, formatting and broadcasting the data to TakshakDB in real-time.

### 4. C WASM Chess Engine (`/Chess_Engine`)
* **Role:** A high-performance AI chess bot executed entirely in the browser.
* **Tech Stack:** C, WebAssembly (Emscripten).
* **Features:** A custom C minimax algorithm with alpha-beta pruning compiled to WebAssembly. Uses TakshakDB via API routes to persistently cache millions of board evaluations (`SET chess_cache...`), ensuring the AI learns and responds instantly to previously seen positions.

### 5. Niyanta Web (`/Niyanta`)
* **Role:** The central Command Center UI.
* **Tech Stack:** Next.js (React), Tailwind CSS, HTML5 Canvas.
* **Features:** Consumes live telemetry via persistent SSE connections. Features dynamic HTML5 Canvas rendering to display real-time robot odometry, active LIDAR laser scans, high-frequency RL reward charts, and the interactive WASM Chess Arena.

---

## 🚀 Quick Start (Docker Deployment)

Niyanta is fully containerized. You do not need ROS 2, PyTorch, or C++ compilers installed on your host machine to run it—just Docker.

### Prerequisites
* Docker Engine & Docker Compose
* At least 4 CPU Cores and 8GB RAM available.

### Deployment

1. **Clone the repository:**
   ```bash
   git clone https://github.com/yourusername/Niyanta-Ecosystem.git
   cd Niyanta-Ecosystem
   ```

2. **Build and launch the ecosystem:**
   ```bash
   sudo docker compose up --build
   ```

3. **Access the Dashboard:**
   * Open your browser and navigate to: `http://localhost:3000`
   * You will instantly see the live streams of the RL Agent training and the AMR navigating.

---

## ⚙️ Development & Modifying

If you wish to modify the system:
* The **Docker layer caching** has been heavily optimized. Python dependencies and C++ binaries are cached separately from the source code. Modifying business logic in Python or C++ will result in sub-10-second rebuilds.
* TakshakDB's memory is persistently mapped to a local `./takshak_data` volume to prevent cache loss between restarts.

---

*Built by Parv as a demonstration of extreme full-stack systems engineering.*
