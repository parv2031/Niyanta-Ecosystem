# TakshakDB: High-Performance In-Memory Key-Value Store

TakshakDB is a high-performance, from-scratch C++ implementation of an in-memory key-value database engine. Heavily inspired by the core architecture of Redis, this project is built entirely from the ground up without relying on external networking or database libraries. It operates as a fully functional standalone server, capable of handling multiple concurrent clients, caching data efficiently, and providing data persistence.

## 🚀 Key Features & Systems Concepts

### 1. Custom TCP Socket Server
*   Acts as the "front door" of the database, listening on port **6379**.
*   Built directly on top of Linux POSIX sockets (`socket()`, `bind()`, `listen()`, `accept()`).
*   Parses raw byte streams from network sockets into actionable database commands using a custom RESP-like (Redis Serialization Protocol) string deserializer.

### 2. $O(1)$ LRU Cache Engine
*   Implements a custom **Least Recently Used (LRU)** eviction policy to manage memory constraints effectively (Capacity: 100 items).
*   Uses a synergized data structure combining `std::unordered_map` for $O(1)$ key lookups with a custom **Doubly Linked List** to seamlessly maintain temporal access order. 
*   Utilizes Dummy Head/Tail nodes to safely eliminate `nullptr` edge cases.

### 3. High-Concurrency Threading Model
*   Implements a **Thread-per-Client** architecture to serve multiple simultaneous connections asynchronously.
*   Ensures thread-safe operations on the shared key-value store state using **Mutexes (`std::mutex`)** and the **RAII** pattern (`std::lock_guard`), preventing dangerous race conditions and segmentation faults.

### 4. Dual-Mechanism TTL (Time-To-Live)
*   **Passive Expiry (Lazy Deletion):** Checks a key's expiration timestamp via `std::chrono` at the exact moment of a `GET` request, deleting it instantly if the time has passed.
*   **Active Expiry (Background Sweeper):** Runs a detached background daemon thread (`std::thread`) that sweeps the Hash Map every 1 second, automatically purging forgotten expired keys to free up RAM. Uses `std::atomic<bool>` for safe thread shutdown.

### 5. AOF (Append-Only File) Persistence
*   Guarantees data durability across server crashes or power outages.
*   Every mutating operation (`SET`, `DEL`, `EXPIRE`) is systematically logged to an on-disk text file (`takshak.aof`).
*   **Crash Recovery (Rehydration):** Upon startup, the database state is flawlessly reconstructed in RAM by replaying the exact sequence of historical commands from the AOF log.

---

## 🛠️ Tech Stack

*   **Language:** C++17
*   **Networking:** POSIX Sockets (`<sys/socket.h>`)
*   **Concurrency:** C++ Standard Template Library (`<thread>`, `<mutex>`, `<atomic>`)
*   **Timekeeping:** `<chrono>` (Steady Clocks)
*   **Build System:** CMake

---

## ⚙️ How to Build and Run

### Prerequisites
*   A Linux environment (or WSL on Windows)
*   `g++` compiler with C++17 support
*   `cmake`
*   `netcat` (for testing)

### 1. Build the Database
```bash
git clone https://github.com/yourusername/TakshakDB.git
cd TakshakDB
mkdir build && cd build
cmake ..
cmake --build .
```

### 2. Start the Server
```bash
./takshak-server
```
*You should see:* `TakshakDB Server is listening on port 6379...`

---

## 💻 Interacting with TakshakDB (Command Reference)

You can connect to TakshakDB using the standard `netcat` (`nc`) utility from any terminal.

```bash
# Open a new terminal window and connect to the server
nc localhost 6379
```

Once connected, you can type the following commands:

| Command | Syntax | Description | Example Response |
| :--- | :--- | :--- | :--- |
| **PING** | `PING` | Tests the server connection. | `+PONG` |
| **SET** | `SET <key> <value>` | Stores a value under the given key. | `+OK` |
| **GET** | `GET <key>` | Retrieves the value of a key. Returns `-1` if missing. | `$5\r\nvalue\r\n` |
| **DEL** | `DEL <key>` | Deletes a key from the database. Returns `1` if successful. | `:1` |
| **EXPIRE** | `EXPIRE <key> <seconds>` | Sets a timeout on an existing key. | `:1` |

### Demo Interaction
```text
nc localhost 6379

> PING
+PONG

> SET hero Batman
+OK

> GET hero
$6
Batman

> EXPIRE hero 5
:1

> GET hero       (Type this immediately)
$6
Batman

> GET hero       (Wait 5 seconds and type again)
$-1

> DEL somekey
:0
```
