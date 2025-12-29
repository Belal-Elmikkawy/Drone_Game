
# Drone Simulator

This project implements a multi-process drone simulation system using a **Blackboard Architecture**. It features a physics‑based drone, interactive keyboard controls, dynamic obstacles and targets, **a watchdog process for health monitoring**, and **structured log files** with real‑time monitoring via external terminal windows.

---

## 1) Architecture Overview

The system follows a centralized **Process/Pipes/Select** model. The Blackboard Server (Process **B**) acts as the central hub, managing state and coordinating communication between all components. A new **Watchdog (Process W)** monitors process liveness.

```
 [ User Input ]   [ Random Gen ]   [ Random Gen ]         [ Watchdog ]
        \               \                \                     |
         v               v                v                     |
 +-------------+  +-------------+  +-------------+              |
 |  Process I  |  |  Process O  |  |  Process T  |              |  SIGUSR1 liveness ping
 |  (Input)    |  | (Obstacles) |  |  (Targets)  |              v
 +-------------+  +-------------+  +-------------+         +--------------+
         \             (Pipe)            /                 |  FILE_PID    |
          \------------ writes ---------/                  |  registry   |
                        coords                              +--------------+
                                 v
  +--------------------------------------------------------------------------+
  |                       PROCESS B (SERVER)                                  |
  | * Master Process & UI (ncurses)                                           |
  | * Holds World State: {Drone, Obstacles, Targets, Score}                   |
  | * Multiplexes inputs using select()                                       |
  | * Spawns external monitoring terminals (xterm)                            |
  +--------------------------------------------------------------------------+
              ^                      (Pipe: Sends State)
              | (Pipe: Reads Position)
              |
        +-------------+
        |  Process D  |
        |  (Drone)    |
        +-------------+
```

> **Watchdog behavior (W):** periodically reads the PID registry file (`FILE_PID`), sends `SIGUSR1` to each registered process, and writes results to `LOG_WATCHDOG`. If any process is unresponsive, **W stops the system by killing the Server PID** so a clean cascade shutdown occurs (fallback: kill the dead process itself). See code in `pro_W.c` and macros in `common.h`.  

---

## 2) Active Components (Definitions)

### Process B (Blackboard Server)
- **Role:** The Master Process. Initializes the simulation, spawns all child processes (I, D, O, T, **W**), and creates communication pipes.
- **Functionality:**
  - Maintains central World State (Drone position, Obstacle list, Target list, Score).
  - Uses `select()` for multiplexing.
  - Renders UI using ncurses.
  - Spawns external xterm windows for logs.

### Process D (Drone Dynamics)
- **Role:** Physics Engine.
- **Functionality:** Calculates drone movement using Newton's laws (`F = Ma + Kv`). Handles repulsive forces from walls/obstacles. **Attraction to targets was removed to enforce manual control** (see Fixation section).  

### Process I (Input Manager)
- **Role:** Captures user keyboard input and converts it into force vectors.

### Process O (Obstacle Generator)
- **Role:** Generates random obstacle coordinates periodically.

### Process T (Target Generator)
- **Role:** Generates random target coordinates periodically.

### **Process W (Watchdog – NEW)**
- **Role:** Health monitor and failsafe.
- **Functionality:**
  - Registers itself in `FILE_PID` and clears old watchdog logs on startup.
  - Acquires a **shared file lock** when reading the PID registry to avoid races with writers.
  - Issues `SIGUSR1` liveness probes to each process.
  - On failure, logs an alert and **kills the Server PID** to stop the system safely; otherwise shows each process as **ACTIVE** in an ncurses window.
  - Logs are written to `LOG_WATCHDOG` (path defined in `common.h`).

---

## 3) Project Structure (updated)

Below is a representative layout. File names for logs and binaries are defined in `common.h`/`Makefile` and may vary slightly per platform.

```
├── Makefile
├── common.h
├── params.txt
├── pro_B.c            # Server (Master)
├── pro_D.c            # Drone (Physics)
├── pro_I.c            # Input Manager
├── pro_O.c            # Obstacle Generator
├── pro_T.c            # Target Generator
├── pro_W.c            # Watchdog (NEW)
├── README.md
├── bin/               # Built executables after `make` (names mirror sources)
│   ├── pro_B  pro_D  pro_I  pro_O  pro_T  pro_W
│   └── ...
├── run/               # Convenience launchers (if present)
│   ├── run_all.sh     # starts Server + children incl. Watchdog
│   ├── run_watchdog.sh
│   └── run_clean.sh
└── logs/              # Log files (paths set in common.h)
    ├── game.log
    ├── physics.log
    ├── controls.log
    └── watchdog.log   # LOG_WATCHDOG (NEW)
```

> If your environment does not generate `bin/` and `logs/` automatically, they will be created or files will be emitted in the project root as configured by the macros in `common.h`.

---

## 4) Makefile
- `make`: Compiles all source files.
- `make run`: Compiles everything and launches the simulation (**Watchdog is started as part of the run** if integrated in the Makefile).
- `make clean`: Removes executables and logs.

---

## 5) Installation & Running

### Prerequisites
- Linux environment
- gcc, make
- ncurses library (`libncurses-dev`)
- xterm

Install on Ubuntu/Debian:

```bash
sudo apt-get update
sudo apt-get install build-essential libncurses-dev xterm
```

### How to Run

```bash
make clean
make run
```

Notes:
- The system may open additional xterm windows for live logs.
- The Watchdog opens an ncurses window titled `--- WATCHDOG MONITOR ---` and reports process status; it also prints the path to `LOG_WATCHDOG` at the bottom.

