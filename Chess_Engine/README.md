# ♚ Advanced GTK Chess Engine

A professional-grade, high-performance GUI-based Chess Game written in C using GTK3. This project features a completely custom-built chess engine with **Minimax & Alpha-Beta Pruning**, full **Undo/Redo stack architecture**, comprehensive rules validation (including Pawn Promotion and Stalemate), and an incredibly polished aesthetic interface powered by a Global CSS provider.

---

## ✨ Features

- **♟️ Complete Chess Logic:** Rigorous move validation, Check/Checkmate detection, Stalemate draws, and automatic Pawn Promotion.
- **🤖 Intelligent Engine:** Custom `Minimax` search algorithm with `Alpha-Beta Pruning` for challenging "vs Computer" gameplay.
- **🔄 Stack-Based State Management:** Full Undo and Redo capabilities powered by a robust dual-stack memory implementation, seamlessly reverting both logical board state and GTK graphical interfaces.
- **👥 Player vs Player Mode:** Fully supported local PvP play with turn-switching and immediate GUI response.
- **🎨 Premium Visuals:** Modern aesthetic with a custom Global CSS system—features glassmorphism UI logic, high-res HD graphics, smooth transitions, hover effects, and a highly polished dark-mode styling.
- **🪟 Interactive Board GUI:** Custom GTK3 chessboard rendering using Cairo graphics with legal-move highlighting (red/blue dots) and instant piece-drag/snap visual feedback.
- **🌍 Cross Platform Support:** Fully builds on both Linux and Windows (via MSYS2 / MinGW64).

---

## 🧠 Core Computer Science Concepts Explained

This project serves as a practical demonstration of several fundamental Computer Science topics. Below is an overview of the core concepts implemented in the engine:

### 1. Game Tree Search (Minimax Algorithm)
The "vs Computer" mode is powered by the **Minimax Algorithm**, a recursive decision rule used in artificial intelligence for two-player games.
- **State Evaluation:** The algorithm uses a custom heuristic function `evaluate_board()` that calculates the material advantage of the current board (e.g., King = 20,000 pts, Queen = 900 pts). 
- **Recursive Depth-Limited Search:** The AI generates a "game tree" of all possible future moves up to a fixed depth limit. It simulates the human player trying to minimize the score, while the AI tries to maximize it. The engine assumes perfect play from the opponent and selects the path that guarantees the highest possible outcome.

### 2. Algorithmic Optimization (Alpha-Beta Pruning)
Chess has an incredibly high branching factor. To make the Minimax search efficient, **Alpha-Beta Pruning** is integrated directly into the recursive tree search.
- The algorithm maintains two variables: `alpha` (the minimum score the maximizing player is assured of) and `beta` (the maximum score the minimizing player is assured of).
- If the algorithm discovers a move that is worse than a previously evaluated move (`beta <= alpha`), it "prunes" (stops evaluating) that entire branch of the game tree. 
- This mathematically guarantees the exact same output as standard Minimax but drastically reduces the number of nodes evaluated, allowing the AI to search deeper into the future within the same time constraints.

### 3. Data Structures: Dual-Stack State Management (Undo / Redo)
To provide robust time-travel mechanics (Undo/Redo), the engine utilizes an Array-Based **Dual-Stack Architecture**.
- **Undo Stack:** Every time a piece is moved, a `MoveRecord` struct is instantiated. This struct holds a complete snapshot of the transition: the original coordinates, the destination coordinates, logical string identifiers of the pieces, and explicit pointers to the GTK GUI Widgets. This record is `pushed` onto the top of the history stack.
- **Redo Stack:** When a user clicks Undo, the engine `pops` the top record, reverses the logical and graphical state, and leaves the record in memory, decrementing the `history_top` pointer. If Redo is clicked, the pointer is incremented and the move is re-applied forward.
- **State Invalidation:** If a user undoes a move and then makes a *new* move, the "future" timeline is explicitly destroyed by clamping the `history_max` bound.

### 4. Event-Driven Architecture & UI Decoupling
The graphical interface is built using the GTK3 library, which operates on an **Event-Driven Architecture**.
- The program halts and listens continuously in a `gtk_main()` loop.
- Interactions (like clicking a square) fire asynchronous signals that trigger highly specific callback functions (e.g., `pvp_button_press_event_callback`).
- **Separation of Concerns:** The engine strictly decouples logical state mutations from graphical rendering. When a move is executed, the backend `chess_board[8][8]` array is updated first. Then, rather than forcing manual pixel updates, the engine queues a redraw (`gtk_widget_queue_draw()`), allowing the GUI thread to smoothly consume the new logical state and paint the board.

### 5. Vector Graphics & Matrix Rendering (Cairo)
The board grid is not a static image. It is procedurally generated using **Cairo 2D Graphics**.
- The board coordinates are mapped linearly to graphical offsets using matrix multiplication (`col * SQUARE_SIZE`, `row * SQUARE_SIZE`).
- Legal move hints (the red and blue indicator dots) are drawn by hooking into the paint event and utilizing trigonometry to draw perfect geometric arcs (`cairo_arc()`) dynamically based on the engine's real-time valid move arrays.

### 6. Software Engineering: DRY Principles
The codebase is structured to avoid redundancy using "Don't Repeat Yourself" (DRY) principles.
- Instead of having separate hardcoded functions for every single piece logic (e.g., `pawn_moves_white()` vs `pawn_moves_black()`), the engine defines generic movement constraints using a polymorphic `Color` enum.
- This cuts the boilerplate codebase size in half, improves cache locality, and makes extending the codebase significantly easier.

---

## 📁 Project Structure

```text
Chess_Game/
│
├── src/
│   ├── main.c        # Entry point and GTK initialization
│   ├── game.c        # Chess piece mechanics, move generators, check validation
│   ├── game.h        # Enums, Core Logic APIs
│   ├── gui.c         # Cairo board rendering, CSS providers, signal handling
│   ├── gui.h         # Layout variables, GUI APIs
│   ├── globals.c     # State management, Undo/Redo Stacks
│   ├── globals.h     # Shared memory maps, Macros
│   ├── ai.c          # Minimax algorithm, Alpha-Beta pruning, Engine Evaluation
│   ├── ai.h          # AI interface API
│   └── assets/       # High-res piece images and GUI overlays
│
├── CMakeLists.txt    # Build system directives
└── README.md         # Project documentation
```

---

## 💻 Dependencies & Setup

### Technologies Used
- **C11 / C17**
- **GTK3** (Cross-platform UI Toolkit)
- **CairoGraphics** (2D vector drawing)
- **GLib** (Data structures & event loops)
- **CMake** (Build system)

### 🐧 Linux (Ubuntu) Setup

**1. Install Dependencies**
```bash
sudo apt update
sudo apt install build-essential cmake libgtk-3-dev pkg-config
```

**2. Build & Run**
```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
./chessbot
```

### 🪟 Windows Setup (MSYS2 / MinGW64)

**1. Install MSYS2**
Download and install from: [https://www.msys2.org/](https://www.msys2.org/)

**2. Install Required Packages**
Open the **MSYS2 MinGW64 Terminal** and run:
```bash
pacman -Syu
pacman -S mingw-w64-x86_64-gcc
pacman -S mingw-w64-x86_64-cmake
pacman -S mingw-w64-x86_64-gtk3
pacman -S mingw-w64-x86_64-pkg-config
```

**3. Build & Run**
```bash
mkdir build
cd build
cmake -G "MinGW Makefiles" ..
mingw32-make
./chessbot
```

---

*Designed and engineered as a showcase of C system programming, advanced data structures, algorithmic design, and full-stack native application development.*
