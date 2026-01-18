/*
 * OBSTACLE GENERATOR PROCESS (pro_O)
 * ----------------------------------
 * Periodically generates spawn coordinates for obstacles.
 * 1. Generates random X,Y.
 * 2. Sometimes sends 0,0 to signal a "Despawn" event.
 * 3. Sends coordinates to Server.
 */

#include "common.h"

int main(int argc, char *argv[]) {
    register_process("Obstacles");
    setup_watchdog_monitor("Obstacles");

    srand(time(NULL) ^ getpid());
    int max_w = DEFAULT_WIDTH;
    int max_h = DEFAULT_HEIGHT;

    // Initial Burst: Spawn 3 obstacles immediately
    for(int i=0; i<3; i++) {
        printf("%d,%d\n", rand()%(max_w-2)+1, rand()%(max_h-2)+1);
        fflush(stdout);
    }

    while(1) {
        set_status("Sleeping");
        sleep((rand()%3)+1); // Random interval 1-3 seconds

        set_status("Updating Obstacle State");

        // 50% Chance: Spawn New vs Despawn Old
        // This prevents the screen from filling up indefinitely
        if (rand() % 2 == 0) {
            // SPAWN: Random Location
            printf("%d,%d\n", rand()%(max_w-2)+1, rand()%(max_h-2)+1);
        } else {
            // DESPAWN: Signal (0,0)
            // Server interprets (0,0) as "Remove oldest obstacle"
            printf("0,0\n");
        }

        fflush(stdout);
    }
    return 0;
}