---

## 6) Operational Instructions

### Controls
- **E**: Move Up
- **C**: Move Down
- **S**: Move Left
- **F**: Move Right
- **W, R**: Diagonal Up‑Left / Up‑Right
- **X, V**: Diagonal Down‑Left / Down‑Right
- **SPACE or D**: Brake
- **Q**: Quit
- **Aliases** (for convenience): **I**/**J**/**L**/**,**/**K** map to the same directions/brake as above.

### Dashboard Overview
- **Main Window**: Visual simulation.
  - `+` (Blue): Drone
  - `O` (Orange): Obstacles
  - `1, 2...` (Green): Targets
- **Controls Window**: Key map and input log.
- **Physics Log**: Real‑time physics data.
- **Game Log**: Game events.
- **Watchdog Window (NEW)**: Liveness status for all registered processes.

### Physics Behavior
- Repulsion: Pushes drone away from walls/obstacles.
- Attraction: **Removed** (see Fixation section) — the drone will not “auto‑home” to targets.
- Dynamics: Drone has inertia; use **Brake** to stop instantly.

---

## 7) Logs & Monitoring (NEW)

The system writes structured logs to files whose paths are defined in `common.h` via macros such as `LOG_*` and the PID registry file `FILE_PID`.

- **PID Registry (`FILE_PID`)**: A text file mapping process names to PIDs. A read lock is acquired while reading to avoid races.
- **Watchdog Log (`LOG_WATCHDOG`)**: Truncated on Watchdog startup, then appended with liveness checks and alerts.
- **Other Logs**: Physics, game, and controls logs (names may vary) used by the monitoring xterms and for debugging/replay.

> Tip: For quick triage, tail the watchdog log while the system runs:
>
> ```bash
> tail -f logs/watchdog.log
> ```

---

## 8) Fixation for Assignment1 (UPDATED)

This section captures the **fixations** implemented after Assignment 1, based on changes in `pro_I.c`, `pro_D.c`, and `pro_B.c`.

### A) Input: Button‑based control without unintended drift
1. **Orthogonal force reset on keypress** — pressing a **vertical** key (Up/Down) sets `Fy` accordingly **and resets `Fx = 0.0`**; pressing a **horizontal** key (Left/Right) sets `Fx` and **resets `Fy = 0.0`**. This prevents accidental diagonal movement when users change direction quickly.  
   *Code reference:* see the switch cases for `'e'/'i'` (Up), `'c'/','/'x'` (Down), `'s'/'j'` (Left), `'f'/'l'` (Right) in `pro_I.c`.  citeturn2search3
2. **Explicit Brake** — pressing **SPACE / `d` / `k`** resets **both** forces to `0.0`, guaranteeing an immediate stop regardless of prior inputs.  citeturn2search3
3. **Force clamping** — command forces are hard‑limited to `[-10.0, +10.0]` to maintain physics stability and avoid runaway acceleration.  citeturn2search3
4. **Raw input mode** — terminal is placed in non‑canonical, no‑echo mode so keypresses are read instantly (no ENTER required), improving responsiveness.  citeturn2search3

### B) Drone Physics: Manual control only (no auto movement)
1. **Removed target attraction** — the previous “attractive” field toward targets was eliminated. The drone now moves **only** under user command forces (`F_cmd`) plus **repulsion** from walls/obstacles (`F_rep`).  
   *Code reference:* comment `ASSIGNMENT 1 FIX: MANUAL CONTROL ONLY` and the computation `F_total = F_cmd + F_rep` in `pro_D.c`.  citeturn2search2
2. **Parameter hot‑reload** — physics parameters (`M`, `K`, `T`, `ETA`, `RHO`) can be tuned in `params.txt` and are **reloaded periodically** during runtime, making it easier to dial in control feel without recompiling.  citeturn2search2
3. **Geo‑fencing & stability** — positions are clamped within window bounds and updated using an Euler‑style integration that balances mass/inertia and damping.  citeturn2search2

### C) Server/UI: Scoring and state transmission fixes
1. **Score = targets collected × 1000** — scoring now strictly reflects achievements (hitting targets). Distance and time are shown in the UI for context but **do not penalize or add to the score**.  citeturn2search1
2. **Consistent world‑state payload** — the server sends `W:` (window size), `F:` (command force), `O:` (obstacles), and `T:` (targets) to the Drone each cycle, ensuring the physics engine reacts only to the current inputs and environment snapshot.  citeturn2search1
3. **UI safety & readability** — drawing clamps the drone within the visible border and renders obstacles/targets with color‑coded glyphs; auxiliary xterms display live logs and a controls guide for better operator feedback.  citeturn2search1

### Outcome (what happened)
- **Precise, button‑driven control:** The drone **only** moves when buttons are pressed, with clean direction changes and predictable braking. No more unintended diagonal drift.  citeturn2search3turn2search2
- **Stable physics with clear bounds:** Repulsion and clamping keep the drone in‑bounds; tunable parameters allow smooth adjustments to responsiveness.  citeturn2search2
- **Transparent scoring and monitoring:** Progress is measured by targets collected; logs and UI present an auditable, real‑time view of system state.  citeturn2search1

---

## 9) Known Limitations & Next Steps
- If a process ignores `SIGUSR1`, the Watchdog treats it as unresponsive; ensure all long‑running processes implement the liveness signal handler.
- Consider adding exponential backoff or multiple probes before shutdown to tolerate transient hiccups.
- Optional: parse logs into metrics and expose a tiny dashboard.

