
# Drone Simulator (Assignments 1–3)

A multi‑process drone simulation system that evolves across three assignments:

1. **Basic Simulation** – Physics‑based drone controlled by keyboard forces.
2. **Full System** – Added dynamic obstacles, targets, logs, and a **Watchdog** for health monitoring.
3. **Networked Multiplayer** – Connects two instances (Server/Client) over TCP sockets to share a virtual space.

---

## Table of Contents
- [Architecture Overview](#architecture-overview)
  - [Processes & Responsibilities](#processes--responsibilities)
  - [Startup Sequence](#startup-sequence)
  - [IPC Topology (Pipes) & Event Loop](#ipc-topology-pipes--event-loop)
  - [Message Formats](#message-formats)
  - [Network Mode (Assignment 3)](#network-mode-assignment-3)
  - [Signals & Watchdog Flow](#signals--watchdog-flow)
- [Data Exchange & Virtual Coordinates](#data-exchange--virtual-coordinates)
- [Assignment 2 Features & Fixes](#assignment-2-features--fixes)
  - [Watchdog & Health Monitoring](#watchdog--health-monitoring)
  - [Obstacle Logic Fix](#obstacle-logic-fix)
  - [Logging Reset](#logging-reset)
- [Logs & File Outputs](#logs--file-outputs)
  - [Log Files](#log-files)
  - [Log Format](#log-format)
  - [Examples](#examples)
- [Project Structure](#project-structure)
- [Build & Run Instructions](#build--run-instructions)
  - [Compile](#compile)
  - [Run](#run)
  - [Usage & Controls](#usage--controls)
- [Known Limitations](#known-limitations)
- [Troubleshooting](#troubleshooting)

---

## Architecture Overview

The system implements a centralized **Blackboard Architecture** with a **Process / Pipes / `select()`** design. The Blackboard (Process **B**) owns the world state and UI, and orchestrates all other processes.

```
+--------------------+             TCP               +--------------------+
|  Local Instance    | <===========================> |  Remote Instance   |
| (Server or Client) |                                | (Client or Server) |
+---------+----------+                                +----------+---------+
          |                                                    |
          |  spawn/pipe                                        |
          v                                                    v
   +-------------+     pipes      +-------------+       (disabled in
   |  Process I  |  ----------->  |  Process B  |       network mode)
   |   (Input)   |  <-----------  |  (Blackboard|<---+   +-------------+
   +-------------+                 |  + UI)     |    |   |  Process O  |
          ^                        +-------------+    |   | (Obstacle)  |
          |                               ^          |   +-------------+
          |                               |          |   +-------------+
          |                               |          +---|  Process T  |
          |                               |              |  (Target)   |
          |                               |              +-------------+
          |                               |
          |                               v
          |                        +-------------+
          +------------------------|  Process D  |
                                   |  (Physics)  |
                                   +-------------+
```

### Processes & Responsibilities

- **Process B — Blackboard / Server (`src/server/pro_B.c`)**
  - Initializes ncurses UI, reads `params.txt`, creates pipes, and spawns children.
  - Maintains authoritative world state (drone pose/vel, obstacles, targets, score).
  - Non‑blocking event loop using `select()` over pipes, keyboard (if local), and socket (if networked).
  - Renders the scene and routes messages between processes.

- **Process D — Physics (`src/drone/pro_D.c`)**
  - Integrates motion using mass, friction, time step, and repulsion forces.
  - Receives commanded forces from **I** and environment info from **B**; sends back updated pose/velocity.

- **Process I — Input (`src/input/pro_I.c`)**
  - Puts terminal in raw mode; maps keys to force vectors and special commands (brake, quit).
  - Sends compact control messages to **B**.

- **Process O — Obstacle Generator (`src/obstacle/pro_O.c`)** *(disabled in network mode)*
  - Emits spawn/despawn events (including `0,0` as **despawn**) at random intervals.

- **Process T — Target Generator (`src/target/pro_T.c`)** *(disabled in network mode)*
  - Emits target spawn events.

- **Process W — Watchdog (`src/watchdog/pro_W.c`)** *(disabled in network mode)*
  - Periodically probes liveness via `SIGUSR1`; triggers shutdown if a process misbehaves.

### Startup Sequence

1. **B** starts; loads params from `params.txt`.
2. **B** creates named/unnamed pipes (read ends set **O_NONBLOCK**), installs signal handlers, and initializes ncurses.
3. **B** `fork()`/`exec()` each child with relative paths (e.g., `src/drone/drone`).
4. Each child appends its PID to `pid_registry.txt` and sends an **online** announcement to **B**.
5. In **local mode**: **O**, **T**, **W** start; in **network mode**: they are skipped.
6. The main loop in **B** begins.

### IPC Topology (Pipes) & Event Loop

- **Pipes**: point‑to‑point unidirectional channels. Typical pairs:
  - `I -> B`: control input
  - `B -> D`: environment snapshot / forces
  - `D -> B`: state updates (pose, velocity)
  - `O -> B`: obstacle spawn/despawn
  - `T -> B`: target spawn

- **Event Loop** (in **B**):
  1. Build an `fd_set` of all readable FDs (pipes + socket + optional keyboard FD).
  2. `select(timeout)`; on readiness, read available messages without blocking.
  3. Update world state deterministically (apply physics tick order, cull entities, score).
  4. Render via ncurses and dispatch outgoing messages to relevant processes.

> The loop is resilient: if a producer is slow or silent, **B** continues rendering using the latest known state.

### Message Formats

All payloads are compact and parseable. Examples (illustrative):

- **Input → B** (ASCII line):
  ```
  key I  # up
  key .  # diag down-right
  cmd brake
  cmd quit
  ```

- **B → D** (ASCII or struct‑binary; example as ASCII):
  ```
  env dt=0.016 k=0.12 m=1.0 ax=+0.40 ay=-0.25 obs=3
  obs 12 18
  obs 40  5
  obs 60 24
  ```

- **D → B**:
  ```
  state x=32.6 y=14.2 vx=1.8 vy=-0.3
  ```

- **O → B** (spawn/despawn):
  ```
  obst 21 7        # spawn
  obst 0 0         # despawn (server selects which one to remove)
  ```

- **T → B**:
  ```
  targ 55 11
  ```

### Network Mode (Assignment 3)

- **Port**: `5555` (TCP).
- **Handshake**:
  1. Client connects.
  2. Server → `ok`; Client → `ook`.
  3. Server → `size W H`; Client resizes window.
  4. Client → `sok`; both sides mark **ready**.
- **Data Exchange**: each side sends its drone position in **Virtual Coordinates** (origin bottom‑left). Upon receipt, positions are transformed back to **Local Coordinates** (origin top‑left). The remote drone is modeled as a repulsive obstacle in local physics.
- **Generators Disabled**: **O**, **T**, **W** are disabled to emphasize PvP interaction.

### Signals & Watchdog Flow

- Each process registers its PID in `pid_registry.txt` at startup.
- **W** periodically sends `SIGUSR1` to all registered PIDs.
- On signal, a healthy process updates an internal heartbeat (and may log a `beat` line).
- If any process fails to respond within a grace period, **W** marks it **UNRESPONSIVE** and **terminates B** for a clean shutdown.

---

## Data Exchange & Virtual Coordinates

- Transmit in **Virtual** (origin bottom‑left) to decouple screens.
- Convert to **Local** (origin top‑left) on receipt.
- Remote drone is treated as an **Obstacle** (repulsive force) in local physics to simulate avoidance/dogfighting.

---

## Assignment 2 Features & Fixes

### Watchdog & Health Monitoring
- **Process Registration:** Every process (`I`, `D`, `O`, `T`) registers its PID in `pid_registry.txt`.
- **Liveness Check:** Watchdog (`pro_W`) sends `SIGUSR1` and expects a timely handler update; otherwise marks **UNRESPONSIVE**.
- **System Kill:** On failure, Watchdog kills **B** for clean teardown.

### Obstacle Logic Fix
- **Previous:** Obstacles accumulated without culling.
- **Now:** `pro_O` randomly emits `0,0`; **B** interprets it as a **despawn** request for an existing obstacle.

### Logging Reset
- At startup, `reset_logs()` clears `input.log`, `drone.log`, etc., to ensure clean runs with fresh data.

---

## Logs & File Outputs

### Log Files

> Logs live in the project root by default (unless you changed paths in code). The core ones are:

- **`input.log`** — Keys captured by **I** and routed via **B**.
- **`drone.log`** — Physics integration snapshots from **D** (time, pose, velocity, applied forces, collisions).
- **`pid_registry.txt`** — Appended by each process at startup with its PID.

**Optional / Common additions** (if implemented in your codebase):
- **`server.log`** — Blackboard loop ticks, routing decisions, FPS/UPS, and select() wake causes.
- **`obstacle.log`** — Spawns/despawns emitted by **O** and applied by **B**.
- **`target.log`** — Spawns emitted by **T** and pickups.
- **`watchdog.log`** — Heartbeats, probes, and kill events.
- **`network.log`** — Handshake steps and periodic position packets (Assignment 3).

### Log Format

- **Timestamps**: `YYYY-MM-DD HH:MM:SS.mmm` (local time) at the start of each line.
- **Structured fields** separated by spaces (or CSV/TSV if you prefer). Recommended canonical schema:

```
[ts] component=I level=INFO event=key code=I mods=none
[ts] component=D level=INFO event=state x=32.60 y=14.20 vx=1.80 vy=-0.30 ax=0.40 ay=-0.25
[ts] component=B level=DEBUG event=route from=I to=D bytes=18
[ts] component=O level=INFO event=spawn x=21 y=7
[ts] component=O level=INFO event=despawn x=0 y=0
[ts] component=W level=WARN event=unresponsive pid=12345
```

- **Levels**: `DEBUG`, `INFO`, `WARN`, `ERROR`.
- **Rotation**: Logs are cleared at startup by `reset_logs()` to avoid growth across runs.

### Examples

- **`input.log`**
  ```
  2026-01-10 12:01:20.153 component=I level=INFO event=key code=I mods=none
  2026-01-10 12:01:20.219 component=I level=INFO event=key code=SPACE mods=none
  2026-01-10 12:01:21.004 component=I level=INFO event=cmd name=quit
  ```

- **`drone.log`**
  ```
  2026-01-10 12:01:20.170 component=D level=INFO event=state x=32.60 y=14.20 vx=1.80 vy=-0.30 ax=0.40 ay=-0.25
  2026-01-10 12:01:20.186 component=D level=DEBUG event=repel src=obs id=2 dx=-4 dy=+3 mag=0.22
  2026-01-10 12:01:20.202 component=D level=INFO event=state x=32.63 y=14.18 vx=1.85 vy=-0.32 ax=0.38 ay=-0.26
  ```

- **`server.log`** *(if enabled)*
  ```
  2026-01-10 12:01:20.150 component=B level=DEBUG event=select ready=I,D timeout=16ms
  2026-01-10 12:01:20.166 component=B level=INFO  event=render fps=60 entities=7
  2026-01-10 12:01:20.182 component=B level=DEBUG event=route from=O to=B bytes=10
  ```

---

## Project Structure

```
├── include/
│   └── common.h                # Shared constants, structs, and network headers
├── src/
│   ├── server/pro_B.c          # MASTER: Handles UI, Network, and Pipe routing
│   ├── drone/pro_D.c           # PHYSICS: Mass, friction, repulsion forces
│   ├── input/pro_I.c           # INPUT: Keyboard handling (raw mode)
│   ├── obstacle/pro_O.c        # GENERATOR: Spawns/Despawns obstacles
│   ├── target/pro_T.c          # GENERATOR: Spawns targets
│   └── watchdog/pro_W.c        # MONITOR: Checks PIDs and system health
├── params.txt                  # Physics parameters (M, K, T, etc.)
└── README.md
```

---

## Build & Run Instructions

> **Important:** Run all commands from the **project root** (e.g., `Drone_Game-main`). The Master process uses relative paths (e.g., `src/drone/drone`).

### Compile

```bash
# 1) Compile Master (Server) – links ncurses & math
gcc -Iinclude src/server/pro_B.c -o src/server/master -lncurses -lm

# 2) Compile Drone (Physics) – links math
gcc -Iinclude src/drone/pro_D.c -o src/drone/drone -lm

# 3) Compile Input Handler
gcc -Iinclude src/input/pro_I.c -o src/input/input

# 4) Compile Watchdog – links ncurses
gcc -Iinclude src/watchdog/pro_W.c -o src/watchdog/watchdog -lncurses

# 5) Compile Obstacle Generator
gcc -Iinclude src/obstacle/pro_O.c -o src/obstacle/obstacle

# 6) Compile Target Generator
gcc -Iinclude src/target/pro_T.c -o src/target/target
```

### Run

Launch the Master executable. It will automatically spawn and connect the other processes.

```bash
./src/server/master
```

### Usage & Controls

- **Mode Selection:** A menu appears at startup.
  - Press **`s`** to host a game (**Server**).
  - Press **`c`** to join a game (**Client**) → enter the Server IP (e.g., `127.0.0.1`).
  - Press **`l`** for **Local** single‑player.

- **Controls:**
  - `I` / `E`: Up
  - `K` / `C`: Down
  - `J` / `S`: Left
  - `L` / `F`: Right
  - `U` / `W`: Diagonal Up‑Left
  - `O` / `R`: Diagonal Up‑Right
  - `M` / `X`: Diagonal Down‑Left
  - `.` / `V`: Diagonal Down‑Right
  - `SPACE`: Brake (stop instantly)
  - `Q`: Quit

---

## Known Limitations

- **Firewall/Networking:** Ensure TCP port **5555** is open for cross‑machine play.
- **Working Directory:** Always run the master from the **project root** so relative paths to subprocesses resolve correctly.

---

## Troubleshooting

- **Black Screen or UI Mismatch:** Confirm terminal supports **ncurses** and that window size matches the server’s announced `size W H`.
- **Client Can’t Connect:** Verify IP address and that port **5555** is open and not in use.
- **Processes Not Spawning:** Run from project root; paths like `src/drone/drone` must be valid. Check permissions (`chmod +x`).
- **Watchdog Kill Events:** Inspect `pid_registry.txt` and logs (`input.log`, `drone.log`, and optional logs) to see which process became unresponsive.
- **Obstacle Overflow (Local Mode):** Ensure you rebuilt `pro_O` with the despawn logic and that the server interprets `0,0` as a despawn event.

---



### Ubuntu (Linux) Quick Commands — Connect Two Devices

> Use these terminal commands on Ubuntu to open the port, find the server IP, and start the game on both machines.

#### 1) Open Port 5555 (Firewall)
Check if UFW is active; only add the rule if it is:
```bash
sudo ufw status
# If Status: inactive → skip the next line
sudo ufw allow 5555/tcp   # opens TCP 5555 when UFW is active
```

#### 2) Find the Server’s IP
**Quick (recommended):**
```bash
hostname -I | awk '{print $1}'   # prints your primary LAN IP, e.g., 192.168.1.45
```
**Detailed:**
```bash
ip -4 addr show                  # look for 'inet 192.168.x.y/..' on wlo1/wlan0/eth0
```

#### 3) Start the Server (Host)
On the **server** machine:
```bash
./src/server/master
# In the menu: press 's'  (Server/Host)
```

#### 4) Connect the Client (Join)
On the **client** machine:
```bash
./src/server/master
# In the menu: press 'c'  (Client/Join)
# When prompted, type the server IP (e.g., 192.168.1.50) and press Enter
```

#### 5) One‑liners (Optional)
**Server:** get IP and start host
```bash
SERVER_IP=$(hostname -I | awk '{print $1}'); echo "Server IP: $SERVER_IP"; ./src/server/master
# then press 's'
```
**Client:** start and join
```bash
./src/server/master
# press 'c', then enter the server IP shown above
```

**Tip:** To verify the server is listening you can run (on the server):
```bash
ss -tlnp | grep 5555 || netstat -tlnp | grep 5555 2>/dev/null || true
```

**Happy flying!**
