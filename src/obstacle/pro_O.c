#include "common.h"

int main(int argc, char *argv[]) {
    register_process("Obstacles");
    setup_watchdog_monitor("Obstacles");

    srand(time(NULL) ^ getpid());
    int max_w = DEFAULT_WIDTH; 
    int max_h = DEFAULT_HEIGHT;

    for(int i=0; i<5; i++) { 
        printf("%d,%d\n", rand()%(max_w-2)+1, rand()%(max_h-2)+1); 
        fflush(stdout); 
    }

    while(1) {
        set_status("Sleeping");
        sleep((rand()%3)+2);
        
        set_status("Generating Obstacle");
        printf("%d,%d\n", rand()%(max_w-2)+1, rand()%(max_h-2)+1); 
        fflush(stdout);
    }
    return 0;
}
