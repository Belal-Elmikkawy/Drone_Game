/*
 * OBSTACLE GENERATOR PROCESS (pro_O)
 * ----------------------------------
 * Periodically generates random obstacles in the game world.
 * - Runs only in Standalone Mode.
 * - Uses a timer loop to spawn coordinates.
 * - Sends coordinates to Server via Pipe.
 */

#include "common.h"

int main(void) {
    // 1. Startup
    // Register this process ID with the Watchdog so it can be monitored.
    // See common.h for details on the registry format.
    register_process("Obstacles");

    // Seed the random number generator using time and a unique offset
    // to ensure this process produces different sequences than others.
    srand(time(NULL) + 123);

    int x, y;

    // Config: Spawn Interval
    // Defines how often the process wakes up to generate an event.
    // 2.5 seconds + 0ns.
    struct timespec ts = {2, 500000000};

    while(1) {
        // --- ASSIGNMENT 2 FIX: Obstacle Despawn Logic ---
        // Instead of only spawning, we now sometimes request a removal.
        // We use a simple random chance (e.g., 25%) to decide the action.

        int action_roll = rand() % 100; // 0 to 99

        if (action_roll < 25) {
            // CASE A: DESPAWN COMMAND
            // We send "0,0" to the server.
            // The Server interprets "0,0" as a signal to remove an existing obstacle.
            x = 0;
            y = 0;
        } else {
            // CASE B: SPAWN COMMAND
            // Generate a random position within the map boundaries.
            // We avoid the absolute edges (0 and WIDTH/HEIGHT-1) to keep the border clear.
            x = (rand() % (DEFAULT_WIDTH - 2)) + 1;
            y = (rand() % (DEFAULT_HEIGHT - 2)) + 1;
        }

        // 3. Send to Server via Pipe
        // The output is redirected to a pipe connected to the Server process.
        // Protocol: "x,y" (e.g., "10,5" for spawn, "0,0" for despawn)
        printf("%d,%d", x, y);
        fflush(stdout); // Ensure immediate transmission

        // Sleep to throttle the generation rate
        nanosleep(&ts, NULL);
    }
    return 0;
}
