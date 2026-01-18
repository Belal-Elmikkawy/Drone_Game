#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

// --- DIMENSIONS & LIMITS ---
#define W_WIDTH  80
#define W_HEIGHT 24
// Alias for legacy files
#define DEFAULT_WIDTH  W_WIDTH
#define DEFAULT_HEIGHT W_HEIGHT

#define MAX_HAZARDS 10
#define MAX_OBSTACLES MAX_HAZARDS
#define MAX_TARGETS 10
#define BUF_SIZE 512
#define NET_BUF_SIZE 512

// --- PHYSICS CONSTANTS ---
#define PHYS_M 1.0f
#define PHYS_K 1.0f
#define STEP_T 0.1f
#define DEFAULT_M PHYS_M
#define DEFAULT_K PHYS_K
#define DEFAULT_T STEP_T
#define DEFAULT_ETA 1.0f
#define DEFAULT_RHO 10.0f

// --- FILE PATHS ---
#define F_PID_REG "registry.pid"
#define FILE_PID F_PID_REG

// Log Files
#define LOG_INPUT    "input.log"
#define LOG_DRONE    "drone.log"
#define LOG_GAME     "game.log"
#define LOG_WATCHDOG "watchdog.log"
#define LOG_NETWORK  "network.log"

// --- APP MODES ---
#define MODE_STANDALONE 0
#define MODE_SERVER     1
#define MODE_CLIENT     2

// --- DATA STRUCTURES ---

// 1. Basic Point (used by Obstacles/Targets)
typedef struct {
    int x;
    int y;
} Point;

// 2. Drone State (Used by Blackboard and Network)
// CRITICAL: This must match exactly between pro_B and network.c
typedef struct {
    float x;
    float y;
} DroneState;

// Alias 'Telemetry' to 'DroneState' so network.c works
typedef DroneState Telemetry;

// 3. Obstacle Array (Used by Network protocol)
typedef struct {
    int x[MAX_HAZARDS];
    int y[MAX_HAZARDS];
    int count;
} Hazard;

// --- SHARED FUNCTIONS ---

// 1. Logging Wrapper (Maps old 'log_message' to new logic)
static char P_NAME[32] = "Unknown";
static char P_STATE[64] = "BOOT";

static inline void log_message(const char *file, const char *text) {
    int f = open(file, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (f < 0) return;

    if (flock(f, LOCK_EX) == 0) {
        time_t t = time(NULL);
        char *ts = ctime(&t);
        ts[strlen(ts)-1] = '\0';

        char line[512];
        snprintf(line, sizeof(line), "[%s] [%s] %s\n", ts, P_NAME, text);
        write(f, line, strlen(line));
        flock(f, LOCK_UN);
    }
    close(f);
}
// Alias for network.c
#define log_error_custom(msg) log_message(LOG_NETWORK, msg)

// 2. Process Registration
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

// 3. Watchdog Setup
static void sig_watchdog_handler(int s) {
    char buf[128];
    snprintf(buf, sizeof(buf), "HEARTBEAT | Status: %s", P_STATE);
    log_message(LOG_WATCHDOG, buf);
}

static inline void setup_watchdog_monitor(const char *proc_name) {
    // register_process is usually called before this, but we ensure name is set
    if(strlen(P_NAME) == 0 || strcmp(P_NAME, "Unknown") == 0) strncpy(P_NAME, proc_name, 31);

    struct sigaction sa = {0};
    sa.sa_handler = sig_watchdog_handler;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &sa, NULL);
}

static inline void set_status(const char *s) {
    strncpy(P_STATE, s, 63);
}

// 4. Coordinate Conversion (Network Helper)
static inline float invert_axis(float val) {
    return (float)(W_HEIGHT - 1) - val;
}

#endif
