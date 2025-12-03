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
#include <signal.h>
#include <time.h>
#include <math.h>

// Dimensions
#define DEFAULT_WIDTH  100
#define DEFAULT_HEIGHT 30

// Physics Constants
#define DEFAULT_M 1.0f
#define DEFAULT_K 1.0f
#define DEFAULT_T 0.1f
#define DEFAULT_ETA 10.0f
#define DEFAULT_RHO 5.0f

// Game Rules
#define MAX_OBSTACLES 10
#define MAX_TARGETS 5
#define COLLISION_DIST 2.0f 

// Log Files for the External Terminals
#define LOG_INPUT "input.log"
#define LOG_DRONE "drone.log"
#define LOG_GAME  "game_events.log"

#define BUF_SIZE 1024 

typedef struct { int x; int y; } Point;

#endif
