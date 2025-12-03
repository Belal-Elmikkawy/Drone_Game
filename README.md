# Drone Simulator 

This project implements a multi-process drone simulation system using a **Blackboard Architecture**. It features a physics-based drone, interactive keyboard controls, dynamic obstacles and targets, and real-time monitoring via external terminal windows.

---

## 1. Architecture Sketch

The system follows a centralized **Process/Pipes/Select** model. The Blackboard Server (Process B) acts as the central hub, managing state and coordinating communication between all components.

```
      [ User Input ]              [ Random Gen ]           [ Random Gen ]
           |                            |                        |
           v                            v                        v
    +-------------+              +--------------+         +--------------+
    |  Process I  |              |  Process O   |         |  Process T   |
    |   (Input)   |              | (Obstacles)  |         |  (Targets)   |
    +-------------+              +--------------+         +--------------+
           | (Pipe)                     | (Pipe)                 | (Pipe)
           | writes force               | writes coords          | writes coords
           v                            v                        v
    +--------------------------------------------------------------------+
    |                        PROCESS B (SERVER)                          |
    |--------------------------------------------------------------------|
    |  * Master Process & UI (ncurses)                                   |
    |  * Holds World State: {Drone, Obstacles, Targets}                  |
    |  * Multiplexes inputs using select()                               |
    |  * Spawns external monitoring terminals (xterm)                    |
    +--------------------------------------------------------------------+
           ^                                            |
           | (Pipe: Reads Position)                     | (Pipe: Sends State)
           |                                            |
    +-------------+                                     v
    |  Process D  | <-----------------------------------+
    |   (Drone)   |      Message: "W:Size | F:Force | O:List | T:List"
    +-------------+
```

---

## 2. Active Components (Definitions)

### Process B (Blackboard Server)
- **Role:** The Master Process. Initializes the simulation, spawns all child processes (I, D, O, T), and creates communication pipes.
- **Functionality:**
  - Maintains central World State (Drone position, Obstacle list, Target list, Score).
  - Uses `select()` for multiplexing.
  - Renders UI using ncurses.
  - Spawns external xterm windows for logs.

### Process D (Drone Dynamics)
- **Role:** Physics Engine.
- **Functionality:** Calculates drone movement using Newton's laws (`F = Ma + Kv`). Handles repulsive and attractive forces.

### Process I (Input Manager)
- **Role:** Captures user keyboard input and converts it into force vectors.

### Process O (Obstacle Generator)
- **Role:** Generates random obstacle coordinates periodically.

### Process T (Target Generator)
- **Role:** Generates random target coordinates periodically.

---

## 3. List of Files

| File        | Description |
|-------------|-------------|
| common.h    | Shared header file defining constants and data structures. |
| pro_B.c     | Source code for the Server (Master process). |
| pro_D.c     | Source code for the Drone (Physics engine). |
| pro_I.c     | Source code for the Input Manager. |
| pro_O.c     | Source code for the Obstacle Generator. |
| pro_T.c     | Source code for the Target Generator. |
| params.txt  | Configuration file for simulation parameters. |
| Makefile    | Compilation script to build the project and launch it. |

---

## 4. Makefile

- `make`: Compiles all source files.
- `make run`: Compiles everything and launches the simulation.
- `make clean`: Removes executables and logs.

---

## 5. Installation & Running

### Prerequisites
- Linux environment
- gcc, make
- ncurses library (`libncurses-dev`)
- xterm

Install on Ubuntu/Debian:
```
sudo apt-get update
sudo apt-get install build-essential libncurses-dev xterm
```

### How to Run
```
make clean
make run
```

Note: The system opens 3 additional xterm windows automatically.

---

## 6. Operational Instructions

### Controls
- **E**: Move Up
- **C**: Move Down
- **S**: Move Left
- **F**: Move Right
- **W, R**: Diagonal Up-Left / Up-Right
- **X, V**: Diagonal Down-Left / Down-Right
- **SPACE or D**: Brake
- **Q**: Quit

### Dashboard Overview
- **Main Window**: Visual simulation.
  - `+` (Blue): Drone
  - `O` (Orange): Obstacles
  - `1, 2...` (Green): Targets
- **Controls Window**: Key map and input log.
- **Physics Log**: Real-time physics data.
- **Game Log**: Game events.

### Physics Behavior
- Repulsion: Pushes drone away from walls/obstacles.
- Attraction: Pulls drone toward targets.
- Dynamics: Drone has inertia; use Brake to stop instantly.

---
