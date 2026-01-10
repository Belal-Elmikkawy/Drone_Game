#include "common.h"

int main(int argc, char *argv[]) {
    // --- [ASSIGNMENT 2 CORRECTION] ---
    // Register this process ID so the Watchdog can track it.
    register_process("Obstacles");
    
    // --- [ASSIGNMENT 2 CORRECTION] ---
    // Set up the signal handler to respond to Watchdog's "Are you alive?" checks.
    setup_watchdog_monitor("Obstacles");

    srand(time(NULL) ^ getpid());
    int max_w = DEFAULT_WIDTH; 
    int max_h = DEFAULT_HEIGHT;

    // Initial batch to populate the screen slightly
    for(int i=0; i<3; i++) { 
        printf("%d,%d\n", rand()%(max_w-2)+1, rand()%(max_h-2)+1); 
        fflush(stdout); 
    }

    while(1) {
        // --- [ASSIGNMENT 2 CORRECTION] ---
        // Update status string for logging purposes before sleeping
        set_status("Sleeping");
        
        // Random sleep (1-3 seconds) to vary the timing
        sleep((rand()%3)+1);
        
        set_status("Updating Obstacle State");
        
        // --- [BUG FIX: RANDOM SPAWN / DESPAWN] ---
        // Requirement: "Obstacles appear randomly and randomly disappear"
        // Logic: 
        //   - rand() % 3 != 0 (66% chance): SPAWN a new obstacle.
        //   - rand() % 3 == 0 (33% chance): DESPAWN an obstacle.
        // Sending "0,0" tells the Server to clear the current slot in its cyclic buffer.
        
        if (rand() % 3 != 0) {
            // SPAWN: Send valid coordinates
            printf("%d,%d\n", rand()%(max_w-2)+1, rand()%(max_h-2)+1); 
        } else {
            // DESPAWN: Send 0,0 to clear an obstacle from the screen
            printf("0,0\n");
        }
        
        fflush(stdout);
    }
    return 0;
}
