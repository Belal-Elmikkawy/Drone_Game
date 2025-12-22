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
#include <sys/stat.h>
#include <sys/file.h>
#include <signal.h>
#include <time.h>
#include <math.h>

// Dimensions & Physics Defaults
#define DEFAULT_WIDTH  100
#define DEFAULT_HEIGHT 30
#define DEFAULT_M 1.0f
#define DEFAULT_K 1.0f
#define DEFAULT_T 0.1f
#define DEFAULT_ETA 10.0f
#define DEFAULT_RHO 5.0f

// Game Rules
#define MAX_OBSTACLES 10
#define MAX_TARGETS 5
#define COLLISION_DIST 2.0f 

// Log Files  
#define LOG_INPUT "logs/input.log"
#define LOG_DRONE "logs/drone.log"
#define LOG_GAME  "logs/game_events.log"
#define LOG_WATCHDOG "logs/watchdog.log"
#define FILE_PID "logs/pid_registry.txt"

#define BUF_SIZE 1024 

typedef struct { int x; int y; } Point;


// Helper to write to log with File Locking [cite: 141]
static inline void log_message(const char *filename, const char *msg) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd == -1) return;

    // Lock
    if (flock(fd, LOCK_EX) == 0) {
        time_t now = time(NULL);
        char *t_str = ctime(&now);
        t_str[strlen(t_str)-1] = '\0'; // Remove newline
        
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "[%s] %s\n", t_str, msg);
        write(fd, buffer, strlen(buffer));
        
        // Unlock
        flock(fd, LOCK_UN);
    }
    close(fd);
}

// Helper to register PID on startup [cite: 143]
static inline void register_process(const char *name) {
    int fd = open(FILE_PID, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd == -1) return;

    if (flock(fd, LOCK_EX) == 0) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%s %d\n", name, getpid());
        write(fd, buffer, strlen(buffer));
        flock(fd, LOCK_UN);
    }
    close(fd);
}

// Watchdog Status Reporting
static char PROC_NAME[32];
static char CURRENT_STATUS[64] = "Initializing"; 

// Helper to update status (Code Area) from main loop
static inline void set_status(const char *status) {
    snprintf(CURRENT_STATUS, sizeof(CURRENT_STATUS), "%s", status);
}

static void watchdog_handler(int sig) {
    char msg[128];
    // Requirement: Report the code area currently being executed [cite: 129]
    snprintf(msg, sizeof(msg), "%s is ALIVE | State: %s", PROC_NAME, CURRENT_STATUS);
    log_message(LOG_WATCHDOG, msg);
}

static inline void setup_watchdog_monitor(const char *name) {
    strncpy(PROC_NAME, name, 31);
    struct sigaction sa;
    sa.sa_handler = watchdog_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &sa, NULL); // Watchdog sends SIGUSR1
}

#endif
