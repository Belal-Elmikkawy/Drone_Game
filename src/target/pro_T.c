/*
 * TARGET GENERATOR PROCESS (pro_T)
 * --------------------------------
 * Periodically generates score targets.
 * - Similar logic to Obstacle Generator.
 * - Sends "x,y" coordinates to Server.
 */

#include "common.h"

int main(void) {
    // 1. Startup
    register_process("Targets");
    srand(time(NULL) + 999); // Unique seed

    int x, y;
    struct timespec ts = {4, 0}; // Every 4 Seconds

    while(1) {
        // 2. Generate Random Coordinates
        // Keep away from borders (1..W-2) to avoid spawning inside the wall.
        // The '+1' offset ensures we are at least at index 1.
        x = (rand() % (DEFAULT_WIDTH - 2)) + 1;
        y = (rand() % (DEFAULT_HEIGHT - 2)) + 1;

        // 3. Send to Server
        printf("%d,%d", x, y);
        fflush(stdout);

        nanosleep(&ts, NULL);
    }
    return 0;
}
