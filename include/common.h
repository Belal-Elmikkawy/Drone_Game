#ifndef COMMON_H
#define COMMON_H

/*
 * COMMON HEADER
 * -------------
 * This file contains shared definitions, macros, and helper functions used by all
 * processes in the system. It ensures consistency across the Blackboard (Server),
 * Drone, Input, and other modules.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/file.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>
#include <ctype.h>

// --- NETWORK INCLUDES ---
// Required for socket and IP operations in the Network Bridge
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

// --- DIMENSIONS & LIMITS ---
// The game world dimensions (columns x rows)
#define W_WIDTH  80
#define W_HEIGHT 24

// Alias for legacy files that might use DEFAULT_ naming
#define DEFAULT_WIDTH  W_WIDTH
#define DEFAULT_HEIGHT W_HEIGHT

// Maximum concurrent entities
#define MAX_HAZARDS 10
#define MAX_OBSTACLES MAX_HAZARDS
#define MAX_TARGETS 10

// Buffer sizes for IPC reads
#define BUF_SIZE 512
#define NET_BUF_SIZE 512

// --- PHYSICS CONSTANTS ---
// These parameters tune the "feel" of the drone movement
#define PHYS_M 1.0f     // Mass (Inertia)
#define PHYS_K 1.0f     // Damping (Friction)
#define STEP_T 0.1f     // Time step for integration (dt)

// Aliases for the Drone process
#define DEFAULT_M PHYS_M
#define DEFAULT_K PHYS_K
#define DEFAULT_T STEP_T

// Repulsive Field Parameters (Potential Field Method)
#define DEFAULT_ETA 1.0f // Force Multiplier (Strength/Gain)
#define DEFAULT_RHO 10.0f // Effect Radius (How close before repulsion starts)

// --- FILE PATHS ---
// Path for the process registry file (PID tracking)
#define F_PID_REG "registry.pid"
#define FILE_PID F_PID_REG

// Log Files
// Each process logs to its own file to avoid race conditions on stdout/stderr
#define LOG_INPUT    "input.log"
#define LOG_DRONE    "drone.log"
#define LOG_GAME     "game.log"
#define LOG_WATCHDOG "watchdog.log"
#define LOG_NETWORK  "network.log"

// --- APP MODES ---
// Determines the operating mode of the Server process
#define MODE_STANDALONE 0 // Local Single User
#define MODE_SERVER     1 // Network Host
#define MODE_CLIENT     2 // Network Guest

// --- DATA STRUCTURES ---

// 1. Basic Point (used by Obstacles/Targets)
// Represents a discrete coordinate in the 2D grid
typedef struct {
    int x;
    int y;
} Point;

// 2. Drone State (Used by Blackboard and Network)
// Represents the continuous position of the drone.
// CRITICAL: This struct layout must match exactly between pro_B and network.c
typedef struct {
    float x;
    float y;
} DroneState;

// Alias 'Telemetry' to 'DroneState' for readability in network contexts
typedef DroneState Telemetry;

// 3. Obstacle Array (Used by Network protocol if we were syncing obstacles)
// Currently mostly unused in Assignment 3 but kept for compatibility
typedef struct {
    int x[MAX_HAZARDS];
    int y[MAX_HAZARDS];
    int count;
} Hazard;

// --- SHARED FUNCTIONS ---
// Helper functions marked 'static inline' to be compiled directly into each unit

// Global variables for Logging Context (per process)
static char P_NAME[32] = "Unknown";
static char P_STATE[64] = "BOOT";

/*
 * log_message
 * -----------
 * Appends a timestamped message to a specified log file.
 * Uses file locking (flock) to ensure thread/process safety.
 *
 * input: file (path to log), text (message content)
 */
static inline void log_message(const char *file, const char *text) {
    int f = open(file, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (f < 0) return;

    // Lock file to prevent interleaved writes from multiple processes
    if (flock(f, LOCK_EX) == 0) {
        time_t t = time(NULL);
        char *ts = ctime(&t);
        ts[strlen(ts)-1] = '\0'; // Remove newline from ctime

        char line[512];
        snprintf(line, sizeof(line), "[%s] [%s] %s\n", ts, P_NAME, text);
        write(f, line, strlen(line));

        flock(f, LOCK_UN);
    }
    close(f);
}
// Alias for network.c to maintain naming convention
#define log_error_custom(msg) log_message(LOG_NETWORK, msg)

/*
 * register_process
 * ----------------
 * Writes the process name and PID to 'registry.pid'.
 * The Watchdog reads this file to know which PIDs to monitor.
 *
 * input: name (Process Name, e.g., "Server")
 */
static inline void register_process(const char *name) {
    strncpy(P_NAME, name, 31);
    int f = open(F_PID_REG, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (f < 0) return;

    if (flock(f, LOCK_EX) == 0) {
        char line[64];
        snprintf(line, sizeof(line), "%s %d\n", name, getpid());
        write(f, line, strlen(line));
        flock(f, LOCK_UN);
    }
    close(f);
}
#define log_process_pid(name) register_process(name)

// Signal Handler for Watchdog Heartbeat
static void sig_watchdog_handler(int s) {
    // When SIGUSR1 is received, write current status to log
    char buf[128];
    snprintf(buf, sizeof(buf), "HEARTBEAT | Status: %s", P_STATE);
    log_message(LOG_WATCHDOG, buf);
}

/*
 * setup_watchdog_monitor
 * ----------------------
 * Installs the signal handler for SIGUSR1.
 * This allows the Watchdog process to ping this process to check if it's alive.
 *
 * input: proc_name (Used to initialize P_NAME if not set)
 */
static inline void setup_watchdog_monitor(const char *proc_name) {
    // register_process is usually called before this, but we ensure name is set
    if(strlen(P_NAME) == 0 || strcmp(P_NAME, "Unknown") == 0) strncpy(P_NAME, proc_name, 31);

    struct sigaction sa = {0};
    sa.sa_handler = sig_watchdog_handler;
    sa.sa_flags = SA_RESTART; // Restart syscalls if interrupted
    sigaction(SIGUSR1, &sa, NULL);
}

/*
 * set_status
 * ----------
 * Updates the global state string.
 * Used for detailed heartbeat logging (e.g., "Waiting for Input", "Calculating").
 */
static inline void set_status(const char *s) {
    strncpy(P_STATE, s, 63);
}

// 4. Coordinate Conversion (Network Helper)
static inline float invert_axis(float val) {
    return (float)(W_HEIGHT - 1) - val;
}

#endif
